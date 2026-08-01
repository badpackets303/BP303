#pragma once

#include <algorithm>
#include <cmath>

// Per-bus feed-forward peak compressor. A simple, musical "glue" comp with three
// controls (threshold, ratio, makeup) and fixed attack/release timing. Detects
// the peak level, computes gain reduction in the dB domain above the threshold,
// smooths it with separate attack/release one-poles, then applies makeup. A
// drive of "off" is a transparent bypass. JUCE-free for standalone testing.
//
// The stereo overload is channel-linked: one detector fed by whichever channel
// is louder drives one gain applied to both, so a hard-panned echo can't pull
// the image over by ducking its own side only. On a mono signal (the two
// channels identical) that reduces exactly to the mono path.
class Compressor
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        env = 0.0f;
        gainSmooth = 1.0f;
        // musically-tuned fixed timing: snappy attack, medium release
        attackCoef  = timeCoef (0.010);   // 10 ms
        releaseCoef = timeCoef (0.120);   // 120 ms
    }

    // threshDb: level (dBFS) above which reduction begins.
    // ratio:    1 = no compression, higher = more.
    // makeupDb: output gain applied after compression.
    void setParams (bool onIn, float threshDb, float ratio, float makeupDb)
    {
        on = onIn;
        thresholdDb = threshDb;
        slope = 1.0f - 1.0f / std::max (1.0f, ratio);   // 0 at 1:1, ->1 at inf:1
        makeup = std::pow (10.0f, makeupDb / 20.0f);
    }

    void process (float* x, int n)
    {
        if (! on)
            return;

        for (int i = 0; i < n; ++i)
            x[i] *= nextGain (std::abs (x[i]));
    }

    void process (float* l, float* r, int n)
    {
        if (! on)
            return;

        for (int i = 0; i < n; ++i)
        {
            const float g = nextGain (std::max (std::abs (l[i]), std::abs (r[i])));
            l[i] *= g;
            r[i] *= g;
        }
    }

private:
    // One sample of the detector + gain computer, returning the gain to apply.
    float nextGain (float rect)
    {
        // peak detector with attack/release ballistics
        env += (rect > env ? attackCoef : releaseCoef) * (rect - env);

        // gain computer (dB domain): how many dB over threshold, scaled by slope
        const float levelDb = 20.0f * std::log10 (std::max (env, 1.0e-6f));
        const float overDb  = levelDb - thresholdDb;
        const float reduceDb = overDb > 0.0f ? slope * overDb : 0.0f;
        const float targetGain = std::pow (10.0f, -reduceDb / 20.0f);

        // smooth the applied gain with the same ballistics (fast to duck,
        // slower to recover) to avoid zipper artifacts
        gainSmooth += (targetGain < gainSmooth ? attackCoef : releaseCoef)
                    * (targetGain - gainSmooth);

        return gainSmooth * makeup;
    }

    float timeCoef (double seconds) const
    {
        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    double sampleRate = 44100.0;
    bool  on = false;
    float thresholdDb = -18.0f, slope = 0.75f, makeup = 1.0f;
    float attackCoef = 0.01f, releaseCoef = 0.001f;
    float env = 0.0f, gainSmooth = 1.0f;
};
