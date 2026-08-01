#pragma once

#include <algorithm>
#include <cmath>

// Industrial-style fuzz for the drum bus: high-gain, asymmetric hard clipping
// that fattens transients into a buzzy square (odd harmonics) with the
// asymmetry adding even harmonics for a rawer, gated edge. A DC blocker after
// the shaper keeps the asymmetry from thumping the kick. Mirrors the bass
// distortion's controls: an explicit on/off (off = transparent bypass), a DRIVE
// amount, and a COLOR that tilts the signal toward highs before the clipper
// (smooth boom -> aggressive buzz). JUCE-free for standalone testing.
class DrumFuzz
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        x1 = y1 = 0.0f;
        tiltLp = 0.0f;
        cTilt = lpCoef (700.0);
    }

    // on:      master enable — off is a transparent bypass
    // drive01: 0 = clean, 1 = full industrial fuzz
    // color01: 0 = darker / smoother, 1 = brighter / buzzier
    void setParams (bool onIn, float drive01, float color01)
    {
        on = onIn;
        drive01 = std::clamp (drive01, 0.0f, 1.0f);
        driveGain = 1.0f + drive01 * 24.0f;   // up to 25x pre-gain
        bias = drive01 * 0.30f;               // asymmetry -> even harmonics
        // clip harder on the negative side than the positive
        negLim = 1.0f - drive01 * 0.30f;      // 1.0 -> 0.7
        makeup = 1.0f / (1.0f + drive01 * 2.0f);
        color = std::clamp (color01, 0.0f, 1.0f);
    }

    void process (float* x, int n)
    {
        if (! on)
            return;

        for (int i = 0; i < n; ++i)
        {
            const float s = x[i];

            // color pre-emphasis: lift the highs ahead of the clipper so the
            // fuzz moves from a rounded boom toward a bright, buzzy edge
            tiltLp += (s - tiltLp) * cTilt;
            const float pre = s + color * 1.5f * (s - tiltLp);

            const float a = pre * driveGain + bias;
            const float clipped = a > 1.0f ? 1.0f : (a < -negLim ? -negLim : a);
            const float y = clipped * makeup;

            // DC blocker (one-pole highpass), removes the asymmetry offset;
            // gentle cutoff (~5 Hz) so percussive transients aren't pumped
            const float out = y - x1 + 0.9992f * y1;
            x1 = y;
            y1 = out;

            x[i] = out;
        }
    }

private:
    float lpCoef (double freqHz) const
    {
        return (float) (1.0 - std::exp (-6.2831853 * freqHz / sampleRate));
    }

    double sampleRate = 44100.0;
    bool  on = false;
    float driveGain = 1.0f, bias = 0.0f, negLim = 1.0f, makeup = 1.0f, color = 0.5f;
    float x1 = 0.0f, y1 = 0.0f, tiltLp = 0.0f, cTilt = 0.1f;
};
