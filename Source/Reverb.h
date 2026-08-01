#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Per-bus mono reverb: a Schroeder/Freeverb tank — parallel damped comb filters
// into a chain of allpasses. SIZE sets the comb feedback (how long the tail
// rings), DAMP rolls the highs off a little more on every pass so the tail gets
// darker as it decays, and MIX adds the wet signal on top of the dry, the same
// way the delay's mix works. A high-pass sits on the send because a 303 bass
// would otherwise flood the tank with low end and turn the tail to mud.
// "Off" is a transparent bypass. JUCE-free for standalone testing.
//
// The stereo overload runs a second tank tuned identically to the first, so it
// adds no width of its own: a mono signal comes out as two identical channels,
// and only what arrives already stereo (the ping-pong delay upstream) keeps its
// image through the tail.
class Reverb
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        const double scale = sr / 44100.0;

        // Freeverb's tuning, scaled to the running sample rate. The lengths are
        // mutually prime so the combs don't line up into a ringing pitch.
        static constexpr int combLen[numCombs]   = { 1116, 1188, 1277, 1356, 1422, 1491 };
        static constexpr int apLen[numAllpasses] = { 556, 441, 341, 225 };

        for (auto& tank : tanks)
        {
            for (int i = 0; i < numCombs; ++i)
            {
                tank.combs[i].buf.assign ((size_t) std::max (8.0, combLen[i] * scale), 0.0f);
                tank.combs[i].pos = 0;
                tank.combs[i].store = 0.0f;
            }
            for (int i = 0; i < numAllpasses; ++i)
            {
                tank.allpasses[i].buf.assign ((size_t) std::max (8.0, apLen[i] * scale), 0.0f);
                tank.allpasses[i].pos = 0;
            }
            tank.hpState = 0.0f;
        }

        cHp = (float) (1.0 - std::exp (-6.2831853 * 140.0 / sr));
        prevOn = false;
    }

    // size01: 0 = short room, 1 = long hall.
    // damp01: 0 = bright tail, 1 = dark tail.
    // mixIn:  amount of wet added to the dry signal.
    void setParams (bool onIn, float size01, float damp01, float mixIn)
    {
        on = onIn;
        feedback = 0.70f + std::clamp (size01, 0.0f, 1.0f) * 0.28f;   // 0.70 .. 0.98
        damp = std::clamp (damp01, 0.0f, 1.0f) * 0.75f;
        mix = std::clamp (mixIn, 0.0f, 1.0f);

        if (on && ! prevOn)
            clearTank();   // fresh space on enable: no tail left over from before
        prevOn = on;
    }

    void process (float* x, int n)
    {
        if (! on)
            return;

        processTank (tanks[0], x, n);
    }

    void process (float* l, float* r, int n)
    {
        if (! on)
            return;

        processTank (tanks[0], l, n);
        processTank (tanks[1], r, n);
    }

private:
    static constexpr int numCombs = 6, numAllpasses = 4;
    // the combs sum, so the send is scaled well down to keep the tank in range
    static constexpr float inputGain = 0.05f;

    struct Comb
    {
        std::vector<float> buf;
        int   pos = 0;
        float store = 0.0f;

        float process (float in, float fb, float dampCoef)
        {
            const float out = buf[(size_t) pos];
            // one-pole lowpass in the feedback path -> the tail darkens as it decays
            store = out * (1.0f - dampCoef) + store * dampCoef;
            buf[(size_t) pos] = in + store * fb;
            if (++pos >= (int) buf.size())
                pos = 0;
            return out;
        }
    };

    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0;

        float process (float in)
        {
            const float bufOut = buf[(size_t) pos];
            buf[(size_t) pos] = in + bufOut * 0.5f;
            if (++pos >= (int) buf.size())
                pos = 0;
            return bufOut - in;
        }
    };

    // One channel's Schroeder tank: parallel damped combs into serial allpasses.
    struct Tank
    {
        Comb    combs[numCombs];
        Allpass allpasses[numAllpasses];
        float   hpState = 0.0f;
    };

    void processTank (Tank& tank, float* x, int n) const
    {
        for (int i = 0; i < n; ++i)
        {
            const float dry = x[i];

            // keep the bass out of the tank
            tank.hpState += (dry - tank.hpState) * cHp;
            const float send = (dry - tank.hpState) * inputGain;

            float wet = 0.0f;
            for (auto& c : tank.combs)
                wet += c.process (send, feedback, damp);

            for (auto& a : tank.allpasses)
                wet = a.process (wet);

            x[i] = dry + wet * mix;
        }
    }

    void clearTank()
    {
        for (auto& tank : tanks)
        {
            for (auto& c : tank.combs)
            {
                std::fill (c.buf.begin(), c.buf.end(), 0.0f);
                c.store = 0.0f;
            }
            for (auto& a : tank.allpasses)
                std::fill (a.buf.begin(), a.buf.end(), 0.0f);
            tank.hpState = 0.0f;
        }
    }

    double sampleRate = 44100.0;
    bool  on = false, prevOn = false;
    float feedback = 0.84f, damp = 0.2f, mix = 0.25f;
    float cHp = 0.02f;

    Tank tanks[2];
};
