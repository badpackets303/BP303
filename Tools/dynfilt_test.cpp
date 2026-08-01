// Offline tests for the two new per-line effects:
//   Compressor — bypass transparency, gain reduction above threshold, stability.
//   Pcf        — bypass transparency, LP vs BP frequency response, stability.
// Build: clang++ -std=c++17 -O2 Tools/dynfilt_test.cpp -o dynfilt_test

#include "../Source/Compressor.h"
#include "../Source/Pcf.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr int sr = 44100;

    double rms (const std::vector<float>& v)
    {
        double s = 0.0;
        for (float x : v) s += (double) x * x;
        return std::sqrt (s / (double) v.size());
    }

    int countNans (const std::vector<float>& v)
    {
        int n = 0;
        for (float x : v) if (! std::isfinite (x)) ++n;
        return n;
    }

    // fill with a sine at freqHz, amplitude amp
    std::vector<float> sine (int n, double freqHz, float amp)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * std::sin (6.2831853 * freqHz * i / sr);
        return v;
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- Compressor: bypass is bit-transparent ----
    {
        Compressor c;
        c.prepare (sr);
        c.setParams (false, -18.0f, 4.0f, 0.0f);
        auto buf = sine (4096, 220.0, 0.5f);
        auto ref = buf;
        c.process (buf.data(), (int) buf.size());
        bool same = true;
        for (size_t i = 0; i < buf.size(); ++i) if (buf[i] != ref[i]) { same = false; break; }
        check (same, "compressor bypass not transparent");
    }

    // ---- Compressor: a hot signal well over threshold is reduced, no makeup ----
    {
        Compressor c;
        c.prepare (sr);
        c.setParams (true, -24.0f, 8.0f, 0.0f);   // hard ratio, low threshold
        auto in  = sine (sr, 220.0, 0.8f);        // ~ -1.9 dBFS, far above -24
        auto out = in;
        c.process (out.data(), (int) out.size());
        const double rIn = rms (in), rOut = rms (out);
        std::printf ("comp: rmsIn=%.3f rmsOut=%.3f nans=%d\n", rIn, rOut, countNans (out));
        check (countNans (out) == 0, "compressor produced NaNs");
        check (rOut < rIn * 0.9, "compressor did not reduce a hot signal");
    }

    // ---- Compressor: makeup restores level on a below-threshold signal ----
    {
        Compressor c;
        c.prepare (sr);
        c.setParams (true, 0.0f, 4.0f, 6.0f);     // threshold at 0 dBFS -> no reduction
        auto in  = sine (sr, 220.0, 0.2f);
        auto out = in;
        c.process (out.data(), (int) out.size());
        const double ratio = rms (out) / rms (in);
        std::printf ("comp makeup ratio=%.3f (expect ~2.0)\n", ratio);
        check (ratio > 1.8 && ratio < 2.2, "compressor makeup gain wrong");
    }

    // ---- Pcf: bypass is bit-transparent ----
    {
        Pcf f;
        f.prepare (sr);
        f.setParams (false, Pcf::LP, 1000.0f, 0.3f, 0.0f);
        auto buf = sine (4096, 500.0, 0.5f);
        auto ref = buf;
        f.process (buf.data(), (int) buf.size());
        bool same = true;
        for (size_t i = 0; i < buf.size(); ++i) if (buf[i] != ref[i]) { same = false; break; }
        check (same, "pcf bypass not transparent");
    }

    // ---- Pcf LP: passes lows, strongly attenuates highs (env off so cutoff is static) ----
    {
        const float cutoff = 500.0f;
        auto measure = [&] (double freqHz) {
            Pcf f; f.prepare (sr);
            f.setParams (true, Pcf::LP, cutoff, 0.2f, 0.0f);
            auto buf = sine (sr, freqHz, 0.5f);
            f.process (buf.data(), (int) buf.size());
            // skip the settling transient
            std::vector<float> tail (buf.begin() + sr / 2, buf.end());
            return rms (tail);
        };
        const double low  = measure (100.0);
        const double high = measure (5000.0);
        std::printf ("pcf LP: low(100Hz)=%.3f high(5kHz)=%.3f\n", low, high);
        check (std::isfinite (low) && std::isfinite (high), "pcf LP NaN");
        check (high < low * 0.5, "pcf LP did not attenuate highs below lows");
    }

    // ---- Pcf BP: attenuates DC/very-low content relative to the passband ----
    {
        const float cutoff = 800.0f;
        auto measure = [&] (double freqHz) {
            Pcf f; f.prepare (sr);
            f.setParams (true, Pcf::BP, cutoff, 0.4f, 0.0f);
            auto buf = sine (sr, freqHz, 0.5f);
            f.process (buf.data(), (int) buf.size());
            std::vector<float> tail (buf.begin() + sr / 2, buf.end());
            return rms (tail);
        };
        const double band = measure (800.0);
        const double lowf = measure (40.0);
        std::printf ("pcf BP: band(800Hz)=%.3f low(40Hz)=%.3f\n", band, lowf);
        check (lowf < band * 0.6, "pcf BP did not attenuate lows relative to passband");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
