#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "Sequencer303.h"

// Song mode: an ordered chain of pattern-pair steps ("play bass 3 with drums 1
// for four repeats, then ..."). The chain is the arrangement.
//
// Playback position is *derived* from the shared transport phase rather than
// counted as it goes, so a host loop, a jump, or starting mid-project all land
// on the right step with no state to resync — locate() is a pure function of
// the phase. Step data lives in atomics so the UI can arrange while the audio
// thread plays. JUCE-free for standalone testing.
class SongPlayer
{
public:
    static constexpr int maxSteps   = 128;
    static constexpr int maxRepeats = 64;
    static constexpr int hold       = -1;   // slot value: keep the line's current pattern

    // A step as the UI sees it. Live storage below is atomic per field.
    struct Step
    {
        int  bassSlot = 0;              // pattern slot, or hold
        int  drumSlot = 0;
        int  repeats  = 1;              // 1..maxRepeats
        bool bassMute = false;          // drop the line for this step
        bool drumMute = false;
    };

    // Where the song is at a given transport phase. Slots have holds already
    // resolved; a slot of `hold` means nothing has selected that line yet — a
    // leading hold, with no pattern to carry on from — so the caller should
    // keep that line silent rather than sound whatever it last had loaded.
    struct Position
    {
        int  stepIndex   = -1;          // -1 when the song is empty
        int  repeatIndex = 0;           // which repeat of that step
        int  bassSlot    = hold;
        int  drumSlot    = hold;
        bool bassMute    = false;
        bool drumMute    = false;
        double beatsPerRepeat  = 0.0;   // one pass of this step's pattern
        double beatsIntoRepeat = 0.0;
        bool finished    = false;       // ran past the end with looping off
    };

    // Beats from the start of the step to the given position — the caller
    // subtracts this from the transport phase to get the phase at which the
    // step began, which is where its patterns should start from.
    static double beatsIntoStep (const Position& p)
    {
        return (double) p.repeatIndex * p.beatsPerRepeat + p.beatsIntoRepeat;
    }

    // --- arrangement (message thread) ---------------------------------------
    // Edits shift elements in place, so the audio thread can briefly observe a
    // half-applied insert/remove. The worst case is one block reading a stale
    // slot number, and switches only take effect on a pattern boundary, so no
    // lock is needed.

    int  getCount() const                  { return numSteps.load(); }
    bool isLooping() const                 { return looping.load(); }
    void setLooping (bool shouldLoop)      { looping.store (shouldLoop); }

    Step getStep (int index) const
    {
        Step s;
        if (index < 0 || index >= numSteps.load())
            return s;

        const auto& a = steps[index];
        s.bassSlot = a.bassSlot.load();
        s.drumSlot = a.drumSlot.load();
        s.repeats  = a.repeats.load();
        s.bassMute = a.bassMute.load();
        s.drumMute = a.drumMute.load();
        return s;
    }

    void setStep (int index, const Step& s)
    {
        if (index < 0 || index >= maxSteps)
            return;
        store (steps[index], s);
    }

    // Inserts before `index` (clamped, so an index of getCount() appends).
    // Returns the index actually used, or -1 when the song is full.
    int insertStep (int index, const Step& s)
    {
        const int n = numSteps.load();
        if (n >= maxSteps)
            return -1;

        index = std::clamp (index, 0, n);
        for (int i = n; i > index; --i)
            copy (steps[i], steps[i - 1]);
        store (steps[index], s);
        numSteps.store (n + 1);
        return index;
    }

    void removeStep (int index)
    {
        const int n = numSteps.load();
        if (index < 0 || index >= n)
            return;

        for (int i = index; i < n - 1; ++i)
            copy (steps[i], steps[i + 1]);
        numSteps.store (n - 1);
    }

    void clear() { numSteps.store (0); }

    // --- playback (audio thread) --------------------------------------------
    // `slotSteps(slot)` returns the pattern length in 16th steps for a bass
    // slot; `fallbackSteps` covers a leading hold, where no slot is resolved
    // yet (the caller passes the live sequencer length).

