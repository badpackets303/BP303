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

    // How many times a gate can fire inside its own step. Four is the drum
    // sequencer's cap too, and for the same reason: past that the repeats stop
    // reading as a stutter and start reading as a pitch.
    static constexpr int maxRatchet = 4;

    struct Step
    {
        std::atomic<int>  key { 0 };      // semitones above base C (0..12)
        std::atomic<int>  octave { 0 };   // -1..+3
        std::atomic<bool> gate { false };
        std::atomic<int>  dyn { dyn303::Normal };   // Dyn: soft, normal or accented
        std::atomic<bool> slide { false };  // ties/glides this step into the next
        std::atomic<int>  hold { 1 };       // note length in steps (1 = single step)

        // Splitting the gate: the step retriggers this many times inside its own
        // slot. One is the single note it has always been.
        //
        // This is the bass side of the drums' ratchets and it subdivides a step
        // the same way — it is not the bass equivalent of FIT, which the bass
        // cannot have while its own length is what defines the bar.
        std::atomic<int>  ratchet { 1 };
    };

    Step steps[maxSteps];

    // THE BAR. Not the number of steps the bass plays — see patternLength below.
    // The song counts in this, queued pattern switches quantise to it, MIDI
    // export renders it, and every drum lane on followMaster takes its length
    // from it. It is the LENGTH control.
    std::atomic<int> length { 16 };

    // How many steps the bass line itself runs, exactly as a drum lane has its
    // own length: followBar means "however long the bar is", which is what the
    // line did before it could run short of one and what an untouched pattern
    // still does.
    //
    // Set shorter it free-runs against the bar rather than resetting with it —
    // the same drift the drum lanes give, now available to the bass.
    static constexpr int followBar = 0;
    std::atomic<int> patternLength { followBar };

    // ...and FIT spreads those steps evenly across one bar instead of running
    // them on the sixteenth grid, which is the drums' laneFit by another name:
    // twelve steps fitted to a 4/4 bar are an eighth-note-triplet bassline.
    std::atomic<bool> patternFit { false };

    int lengthOf (int barLen) const
    {
        const int n = patternLength.load();
        return std::clamp (n == followBar ? barLen : n, 1, maxSteps);
    }

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

    // How many times a step fires. One where there is no note to repeat, so a
    // caller can use it without asking whether the gate is even on.
    int ratchetAt (int step) const
    {
        if (! steps[step].gate.load())
            return 1;
        return std::clamp (steps[step].ratchet.load(), 1, maxRatchet);
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
        subFired = 0;
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

        // The bar, then the line's own cycle within it. `span` is how long one
        // of the line's steps lasts: a sixteenth unless it is fitted, and then
        // whatever divides the bar into `len` of them.
        //
        // The unfitted case takes `sps` literally rather than computing a ratio
        // that works out to one — same reasoning as DrumSequencer::laneClock and
        // the flat EQ. Every saved pattern is on this path.
        const int barLen = std::clamp (length.load(), 1, maxSteps);
        const int len = lengthOf (barLen);
        const bool fit = patternFit.load();
        const double span = fit ? sps * (double) barLen / (double) len : sps;

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

            // Position in the line's own steps. For a fitted line that is not
            // sixteenths, and deriving it here is what keeps it locked to the
            // bar across a host loop or jump instead of only running at the
            // right rate.
            const double posLine = fit ? pos16 * (double) len / (double) barLen : pos16;

            int hostStep;
            double hostPos;
            if (posLine < 0.0)
            {
                hostStep = 0;
                hostPos  = posLine * span;
            }
            else
            {
                const auto absStep = (long long) std::floor (posLine);
                hostStep = (int) (absStep % (long long) len);
                hostPos  = (posLine - (double) absStep) * span;
            }

            // Snap on start, or on loops/jumps; free-run otherwise so steady
            // playback isn't perturbed by rounding.
            const bool drifted = hostStep != stepIdx
                              || std::abs (hostPos - posInStep) > span * 0.25;
            if (! wasRunning || drifted)
            {
                if (activeNote >= 0 && ! wasRunning)
                {
                    events.push_back ({ 0, false, activeNote, dyn303::Normal, false });
                    activeNote = -1;
                }
                stepIdx = hostStep;
                posInStep = hostPos;

                // Mark every repeat whose moment has already gone by, so a jump
                // lands mid-step without firing the whole split at once. The
                // grace is half a repeat, capped at the quarter-step an unsplit
                // gate has always used — which is what it works out to when the
                // step holds one note, so an unsplit pattern resyncs as before.
                const double tOn = onTime (span, shuffle);
                const int count = ratchetAt (stepIdx);
                const double spacing = (span - tOn) / (double) count;
                const double grace = std::min (spacing * 0.5, span * 0.25);

                subFired = 0;
                while (subFired < count
                       && posInStep > tOn + (double) subFired * spacing + grace)
                    ++subFired;

                stepFired = subFired > 0;   // too late — skip, don't double-fire
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
            subFired = 0;
            prevStepSlid = false;
            heldCover = 0;
            stepCovered = false;
        }

        wasRunning = true;

        for (int i = 0; i < numSamples; ++i)
        {
            const double tOn = onTime (span, shuffle);
            const int count = ratchetAt (stepIdx);

            // The repeats fill what the swing left of the step, the same way the
            // drum lanes' do, so a split gate on a shuffled sixteenth stays
            // inside its own slot — and on a fitted line they subdivide its own
            // step rather than a sixteenth it never touches.
            const double spacing = (span - tOn) / (double) count;

            while (subFired < count && posInStep >= tOn + (double) subFired * spacing)
            {
                if (subFired == 0)
                {
                    stepFired = true;
                    offFired = false;
                    if (stepCovered)
                        playingStep.store (stepIdx);   // a held note owns this step: don't retrigger
                    else
                        fireStep (i, events);
                }
                else if (! stepCovered)
                {
                    fireRepeat (i, events);
                }
                ++subFired;
            }

            // Where the gate closes. An unsplit step releases at the point it
            // always has — measured from the step, not from what the swing left
            // of it, which are the same number only when shuffle is zero. A split
            // one releases the same fraction into whichever repeat is sounding,
            // so every repeat gets the same shape.
            const double release = count == 1
                                     ? tOn + span * 0.55
                                     : tOn + (double) (subFired - 1) * spacing + spacing * 0.55;

            // Hold the note while it spans further steps (heldCover) or while this
            // step is one it already covers (stepCovered); otherwise release it at
            // the usual point unless the step is tied forward with slide.
            //
            // Only the *last* repeat ties forward: the earlier ones have to close
            // or the split would be one long note with retriggers buried in it.
            if (stepFired && ! offFired && activeNote >= 0
                && heldCover == 0 && ! stepCovered
                && ! (steps[stepIdx].gate.load() && steps[stepIdx].slide.load()
                        && subFired >= count)
                && posInStep >= release)
            {
                events.push_back ({ i, false, activeNote, dyn303::Normal, false });
                activeNote = -1;
                offFired = true;
            }

            posInStep += 1.0;
            if (posInStep >= span)
            {
                posInStep -= span;
                stepIdx = (stepIdx + 1) % len;
                stepFired = offFired = false;
                subFired = 0;
                stepCovered = heldCover > 0;
                if (heldCover > 0)
                    --heldCover;
            }
        }
    }

    // Samples until the line next wraps to step 0 (for quantized pattern
    // switching). Only meaningful while running.
    //
    // Counted in the line's own steps, because that is where step 0 actually
    // comes round — but a line running short of the bar is the same case a short
    // drum lane is, and switching still belongs to the bar. The processor asks
    // the *bar* for that, so this stays the line's own answer and the caller
    // decides which it wants.
    double samplesUntilPatternStart (double bpm) const
    {
        const double sps = sampleRate * 15.0 / std::clamp (bpm, 20.0, 400.0);
        const int barLen = std::clamp (length.load(), 1, maxSteps);
        const int len = lengthOf (barLen);
        const double span = patternFit.load()
                              ? sps * (double) barLen / (double) len : sps;
        const int idx = std::min (stepIdx, len - 1);
        return (span - posInStep) + (double) (len - 1 - idx) * span;
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
            steps[i].ratchet.store (1);
        }
        length.store (16);
        patternLength.store (followBar);
        patternFit.store (false);
    }

