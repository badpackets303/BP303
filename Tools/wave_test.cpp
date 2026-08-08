// The oscillator's waveforms, measured rather than eyeballed.
//
// The interesting claim is spectral: a square is symmetric and so has only odd
// harmonics, while a narrowed pulse is not and brings the even ones in. That is
// the whole reason PULSE sounds different, so it is what gets asserted here.
// The filter is opened right up and the envelope taken out of the way first, so
// what is measured is the oscillator and not the ladder.
//
// JUCE-free: this only needs Synth303.

#include "../Source/Synth303.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok)
            ++failures;
    }

    constexpr double sr = 96000.0;   // high, to keep aliasing off the low harmonics
    constexpr int    note = 45;      // A2, 110 Hz
    constexpr double freq = 110.0;

    // Spelled out rather than M_PI, which MSVC only defines behind
    // _USE_MATH_DEFINES — the DSP headers do the same, so the Windows build
    // doesn't hinge on it.
    constexpr double pi = 3.14159265358979323846;

    // Render one steady note with the filter wide open and no envelope movement,
    // so the output is the raw oscillator through a barely-doing-anything ladder.
    std::vector<float> render (Synth303::Wave wave, int numSamples)
    {
        Synth303 synth;
        synth.prepare (sr);
        //           wave  tuning  cutoff    res   envMod  decay  accent  vol   vib
        synth.setParams (wave, 0.0f, 20000.0f, 0.0f, 0.0f, 2000.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        synth.noteOn (note, 0, false);

        std::vector<float> out ((size_t) numSamples);
        synth.render (out.data(), numSamples);
        return out;
    }

    // Amplitude of one harmonic, by correlating against a sine and cosine at that
    // frequency (a single-bin DFT). Windowless is fine here: the analysis length
    // is set to a whole number of cycles of the fundamental.
    double harmonic (const std::vector<float>& x, int offset, int count, int which)
    {
        const double w = 2.0 * pi * freq * (double) which / sr;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double s = x[(size_t) (offset + i)];
            re += s * std::cos (w * i);
            im += s * std::sin (w * i);
        }
        return 2.0 * std::sqrt (re * re + im * im) / count;
    }

    double meanOf (const std::vector<float>& x, int offset, int count)
    {
        double sum = 0.0;
        for (int i = 0; i < count; ++i)
            sum += x[(size_t) (offset + i)];
        return sum / count;
    }
}

int main()
{
    // A whole number of fundamental cycles, starting past the amp attack.
    const int cycles = 200;
    const int count  = (int) std::lround (sr / freq * cycles);
    const int offset = (int) (sr * 0.05);
    const int total  = offset + count;

    const auto saw    = render (Synth303::Wave::Saw, total);
    const auto square = render (Synth303::Wave::Square, total);
    const auto pulse  = render (Synth303::Wave::Pulse, total);

    // --- every wave leaves the voice centred -----------------------------------
    // Two separate things have to go right for the pulse. Its own mean is taken
    // off at the oscillator, and then the ladder's tanh puts DC *back* — an odd
    // function fed an asymmetric wave does that, about -0.12 at 25% — which the
    // DC blocker on the way out takes off again.
    std::printf ("      [dc: saw %+.4f  square %+.4f  pulse %+.4f]\n",
                 meanOf (saw, offset, count), meanOf (square, offset, count),
                 meanOf (pulse, offset, count));
    check (std::abs (meanOf (saw, offset, count))    < 0.01, "SAW has no DC offset");
    check (std::abs (meanOf (square, offset, count)) < 0.01, "SQUARE has no DC offset");
    check (std::abs (meanOf (pulse, offset, count))  < 0.01,
           "PULSE has none either, though it is the one that would have had it");

    // --- the spectral claim ----------------------------------------------------
    const double sq1 = harmonic (square, offset, count, 1);
    const double sq2 = harmonic (square, offset, count, 2);
    const double sq3 = harmonic (square, offset, count, 3);

    const double pu1 = harmonic (pulse, offset, count, 1);
    const double pu2 = harmonic (pulse, offset, count, 2);
    const double pu3 = harmonic (pulse, offset, count, 3);

    std::printf ("      [square h1 %.3f h2 %.3f h3 %.3f]\n", sq1, sq2, sq3);
    std::printf ("      [pulse  h1 %.3f h2 %.3f h3 %.3f]\n", pu1, pu2, pu3);

    check (sq2 < sq1 * 0.02,
           "a SQUARE is symmetric, so it has essentially no 2nd harmonic");

    // A pulse's nth harmonic scales with sin(n * pi * width). At 25% the 2nd goes
    // from nothing to sin(pi/2) = 1, against a fundamental scaled by
    // sin(pi/4) = 0.707 — so the ratio the ear reads as "reedy" is a 2nd harmonic
    // sitting at 0.707 of the 1st. That is the whole tonal claim, and it is an
    // exact number rather than a direction, so it is worth pinning down.
    std::printf ("      [pulse h2/h1 %.3f, theory 0.707]\n", pu2 / pu1);
    check (pu2 / pu1 > 0.66 && pu2 / pu1 < 0.75,
           "a 25% PULSE brings in a 2nd harmonic at 0.707 of its fundamental");

    // Both odd harmonics thin by the same sin(pi/4), so the pulse is ~3 dB down
    // on the square across them. Measured a little below that because the ladder's
    // tanh squashes the asymmetric peaks harder than it does a square's.
    check (pu1 > sq1 * 0.55 && pu1 < sq1 * 0.75,
           "and loses about 3 dB of fundamental, which is what thins it out");
    check (pu3 > sq3 * 0.55 && pu3 < sq3 * 0.75,
           "with the 3rd harmonic thinning by the same amount");

    // --- the saw is still a saw ------------------------------------------------
    // Guards the shared pulse branch: a mistake in the width maths must not have
    // leaked into the other wave.
    const double sa1 = harmonic (saw, offset, count, 1);
    const double sa2 = harmonic (saw, offset, count, 2);
    check (sa2 > sa1 * 0.35 && sa2 < sa1 * 0.65,
           "SAW keeps its 1/n harmonic series (2nd about half the 1st)");

    // --- narrowing must not have cost anti-aliasing ----------------------------
    // A pulse has two edges per cycle instead of the saw's one, so if the second
    // PolyBLEP were misplaced it would fold energy back down the spectrum. A
    // partial that should not exist between harmonics is the tell.
    double inharmonic = 0.0;
    for (int i = 0; i < count; ++i)
    {
        // 2.5x the fundamental: a real pulse has nothing there at all.
        const double w = 2.0 * pi * freq * 2.5 / sr;
        inharmonic += pulse[(size_t) (offset + i)] * std::cos (w * i);
    }
    inharmonic = 2.0 * std::abs (inharmonic) / count;
    check (inharmonic < pu1 * 0.05,
           "PULSE is still band-limited - no energy between its harmonics");

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
