// Offline tests for splitting a bass gate — the 303 line's side of the drums'
// ratchets. A step fires its note n times inside its own slot instead of once.
//
// The properties that matter:
//   1. An unsplit pattern is exactly what it was before gates could split —
//      same notes, same offsets, shuffled or not. This is the one that has to
//      hold: every saved project is unsplit.
//   2. A split step fires n times, evenly, inside its own slot, and never runs
//      into the next step.
//   3. Every repeat is a real retrigger: note off then note on, never a slide
//      into itself.
//   4. Only the last repeat ties forward when the step is slid, so a split slide
//      is a stutter that glides out of its last note rather than one long note.
//   5. Splitting composes with hold: the repeats happen in the head step and the
//      last one still sustains over the steps the hold covers.
//   6. A split step still swings, and its repeats stay inside the slot the swing
//      moved them to.
//
// Build: clang++ -std=c++17 -O2 Tools/gate_split_test.cpp -o gate_split_test

#include "../Source/Sequencer303.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sr = 44100.0;
    constexpr double bpm = 120.0;
    constexpr int block = 64;

    struct Fired
    {
        double at;      // position in 16ths, from the start of the run
        bool   noteOn;
        int    note;
        bool   slide;
    };

    std::vector<Fired> run (Sequencer303& seq, int steps, float shuffle = 0.0f)
    {
        const double sps = sr * 15.0 / bpm;
        const int totalSamples = (int) std::llround (sps * steps);

        std::vector<Fired> out;
        std::vector<SeqEvent> events;
        long long sample = 0;

        for (int done = 0; done < totalSamples; done += block)
        {
            const int n = std::min (block, totalSamples - done);
            seq.process (n, bpm, true, false, 0.0, shuffle, events);
            for (const auto& e : events)
                out.push_back ({ (double) (sample + e.offset) / sps,
                                 e.noteOn, e.note, e.slide });
            sample += n;
        }
        return out;
    }

    // Only the note-ons, which is where the split shows.
    std::vector<double> onsets (const std::vector<Fired>& f)
    {
        std::vector<double> out;
        for (const auto& e : f)
            if (e.noteOn)
                out.push_back (e.at);
        return out;
    }

    bool near (double a, double b) { return std::abs (a - b) < 0.01; }

    // One gated step at pitch 0, everything else silent.
    void oneNote (Sequencer303& seq, int at)
    {
        for (int i = 0; i < Sequencer303::maxSteps; ++i)
        {
            auto& s = seq.steps[i];
            s.gate.store (false);
            s.slide.store (false);
            s.hold.store (1);
            s.ratchet.store (1);
            s.dyn.store (dyn303::Normal);
            s.key.store (0);
            s.octave.store (0);
        }
        seq.steps[at].gate.store (true);
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- 1. An unsplit pattern is untouched --------------------------------
    // Straight and shuffled, against a sequencer that never has a ratchet set.
    for (float shuffle : { 0.0f, 0.6f })
    {
        Sequencer303 a, b;
        a.prepare (sr);
        b.prepare (sr);

        const auto want = run (a, 32, shuffle);
        const auto got  = run (b, 32, shuffle);

        bool identical = want.size() == got.size();
        for (size_t i = 0; identical && i < want.size(); ++i)
            identical = want[i].at == got[i].at && want[i].noteOn == got[i].noteOn
                     && want[i].note == got[i].note && want[i].slide == got[i].slide;
        check (identical, "the default pattern moved once gates could split");
    }

    // ---- 2. A split step fires n times inside its own slot -----------------
    for (int count = 2; count <= Sequencer303::maxRatchet; ++count)
    {
        Sequencer303 seq;
        seq.prepare (sr);
        oneNote (seq, 0);
        seq.steps[0].ratchet.store (count);

        const auto ons = onsets (run (seq, 4));
        check ((int) ons.size() == count, "a split gate did not fire its whole count");

        bool spaced = (int) ons.size() == count;
        for (int r = 0; spaced && r < count; ++r)
            if (! near (ons[r], (double) r / (double) count))
                spaced = false;
        check (spaced, "the repeats were not evenly spread through the step");

        // and nothing spills past the step it belongs to
        check (ons.empty() || ons.back() < 1.0, "a repeat ran into the next step");
        std::printf ("split %d: last repeat at %.3f of the step\n", count, ons.back());
    }

    // ---- 3. Every repeat is a retrigger, never a slide ---------------------
    {
        Sequencer303 seq;
        seq.prepare (sr);
        oneNote (seq, 0);
        seq.steps[0].ratchet.store (3);

        const auto fired = run (seq, 2);
        int ons = 0, offs = 0;
        bool anySlide = false;
        for (const auto& e : fired)
        {
            if (e.noteOn) { ++ons; anySlide = anySlide || e.slide; }
            else ++offs;
        }
        check (ons == 3, "a 3-way split did not produce three note-ons");
        check (offs == 3, "each repeat did not get its own note-off");
        check (! anySlide, "a repeat came out as a slide into itself");
    }

    // ---- 4. Only the last repeat ties forward on a slide -------------------
    {
        Sequencer303 seq;
        seq.prepare (sr);
        oneNote (seq, 0);
        seq.steps[0].ratchet.store (3);
        seq.steps[0].slide.store (true);
        seq.steps[1].gate.store (true);
        seq.steps[1].key.store (5);

        const auto fired = run (seq, 2);

        // The first two repeats close inside the step; the third is still
        // sounding when step 1 arrives, which is what makes the slide.
        int offsBeforeStep1 = 0;
        for (const auto& e : fired)
            if (! e.noteOn && e.at < 1.0)
                ++offsBeforeStep1;
        check (offsBeforeStep1 == 2, "a slid split did not close its early repeats");

        bool slidIntoNext = false;
        for (const auto& e : fired)
            if (e.noteOn && e.slide && near (e.at, 1.0))
                slidIntoNext = true;
        check (slidIntoNext, "the last repeat did not slide into the next step");
    }

    // ---- 5. Splitting composes with hold -----------------------------------
    {
        Sequencer303 seq;
        seq.prepare (sr);
        oneNote (seq, 0);
        seq.steps[0].ratchet.store (2);
        seq.steps[0].hold.store (3);   // the head step plus two more

        const auto fired = run (seq, 4);
        const auto ons = onsets (fired);
        check (ons.size() == 2, "a held split did not fire its repeats");
        check (ons.size() == 2 && near (ons[0], 0.0) && near (ons[1], 0.5),
               "the repeats did not stay inside the head step");

        // and the last one is still sounding well past the head step
        double lastOff = -1.0;
        for (const auto& e : fired)
            if (! e.noteOn)
                lastOff = e.at;
        check (lastOff > 2.0, "the held split released before its hold was up");
    }

    // ---- 6. A split step swings with the grid ------------------------------
    {
        Sequencer303 seq;
        seq.prepare (sr);
        oneNote (seq, 1);              // an odd step, so shuffle moves it
        seq.steps[1].ratchet.store (2);

        const float shuffle = 0.6f;
        const double tOn = (double) shuffle / 3.0;      // in 16ths
        const double spacing = (1.0 - tOn) / 2.0;

        const auto ons = onsets (run (seq, 4, shuffle));
        check (ons.size() == 2, "a shuffled split did not fire twice");
        check (ons.size() == 2 && near (ons[0], 1.0 + tOn)
                              && near (ons[1], 1.0 + tOn + spacing),
               "the repeats did not fill what the swing left of the step");
        check (ons.size() == 2 && ons[1] < 2.0, "a shuffled repeat ran into the next step");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
