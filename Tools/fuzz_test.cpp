// Offline test for DrumFuzz: bypass transparency, boundedness / no NaN,
// DC removal, and increased harmonic density (crest factor) under drive.
// Build: clang++ -std=c++17 -O2 Tools/fuzz_test.cpp -o fuzz_test

#include "../Source/DrumFuzz.h"

#include <cstdio>
#include <vector>

static double dcOffset (const std::vector<float>& v)
{
    double s = 0;
    for (float x : v) s += x;
    return s / (double) v.size();
}

int main()
{
    const int sr = 44100;
    int failures = 0;

    auto makeTone = [&] (float amp) {
        std::vector<float> b (sr);
        for (int i = 0; i < sr; ++i)
            b[i] = amp * std::sin (6.2831853f * 110.0f * (float) i / sr);
        return b;
    };

    // --- bypass: off is transparent (even with drive/colour dialled up) ---
    {
        DrumFuzz f; f.prepare (sr);
        f.setParams (false, 0.8f, 1.0f);
        auto b = makeTone (0.5f);
        auto ref = b;
        f.process (b.data(), (int) b.size());
        bool same = true;
        for (size_t i = 0; i < b.size(); ++i)
            if (std::abs (b[i] - ref[i]) > 1.0e-6f) { same = false; break; }
        std::printf ("bypass transparent: %s\n", same ? "yes" : "NO");
        if (! same) ++failures;
    }

    // --- driven: bounded, finite, low DC, and grittier than input ---
    // fuzz squares the wave, so crest factor sits well below a clean sine's
    // 1.414 (a square is ~1.0); increasing asymmetry nudges it up a touch.
    for (float drive : { 0.4f, 0.8f, 1.0f })
    {
        DrumFuzz f; f.prepare (sr);
        f.setParams (true, drive, 0.0f);   // colour 0 keeps the clean-clip crest
        auto b = makeTone (0.5f);
        f.process (b.data(), (int) b.size());

        float peak = 0; double sumSq = 0; int nans = 0;
        for (float x : b)
        {
            if (! std::isfinite (x)) ++nans;
            peak = std::max (peak, std::abs (x));
            sumSq += (double) x * x;
        }
        const double rms = std::sqrt (sumSq / (double) b.size());
        const double crest = peak / (rms + 1e-9);   // sine ~1.41; square ~1.0
        const double dc = dcOffset (b);

        std::printf ("drive %.1f: peak=%.3f rms=%.3f crest=%.2f dc=%.5f nans=%d\n",
                     drive, peak, rms, crest, dc, nans);

        if (! (nans == 0 && peak <= 1.001f && rms > 0.05
               && crest < 1.30 && std::abs (dc) < 0.02))
            ++failures;
    }

    // --- colour: raising COLOR lifts the highs (brighter/buzzier) ---
    // The pre-emphasis tilt boosts content above ~700 Hz, so probe with a
    // bright tone and measure HF energy via the mean squared first difference.
    {
        auto brightTone = [&] {
            std::vector<float> b (sr);
            for (int i = 0; i < sr; ++i)
                b[i] = 0.5f * std::sin (6.2831853f * 2500.0f * (float) i / sr);
            return b;
        };
        auto hfEnergy = [&] (float color) {
            DrumFuzz f; f.prepare (sr);
            f.setParams (true, 0.7f, color);
            auto b = brightTone();
            f.process (b.data(), (int) b.size());
            double s = 0; int nans = 0;
            for (size_t i = 1; i < b.size(); ++i)
            {
                if (! std::isfinite (b[i])) ++nans;
                const double d = (double) b[i] - b[i - 1];
                s += d * d;
            }
            return nans == 0 ? s / (double) b.size() : -1.0;
        };
        const double dark   = hfEnergy (0.0f);
        const double bright = hfEnergy (1.0f);
        std::printf ("colour: hf(dark)=%.4f hf(bright)=%.4f\n", dark, bright);
        if (! (dark > 0.0 && bright > dark * 1.10))
            ++failures;
    }

    std::printf (failures == 0 ? "FUZZ-TEST OK\n" : "FUZZ-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
