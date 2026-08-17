// Measures what modulating CUT OFF at block rate actually costs, so the LFO's
// update rate is chosen from a number rather than by copying the EQ's.
//
// Nothing in the plugin modulates fast yet. `processBlock` sets every parameter
// once per block and hands each unit the whole block, and the XY pad gets away
// with that because a mouse moves slowly — an LFO is the first thing that asks
// the voice to move at audio-adjacent rates. `Eq.h` hit this and answered it
// with a 32-sample chunk, so chunking `processBlock` the same way is the
// obvious move. This measures whether it is the right one. It is not.
//
// The finding, which is that chunking is the wrong fix:
//
// Cutoff is skewed (0.3), so a fixed step in normalised units is a much larger
// step in Hz down at the bottom where the knob spends its time. The table below
// is in cents for that reason — a musical unit, on the axis the ear actually
// judges a sweep by. A 1/16 LFO at a 512-sample block leaves risers of several
// thousand cents, and even `Eq.h`'s 32-sample chunk leaves a couple of hundred.
//
// The cents figure is an upper bound on the *problem*, though, not a measure of
// how loud it is, and the two come apart at the top of the sweep: a jump from
// 3 kHz to 4.8 kHz is the same 800 cents as one from 300 Hz to 480 Hz and is
// nothing like as obvious on a resonant filter. So the table ranks intervals
// honestly and does not decide anything on its own — the WAVs at the end are
// what settles it.
//
// Listened to, at a 1/16 and this depth, the artefact reads as *comb filtering*
// rather than as stepping: sampling the modulator at 43 or 86 Hz puts sidebands
// either side of everything the filter passes, and that is what the ear picks
// up first. Established by ear rather than by table, at a true 1/16:
//
//   1024 (5770 cents)  clearly combed, deeper than 512
//    512 (3368 cents)  combed
//    128 ( 895 cents)  indistinguishable from per-sample
//      1 (   7 cents)  the ideal
//
// So the boundary sits between 128 and 512 — a very long way from the 10-20
// cents a rule of thumb would have predicted, and worth remembering before
// trusting the cents column on its own again. It ranks intervals correctly and
// is useless as an absolute scale.
//
// **128 being transparent is what decided the design.** With cutoff — the most
// stepping-sensitive destination in the plugin — clean at 128, there is no need
// for a modulation input inside `Synth303` at all: one chunk loop in
// `processBlock` covers the voice and the FX units alike, and an LFO becomes
// another offset applied exactly where `macropad::Pad::apply` already applies
// one. Half the mechanism, and no new API on the voice.
//
// 64 is the size to use rather than 128, and the cents table is what justifies
// it: a 1/32 LFO at 64 lands on 896 cents, which is the same riser as the 1/16
// at 128 that measured transparent. So 64 buys the fastest rate worth offering
// at the transparency that was actually tested, rather than extrapolating past
// what the ear confirmed. It is still twice as coarse as `Eq.h`'s 32.
//
// The benchmark at the end also records what per-sample modulation would have
// cost, which is nothing measurable — the ladder already calls `std::tan` every
// sample for the filter envelope, so a modulator evaluated alongside it rides
// inside work the voice is already doing. That mattered while per-sample looked
// necessary. It no longer is (see the listening notes below), and the figure is
// kept only so the option can be re-costed if a much faster LFO is ever wanted.
//
// One thing deliberately *not* asserted here is anything of the form "the
// filter does not click". It sounds like the obvious property to pin and it is
// not well posed: changing a lowpass's cutoff legitimately changes its output
// on the same sample, so the post-change signal is steeper simply because it is
// brighter, and no threshold separates that from an artefact. What is well
// posed is convergence — that coarser updates approach the per-sample ideal as
// the interval shrinks — and that is what the second check measures.
//
// Build: clang++ -std=c++17 -O2 Tools/lfo_rate_test.cpp -o lfo_rate_test

