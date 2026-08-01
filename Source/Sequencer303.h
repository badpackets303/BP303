#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "StepDyn.h"

struct SeqEvent
{
    int  offset;    // sample offset within the block
    bool noteOn;
    int  note;
    int  dyn;       // dyn303::Dyn
    bool slide;
};

// 16-step 303 pattern sequencer. Step data lives in atomics so the UI/message
// thread can edit while the audio thread plays. JUCE-free for standalone testing.
class Sequencer303
{
public:
    static constexpr int maxSteps = 16;
    static constexpr int baseNote = 36;   // step key 0, octave 0 == C2

    struct Step
    {
        std::atomic<int>  key { 0 };      // semitones above base C (0..12)
        std::atomic<int>  octave { 0 };   // -1..+3
        std::atomic<bool> gate { false };
        std::atomic<int>  dyn { dyn303::Normal };   // Dyn: soft, normal or accented
        std::atomic<bool> slide { false };  // ties/glides this step into the next
        std::atomic<int>  hold { 1 };       // note length in steps (1 = single step)
    };

    Step steps[maxSteps];
    std::atomic<int> length { 16 };
    std::atomic<int> playingStep { -1 };  // for the UI

    // Transport phase (beats) the current pattern is counted from. Zero — the
    // default — locks patterns to the host's bar grid, which is what you want
    // when playing a single pattern. Song mode moves it to the start of each
    // song step so a pattern of any length begins at step 0 when that step
    // begins, instead of entering partway through.
    std::atomic<double> phaseOrigin { 0.0 };

    // Pitch as a single semitone offset from base C (key + 12*octave), for
    // drag-editing and transposing.
    static int loadPitch (const Step& s)
    {
        return s.key.load() + 12 * s.octave.load();
    }

    static void storePitch (Step& s, int combined)
    {
        combined = std::clamp (combined, -12, 47);
        int oct = combined >= 0 ? combined / 12 : -1;
        oct = std::clamp (oct, -1, 3);
        s.octave.store (oct);
        s.key.store (std::clamp (combined - 12 * oct, 0, 12));
    }

    Sequencer303() { loadDefaultPattern(); }

    void prepare (double sr)
    {
        sampleRate = sr;
        hardStop();
    }

    void hardStop()
    {
        activeNote = -1;
        wasRunning = false;
        stepIdx = 0;
        posInStep = 0.0;
        stepFired = offFired = false;
        prevStepSlid = false;
        heldCover = 0;
        stepCovered = false;
        playingStep.store (-1);
    }

    void process (int numSamples, double bpm, bool running,
                  bool hostSynced, double ppqPosition, float shuffle,
                  std::vector<SeqEvent>& events)
    {
        events.clear();
        const double sps = sampleRate * 15.0 / std::clamp (bpm, 20.0, 400.0);
        const int len = std::clamp (length.load(), 1, maxSteps);

        if (! running)
        {
            if (activeNote >= 0)
            {
                events.push_back ({ 0, false, activeNote, dyn303::Normal, false });
                activeNote = -1;
            }
            wasRunning = false;
            heldCover = 0;
            stepCovered = false;
            playingStep.store (-1);
            return;
        }

        if (hostSynced)
        {
            // Phase since this pattern's origin, in 16th steps. Can go negative
            // on the block where a song step switches patterns: the origin
            // marks the new step's start, which may land partway through this
            // block rather than at its top. Clamping that to zero would fire
            // step 0 at sample 0 of the block instead of at its real onset, so
            // it's kept negative here and counted up by the sample loop below,
            // the same way posInStep always does.
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

            // Snap on start, or on loops/jumps; free-run otherwise so steady
            // playback isn't perturbed by rounding.
            const bool drifted = hostStep != stepIdx
                              || std::abs (hostPos - posInStep) > sps * 0.25;
            if (! wasRunning || drifted)
            {
                if (activeNote >= 0 && ! wasRunning)
                {
                    events.push_back ({ 0, false, activeNote, dyn303::Normal, false });
                    activeNote = -1;
                }
                stepIdx = hostStep;
                posInStep = hostPos;
                const double tOn = onTime (sps, shuffle);
                stepFired = posInStep > tOn + sps * 0.25;  // too late — skip, don't double-fire
                offFired = false;
                prevStepSlid = false;
                heldCover = 0;
                stepCovered = false;
            }
        }
        else if (! wasRunning)
        {
            stepIdx = 0;
            posInStep = 0.0;
            stepFired = offFired = false;
            prevStepSlid = false;
            heldCover = 0;
            stepCovered = false;
        }

        wasRunning = true;

        for (int i = 0; i < numSamples; ++i)
        {
            const double tOn = onTime (sps, shuffle);

            if (! stepFired && posInStep >= tOn)
            {
                stepFired = true;
                offFired = false;
                if (stepCovered)
                    playingStep.store (stepIdx);   // a held note owns this step: don't retrigger
                else
                    fireStep (i, events);
            }

            // Hold the note while it spans further steps (heldCover) or while this
            // step is one it already covers (stepCovered); otherwise release it at
            // the usual point unless the step is tied forward with slide.
            if (stepFired && ! offFired && activeNote >= 0
                && heldCover == 0 && ! stepCovered
                && ! (steps[stepIdx].gate.load() && steps[stepIdx].slide.load())
                && posInStep >= tOn + sps * 0.55)
            {
                events.push_back ({ i, false, activeNote, dyn303::Normal, false });
                activeNote = -1;
                offFired = true;
            }

            posInStep += 1.0;
            if (posInStep >= sps)
            {
                posInStep -= sps;
                stepIdx = (stepIdx + 1) % len;
                stepFired = offFired = false;
                stepCovered = heldCover > 0;
                if (heldCover > 0)
                    --heldCover;
            }
        }
    }

