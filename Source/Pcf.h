#pragma once

#include <algorithm>
#include <cmath>

// Per-bus envelope-followed multimode filter ("PCF"). A TPT/zero-delay-feedback
// state-variable filter with selectable low-pass or band-pass output. An
// envelope follower on the input level pushes the cutoff upward (in octaves) by
// the Env amount, giving the classic auto-wah / programmable-filter character —
// the filter opens up on louder hits. A drive of "off" is a transparent bypass.
// JUCE-free for standalone testing.
//
// The stereo overload runs a second filter state alongside the first, but the
// envelope follower is *linked*: one detector, fed by whichever channel is
// louder, sets one cutoff for both sides. Per-channel followers would open the
// filter further on whichever side a panned hit landed on, so a hard-panned clap
// would drag the whole bus's brightness across the image with it — the same
// reason Compressor links its detector. On a mono signal (the two channels
// identical) the pair reduces exactly to the mono path.
class Pcf
{
public:
    enum Mode { LP = 0, BP = 1 };

    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& c : ch)
            c.ic1eq = c.ic2eq = 0.0f;
        env = 0.0f;
        // follower ballistics: quick to open, slower to close
        attackCoef  = timeCoef (0.005);   // 5 ms
        releaseCoef = timeCoef (0.150);   // 150 ms
    }

    void setParams (bool onIn, int modeIn, float cutoffHz, float res01, float env01)
    {
        on = onIn;
        mode = modeIn == BP ? BP : LP;
        baseCutoff = cutoffHz;
        // resonance -> quality factor; keep the loop stable well short of self-osc
        const float q = 0.5f + res01 * 9.5f;   // 0.5 .. 10
        k = 1.0f / q;
        envAmt = std::clamp (env01, 0.0f, 1.0f);
    }

    void process (float* x, int n)
    {
        if (! on)
            return;

        for (int i = 0; i < n; ++i)
        {
            const auto co = advance (std::abs (x[i]));
            x[i] = tick (ch[0], x[i], co);
        }
    }

    void process (float* l, float* r, int n)
    {
        if (! on)
            return;

        for (int i = 0; i < n; ++i)
        {
            const auto co = advance (std::max (std::abs (l[i]), std::abs (r[i])));
            l[i] = tick (ch[0], l[i], co);
            r[i] = tick (ch[1], r[i], co);
        }
    }

private:
    // one filter's worth of integrator state
    struct Chan { float ic1eq = 0.0f, ic2eq = 0.0f; };

    // TPT SVF coefficients for this sample, shared by both channels
    struct Coefs { float g, a1, a2; };

    // Steps the (linked) envelope follower and turns the cutoff it implies into
    // filter coefficients. Called once per sample no matter how many channels
    // are running, which is what keeps the pair linked.
    Coefs advance (float rect)
    {
        env += (rect > env ? attackCoef : releaseCoef) * (rect - env);

        // cutoff opens by up to ~4 octaves scaled by the follower and Env amt
        const float octaves = envAmt * 4.0f * env;
        float fc = baseCutoff * std::exp2 (octaves);
        fc = std::clamp (fc, 20.0f, (float) (sampleRate * 0.45));

        const float g = std::tan (3.14159265f * fc / (float) sampleRate);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        return { g, a1, g * a1 };
    }

    float tick (Chan& c, float in, const Coefs& co) const
    {
        const float v1 = co.a1 * c.ic1eq + co.a2 * (in - c.ic2eq);
        const float v2 = c.ic2eq + co.g * v1;
        c.ic1eq = 2.0f * v1 - c.ic1eq;
        c.ic2eq = 2.0f * v2 - c.ic2eq;

        // v2 = low-pass, v1 = band-pass
        return mode == BP ? v1 : v2;
    }

    float timeCoef (double seconds) const
    {
        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    double sampleRate = 44100.0;
    bool  on = false;
    int   mode = LP;
    float baseCutoff = 2000.0f, k = 1.0f, envAmt = 0.3f;
    Chan  ch[2];
    float env = 0.0f, attackCoef = 0.01f, releaseCoef = 0.001f;
};
