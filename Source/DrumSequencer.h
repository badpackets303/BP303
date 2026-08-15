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

    // Ratchets: how many times a step fires inside its own slot. Two bits per
    // step, so the whole lane is still one integer that copies, pastes and
    // serialises like the others — 0 means the single hit it has always been,
    // up to 3 meaning four.
    //
    // This subdivides a step; it does not change the cycle (that is laneLength)
    // and it does not make triplets (three hits inside a sixteenth are 48ths).
    static constexpr int maxRatchet = 4;
    std::atomic<uint32_t> ratchetMask[numLanes];

    // How many steps a lane runs before it wraps. followMaster means "however
    // long the pattern is", which is what every lane did before this existed and
    // what an untouched kit still does.
    //
    // A lane set shorter free-runs against the master rather than resetting with
    // it: a 6-step hat under a 16-step kick lands somewhere new every bar and
    // comes back round after 48 steps. The master still owns the bar — the song,
    // queued pattern switches and MIDI export all count in it — so the drift is
    // audible as drift instead of as the whole kit losing its place.
    static constexpr int followMaster = 0;
    std::atomic<int> laneLength[numLanes];

    // FIT: spread the lane's steps evenly across one master bar instead of
    // running them on the sixteenth grid. This is the other half of the same
    // question and the opposite answer — length alone gives polymeter (same
    // pulse, shorter bar, drifts), FIT gives polyrhythm (same bar, different
    // pulse, locks). Three steps fitted to a sixteen-step bar are three evenly
    // spaced hits per bar; twelve are eighth-note triplets.
    //
    // A lane's step lasts masterLen/laneLength sixteenths, so this is the
    // per-lane clock divider that ratchets deliberately are not: ratchets
    // subdivide a step and leave the cycle alone, FIT changes the cycle itself.
    std::atomic<bool> laneFit[numLanes];

    int lengthOf (int lane, int masterLen) const
    {
        const int n = laneLength[lane].load();
        return n == followMaster ? masterLen : std::clamp (n, 1, maxSteps);
    }

    // Where one lane is, on whatever clock it runs. Everything that used to read
    // absStep/posInStep directly goes through this, so the two modes differ in
    // one place rather than at every use.
    struct LanePos
    {
        long long absStep;   // never wraps; swing parity comes off it
        int       step;      // index within the lane's own length
        double    pos;       // samples into that step
        double    span;      // samples the step lasts
    };

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

    // How many times a step fires. One where there is no hit to repeat, so a
    // caller can multiply by it without asking whether the step is on.
    int ratchetAt (int lane, int step) const
    {
        if (! hasHit (lane, step))
            return 1;
        return 1 + (int) ((ratchetMask[lane].load() >> (2 * clampStep (step))) & 3u);
    }

    // Sets the count, turning the hit on if it wasn't — the same way setDynAt
    // does, since asking for four hits on an empty step can only mean one thing.
    void setRatchetAt (int lane, int step, int count)
    {
        const int shift = 2 * clampStep (step);
        const uint32_t bits = (uint32_t) (std::clamp (count, 1, maxRatchet) - 1);

        uint32_t m = ratchetMask[lane].load();
        m = (m & ~(3u << shift)) | (bits << shift);
        ratchetMask[lane].store (m);

        if (bits != 0)
            stepMask[lane].fetch_or (bitFor (step));
    }

    void clearStep (int lane, int step)
    {
        const uint32_t bit = bitFor (step);
        stepMask[lane].fetch_and (~bit);
        accentMask[lane].fetch_and (~bit);
        softMask[lane].fetch_and (~bit);
        ratchetMask[lane].fetch_and (~(3u << (2 * clampStep (step))));
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

            // Ratchet bits under an empty step say nothing; dropped here so a
            // step that gets its hit back later comes back as a single hit
            // rather than as whatever count it happened to carry before.
            uint32_t ratchets = ratchetMask[lane].load();
            for (int step = 0; step < maxSteps; ++step)
                if ((hits & bitFor (step)) == 0)
                    ratchets &= ~(3u << (2 * step));
            ratchetMask[lane].store (ratchets);
        }
    }

    // Transport phase (beats) the current pattern is counted from — see the
    // matching member on Sequencer303.
    std::atomic<double> phaseOrigin { 0.0 };

    // Step under the playhead, or -1 when this line isn't running. Published for
    // the UI, like Sequencer303's: the drum lane runs on its own enable, so the
    // grid can't take its position from the bass sequencer.
    //
    // playingStep is the position in the *master* — the bar — and is what
    // anything counting bars should read. Once lanes can run short of it that is
    // no longer where every lane actually is, so the grid draws a marker per lane
    // from lanePlayingStep; with every lane following, the six agree.
    std::atomic<int> playingStep { -1 };
    std::atomic<int> lanePlayingStep[numLanes];

    DrumSequencer()
    {
        for (auto& n : laneLength)
            n.store (followMaster);
        for (auto& f : laneFit)
            f.store (false);
        for (auto& m : ratchetMask)
            m.store (0);
        for (auto& s : lanePlayingStep)
            s.store (-1);
        loadDefaultPattern();
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        hardStop();
    }

    void hardStop()
    {
        absStep = 0;
        posInStep = 0.0;
        for (auto& n : subFired)
            n = 0;
        for (auto& n : laneStepAt)
            n = -1;
        wasRunning = false;
        playingStep.store (-1);
        for (auto& s : lanePlayingStep)
            s.store (-1);
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
            for (auto& s : lanePlayingStep)
                s.store (-1);
            return;
        }

        if (hostSynced)
        {
            // See the matching comment in Sequencer303::process: kept negative
            // (instead of clamped to zero) so a mid-block pattern switch fires
            // step 0 at its real sample offset, not at sample 0 of the block.
            const double pos16 = (ppqPosition - phaseOrigin.load()) * 4.0;
            long long hostStep;
            double hostPos;
            if (pos16 < 0.0)
            {
                hostStep = 0;
                hostPos  = pos16 * sps;
            }
            else
            {
                hostStep = (long long) std::floor (pos16);
                hostPos  = (pos16 - (double) hostStep) * sps;
            }

            // Compared on the absolute count, not on a wrapped index: lanes of
            // different lengths only stay in a fixed relationship if they are all
            // derived from the same running position.
            const bool drifted = hostStep != absStep
                              || std::abs (hostPos - posInStep) > sps * 0.25;
            if (! wasRunning || drifted)
            {
                absStep = hostStep;
                posInStep = hostPos;
                catchUp (sps, shuffle, len);
            }
        }
        else if (! wasRunning)
        {
            absStep = 0;
            posInStep = 0.0;
            for (auto& n : subFired)
                n = 0;
        }

        wasRunning = true;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int lane = 0; lane < numLanes; ++lane)
            {
                const auto lp = laneClock (lane, len, sps);

                // The repeat count belongs to the step it was counted for, so it
                // clears when *this lane* moves on rather than when the master
                // wraps a sixteenth — a fitted lane's steps don't land on the
                // bar's grid at all. For a lane on the grid the two are the same
                // instant, which is what keeps the default untouched.
                if (lp.absStep != laneStepAt[lane])
                {
                    laneStepAt[lane] = lp.absStep;
                    subFired[lane] = 0;
                }

                if ((stepMask[lane].load() & bitFor (lp.step)) == 0)
                    continue;

                // The repeats fill what is left of the step after the swing has
                // pushed its start, so a ratchet on a shuffled sixteenth stays
                // inside its own slot instead of running into the next one.
                const double tOn = onTime (lp, shuffle);
                const int count = ratchetAt (lane, lp.step);
                const double spacing = (lp.span - tOn) / (double) count;

                while (subFired[lane] < count
                       && lp.pos >= tOn + subFired[lane] * spacing)
                {
                    events.push_back ({ i, lane, dynAt (lane, lp.step) });
                    ++subFired[lane];
                }
            }

            posInStep += 1.0;
            if (posInStep >= sps)
            {
                posInStep -= sps;
                ++absStep;
            }
        }

        playingStep.store (stepFor (len));
        for (int lane = 0; lane < numLanes; ++lane)
            lanePlayingStep[lane].store (laneClock (lane, len, sps).step);
    }

    // Samples until the next wrap to step 0 of the *master*, for quantized
    // pattern switching. A lane running short of the master gets picked up
    // wherever it is, which is the point of it running short.
    double samplesUntilPatternStart (double bpm, int len) const
    {
        const double sps = sampleRate * 15.0 / std::clamp (bpm, 20.0, 400.0);
        len = std::clamp (len, 1, maxSteps);
        const int idx = stepFor (len);
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
    static int clampStep (int step) { return std::clamp (step, 0, maxSteps - 1); }
    static uint32_t bitFor (int step) { return 1u << clampStep (step); }

    // Where the master is in its own cycle.
    int stepFor (int masterLen) const
    {
        return (int) (absStep % (long long) std::clamp (masterLen, 1, maxSteps));
    }

    // Where one lane is, on its own clock.
    //
    // The grid case returns the master's own step and position untouched rather
    // than deriving them back out of a continuous position — the round trip
    // through a divide and a multiply is exact in arithmetic and not in float,
    // and this is the path every default pattern takes. Same reasoning as the
    // flat EQ being a branch instead of a unity coefficient: the cheap way to
    // guarantee nothing moved is not to compute it.
    LanePos laneClock (int lane, int masterLen, double sps) const
    {
        const int laneLen = lengthOf (lane, masterLen);

        if (! laneFit[lane].load())
            return { absStep, (int) (absStep % (long long) laneLen), posInStep, sps };

        // Fitted: laneLen steps span one master bar, so a step lasts
        // masterLen/laneLen sixteenths and the lane comes back to step 0 with
        // the bar instead of drifting against it.
        const double span = sps * (double) masterLen / (double) laneLen;
        const double lanePos = ((double) absStep + posInStep / sps)
                                 * (double) laneLen / (double) masterLen;

        // Negative before the pattern's origin, the same convention process()
        // uses on the master: held negative so a mid-block switch fires step 0
        // at its real offset rather than at sample 0 of the block.
        if (lanePos < 0.0)
            return { 0, 0, lanePos * span, span };

        const auto absLane = (long long) std::floor (lanePos);
        return { absLane, (int) (absLane % (long long) laneLen),
                 (lanePos - (double) absLane) * span, span };
    }

    // Shuffle delays the odd steps, and "odd" is counted on the absolute
    // position rather than on any lane's wrapped index. Swing is a property of
    // the grid, so a lane running seven steps has to swing with everything else
    // instead of flipping its own idea of which sixteenths are the off-beats
    // every time it wraps.
    //
    // For an even master length — which is every default and every power-of-two
    // pattern — the two agree exactly and nothing moves. They differ only on an
    // odd length, where the old behaviour restarted the swing each cycle.
    //
    // A fitted lane has no sixteenth grid to belong to, so it swings its own
    // steps by the same fraction of its own step: swung triplets rather than
    // triplets nudged onto a grid they never touch. Both fall out of taking the
    // parity and the span off the lane's clock, which for a grid lane is the
    // master's.
    static double onTime (const LanePos& lp, float shuffle)
    {
        return (lp.absStep & 1LL) ? (double) shuffle * lp.span / 3.0 : 0.0;
    }

    // After a jump, marks every repeat whose moment has already gone by so the
    // transport picks up where it landed instead of firing the whole step at
    // once. The grace is half a repeat's spacing, capped at the quarter-step the
    // single-hit case has always used — which is what it works out to when a
    // step holds one hit, so an unratcheted pattern resyncs exactly as before.
    void catchUp (double sps, float shuffle, int len)
    {
        for (int lane = 0; lane < numLanes; ++lane)
        {
            const auto lp = laneClock (lane, len, sps);
            const double tOn = onTime (lp, shuffle);
            const int count = ratchetAt (lane, lp.step);
            const double spacing = (lp.span - tOn) / (double) count;
            const double grace = std::min (spacing * 0.5, lp.span * 0.25);

            int done = 0;
            while (done < count && lp.pos > tOn + done * spacing + grace)
                ++done;
            subFired[lane] = done;

            // Adopted along with the count, or the next sample would see the
            // lane on a step it hasn't recorded and clear the catch-up away.
            laneStepAt[lane] = lp.absStep;
        }
    }

    double    sampleRate = 44100.0;
    double    posInStep = 0.0;
    long long absStep = 0;      // never wraps; every lane is a modulo of it
    int       subFired[numLanes] = {};   // repeats already fired this step
    long long laneStepAt[numLanes] = {};  // the lane step subFired was counted for
    bool      wasRunning = false;
};