#include "../Source/MacroPad.h"
#include "../Source/StepDyn.h"
#include "../Source/Synth303.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        if (! ok)
        {
            std::printf ("FAIL: %s\n", what);
            ++failures;
        }
    }

    constexpr int    sr      = 44100;
    constexpr double bpm     = 130.0;
    constexpr int    samples = sr * 2;
    constexpr int    settle  = sr / 4;   // skip the amp envelope's rise

    // The worst case the voice offers rather than a comfortable one: resonance
    // high enough to make the filter's position obvious, and ENV MOD at zero so
    // the only thing moving cutoff is the LFO. With env mod up the note's own
    // sweep would swamp the artefact and flatter every interval equally.
    constexpr float baseCutoff = 500.0f;   // the default the knob starts on
    constexpr float resonance  = 0.9f;

    // Half the knob's travel: a wide acid sweep, but not a contrived one.
    constexpr float depth = 0.5f;

    void writeWav (const char* path, const std::vector<float>& s)
    {
        std::ofstream f (path, std::ios::binary);
        auto w32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
        auto w16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };

        const uint32_t dataBytes = (uint32_t) s.size() * 2;
        f.write ("RIFF", 4); w32 (36 + dataBytes); f.write ("WAVE", 4);
        f.write ("fmt ", 4); w32 (16); w16 (1); w16 (1);
        w32 ((uint32_t) sr); w32 ((uint32_t) sr * 2); w16 (2); w16 (16);
        f.write ("data", 4); w32 (dataBytes);
        for (float v : s)
        {
            const float c = std::fmin (1.0f, std::fmax (-1.0f, v));
            w16 ((uint16_t) (int16_t) (c * 32767.0f));
        }
    }

    // The cutoff an LFO would ask for at sample `i`, through the same normalised
    // mapping the pad uses — depths are a fraction of the knob's own travel, so
    // the skew is part of what the modulation does and has to be part of what is
    // measured.
    float cutoffAt (double rateHz, double i)
    {
        const auto& r = macropad::range (macropad::Cutoff);
        const double phase = 2.0 * 3.14159265358979 * rateHz * i / sr;
        const float  amt   = depth * (float) std::sin (phase);
        return r.from01 (std::fmin (1.0f, std::fmax (0.0f,
                             r.to01 (baseCutoff) + amt)));
    }

    // One sustained note with cutoff re-set every `interval` samples, which is
    // what a chunked processBlock does: the modulator is sampled at the start of
    // a chunk and held across it. interval == 1 is the ideal.
    std::vector<float> render (double rateHz, float d, int interval)
    {
        Synth303 synth;
        synth.prepare (sr);
        const auto set = [&] (float cut)
        {
            synth.setParams (Synth303::Wave::Saw, 0.0f, cut, resonance,
                             0.0f, 400.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        };
        set (baseCutoff);
        synth.noteOn (45, dyn303::Normal, false);

        std::vector<float> out ((size_t) samples);
        for (int i = 0; i < samples; i += interval)
        {
            set (d > 0.0f ? cutoffAt (rateHz, i) : baseCutoff);
            synth.render (out.data() + i, std::min (interval, samples - i));
        }
        return out;
    }

    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }

    double cents (float from, float to)
    {
        return 1200.0 * std::log2 ((double) to / (double) from);
    }
}

