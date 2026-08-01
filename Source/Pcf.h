#pragma once

#include <algorithm>
#include <cmath>

// Per-bus envelope-followed multimode filter ("PCF"). A TPT/zero-delay-feedback
// state-variable filter with selectable low-pass or band-pass output. An
// envelope follower on the input level pushes the cutoff upward (in octaves) by
// the Env amount, giving the classic auto-wah / programmable-filter character —
// the filter opens up on louder hits. A drive of "off" is a transparent bypass.
// JUCE-free for standalone testing.
class Pcf
{
public:
    enum Mode { LP = 0, BP = 1 };

    void prepare (double sr)
    {
        sampleRate = sr;
        ic1eq = ic2eq = 0.0f;
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

        const float maxHz = (float) (sampleRate * 0.45);
        for (int i = 0; i < n; ++i)
        {
            const float in = x[i];

            // envelope follower on the input level
            const float rect = std::abs (in);
            env += (rect > env ? attackCoef : releaseCoef) * (rect - env);

            // cutoff opens by up to ~4 octaves scaled by the follower and Env amt
            const float octaves = envAmt * 4.0f * env;
            float fc = baseCutoff * std::exp2 (octaves);
            fc = std::clamp (fc, 20.0f, maxHz);

            // TPT SVF coefficients
            const float g = std::tan (3.14159265f * fc / (float) sampleRate);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;

            const float v1 = a1 * ic1eq + a2 * (in - ic2eq);
            const float v2 = ic2eq + g * v1;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;

            // v2 = low-pass, v1 = band-pass
            x[i] = mode == BP ? v1 : v2;
        }
    }

private:
    float timeCoef (double seconds) const
    {
        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    double sampleRate = 44100.0;
    bool  on = false;
    int   mode = LP;
    float baseCutoff = 2000.0f, k = 1.0f, envAmt = 0.3f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
    float env = 0.0f, attackCoef = 0.01f, releaseCoef = 0.001f;
};