private:
    // Shuffle delays the odd steps by a third of the step. The span is the
    // step's own length, so a fitted line swings its own steps rather than a
    // sixteenth grid it never lands on; unfitted, the span is a sixteenth and
    // nothing moves.
    double onTime (double span, float shuffle) const
    {
        return (stepIdx & 1) ? (double) shuffle * span / 3.0 : 0.0;
    }

    // A repeat inside the step: the same note struck again, never a slide —
    // sliding into itself is what the repeats exist to avoid. Closes whatever is
    // sounding first, since with hold or a covered step the usual release may be
    // suppressed and the note still standing.
    void fireRepeat (int offset, std::vector<SeqEvent>& events)
    {
        auto& st = steps[stepIdx];
        if (! st.gate.load())
            return;

        const int note = baseNote + st.key.load() + 12 * st.octave.load();
        if (activeNote >= 0)
            events.push_back ({ offset, false, activeNote, dyn303::Normal, false });
        events.push_back ({ offset, true, note, dyn303::clampDyn (st.dyn.load()), false });
        activeNote = note;
        offFired = false;
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
    int    subFired = 0;        // repeats already fired inside this step
    int    heldCover = 0;       // steps remaining that the active note sustains over
    bool   stepCovered = false; // current step is owned by an ongoing held note
};
