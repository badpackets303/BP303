#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Per-bus mono chorus: two short delay taps whose delay times are swept by the
// same LFO half a cycle apart, mixed back over the dry signal. The detuning that
// the sweep produces is what thickens a single line into an ensemble; two taps
// in antiphase keep the result moving without the obvious single-voice wobble.
// RATE is the sweep speed, DEPTH how far it swings, MIX how much wet is added on
// top of the dry — the same additive mix the delay uses, so MIX at zero is
// silent. "Off" is a transparent bypass. JUCE-free for standalone testing.
//
// The stereo overload runs a second, independent delay line off the same LFO,
// so it adds no width of its own: a mono signal comes out as two identical
// channels, and only what arrives already stereo (the ping-pong delay upstream)
// stays stereo through it. Pick one overload per instance and stay with it —
// the mono one only writes the first line, so switching to the stereo one mid-
// stream would read a second line that has nothing recent in it.
class Chorus
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& b : buf)
            b.assign ((size_t) (sr * 0.05) + 8, 0.0f);   // 50 ms covers base + depth
        writePos = 0;
        phase = 0.0f;
        prevOn = false;

        baseSamples = (float) (0.011 * sr);   // 11 ms centre delay
    }

    // rateHz:  LFO speed.
    // depth01: 0 = static (a fixed short delay), 1 = a ~6 ms swing.
    // mixIn:   amount of wet added to the dry signal.
    void setParams (bool onIn, float rateHz, float depth01, float mixIn)
    {
        on = onIn;
        phaseInc = (float) (std::clamp (rateHz, 0.01f, 10.0f) / sampleRate);
        // stays below baseSamples, so the read pointer never crosses the writer
        depthSamples = (float) (std::clamp (depth01, 0.0f, 1.0f) * 0.006 * sampleRate);
        mix = std::clamp (mixIn, 0.0f, 1.0f);

        if (on && ! prevOn)
        {
            for (auto& b : buf)                          // no stale audio on enable
                std::fill (b.begin(), b.end(), 0.0f);
            phase = 0.0f;
        }
        prevOn = on;
    }

    void process (float* x, int n)
    {
        if (! on)
            return;

        for (int i = 0; i < n; ++i)
        {
            x[i] = tap (0, x[i]);
            advance();
        }
    }

    void process (float* l, float* r, int n)
    {
        if (! on)
            return;

        for (int i = 0; i < n; ++i)
        {
            // both channels read the same LFO, so the sweep is the same on each
            l[i] = tap (0, l[i]);
            r[i] = tap (1, r[i]);
            advance();
        }
    }

private:
    // One sample of one channel, at the LFO's current position. Writing the dry
    // in at writePos is shared with the other channel's tap; advance() moves the
    // write head and the LFO on once both channels have been taken.
    float tap (int ch, float dry)
    {
        buf[ch][(size_t) writePos] = dry;

        float wet = 0.0f;
        for (int v = 0; v < numVoices; ++v)
        {
            float ph = phase + (float) v / (float) numVoices;
            ph -= std::floor (ph);
            const float lfo = std::sin (6.2831853f * ph);
            wet += readAt (ch, baseSamples + depthSamples * lfo);
        }
        wet /= (float) numVoices;

        return dry + wet * mix;
    }

    void advance()
    {
        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        if (++writePos >= (int) buf[0].size())
            writePos = 0;
    }

    // fractional read, delaySamples back from the write head
    float readAt (int ch, float delaySamples) const
    {
        const auto size = (float) buf[ch].size();
        float rp = (float) writePos - delaySamples;
        while (rp < 0.0f)
            rp += size;

        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % (int) buf[ch].size();
        const float frac = rp - (float) i0;
        return buf[ch][(size_t) i0] * (1.0f - frac) + buf[ch][(size_t) i1] * frac;
    }

    static constexpr int numVoices = 2;

    double sampleRate = 44100.0;
    bool  on = false, prevOn = false;
    float baseSamples = 485.0f, depthSamples = 130.0f;
    float phase = 0.0f, phaseInc = 0.0f, mix = 0.4f;
    std::vector<float> buf[2];   // one delay line per channel, shared write head
    int writePos = 0;
};