int main()
{
    // bpm/60 is *quarter* notes a second. A 1/16 LFO completes a cycle per
    // sixteenth note, so it is four times that — spelled out here because
    // getting it wrong once already made every row of this table read four
    // times slower than its label.
    struct Rate { const char* name; double hz; };
    const Rate rates[] = {
        { "0.5 Hz free", 0.5 },
        { "1/4  @130",   bpm / 60.0 },
        { "1/8  @130",   bpm / 60.0 * 2.0 },
        { "1/16 @130",   bpm / 60.0 * 4.0 },
        { "1/32 @130",   bpm / 60.0 * 8.0 }
    };

    const double sixteenth = bpm / 60.0 * 4.0;

    const int intervals[] = { 1024, 512, 256, 128, 64, 32, 16, 8, 1 };

    // --- the staircase ------------------------------------------------------
    // Exact arithmetic on the mapping, no rendering: the riser height is a
    // property of the modulation, and rendering it would only add the filter's
    // opinion to a number that is already decided.
    std::printf ("Largest cutoff jump between consecutive updates, in cents.\n");
    std::printf ("Base %.0f Hz, depth %.2f of knob travel (skew 0.3).\n",
                 baseCutoff, depth);
    std::printf ("Rule of thumb only: a sweep tends to read as stepped somewhere\n"
                 "around 10-20 cents a riser, but cents overstate the top of the\n"
                 "sweep, so rank intervals by this and judge loudness by the WAVs.\n\n");
    std::printf ("  %-12s", "rate");
    for (int n : intervals)
        std::printf ("%8d", n);
    std::printf ("\n");

    for (const auto& rate : rates)
    {
        std::printf ("  %-12s", rate.name);
        for (int n : intervals)
        {
            double worst = 0.0;
            for (int i = n; i < samples; i += n)
                worst = std::max (worst, std::abs (cents (cutoffAt (rate.hz, i - n),
                                                          cutoffAt (rate.hz, i))));
            std::printf ("%8.0f", worst);
        }
        std::printf ("\n");
    }

    // --- the audio converges on the per-sample ideal -------------------------
    // The well-posed half of "how wrong is it". This is a convergence check and
    // not an audibility one: the number is dominated by phase, because a
    // resonant ladder at a fractionally different cutoff shifts the whole
    // waveform in time, and a shift that large in RMS terms is inaudible. What
    // it does prove is that the staircase is the only thing going on — coarser
    // updates approach the ideal smoothly rather than diverging into something
    // structurally different. Judge loudness by the cents table and by the WAVs.
    std::printf ("\nDeviation from the per-sample ideal (phase-dominated — this "
                 "shows convergence,\nnot audibility):\n\n");
    std::printf ("  %-12s", "rate");
    for (int n : intervals)
        if (n > 1) std::printf ("%8d", n);
    std::printf ("\n");

    for (const auto& rate : rates)
    {
        const auto ref = render (rate.hz, depth, 1);
        std::printf ("  %-12s", rate.name);

        double prev = 0.0;
        bool   first = true, monotonic = true;

        for (int n : intervals)
        {
            if (n == 1)   // the reference against itself says nothing
                continue;

            const auto x = render (rate.hz, depth, n);
            double sigSq = 0.0, errSq = 0.0;
            for (size_t i = settle; i < ref.size(); ++i)
            {
                const double e = (double) x[i] - ref[i];
                sigSq += (double) ref[i] * ref[i];
                errSq += e * e;
            }
            const double db = errSq <= 0.0 ? -200.0
                            : 20.0 * std::log10 (std::sqrt (errSq / sigSq));
            std::printf ("%8.1f", db);

            // Intervals descend, so each should be at least as good as the last.
            if (! first && db > prev + 1.0)
                monotonic = false;
            prev = db;
            first = false;
        }
        std::printf ("\n");
        check (monotonic, "error should fall as the update interval shrinks");
    }

    // --- zero depth costs nothing -------------------------------------------
    // Not close, identical. An LFO present but unrouted must leave the voice
    // exactly where it was, which is the guarantee the flat-EQ branch,
    // DrumSequencer::laneClock and macropad::Pad::apply each make in their own
    // way — the cheap way to be sure nothing moved is not to compute it.
    {
        const auto ref = render (0.0, 0.0f, 1);
        bool allIdentical = true;
        for (int n : intervals)
            allIdentical = allIdentical && identical (render (0.0, 0.0f, n), ref);

        check (allIdentical, "zero depth must be bit-identical at every interval");
        check (! identical (ref, render (sixteenth, depth, 1)),
               "depth must reach the filter");
    }

    // --- per-sample modulation is close to free -----------------------------
    // The claim the plan rests on: the ladder already recomputes `tan` every
    // sample for the filter envelope, so evaluating a modulator per sample rides
    // along inside work the voice is doing anyway.
    //
    // The cutoff values are precomputed, because the skew round trip is the
    // test harness's cost and not the voice's — a real implementation resolves
    // the LFO's target once per block and interpolates between targets per
    // sample, so it never calls `pow` in the inner loop either. What is left is
    // still an upper bound: it drives every sample through the whole of
    // `setParams`, which recomputes gain and re-looks-up the held note. The
    // "same value" row isolates exactly that overhead, so the true cost of the
    // modulation is the gap between the two right-hand columns rather than the
    // gap from the left.
    //
    // Reported rather than asserted — a timing threshold in a test is a flake
    // waiting to happen — but a regression here would mean revisiting the plan.
    {
        std::vector<float> table ((size_t) samples);
        for (int i = 0; i < samples; ++i)
            table[(size_t) i] = cutoffAt (sixteenth, i);

        const auto time = [&] (int interval, bool useTable)
        {
            double best = 1.0e30;
            for (int rep = 0; rep < 5; ++rep)
            {
                Synth303 synth;
                synth.prepare (sr);
                synth.setParams (Synth303::Wave::Saw, 0.0f, baseCutoff, resonance,
                                 0.0f, 400.0f, 0.0f, 0.0f, 0.0f, 0.0f);
                synth.noteOn (45, dyn303::Normal, false);
                std::vector<float> out ((size_t) samples);

                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < samples; i += interval)
                {
                    synth.setParams (Synth303::Wave::Saw, 0.0f,
                                     useTable ? table[(size_t) i] : baseCutoff,
                                     resonance, 0.0f, 400.0f, 0.0f, 0.0f, 0.0f, 0.0f);
                    synth.render (out.data() + i, std::min (interval, samples - i));
                }
                const auto t1 = std::chrono::steady_clock::now();

                if (out[samples / 2] == 12345.0f) std::printf (" ");   // keep it alive
                best = std::min (best,
                                 std::chrono::duration<double> (t1 - t0).count());
            }
            return best / samples * 1.0e9;
        };

        const double perBlock = time (512, true);
        const double sameVal  = time (1, false);
        const double perSamp  = time (1, true);
        std::printf ("\nVoice cost, ns/sample:  %.1f per-block  |  %.1f setParams "
                     "every sample, same value  |  %.1f modulated every sample\n",
                     perBlock, sameVal, perSamp);
        std::printf ("The modulation itself is the last gap: %+.0f%% over the "
                     "same-value row.\n", (perSamp / sameVal - 1.0) * 100.0);
    }

    // The whole ladder for ears, at a genuine 1/16 — the fastest rate worth
    // supporting and so the one that decides the chunk size. The numbers cannot
    // settle audibility and are not trying to; these are what settles it, which
    // is why every interval gets a file rather than a chosen few.
    for (int n : intervals)
    {
        char path[64];
        std::snprintf (path, sizeof (path), "lfo_rate_16th_%04d.wav", n);
        writeWav (path, render (sixteenth, depth, n));
    }
    std::printf ("wrote lfo_rate_16th_NNNN.wav for every interval, at a true "
                 "1/16 @130 (%.2f Hz)\n", sixteenth);

    std::printf (failures == 0 ? "\nALL PASS\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
