// Offline tests for the selectable per-line Distortion:
//   bypass transparency, every type bounded and NaN-free, each type's defining
//   behaviour (soft vs hard clipping, quantisation, folding, octave-up), and the
//   LOWS band split keeping the sub clean while the top is destroyed.
// Build: clang++ -std=c++17 -O2 Tools/dist_test.cpp -o dist_test

#include "../Source/Distortion.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr int sr = 44100;

    double rms (const std::vector<float>& v, size_t from = 0)
    {
        double s = 0.0;
        for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
        return std::sqrt (s / (double) (v.size() - from));
    }

    float peak (const std::vector<float>& v)
    {
        float p = 0.0f;
        for (float x : v) p = std::max (p, std::abs (x));
        return p;
    }

    int countNans (const std::vector<float>& v)
    {
        int n = 0;
        for (float x : v) if (! std::isfinite (x)) ++n;
        return n;
    }

    std::vector<float> sine (int n, double freqHz, float amp)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * std::sin (6.2831853 * freqHz * i / sr);
        return v;
    }

    // How many distinct sample values appear — a bit crusher collapses this.
    size_t distinctValues (const std::vector<float>& v, size_t from)
    {
        std::vector<float> vals (v.begin() + (long) from, v.end());
        std::sort (vals.begin(), vals.end());
        vals.erase (std::unique (vals.begin(), vals.end()), vals.end());
        return vals.size();
    }

    // Energy at a given frequency, by correlating against a complex exponential.
    double bandEnergy (const std::vector<float>& v, double freqHz, size_t from)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = from; i < v.size(); ++i)
        {
            const double ph = 6.2831853 * freqHz * (double) i / sr;
            re += v[i] * std::cos (ph);
            im += v[i] * std::sin (ph);
        }
        const double n = (double) (v.size() - from);
        return std::sqrt (re * re + im * im) / n;
    }

    Distortion::Params base (int type)
    {
        Distortion::Params p;
        p.on = true;
        p.type = type;
        return p;
    }

    std::vector<float> run (const Distortion::Params& p, std::vector<float> buf)
    {
        Distortion d;
        d.prepare (sr);
        d.setParams (p);
        d.process (buf.data(), (int) buf.size());
        return buf;
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    static const char* typeName[] = { "SOFT", "FUZZ", "CRUSH", "FOLD", "RECT" };

    // ---- every type: off is bit-transparent ----
    for (int t = 0; t < Distortion::numTypes; ++t)
    {
        auto p = base (t);
        p.on = false;
        const auto ref = sine (4096, 220.0, 0.5f);
        const auto out = run (p, ref);
        bool same = true;
        for (size_t i = 0; i < out.size(); ++i) if (out[i] != ref[i]) { same = false; break; }
        if (! same) { ++failures; std::printf ("FAIL: %s bypass not transparent\n", typeName[t]); }
    }

    // ---- every type: bounded and finite on a hot input ----
    for (int t = 0; t < Distortion::numTypes; ++t)
    {
        auto p = base (t);
        p.drive = 1.0f; p.foldAmount = 1.0f; p.rectAmount = 1.0f;
        p.bits = 2.0f;  p.rateSamples = 9.0f;
        const auto out = run (p, sine (sr, 110.0, 0.95f));
        std::printf ("%-5s hot: peak=%.3f rms=%.3f nans=%d\n",
                     typeName[t], peak (out), rms (out), countNans (out));
        if (countNans (out) != 0) { ++failures; std::printf ("FAIL: %s NaNs\n", typeName[t]); }
        if (peak (out) > 2.0f)    { ++failures; std::printf ("FAIL: %s unbounded\n", typeName[t]); }
    }

    // ---- SOFT: adds harmonics but stays smooth (no hard corners) ----
    {
        auto p = base (Distortion::Soft);
        p.drive = 0.8f;
        const auto out = run (p, sine (sr, 220.0, 0.6f));
        const double f1 = bandEnergy (out, 220.0, sr / 4);
        const double f3 = bandEnergy (out, 660.0, sr / 4);
        std::printf ("SOFT: f1=%.4f f3=%.4f\n", f1, f3);
        check (f3 > f1 * 0.02, "soft clip produced no third harmonic");
        check (f3 < f1 * 0.8, "soft clip is not smooth (third harmonic dominates)");
    }

    // ---- FUZZ: harder than SOFT ----
    // Driven hard both curves approach a square wave, so the giveaway is the
    // high-order content: a corner spreads energy far up the series, where tanh
    // rolls off. Compare the 9th harmonic, not the 3rd.
    {
        auto pSoft = base (Distortion::Soft);  pSoft.drive = 0.35f;
        auto pFuzz = base (Distortion::Fuzz);  pFuzz.drive = 0.35f;
        const auto in = sine (sr, 220.0, 0.3f);

        auto ratio = [&] (const Distortion::Params& p) {
            const auto out = run (p, in);
            return bandEnergy (out, 220.0 * 9.0, sr / 4) / bandEnergy (out, 220.0, sr / 4);
        };
        const double soft = ratio (pSoft), fuzz = ratio (pFuzz);
        std::printf ("9th-harmonic ratio: soft=%.5f fuzz=%.5f\n", soft, fuzz);
        check (fuzz > soft * 2.0, "fuzz is not harder than soft clip");
    }

    // ---- CRUSH: quantises to few levels, and holds samples ----
    {
        auto p = base (Distortion::Crush);
        p.bits = 3.0f;            // 4 quantisation levels
        p.rateSamples = 1.0f;     // no decimation, isolate the bit depth
        const auto out = run (p, sine (sr / 4, 220.0, 0.9f));
        const auto levels = distinctValues (out, 128);
        std::printf ("CRUSH 3-bit: %zu distinct values\n", levels);
        check (levels <= 10, "bit crusher did not collapse the value set");
        check (levels > 1, "bit crusher flattened everything");
    }
    {
        auto p = base (Distortion::Crush);
        p.bits = 16.0f;           // transparent bit depth, isolate the hold
        p.rateSamples = 8.0f;
        const auto out = run (p, sine (sr / 4, 220.0, 0.9f));

        // with an 8-sample hold, runs of identical samples must appear
        int longestRun = 1, current = 1;
        for (size_t i = 1 + 128; i < out.size(); ++i)
        {
            current = out[i] == out[i - 1] ? current + 1 : 1;
            longestRun = std::max (longestRun, current);
        }
        std::printf ("CRUSH hold=8: longest run of equal samples=%d\n", longestRun);
        check (longestRun >= 7, "sample-rate reduction did not hold samples");
    }

    // ---- FOLD: folding is non-monotonic — a louder input can come out quieter ----
    {
        auto p = base (Distortion::Fold);
        p.foldAmount = 1.0f;
        p.foldSym = 0.5f;

        // sweep the input level and look for a level that goes back down
        double prev = -1.0, best = 0.0;
        bool wentDown = false;
        for (int i = 1; i <= 20; ++i)
        {
            const auto out = run (p, sine (sr / 4, 220.0, (float) i * 0.05f));
            const double e = rms (out, sr / 8);
            if (prev >= 0.0 && e < prev * 0.9)
                wentDown = true;
            best = std::max (best, e);
            prev = e;
        }
        std::printf ("FOLD: non-monotonic=%d peak rms over sweep=%.3f\n", (int) wentDown, best);
        check (wentDown, "wavefolder response is monotonic (it is not folding)");
    }

    // ---- RECT: rectification puts energy at the octave above ----
    {
        auto p = base (Distortion::Rect);
        p.rectAmount = 1.0f;
        p.rectTone = 1.0f;        // open, so the octave is not filtered away
        const auto in = sine (sr, 220.0, 0.7f);

        const double dryOctave = bandEnergy (in, 440.0, sr / 4);
        const auto out = run (p, in);
        const double wetOctave = bandEnergy (out, 440.0, sr / 4);
        std::printf ("RECT: octave energy dry=%.5f wet=%.5f\n", dryOctave, wetOctave);
        check (wetOctave > dryOctave * 10.0 && wetOctave > 0.01,
               "rectifier produced no octave-up");
    }

    // ---- LOWS at zero must not change the signal path at all ----
    {
        auto p = base (Distortion::Fold);
        p.foldAmount = 0.7f;
        const auto in = sine (4096, 220.0, 0.5f);

        auto q = p;
        q.lowsKept = 0.0f;
        const auto a = run (p, in), b = run (q, in);
        bool same = true;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) { same = false; break; }
        check (same, "LOWS at zero altered the signal path");
    }

    // ---- LOWS: the sub survives while the top is destroyed ----
    {
        auto p = base (Distortion::Fold);
        p.foldAmount = 1.0f;

        // a 50 Hz tone is well below the 120 Hz split
        const auto in = sine (sr, 50.0, 0.6f);
        const double dryFundamental = bandEnergy (in, 50.0, sr / 4);

        auto driven = p; driven.lowsKept = 0.0f;
        auto kept   = p; kept.lowsKept = 1.0f;

        const double withoutKeep = bandEnergy (run (driven, in), 50.0, sr / 4);
        const double withKeep    = bandEnergy (run (kept, in), 50.0, sr / 4);
        std::printf ("LOWS: 50Hz dry=%.4f driven=%.4f kept=%.4f\n",
                     dryFundamental, withoutKeep, withKeep);
        check (withKeep > withoutKeep * 1.5, "LOWS did not preserve the low band");
        check (withKeep > dryFundamental * 0.5, "LOWS lost most of the low band anyway");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
