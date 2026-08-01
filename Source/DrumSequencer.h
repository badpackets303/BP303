#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "DrumMachine.h"
#include "StepDyn.h"

struct DrumEvent
{
    int offset;   // sample offset within the block
    int voice;    // DrumMachine::Voice
    int dyn;      // dyn303::Dyn
};

// 5-lane × 16-step trigger sequencer for the drum machine. Lanes are stored as
// bitmasks in atomics so the UI can edit them while audio runs.
//
// A hit's dynamics live across two masks rather than in one field, because the
// masks are what makes the rest of this cheap: a whole lane rotates with a shift,
// paints with one atomic op, and serialises as a single integer. The pair can
// encode a state that means nothing (a step both soft and accented), so nothing
// outside touches the masks directly — dynAt / setDynAt are the way in, and
// anything that stores masks wholesale (a preset, a paste) runs them through
// normalise() first.
class DrumSequencer
{
public:
    static constexpr int maxSteps = 16;
    static constexpr int numLanes = DrumMachine::numVoices;

    std::atomic<uint32_t> stepMask[numLanes];
    std::atomic<uint32_t> accentMask[numLanes];
    std::atomic<uint32_t> softMask[numLanes];

    bool hasHit (int lane, int step) const
    {
        return (stepMask[lane].load() & bitFor (step)) != 0;
    }

    // The dynamics of a step, or Normal where there is no hit to have any.
    int dynAt (int lane, int step) const
    {
        const uint32_t bit = bitFor (step);
        if ((stepMask[lane].load() & bit) == 0)
            return dyn303::Normal;
        if (accentMask[lane].load() & bit)
            return dyn303::Hard;
        if (softMask[lane].load() & bit)
            return dyn303::Soft;
        return dyn303::Normal;
    }

    // Sets a step's dynamics, turning the hit on if it wasn't. Exactly one of the
    // two level bits ends up set, so the pair can never disagree.
    void setDynAt (int lane, int step, int dyn)
    {
        const uint32_t bit = bitFor (step);
        stepMask[lane].fetch_or (bit);

        if (dyn > 0) { accentMask[lane].fetch_or (bit);  softMask[lane].fetch_and (~bit); }
        else if (dyn < 0) { softMask[lane].fetch_or (bit); accentMask[lane].fetch_and (~bit); }
        else { accentMask[lane].fetch_and (~bit); softMask[lane].fetch_and (~bit); }
    }

    void clearStep (int lane, int step)
    {
        const uint32_t bit = bitFor (step);
        stepMask[lane].fetch_and (~bit);
        accentMask[lane].fetch_and (~bit);
        softMask[lane].fetch_and (~bit);
    }

    // Drops level bits that no hit stands under, and lets the accent win where a
    // step somehow claims both. Run after storing masks that came from outside —
    // a file, a paste, a bank slot — so the invariant holds however they were
    // written, including by a build that knew nothing about soft hits.
    void normalise()
    {
        for (int lane = 0; lane < numLanes; ++lane)
        {
            const uint32_t hits = stepMask[lane].load();
            const uint32_t hard = accentMask[lane].load() & hits;
            accentMask[lane].store (hard);
            softMask[lane].store (softMask[lane].load() & hits & ~hard);
        }
    }

    // Transport phase (beats) the current pattern is counted from — see the
    // matching member on Sequencer303.
    std::atomic<double> phaseOrigin { 0.0 };

    // Step under the playhead, or -1 when this line isn't running. Published for
    // the UI, like Sequencer303's: the drum lane runs on its own enable, so the
    // grid can't take its position from the bass sequencer.
    std::atomic<int> playingStep { -1 };

    DrumSequencer()
    {
        loadDefaultPattern();
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        hardStop();
    }

    void hardStop()
    {
        stepIdx = 0;
        posInStep = 0.0;
        stepFired = false;
        wasRunning = false;
        playingStep.store (-1);
    }

