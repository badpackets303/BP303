// Offline tests for the bass line running on a cycle of its own — the same two
// halves the drum lanes have, on the line that also defines the bar.
//
// The distinction that matters, and the reason both exist:
//   patternLength alone gives polymeter — the line keeps the sixteenth pulse,
//   runs a shorter bar than LENGTH says, and drifts against it.
//   patternFit gives polyrhythm — the line's steps divide one bar evenly, so it
//   keeps the bar and changes the pulse.
//
// And the one that matters most: LENGTH is still the bar. patternLength is a
// separate value that defaults to followBar, so every pattern saved before this
// existed plays exactly as it did.
//
// Build: clang++ -std=c++17 -O2 Tools/bass_meter_test.cpp -o bass_meter_test

#include "../Source/Sequencer303.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sr = 44100.0;
    constexpr double bpm = 120.0;
    constexpr int block = 64;

    // Note-on positions, in sixteenths of the bar, over a free-running transport.
    std::vector<double> onsets (Sequencer303& seq, int steps, float shuffle = 0.0f)
    {
        const double sps = sr * 15.0 / bpm;
        const int totalSamples = (int) std::llround (sps * steps);

        std::vector<double> out;
        std::vector<SeqEvent> events;
        long long sample = 0;

        for (int done = 0; done < totalSamples; done += block)
        {
            const int n = std::min (block, totalSamples - done);
            seq.process (n, bpm, true, false, 0.0, shuffle, events);
            for (const auto& e : events)
                if (e.noteOn)
                    out.push_back ((double) (sample + e.offset) / sps);
            sample += n;
        }
        return out;
    }

    bool near (double a, double b) { return std::abs (a - b) < 0.01; }

    // Every step gated at the same pitch, so a firing is only about timing.
    void allGated (Sequencer303& seq)
    {
        for (int i = 0; i < Sequencer303::maxSteps; ++i)
        {
            auto& s = seq.steps[i];
            s.gate.store (true);
            s.slide.store (false);
            s.hold.store (1);
            s.ratchet.store (1);
            s.dyn.store (dyn303::Normal);
            s.key.store (0);
            s.octave.store (0);
        }
        seq.length.store (16);
        seq.patternLength.store (Sequencer303::followBar);
        seq.patternFit.store (false);
    }

    // ...and only step 0, for reading a cycle off the firings.
    void firstOnly (Sequencer303& seq)
    {
        allGated (seq);
        for (int i = 1; i < Sequencer303::maxSteps; ++i)
            seq.steps[i].gate.store (false);
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- 1. Following the bar is exactly what the line did before -----------
    // Straight and shuffled, against a sequencer whose new fields are untouched.
    for (float shuffle : { 0.0f, 0.6f })
    {
        Sequencer303 a, b;
        a.prepare (sr);
        b.prepare (sr);
        b.patternLength.store (Sequencer303::followBar);

        const auto want = onsets (a, 48, shuffle);
        const auto got  = onsets (b, 48, shuffle);

        bool identical = want.size() == got.size();
        for (size_t i = 0; identical && i < want.size(); ++i)
            identical = want[i] == got[i];
        check (identical, "the default pattern moved once the line could run short");
    }

    // ---- 2. LENGTH still means the bar, not the line -----------------------
    // Setting the line shorter must not touch what LENGTH reads: the song, the
    // drum lanes and the export all take the bar from it.
    {
        Sequencer303 seq;
        seq.prepare (sr);
        allGated (seq);
        seq.patternLength.store (6);

        check (seq.length.load() == 16, "shortening the line moved the bar");
        check (seq.lengthOf (16) == 6, "the line did not take its own length");
        check (seq.lengthOf (16) != seq.length.load(), "the line and the bar are the same value");

        seq.patternLength.store (Sequencer303::followBar);
        check (seq.lengthOf (16) == 16, "followBar did not give the line the bar's length");
        check (seq.lengthOf (12) == 12, "followBar did not track a changed bar");
    }

    // ---- 3. A short line free-runs against the bar (polymeter) -------------
    {
        Sequencer303 seq;
        seq.prepare (sr);
        firstOnly (seq);
        seq.patternLength.store (6);      // 6 steps under a 16-step bar

        const auto ons = onsets (seq, 48);
        const std::vector<double> want { 0, 6, 12, 18, 24, 30, 36, 42 };
        bool ok = ons.size() == want.size();
        for (size_t i = 0; ok && i < ons.size(); ++i)
            ok = near (ons[i], want[i]);
        check (ok, "a 6-step line did not fire every 6 steps");
        std::printf ("6-step line over 48: %zu hits, last at %.1f\n", ons.size(), ons.back());
    }

    // ---- 4. A fitted line divides the bar evenly (polyrhythm) --------------
    {
        Sequencer303 seq;
        seq.prepare (sr);
        allGated (seq);
        seq.patternLength.store (12);     // eighth-note triplets across a 4/4 bar
        seq.patternFit.store (true);

        const auto ons = onsets (seq, 32);
        check (ons.size() == 24, "twelve fitted steps did not give twelve notes a bar");

        bool even = ons.size() == 24;
        for (size_t i = 1; even && i < ons.size(); ++i)
            even = near (ons[i] - ons[i - 1], 4.0 / 3.0);
        check (even, "a fitted line's steps were not evenly spread across the bar");
    }

    // ---- 5. A fitted line locks to the bar, a short one does not -----------
    {
        Sequencer303 fitted, shortLine;
        fitted.prepare (sr);
        shortLine.prepare (sr);
        firstOnly (fitted);
        firstOnly (shortLine);

        fitted.patternLength.store (3);
        fitted.patternFit.store (true);
        shortLine.patternLength.store (3);

        const auto f = onsets (fitted, 16 * 4);
        const auto s = onsets (shortLine, 16 * 4);

        bool locked = ! f.empty();
        for (size_t i = 1; locked && i < f.size(); ++i)
            locked = near (f[i] - f[i - 1], 16.0);
        check (locked, "a fitted line drifted against the bar");

        bool drifts = ! s.empty();
        for (size_t i = 1; drifts && i < s.size(); ++i)
            drifts = near (s[i] - s[i - 1], 3.0);
        check (drifts, "a 3-step line did not keep the sixteenth pulse");
        std::printf ("3 steps: fitted every %.2f, short every %.2f sixteenths\n",
                     f.size() > 1 ? f[1] - f[0] : 0.0,
                     s.size() > 1 ? s[1] - s[0] : 0.0);
    }

    // ---- 6. Fitting a line that already spans the bar changes nothing ------
    {
        Sequencer303 a, b;
        a.prepare (sr);
        b.prepare (sr);
        allGated (a);
        allGated (b);
        b.patternFit.store (true);       // still followBar, so span is a sixteenth

        const auto want = onsets (a, 48);
        const auto got  = onsets (b, 48);
        bool identical = want.size() == got.size();
        for (size_t i = 0; identical && i < want.size(); ++i)
            identical = want[i] == got[i];
        check (identical, "fitting a line that already spans the bar moved it");
    }

    // ---- 7. A split gate subdivides the line's own step --------------------
    // Not a sixteenth, which a fitted line never lands on.
    {
        Sequencer303 seq;
        seq.prepare (sr);
        firstOnly (seq);
        seq.patternLength.store (3);
        seq.patternFit.store (true);
        seq.steps[0].ratchet.store (2);

        const auto ons = onsets (seq, 16);
        const double span = 16.0 / 3.0;
        check (ons.size() == 2, "a split on a fitted step did not fire twice");
        check (ons.size() == 2 && near (ons[0], 0.0) && near (ons[1], span / 2.0),
               "the split did not halve the fitted step");
    }

    // ---- 8. Quantised switching counts the line's own cycle ----------------
    // A line running short comes round on its own steps, which is when a queued
    // bass pattern should land — the bar still owns everything else.
    {
        Sequencer303 seq;
        seq.prepare (sr);
        firstOnly (seq);
        seq.patternLength.store (4);

        std::vector<SeqEvent> events;
        seq.process (block, bpm, true, false, 0.0, 0.0f, events);

        const double sps = sr * 15.0 / bpm;
        const double left = seq.samplesUntilPatternStart (bpm);
        check (std::abs (left - (4.0 * sps - (double) block)) < 2.0,
               "a 4-step line did not report its own wrap");
        std::printf ("4-step line: %.0f samples to the wrap, expects %.0f\n",
                     left, 4.0 * sps - (double) block);
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