    // Samples until the sequencer next wraps to step 0 (for quantized pattern
    // switching). Only meaningful while running.
    double samplesUntilPatternStart (double bpm) const
    {
        const double sps = sampleRate * 15.0 / std::clamp (bpm, 20.0, 400.0);
        const int len = std::clamp (length.load(), 1, maxSteps);
        const int idx = std::min (stepIdx, len - 1);
        return (sps - posInStep) + (double) (len - 1 - idx) * sps;
    }

    void loadDefaultPattern()
    {
        // key, octave, gate, dynamics, slide — a classic A-minor acid line
        struct D { int k, o; bool g; int d; bool s; };
        const D d[16] = {
            { 9, -1, true, dyn303::Hard,   false }, { 9, -1, true, dyn303::Normal, false },
            { 9,  0, true, dyn303::Normal, true  }, { 0,  0, false, dyn303::Normal, false },
            { 9, -1, true, dyn303::Normal, false }, { 0,  0, true, dyn303::Hard,   false },
            { 0,  0, false, dyn303::Normal, false }, { 9, -1, true, dyn303::Normal, false },
            { 4,  0, true, dyn303::Normal, true  }, { 2,  0, true, dyn303::Normal, true  },
            { 9, -1, true, dyn303::Normal, false }, { 0,  0, false, dyn303::Normal, false },
            { 9,  0, true, dyn303::Hard,   false }, { 9, -1, true, dyn303::Normal, false },
            { 7, -1, true, dyn303::Normal, true  }, { 9, -1, true, dyn303::Normal, false },
        };
        for (int i = 0; i < maxSteps; ++i)
        {
            steps[i].key.store (d[i].k);
            steps[i].octave.store (d[i].o);
            steps[i].gate.store (d[i].g);
            steps[i].dyn.store (d[i].d);
            steps[i].slide.store (d[i].s);
            steps[i].hold.store (1);
        }
        length.store (16);
    }

private:
    double onTime (double sps, float shuffle) const
    {
        return (stepIdx & 1) ? (double) shuffle * sps / 3.0 : 0.0;
    }

    void fireStep (int offset, std::vector<SeqEvent>& events)
    {
        auto& st = steps[stepIdx];
        const int prev = activeNote;

        if (st.gate.load())
        {
            const int note = baseNote + st.key.load() + 12 * st.octave.load();
            const bool slide = prev >= 0 && prevStepSlid;
            events.push_back ({ offset, true, note, dyn303::clampDyn (st.dyn.load()), slide });
            if (prev >= 0)
                events.push_back ({ offset, false, prev, dyn303::Normal, false });
            activeNote = note;
            prevStepSlid = st.slide.load();
            heldCover = std::max (0, st.hold.load() - 1);   // steps this note also covers
        }
        else
        {
            if (prev >= 0)
                events.push_back ({ offset, false, prev, dyn303::Normal, false });
            activeNote = -1;
            prevStepSlid = false;
            heldCover = 0;
        }

        playingStep.store (stepIdx);
    }

    double sampleRate = 44100.0;
    double posInStep = 0.0;
    int    stepIdx = 0;
    int    activeNote = -1;
    bool   stepFired = false, offFired = false;
    bool   prevStepSlid = false;
    bool   wasRunning = false;
    int    heldCover = 0;       // steps remaining that the active note sustains over
    bool   stepCovered = false; // current step is owned by an ongoing held note
};