    template <typename LengthFn>
    Position locate (double phaseBeats, const LengthFn& slotSteps,
                     int fallbackSteps = Sequencer303::maxSteps) const
    {
        Position pos;
        const int n = std::clamp (numSteps.load(), 0, maxSteps);
        if (n == 0)
            return pos;

        if (looping.load())
        {
            const double total = totalBeats (slotSteps, fallbackSteps);
            if (total <= 0.0)
                return pos;

            phaseBeats = std::fmod (phaseBeats, total);
            if (phaseBeats < 0.0)
                phaseBeats += total;
        }
        else if (phaseBeats < 0.0)
        {
            phaseBeats = 0.0;
        }

        int heldBass = hold, heldDrum = hold;
        double acc = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto r = resolve (i, heldBass, heldDrum, slotSteps, fallbackSteps);
            const double span = r.beatsPerRepeat * (double) r.repeats;
            const bool last = i == n - 1;

            if (phaseBeats < acc + span || last)
            {
                double into = phaseBeats - acc;

                // Past the end with looping off: hold on the final repeat.
                if (last && into >= span)
                {
                    into = span - r.beatsPerRepeat;
                    pos.finished = true;
                }

                const int rep = (int) std::floor (into / r.beatsPerRepeat);
                pos.stepIndex   = i;
                pos.repeatIndex = std::clamp (rep, 0, r.repeats - 1);
                pos.beatsPerRepeat  = r.beatsPerRepeat;
                pos.beatsIntoRepeat = into - (double) pos.repeatIndex * r.beatsPerRepeat;
                pos.bassSlot = r.bassSlot;
                pos.drumSlot = r.drumSlot;
                pos.bassMute = steps[i].bassMute.load();
                pos.drumMute = steps[i].drumMute.load();
                return pos;
            }

            acc += span;
        }

        return pos;
    }

    template <typename LengthFn>
    double totalBeats (const LengthFn& slotSteps,
                       int fallbackSteps = Sequencer303::maxSteps) const
    {
        const int n = std::clamp (numSteps.load(), 0, maxSteps);
        int heldBass = hold, heldDrum = hold;
        double total = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto r = resolve (i, heldBass, heldDrum, slotSteps, fallbackSteps);
            total += r.beatsPerRepeat * (double) r.repeats;
        }
        return total;
    }

    // Phase at which `index` starts — the UI uses this to drop the playhead on
    // a row. An index past the end returns the song's total length.
    template <typename LengthFn>
    double startBeats (int index, const LengthFn& slotSteps,
                       int fallbackSteps = Sequencer303::maxSteps) const
    {
        const int n = std::clamp (numSteps.load(), 0, maxSteps);
        index = std::clamp (index, 0, n);

        int heldBass = hold, heldDrum = hold;
        double acc = 0.0;

        for (int i = 0; i < index; ++i)
        {
            const auto r = resolve (i, heldBass, heldDrum, slotSteps, fallbackSteps);
            acc += r.beatsPerRepeat * (double) r.repeats;
        }
        return acc;
    }

private:
    struct AtomicStep
    {
        std::atomic<int>  bassSlot { 0 }, drumSlot { 0 }, repeats { 1 };
        std::atomic<bool> bassMute { false }, drumMute { false };
    };

    struct Resolved
    {
        int bassSlot, drumSlot, repeats;
        double beatsPerRepeat;
    };

    // Resolves one step against the running hold state, advancing it.
    template <typename LengthFn>
    Resolved resolve (int i, int& heldBass, int& heldDrum,
                      const LengthFn& slotSteps, int fallbackSteps) const
    {
        const int b = steps[i].bassSlot.load();
        const int d = steps[i].drumSlot.load();
        if (b != hold) heldBass = b;
        if (d != hold) heldDrum = d;

        // The bass pattern owns the loop length (the drum sequencer is driven
        // from it), so it decides how long the step lasts.
        const int len = heldBass == hold ? fallbackSteps : slotSteps (heldBass);

        Resolved r;
        r.bassSlot = heldBass;
        r.drumSlot = heldDrum;
        r.repeats  = std::clamp (steps[i].repeats.load(), 1, maxRepeats);
        r.beatsPerRepeat = (double) std::clamp (len, 1, Sequencer303::maxSteps) / 4.0;
        return r;
    }

    static void store (AtomicStep& a, const Step& s)
    {
        a.bassSlot.store (std::max (s.bassSlot, hold));
        a.drumSlot.store (std::max (s.drumSlot, hold));
        a.repeats.store (std::clamp (s.repeats, 1, maxRepeats));
        a.bassMute.store (s.bassMute);
        a.drumMute.store (s.drumMute);
    }

    static void copy (AtomicStep& dst, const AtomicStep& src)
    {
        dst.bassSlot.store (src.bassSlot.load());
        dst.drumSlot.store (src.drumSlot.load());
        dst.repeats.store  (src.repeats.load());
        dst.bassMute.store (src.bassMute.load());
        dst.drumMute.store (src.drumMute.load());
    }

    AtomicStep steps[maxSteps];
    std::atomic<int>  numSteps { 0 };
    std::atomic<bool> looping { true };
};
