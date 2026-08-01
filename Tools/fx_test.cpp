// Offline test for Fx303 (the tempo-synced delay): bypass transparency, echo
// timing and stability, and the MONO / STEREO routing. Distortion moved out to
// Distortion.h — see dist_test.
// Build: clang++ -std=c++17 -O2 Tools/fx_test.cpp -o fx_test

#include "../Source/Fx303.h"

#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    // The plugin always feeds the delay a mono line, so every case here sends
    // the same signal to both channels, exactly as processBlock does.
    struct Stereo
    {
        std::vector<float> l, r;

        explicit Stereo (size_t n) : l (n, 0.0f), r (n, 0.0f) {}

        void set (size_t i, float v) { l[i] = r[i] = v; }
        void run (Fx303& fx) { fx.process (l.data(), r.data(), (int) l.size()); }
    };

    // index and height of the loudest sample in [from, to)
    void peakIn (const std::vector<float>& v, int from, int to, int& atOut, float& peakOut)
    {
        atOut = -1;
        peakOut = 0.0f;
        for (int i = from; i < to && i < (int) v.size(); ++i)
            if (std::abs (v[i]) > peakOut)
            {
                peakOut = std::abs (v[i]);
                atOut = i;
            }
    }
}

int main()
{
    const int sr = 44100;

    // --- bypass: everything off must be bit-identical ---
    {
        Fx303 fx;
        fx.prepare (sr);
        fx.setParams (false, Fx303::Mono, 3, 0.45f, 0.25f, 120.0);
        Stereo buf (4096);
        std::vector<float> ref (4096);
        for (size_t i = 0; i < ref.size(); ++i)
        {
            ref[i] = std::sin (0.05f * (float) i) * 0.5f;
            buf.set (i, ref[i]);
        }
        buf.run (fx);
        for (size_t i = 0; i < ref.size(); ++i)
            if (buf.l[i] != ref[i] || buf.r[i] != ref[i])
            { ++failures; std::printf ("BYPASS not transparent\n"); break; }
    }

    // --- delay: impulse must echo at one eighth note (11025 samples @120) ---
    {
        Fx303 fx;
        fx.prepare (sr);
        fx.setParams (true, Fx303::Mono, 2, 0.5f, 0.3f, 120.0);
        Stereo buf ((size_t) sr * 2);
        buf.set (0, 1.0f);
        buf.run (fx);

        int firstEcho; float echoPeak;
        peakIn (buf.l, 100, 12500, firstEcho, echoPeak);
        std::printf ("delay: firstEcho=%d peak=%.3f\n", firstEcho, echoPeak);
        // fbLp smears the impulse slightly; echo should land just after 11025
        if (! (firstEcho >= 11025 && firstEcho < 11100 && echoPeak > 0.05f))
            ++failures;

        // a mono line in, both channels identical out: MONO adds no width
        bool same = true;
        for (size_t i = 0; i < buf.l.size(); ++i)
            if (buf.l[i] != buf.r[i]) { same = false; break; }
        std::printf ("MONO keeps both channels identical: %s\n", same ? "ok" : "FAIL");
        if (! same) ++failures;

        // long-run stability at high feedback
        Fx303 fx2;
        fx2.prepare (sr);
        fx2.setParams (true, Fx303::Mono, 1, 0.95f, 0.5f, 120.0);
        Stereo b2 ((size_t) sr * 4);
        b2.set (0, 1.0f);
        b2.run (fx2);
        float tail = 0.0f; int nans = 0;
        for (size_t i = b2.l.size() - 4410; i < b2.l.size(); ++i)
        {
            if (! std::isfinite (b2.l[i]) || ! std::isfinite (b2.r[i])) ++nans;
            tail = std::max (tail, std::abs (b2.l[i]));
        }
        std::printf ("delay stability: tail=%.4f nans=%d\n", tail, nans);
        if (! (nans == 0 && tail < 1.0f)) ++failures;
    }

    // --- STEREO: the echoes must ping-pong, one side per repeat ---
    {
        Fx303 fx;
        fx.prepare (sr);
        // eighth notes at 120 -> a repeat every 11025 samples
        fx.setParams (true, Fx303::Stereo, 2, 0.7f, 0.5f, 120.0);
        Stereo buf ((size_t) sr * 4);
        buf.set (0, 1.0f);
        buf.run (fx);

        // echo 1 belongs to the left channel, echo 2 to the right, echo 3 left
        struct { int from, to; } window[] = { { 10500, 11600 }, { 21500, 22600 },
                                              { 32500, 33600 } };
        float peakL[3], peakR[3];
        for (int e = 0; e < 3; ++e)
        {
            int at;
            peakIn (buf.l, window[e].from, window[e].to, at, peakL[e]);
            peakIn (buf.r, window[e].from, window[e].to, at, peakR[e]);
        }
        std::printf ("ping-pong: L=%.3f/%.3f/%.3f  R=%.3f/%.3f/%.3f\n",
                     peakL[0], peakL[1], peakL[2], peakR[0], peakR[1], peakR[2]);

        // each repeat lands hard on one side: at least 8x the other channel
        const bool alternates = peakL[0] > peakR[0] * 8.0f
                             && peakR[1] > peakL[1] * 8.0f
                             && peakL[2] > peakR[2] * 8.0f;
        std::printf ("echoes alternate L/R/L: %s\n", alternates ? "ok" : "FAIL");
        if (! alternates) ++failures;

        // and they decay, repeat over repeat, rather than running away
        const bool decays = peakR[1] < peakL[0] && peakL[2] < peakR[1];
        std::printf ("repeats decay: %s\n", decays ? "ok" : "FAIL");
        if (! decays) ++failures;

        // the dry signal itself must stay centred: sample 0 is untouched input
        if (buf.l[0] != buf.r[0]) { ++failures; std::printf ("dry is not centred\n"); }
    }

    // --- STEREO stability at maximum feedback ---
    {
        Fx303 fx;
        fx.prepare (sr);
        fx.setParams (true, Fx303::Stereo, 1, 0.95f, 0.5f, 120.0);
        Stereo buf ((size_t) sr * 4);
        buf.set (0, 1.0f);
        buf.run (fx);

        float tail = 0.0f; int nans = 0;
        for (size_t i = buf.l.size() - 4410; i < buf.l.size(); ++i)
        {
            if (! std::isfinite (buf.l[i]) || ! std::isfinite (buf.r[i])) ++nans;
            tail = std::max (tail, std::max (std::abs (buf.l[i]), std::abs (buf.r[i])));
        }
        std::printf ("stereo stability: tail=%.4f nans=%d\n", tail, nans);
        if (! (nans == 0 && tail < 1.0f)) ++failures;
    }

    // --- switching type clears the lines, so no stale echo crosses over ---
    {
        Fx303 fx;
        fx.prepare (sr);
        fx.setParams (true, Fx303::Mono, 2, 0.9f, 0.5f, 120.0);
        Stereo buf ((size_t) sr);
        buf.set (0, 1.0f);
        buf.run (fx);

        fx.setParams (true, Fx303::Stereo, 2, 0.9f, 0.5f, 120.0);
        Stereo after ((size_t) sr);   // silence in
        after.run (fx);

        float peak = 0.0f;
        for (size_t i = 0; i < after.l.size(); ++i)
            peak = std::max (peak, std::max (std::abs (after.l[i]), std::abs (after.r[i])));
        std::printf ("type change leaves no stale echo: peak=%.6f\n", peak);
        if (peak != 0.0f) ++failures;
    }

    // --- drum-delay config: the drum line's settings, eighth-note echo ---
    {
        Fx303 fx;
        fx.prepare (sr);
        // matches processBlock's drum path
        fx.setParams (true, Fx303::Mono, 2, 0.35f, 0.2f, 120.0);
        Stereo buf ((size_t) sr);
        buf.set (0, 1.0f);
        buf.run (fx);

        int firstEcho; float echoPeak; int nans = 0;
        peakIn (buf.l, 100, 12500, firstEcho, echoPeak);
        for (float v : buf.l) if (! std::isfinite (v)) ++nans;
        std::printf ("drum-delay: firstEcho=%d peak=%.3f nans=%d\n", firstEcho, echoPeak, nans);
        if (! (nans == 0 && firstEcho >= 11025 && firstEcho < 11100 && echoPeak > 0.03f))
            ++failures;
    }

    std::printf (failures == 0 ? "FX-TEST OK\n" : "FX-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
