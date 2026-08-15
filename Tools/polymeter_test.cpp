// Offline tests for per-lane drum pattern length and for ratchets — the two
// things that let a static grid stop repeating itself. They share a file because
// they share the firing loop: length decides which step a lane is on, ratchets
// decide how many times that step fires, and the two have to compose.
//
// The properties that matter:
//   1. Every lane following the master is exactly what the sequencer did before
//      lanes had a length of their own — same steps, same order, same timing.
//   2. A lane set shorter free-runs against the master rather than resetting
//      with it, which is the whole point: a 6-step lane under a 16-step bar
//      lands somewhere new each time round and comes back after lcm(6,16).
//   3. The master still owns the bar. Pattern switches quantise to it, not to
//      whichever lane happens to be shortest.
//   4. Shuffle stays locked to the grid, so lanes of different lengths swing
//      together instead of each having its own idea of the off-beats.
//   5. A ratchet fires its step n times, evenly, inside that step's own slot —
//      never spilling into the next one, and never when the step has no hit.
//   6. Ratchets and lengths compose: a ratcheted step on a short lane repeats on
//      that lane's cycle, with the repeats intact each time round.
//
// Build: clang++ -std=c++17 -O2 Tools/polymeter_test.cpp -o polymeter_test

#include "../Source/DrumSequencer.h"

#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

namespace
{
    constexpr double sr = 44100.0;
    constexpr double bpm = 120.0;
    constexpr int block = 64;

    // Steps at which a lane fired, over `steps` sixteenths of free-running
    // transport. Absolute step numbers, so a lane's cycle is visible in them.
    std::vector<int> firings (DrumSequencer& seq, int lane, int masterLen, int steps,
                              float shuffle = 0.0f)
    {
        const double sps = sr * 15.0 / bpm;
        const int totalSamples = (int) std::llround (sps * steps);

        std::vector<int> hits;
        std::vector<DrumEvent> events;
        long long sample = 0;

        for (int done = 0; done < totalSamples; done += block)
        {
            const int n = std::min (block, totalSamples - done);
            seq.process (n, bpm, true, false, 0.0, shuffle, masterLen, events);
            for (const auto& e : events)
                if (e.voice == lane)
                    // Floor, not nearest: the step a hit belongs to is the one
                    // it falls inside, and a ratchet puts hits two thirds of the
                    // way through a step, which rounding would push into the
                    // next one. The epsilon is for a hit landing on a boundary —
                    // sps is 5512.5 at 44.1k/120, so this also has to divide in
                    // floating point rather than in whole samples.
                    hits.push_back ((int) std::floor ((double) (sample + e.offset) / sps + 1.0e-6));
            sample += n;
        }
        return hits;
    }

    // When a lane fired, in master sixteenths, to sub-step precision. Fitting a
    // lane puts its steps between the grid's own, so what floor() throws away is
    // exactly what these tests are about.
    std::vector<double> firingTimes (DrumSequencer& seq, int lane, int masterLen,
                                     int steps, float shuffle = 0.0f)
    {
        const double sps = sr * 15.0 / bpm;
        const int totalSamples = (int) std::llround (sps * steps);

        std::vector<double> hits;
        std::vector<DrumEvent> events;
        long long sample = 0;

        for (int done = 0; done < totalSamples; done += block)
        {
            const int n = std::min (block, totalSamples - done);
            seq.process (n, bpm, true, false, 0.0, shuffle, masterLen, events);
            for (const auto& e : events)
                if (e.voice == lane)
                    hits.push_back ((double) (sample + e.offset) / sps);
            sample += n;
        }
        return hits;
    }

    // A hit fires on the first sample at or past its moment, so a sixteenth of
    // 5512.5 samples can only ever be one sample late. Anything this tolerance
    // hides is well under a sample.
    bool near (double a, double b) { return std::abs (a - b) < 0.01; }

    bool sameTimes (const std::vector<double>& got, const std::vector<double>& want)
    {
        if (got.size() != want.size())
            return false;
        for (size_t i = 0; i < got.size(); ++i)
            if (! near (got[i], want[i]))
                return false;
        return true;
    }

