// Offline tests for the two new per-line effects:
//   Reverb — bypass transparency, an audible tail that decays, damping darkens
//            it, longer size rings longer, stability.
//   Chorus — bypass transparency, the delay taps actually move (the output is
//            not a static copy of the input), stability.
// Build: clang++ -std=c++17 -O2 Tools/space_test.cpp -o space_test

#include "../Source/Chorus.h"
#include "../Source/Reverb.h"

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

    // A short burst followed by silence — the silent part is the tail.
    std::vector<float> burst (int n, int burstLen, double freqHz, float amp)
    {
        auto v = sine (n, freqHz, amp);
        for (int i = burstLen; i < n; ++i)
            v[(size_t) i] = 0.0f;
        return v;
    }

    // Broadband burst then silence, for looking at the tail's spectrum: a sine
    // tail can only get quieter, never darker.
    std::vector<float> noiseBurst (int n, int burstLen, float amp)
    {
        std::vector<float> v ((size_t) n, 0.0f);
        uint32_t seed = 12345;
        for (int i = 0; i < burstLen; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            v[(size_t) i] = amp * ((float) (seed >> 8) / 8388608.0f - 1.0f);
        }
        return v;
    }

    // crude brightness measure: energy of the first difference over total energy
    double brightness (const std::vector<float>& v, size_t from)
    {
        double hi = 0.0, all = 0.0;
        for (size_t i = from + 1; i < v.size(); ++i)
        {
            const double d = (double) v[i] - v[i - 1];
            hi  += d * d;
            all += (double) v[i] * v[i];
        }
        return all > 0.0 ? hi / all : 0.0;
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- Reverb: bypass is bit-transparent ----
    {
        Reverb r;
        r.prepare (sr);
        r.setParams (false, 0.5f, 0.5f, 0.5f);
        auto buf = sine (4096, 440.0, 0.5f);
        auto ref = buf;
        r.process (buf.data(), (int) buf.size());
        bool same = true;
        for (size_t i = 0; i < buf.size(); ++i) if (buf[i] != ref[i]) { same = false; break; }
        check (same, "reverb bypass not transparent");
    }

    // ---- Reverb: a burst leaves an audible tail in the silence after it ----
    {
        Reverb r;
        r.prepare (sr);
        r.setParams (true, 0.6f, 0.3f, 1.0f);
        auto buf = burst (sr, sr / 10, 440.0, 0.5f);
        const auto dry = buf;
        r.process (buf.data(), (int) buf.size());
        const double tail = rms (buf, (size_t) sr / 5);   // well past the burst
        std::printf ("reverb: tail rms=%.4f (dry burst rms=%.4f) nans=%d\n",
                     tail, rms (dry), countNans (buf));
        check (countNans (buf) == 0, "reverb produced NaNs");
        check (tail > 0.002, "reverb tail is inaudible");
        check (tail < 0.5, "reverb tail is out of control");
    }

    // ---- Reverb: more size = longer tail ----
    {
        auto tailFor = [] (float size) {
            Reverb r; r.prepare (sr);
            r.setParams (true, size, 0.3f, 1.0f);
            auto buf = burst (2 * sr, sr / 10, 440.0, 0.5f);
            r.process (buf.data(), (int) buf.size());
            return rms (buf, (size_t) sr);   // one second after the burst started
        };
        const double shortT = tailFor (0.1f), longT = tailFor (0.95f);
        std::printf ("reverb size: short=%.5f long=%.5f\n", shortT, longT);
        check (longT > shortT * 2.0, "reverb size does not lengthen the tail");
    }

    // ---- Reverb: damping darkens the tail ----
    {
        auto brightFor = [] (float damp) {
            Reverb r; r.prepare (sr);
            r.setParams (true, 0.8f, damp, 1.0f);
            auto buf = noiseBurst (sr, sr / 20, 0.5f);
            r.process (buf.data(), (int) buf.size());
            return brightness (buf, (size_t) sr / 5);
        };
        const double bright = brightFor (0.0f), dark = brightFor (1.0f);
        std::printf ("reverb damp: bright=%.4f dark=%.4f\n", bright, dark);
        check (dark < bright * 0.8, "reverb damping did not darken the tail");
    }

    // ---- Reverb: mix of zero leaves the dry signal alone ----
    {
        Reverb r;
        r.prepare (sr);
        r.setParams (true, 0.8f, 0.5f, 0.0f);
        auto buf = sine (4096, 440.0, 0.5f);
        auto ref = buf;
        r.process (buf.data(), (int) buf.size());
        bool same = true;
        for (size_t i = 0; i < buf.size(); ++i) if (buf[i] != ref[i]) { same = false; break; }
        check (same, "reverb at zero mix altered the dry signal");
    }

    // ---- Chorus: bypass is bit-transparent ----
    {
        Chorus c;
        c.prepare (sr);
        c.setParams (false, 1.0f, 0.5f, 0.5f);
        auto buf = sine (4096, 440.0, 0.5f);
        auto ref = buf;
        c.process (buf.data(), (int) buf.size());
        bool same = true;
        for (size_t i = 0; i < buf.size(); ++i) if (buf[i] != ref[i]) { same = false; break; }
        check (same, "chorus bypass not transparent");
    }

    // ---- Chorus: the sweep modulates, so the output level moves over time ----
    {
        Chorus c;
        c.prepare (sr);
        c.setParams (true, 1.0f, 0.8f, 1.0f);   // one full LFO cycle per second
        auto buf = sine (2 * sr, 440.0, 0.5f);
        c.process (buf.data(), (int) buf.size());
        check (countNans (buf) == 0, "chorus produced NaNs");

        // measure short windows across one LFO cycle: comb cancellation between
        // dry and swept taps makes the level rise and fall
        double lo = 1e9, hi = 0.0;
        const int win = sr / 20;
        for (int start = sr; start + win <= 2 * sr; start += win)
        {
            std::vector<float> w (buf.begin() + start, buf.begin() + start + win);
            const double e = rms (w);
            lo = std::min (lo, e);
            hi = std::max (hi, e);
        }
        std::printf ("chorus: window rms lo=%.4f hi=%.4f\n", lo, hi);
        check (hi > lo * 1.2, "chorus output does not move (LFO not sweeping)");
    }

    // ---- Chorus: mix of zero leaves the dry signal alone ----
    {
        Chorus c;
        c.prepare (sr);
        c.setParams (true, 2.0f, 1.0f, 0.0f);
        auto buf = sine (4096, 440.0, 0.5f);
        auto ref = buf;
        c.process (buf.data(), (int) buf.size());
        bool same = true;
        for (size_t i = 0; i < buf.size(); ++i) if (buf[i] != ref[i]) { same = false; break; }
        check (same, "chorus at zero mix altered the dry signal");
    }

    // ---- Chorus: a hard sweep stays bounded ----
    {
        Chorus c;
        c.prepare (sr);
        c.setParams (true, 8.0f, 1.0f, 1.0f);
        auto buf = sine (2 * sr, 110.0, 0.9f);
        c.process (buf.data(), (int) buf.size());
        float peak = 0.0f;
        for (float v : buf) peak = std::max (peak, std::abs (v));
        std::printf ("chorus fast sweep: peak=%.3f nans=%d\n", peak, countNans (buf));
        check (countNans (buf) == 0, "chorus fast sweep produced NaNs");
        check (peak < 2.5f, "chorus fast sweep exploded");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