    void process (int numSamples, double bpm, bool running,
                  bool hostSynced, double ppqPosition, float shuffle, int len,
                  std::vector<DrumEvent>& events)
    {
        events.clear();
        const double sps = sampleRate * 15.0 / std::clamp (bpm, 20.0, 400.0);
        len = std::clamp (len, 1, maxSteps);

        if (! running)
        {
            wasRunning = false;
            playingStep.store (-1);
            return;
        }

        if (hostSynced)
        {
            // See the matching comment in Sequencer303::process: kept negative
            // (instead of clamped to zero) so a mid-block pattern switch fires
            // step 0 at its real sample offset, not at sample 0 of the block.
            const double pos16 = (ppqPosition - phaseOrigin.load()) * 4.0;
            int hostStep;
            double hostPos;
            if (pos16 < 0.0)
            {
                hostStep = 0;
                hostPos  = pos16 * sps;
            }
            else
            {
                const auto absStep = (long long) std::floor (pos16);
                hostStep = (int) (absStep % (long long) len);
                hostPos  = (pos16 - (double) absStep) * sps;
            }

            const bool drifted = hostStep != stepIdx
                              || std::abs (hostPos - posInStep) > sps * 0.25;
            if (! wasRunning || drifted)
            {
                stepIdx = hostStep;
                posInStep = hostPos;
                stepFired = posInStep > onTime (sps, shuffle) + sps * 0.25;
            }
        }
        else if (! wasRunning)
        {
            stepIdx = 0;
            posInStep = 0.0;
            stepFired = false;
        }

        wasRunning = true;

        for (int i = 0; i < numSamples; ++i)
        {
            if (! stepFired && posInStep >= onTime (sps, shuffle))
            {
                stepFired = true;
                const uint32_t bit = 1u << stepIdx;
                for (int lane = 0; lane < numLanes; ++lane)
                    if (stepMask[lane].load() & bit)
                        events.push_back ({ i, lane, dynAt (lane, stepIdx) });
            }

            posInStep += 1.0;
            if (posInStep >= sps)
            {
                posInStep -= sps;
                stepIdx = (stepIdx + 1) % len;
                stepFired = false;
            }
        }

        playingStep.store (stepIdx);
    }

    // Samples until the next wrap to step 0 (for quantized pattern switching).
    double samplesUntilPatternStart (double bpm, int len) const
    {
        const double sps = sampleRate * 15.0 / std::clamp (bpm, 20.0, 400.0);
        len = std::clamp (len, 1, maxSteps);
        const int idx = std::min (stepIdx, len - 1);
        return (sps - posInStep) + (double) (len - 1 - idx) * sps;
    }

    void loadDefaultPattern()
    {
        auto bits = [] (std::initializer_list<int> stepsOn)
        {
            uint32_t m = 0;
            for (int s : stepsOn) m |= 1u << s;
            return m;
        };

        stepMask[DrumMachine::BD].store (bits ({ 0, 4, 8, 12 }));
        stepMask[DrumMachine::SD].store (bits ({ 4, 12 }));
        stepMask[DrumMachine::CP].store (bits ({ 12 }));
        stepMask[DrumMachine::CH].store (bits ({ 2, 6, 10, 14 }));
        stepMask[DrumMachine::OH].store (bits ({}));

        accentMask[DrumMachine::BD].store (bits ({ 0 }));
        for (int lane = 1; lane < numLanes; ++lane)
            accentMask[lane].store (0);
        for (int lane = 0; lane < numLanes; ++lane)
            softMask[lane].store (0);
    }

private:
    static uint32_t bitFor (int step) { return 1u << std::clamp (step, 0, maxSteps - 1); }

    double onTime (double sps, float shuffle) const
    {
        return (stepIdx & 1) ? (double) shuffle * sps / 3.0 : 0.0;
    }

    double sampleRate = 44100.0;
    double posInStep = 0.0;
    int    stepIdx = 0;
    bool   stepFired = false;
    bool   wasRunning = false;
};