    void allOn (DrumSequencer& seq)
    {
        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            seq.stepMask[lane].store (0);
            seq.accentMask[lane].store (0);
            seq.softMask[lane].store (0);
            seq.laneLength[lane].store (DrumSequencer::followMaster);
            seq.laneFit[lane].store (false);
        }
    }

    bool same (const std::vector<int>& a, const std::vector<int>& b)
    {
        return a == b;
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- 1. Following the master reproduces the old behaviour --------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        // hits on 0 and 5 of a 16-step bar
        seq.stepMask[0].store ((1u << 0) | (1u << 5));

        const auto hits = firings (seq, 0, 16, 48);
        const std::vector<int> want { 0, 5, 16, 21, 32, 37 };
        check (same (hits, want), "a following lane did not repeat on the master's cycle");
    }

    // ---- 2. A short lane free-runs against the master -----------------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (1u << 0);   // one hit, on its own step 0
        seq.laneLength[0].store (6);       // 3/8 against a 16-step bar

        const auto hits = firings (seq, 0, 16, 48);
        const std::vector<int> want { 0, 6, 12, 18, 24, 30, 36, 42 };
        check (same (hits, want), "a 6-step lane did not fire every 6 steps");

        // and it only lines back up with the bar at the lcm
        const int cycle = std::lcm (6, 16);
        check (cycle == 48, "lcm(6,16) is not 48");
        check (hits.back() + 6 == cycle, "the 6-step lane did not come back round at 48");
    }

    // ---- 3. Lanes of different lengths keep their own cycles ---------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        for (int lane = 0; lane < 3; ++lane)
            seq.stepMask[lane].store (1u << 0);
        seq.laneLength[0].store (DrumSequencer::followMaster);   // 16
        seq.laneLength[1].store (7);
        seq.laneLength[2].store (3);

        const auto kick = firings (seq, 0, 16, 42);
        DrumSequencer seq2; seq2.prepare (sr); allOn (seq2);
        for (int lane = 0; lane < 3; ++lane) seq2.stepMask[lane].store (1u << 0);
        seq2.laneLength[1].store (7);
        seq2.laneLength[2].store (3);
        const auto seven = firings (seq2, 1, 16, 42);
        DrumSequencer seq3; seq3.prepare (sr); allOn (seq3);
        for (int lane = 0; lane < 3; ++lane) seq3.stepMask[lane].store (1u << 0);
        seq3.laneLength[1].store (7);
        seq3.laneLength[2].store (3);
        const auto three = firings (seq3, 2, 16, 42);

        check (same (kick,  std::vector<int> { 0, 16, 32 }), "the following lane drifted");
        check (same (seven, std::vector<int> { 0, 7, 14, 21, 28, 35 }), "the 7-step lane is wrong");
        check (same (three, std::vector<int> { 0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39 }),
               "the 3-step lane is wrong");
        std::printf ("cycles: kick=%zu hits, 7-step=%zu, 3-step=%zu over 42 steps\n",
                     kick.size(), seven.size(), three.size());
    }

    // ---- 4. Pattern switches still quantise to the master ------------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.laneLength[0].store (3);       // shortest lane runs at 3

        // step the transport a little way in, then ask how far the bar has to go
        std::vector<DrumEvent> events;
        const double sps = sr * 15.0 / bpm;
        const int intoBar = 5;
        for (int i = 0; i < (int) std::llround (sps * intoBar); i += block)
            seq.process (block, bpm, true, false, 0.0, 0.0f, 16, events);

        const double left = seq.samplesUntilPatternStart (bpm, 16);
        const double expected = sps * (16 - intoBar);
        std::printf ("switch quantise: %.0f samples left, bar of 16 expects %.0f\n",
                     left, expected);
        check (std::abs (left - expected) < sps * 0.5,
               "a pattern switch did not quantise to the master length");
    }

    // ---- 5. Shuffle stays on the grid, not on the lane's cycle -------------
    {
        // A lane of 7 under shuffle: its hits must land late on the odd
        // sixteenths of the *bar*. With the parity taken from the lane's own
        // wrapped index it would flip every time the lane wrapped.
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (0xFFFFu);   // every step, so every parity is sampled
        seq.laneLength[0].store (7);

        const double sps = sr * 15.0 / bpm;
        const float shuffle = 0.6f;
        const int steps = 28;
        const int totalSamples = (int) std::llround (sps * steps);

        std::vector<DrumEvent> events;
        std::vector<double> lateness;
        long long sample = 0;
        for (int done = 0; done < totalSamples; done += block)
        {
            const int n = std::min (block, totalSamples - done);
            seq.process (n, bpm, true, false, 0.0, shuffle, 16, events);
            for (const auto& e : events)
                if (e.voice == 0)
                {
                    const double at = (double) (sample + e.offset);
                    const double step = std::floor (at / sps + 0.5);
                    lateness.push_back (at - step * sps);
                }
            sample += n;
        }

        // odd steps late, even steps on the beat, judged by absolute position
        int wrong = 0;
        for (size_t i = 0; i < lateness.size(); ++i)
        {
            const bool odd = (i & 1) != 0;
            const bool late = lateness[i] > sps * 0.1;
            if (odd != late)
                ++wrong;
        }
        std::printf ("shuffle: %zu hits, %d with the wrong swing\n", lateness.size(), wrong);
        check (wrong == 0, "a 7-step lane did not swing with the grid");
    }

    // ---- 6. A ratchet fires n times, evenly, inside its own step -----------
    for (int count = 1; count <= DrumSequencer::maxRatchet; ++count)
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (1u << 0);
        seq.setRatchetAt (0, 0, count);

        const double sps = sr * 15.0 / bpm;
        std::vector<DrumEvent> events;
        std::vector<double> at;
        long long sample = 0;
        const int totalSamples = (int) std::llround (sps * 4);

        for (int done = 0; done < totalSamples; done += block)
        {
            const int n = std::min (block, totalSamples - done);
            seq.process (n, bpm, true, false, 0.0, 0.0f, 16, events);
            for (const auto& e : events)
                if (e.voice == 0)
                    at.push_back ((double) (sample + e.offset));
            sample += n;
        }

        char msg[96];
        std::snprintf (msg, sizeof msg, "a ratchet of %d did not fire %d times", count, count);
        check ((int) at.size() == count, msg);

        if ((int) at.size() == count)
        {
            // evenly spread across the step, and all of them inside it
            bool even = true;
            for (int i = 0; i < count; ++i)
                if (std::abs (at[(size_t) i] - (double) i * sps / count) > 2.0)
                    even = false;
            std::snprintf (msg, sizeof msg, "a ratchet of %d was not evenly spaced", count);
            check (even, msg);

            std::snprintf (msg, sizeof msg, "a ratchet of %d spilled past its step", count);
            check (at.back() < sps, msg);
        }
        std::printf ("ratchet %d: %zu hits, last at %.3f of the step\n",
                     count, at.size(), at.empty() ? 0.0 : at.back() / sps);
    }

    // ---- 7. A ratchet needs a hit under it ---------------------------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        // ratchet bits set on a step with no hit, the way a stale file could
        seq.setRatchetAt (0, 0, 4);
        seq.stepMask[0].store (0);
        seq.normalise();

        check (seq.ratchetAt (0, 0) == 1, "normalise left a ratchet on an empty step");

        const auto hits = firings (seq, 0, 16, 16);
        check (hits.empty(), "an empty step fired anyway");
    }

    // ---- 8. Ratchets and per-lane length compose ---------------------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (1u << 0);
        seq.setRatchetAt (0, 0, 3);
        seq.laneLength[0].store (6);

        // 24 steps of a 6-step lane is four times round, three hits each
        const auto hits = firings (seq, 0, 16, 24);
        check (hits.size() == 12, "a ratchet on a short lane did not repeat with it");

        const std::vector<int> want { 0, 0, 0, 6, 6, 6, 12, 12, 12, 18, 18, 18 };
        check (same (hits, want), "the repeats did not land on the lane's own cycle");
        std::printf ("ratcheted 6-step lane: %zu hits over 24 steps\n", hits.size());
    }

    // ---- 9. Fitting a lane that follows the master changes nothing ---------
    // The span works out at one sixteenth, so this is the identity case, and it
    // is the one a user hits by fitting a lane before shortening it.
    {
        DrumSequencer seq, plain;
        seq.prepare (sr);
        plain.prepare (sr);
        allOn (seq);
        allOn (plain);
        seq.stepMask[0].store ((1u << 0) | (1u << 5));
        plain.stepMask[0].store ((1u << 0) | (1u << 5));
        seq.laneFit[0].store (true);

        check (same (firings (seq, 0, 16, 48), firings (plain, 0, 16, 48)),
               "fitting a lane that already spans the bar moved it");
    }

    // ---- 10. Three steps fitted to a 16-step bar are 3 against 4 -----------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (0b111);   // all three steps
        seq.laneLength[0].store (3);
        seq.laneFit[0].store (true);

        const auto hits = firingTimes (seq, 0, 16, 32);
        const double span = 16.0 / 3.0;
        const std::vector<double> want { 0.0, span, 2 * span,
                                         16.0, 16.0 + span, 16.0 + 2 * span };
        check (sameTimes (hits, want), "three fitted steps were not evenly spaced across the bar");
        std::printf ("3 fitted to 16: %.3f %.3f %.3f (span %.3f)\n",
                     hits[0], hits[1], hits[2], span);
    }

    // ---- 11. A fitted lane locks to the bar instead of drifting ------------
    // This is the whole difference from a short lane: same three hits a bar, in
    // the same three places every bar, however many bars run.
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (0b111);
        seq.laneLength[0].store (3);
        seq.laneFit[0].store (true);

        const auto hits = firingTimes (seq, 0, 16, 16 * 5);
        check (hits.size() == 15, "a fitted 3-step lane did not fire three times a bar");

        bool locked = true;
        for (size_t i = 3; i < hits.size(); ++i)
            if (! near (hits[i] - hits[i - 3], 16.0))
                locked = false;
        check (locked, "a fitted lane drifted against the bar");
    }

    // ---- 12. Twelve fitted steps are eighth-note triplets ------------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (0xFFFu);   // all twelve
        seq.laneLength[0].store (12);
        seq.laneFit[0].store (true);

        const auto hits = firingTimes (seq, 0, 16, 16);
        check (hits.size() == 12, "twelve fitted steps did not give twelve hits a bar");

        // three to the quarter — a quarter is four sixteenths, so 4/3 apart
        bool even = true;
        for (size_t i = 1; i < hits.size(); ++i)
            if (! near (hits[i] - hits[i - 1], 4.0 / 3.0))
                even = false;
        check (even, "eighth triplets were not evenly spaced");
    }

    // ---- 13. Ratchets subdivide a fitted step, not a sixteenth -------------
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (1u << 0);
        seq.setRatchetAt (0, 0, 2);
        seq.laneLength[0].store (3);
        seq.laneFit[0].store (true);

        const auto hits = firingTimes (seq, 0, 16, 16);
        const double span = 16.0 / 3.0;
        check (sameTimes (hits, { 0.0, span / 2.0 }),
               "a ratchet on a fitted step did not halve that step");
    }

    // ---- 14. Shuffle swings a fitted lane on its own clock -----------------
    // Not on the sixteenth grid, which a fitted lane never touches: the odd
    // step is pushed by shuffle/3 of the *lane's* step.
    {
        DrumSequencer seq;
        seq.prepare (sr);
        allOn (seq);
        seq.stepMask[0].store (0b111);
        seq.laneLength[0].store (3);
        seq.laneFit[0].store (true);

        const float shuffle = 0.6f;
        const double span = 16.0 / 3.0;
        const double swing = (double) shuffle * span / 3.0;

        const auto hits = firingTimes (seq, 0, 16, 16, shuffle);
        check (sameTimes (hits, { 0.0, span + swing, 2 * span }),
               "shuffle on a fitted lane did not ride the lane's own step");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
