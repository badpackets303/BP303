// Offline tests for the ten-band graphic EQ.
//
// The one that matters most is the first: flat has to be *nothing*. Every tone
// and width control in this instrument is pinned by a test saying that at its
// default the output is what it was before the control existed, and for anything
// sitting in the bass line that means bit-identical rather than close. A peaking
// filter at 0 dB is only mathematically transparent — run in float it still
// rounds — so the EQ has to branch around itself, and this is what proves it
// does, including after a band has been moved and put back.
//
// The rest check that each fader does what its label says at its own centre and
// not at its neighbour's, that the pair never picks up width, and that the band
// meters follow the signal rather than the fader.
// Build: clang++ -std=c++17 -O2 Tools/eq_test.cpp -o eq_test

#include "../Source/Eq.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace
{
    constexpr int sr = 44100;

    std::vector<float> sine (float hz, float amp, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * std::sin (6.2831853f * hz * (float) i / (float) sr);
        return v;
    }

    // Broadband, with a low fundamental and transients — enough for every band
    // to have something in it.
    std::vector<float> testSignal (int n, float amp = 0.5f)
    {
        std::vector<float> v ((size_t) n);
        uint32_t rng = 0x9e3779b9u;
        for (int i = 0; i < n; ++i)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const float noise = (float) (int32_t) rng * 4.656613e-10f;
            const float env = std::exp (-(float) (i % 1024) / 200.0f);
            v[(size_t) i] = amp * (env * std::sin (6.2831853f * 55.0f * (float) i / (float) sr)
                                   + 0.4f * noise);
        }
        return v;
    }

    double rms (const std::vector<float>& v, int from = 0)
    {
        double s = 0.0;
        int n = 0;
        for (size_t i = (size_t) from; i < v.size(); ++i, ++n)
            s += (double) v[i] * v[i];
        return n > 0 ? std::sqrt (s / n) : 0.0;
    }

    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }

    struct Gains
    {
        float g[GraphicEq::numBands] = {};
        float* operator()() { return g; }
    };

    // Run the EQ over a copy of `in` in `blockLen`-sample blocks, the way a host
    // would. Returns the processed left channel; `outR` takes the right.
    std::vector<float> run (GraphicEq& eq, const std::vector<float>& in,
                            std::vector<float>* outR = nullptr, int blockLen = 256)
    {
        std::vector<float> l = in, r = in;
        for (int i = 0; i < (int) in.size(); i += blockLen)
        {
            const int n = std::min (blockLen, (int) in.size() - i);
            eq.process (l.data() + i, r.data() + i, n);
        }
        if (outR != nullptr)
            *outR = r;
        return l;
    }

    // Level of a single band, measured with a sine at that band's centre. The
    // first half is dropped so the gain glide has arrived.
    double bandGainDb (int band, float gainDb)
    {
        const int n = sr;   // one second: long enough for 31 Hz to settle
        const auto in = sine (GraphicEq::centreHz[band], 0.25f, n);

        GraphicEq eq;
        eq.prepare (sr);
        Gains g;
        g.g[band] = gainDb;
        eq.setParams (true, g());

        const auto out = run (eq, in);
        return 20.0 * std::log10 (rms (out, n / 2) / rms (in, n / 2));
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- flat is bit-identical ---------------------------------------------
    {
        const auto in = testSignal (8192);

        GraphicEq eq;
        eq.prepare (sr);
        Gains flat;
        eq.setParams (true, flat());

        std::vector<float> r;
        const auto out = run (eq, in, &r);
        check (identical (in, out), "EQ on and flat altered the left channel");
        check (identical (in, r), "EQ on and flat altered the right channel");
        std::printf ("flat, EQ on: bit-identical\n");
    }

    // ---- switched off, faders up, still bit-identical ----------------------
    {
        const auto in = testSignal (8192);

        GraphicEq eq;
        eq.prepare (sr);
        Gains g;
        for (int b = 0; b < GraphicEq::numBands; ++b)
            g.g[b] = (b % 2 == 0) ? 12.0f : -12.0f;
        eq.setParams (false, g());

        const auto out = run (eq, in);
        check (identical (in, out), "EQ switched off still altered the signal");
        std::printf ("off with faders up: bit-identical\n");
    }

    // ---- moved and put back: still bit-identical ---------------------------
    // The bypass branch is only worth anything if a band that has been used
    // falls back into it, states and all.
    {
        const auto in = testSignal (8192);

        GraphicEq eq;
        eq.prepare (sr);
        Gains g;
        g.g[4] = 9.0f;
        eq.setParams (true, g());
        run (eq, in);                       // use it

        g.g[4] = 0.0f;
        eq.setParams (true, g());
        run (eq, testSignal (16384));       // let the glide arrive

        const auto out = run (eq, in);
        check (identical (in, out), "EQ did not return to bit-identical after a band was reset");
        std::printf ("boosted then flattened: bit-identical\n");
    }

    // ---- each fader hits its own centre ------------------------------------
    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        const double up   = bandGainDb (b, 12.0f);
        const double down = bandGainDb (b, -12.0f);
        std::printf ("band %2d  %6.0f Hz   +12 -> %+6.2f dB   -12 -> %+6.2f dB\n",
                     b, (double) GraphicEq::centreHz[b], up, down);

        check (std::abs (up - 12.0) < 0.5, "a +12 dB band did not deliver +12 dB at its centre");
        check (std::abs (down + 12.0) < 0.5, "a -12 dB band did not deliver -12 dB at its centre");
    }

    // ---- and leaves its neighbours-but-one alone ---------------------------
    // Octave skirts overlap by design, so the band next door does move. Two
    // octaves out is where a fader has to stop being audible.
    {
        const int n = sr;
        const auto in = sine (GraphicEq::centreHz[7], 0.25f, n);   // 4 kHz

        GraphicEq eq;
        eq.prepare (sr);
        Gains g;
        g.g[5] = 12.0f;                                            // 1 kHz
        eq.setParams (true, g());

        const auto out = run (eq, in);
        const double leak = 20.0 * std::log10 (rms (out, n / 2) / rms (in, n / 2));
        std::printf ("1 kHz +12 measured at 4 kHz: %+.2f dB\n", leak);
        check (std::abs (leak) < 1.0, "a band leaked more than 1 dB two octaves away");
    }

    // ---- the pair stays a pair ---------------------------------------------
    {
        const auto in = testSignal (8192);

        GraphicEq eq;
        eq.prepare (sr);
        Gains g;
        for (int b = 0; b < GraphicEq::numBands; ++b)
            g.g[b] = (b % 3 == 0) ? 8.0f : -5.0f;
        eq.setParams (true, g());

        std::vector<float> r;
        const auto l = run (eq, in, &r);
        check (identical (l, r), "the EQ widened a mono signal");
        std::printf ("identical channels in, identical channels out\n");
    }

    // ---- meters follow the signal ------------------------------------------
    {
        const int n = sr;
        const auto in = sine (1000.0f, 0.5f, n);

        GraphicEq eq;
        eq.prepare (sr);
        Gains flat;
        eq.setParams (true, flat());
        eq.setMetering (true);
        run (eq, in);

        const float atBand = eq.bandLevel (5);    // 1 kHz
        const float twoAway = eq.bandLevel (3);   // 250 Hz
        std::printf ("1 kHz sine: meter[1k]=%.3f meter[250]=%.3f\n", atBand, twoAway);
        check (atBand > 0.7f, "the meter on a driven band barely moved");
        // The bars have to separate, or ten of them read as one blob. Two
        // octaves out the cascaded bandpass pair is ~29 dB down, which over the
        // 48 dB scale is a bar well under half height next to a nearly full one.
        check (twoAway < atBand * 0.6f, "a band two octaves off read nearly as hot as the driven one");
    }

    // ---- metering does not touch the audio ---------------------------------
    // The meters have to be usable while the EQ is flat, which means the tap
    // has to be a read and nothing else.
    {
        const auto in = testSignal (8192);

        GraphicEq eq;
        eq.prepare (sr);
        Gains flat;
        eq.setParams (true, flat());
        eq.setMetering (true);

        const auto out = run (eq, in);
        check (identical (in, out), "metering altered the signal");
        std::printf ("metering a flat EQ: bit-identical\n");
    }

    // ---- the drawn curve is the filter -------------------------------------
    // The editor draws GraphicEq::responseDb rather than measuring anything, so
    // the one thing that can go wrong is the two drifting apart — a curve that
    // confidently draws a shape the filters aren't producing. Held against a
    // measured sine at each centre, and at a couple of points between them
    // where the octave skirts overlap and the error would show up first.
    {
        Gains g;
        for (int b = 0; b < GraphicEq::numBands; ++b)
            g.g[b] = (b % 3 == 0) ? 9.0f : (b % 3 == 1) ? -7.0f : 4.0f;

        double worst = 0.0;
        for (float hz : { 31.25f, 62.5f, 90.0f, 125.0f, 250.0f, 350.0f, 500.0f,
                          1000.0f, 1400.0f, 2000.0f, 4000.0f, 8000.0f })
        {
            const int n = sr;
            const auto in = sine (hz, 0.25f, n);

            GraphicEq eq;
            eq.prepare (sr);
            eq.setParams (true, g());
            const auto out = run (eq, in);

            const double measured = 20.0 * std::log10 (rms (out, n / 2) / rms (in, n / 2));
            const double drawn = GraphicEq::responseDb (g(), hz, sr);
            worst = std::max (worst, std::abs (measured - drawn));
        }

        std::printf ("drawn curve vs measured response: worst error %.3f dB\n", worst);
        check (worst < 0.1, "the drawn curve does not match what the filters do");
    }

    // ---- a band past the usable range switches itself out ------------------
    {
        const int lowSr = 32000;
        const auto in = testSignal (8192);

        GraphicEq eq;
        eq.prepare (lowSr);
        Gains g;
        g.g[9] = 12.0f;                    // 16 kHz, above 0.45 * 32 kHz
        eq.setParams (true, g());

        const auto out = run (eq, in);
        check (identical (in, out), "a band above the usable range was not switched out");
        std::printf ("16 kHz band at 32 kHz sample rate: switched out\n");
    }

    // ---- nothing blows up ---------------------------------------------------
    {
        const auto in = testSignal (16384, 0.9f);

        GraphicEq eq;
        eq.prepare (sr);
        Gains g;
        for (int b = 0; b < GraphicEq::numBands; ++b)
            g.g[b] = 12.0f;                // everything up, worst case
        eq.setParams (true, g());
        eq.setMetering (true);

        const auto out = run (eq, in);
        int bad = 0;
        for (float x : out)
            if (! std::isfinite (x))
                ++bad;
        check (bad == 0, "all bands at +12 dB produced non-finite output");
        std::printf ("all bands +12 dB: %d non-finite samples, peak rms %.3f\n",
                     bad, rms (out));
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
