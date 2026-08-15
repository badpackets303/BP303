#pragma once

#include <algorithm>
#include <cmath>

namespace dsp303
{
    // How far below full scale the summed mix is placed before it reaches the
    // master stage. The bass and the drums are scaled together, so the balance
    // you dial in is untouched — the whole instrument simply sits lower.
    //
    // Without this the mix ran hot: a default pulse line peaks at 0.94 on its
    // own and a 909 pattern at 1.59, so the sum reached +6.6 dBFS and whatever
    // sat at the output had to claw back 6 dB on every kick. Anything doing that
    // is reaching across from the drums onto the bass. -9 dB is measured to keep
    // a hard pattern clear of the ceiling entirely, so the limiter below never
    // engages and the bass comes out sample-for-sample identical whether or not
    // the drums are playing. Raise the track fader in the host to taste.
    inline constexpr float masterHeadroom = 0.3548f;   // -9 dB

    // Constant-power pan: -1 hard left, 0 centre, +1 hard right. A source keeps
    // its level as it moves out rather than dropping into the middle, so hard
    // over is +3 dB on its own side.
    //
    // Centre is special-cased to exactly unity on both sides. The trig lands a
    // hair under 1.0 in float, and "a hair under" is the difference between a
    // width control at zero leaving a project alone and quietly re-voicing it.
    // Both the drum spread and the bass unison rely on that being exact.
    inline void panGains (float pan, float& gl, float& gr)
    {
        if (pan == 0.0f)
        {
            gl = gr = 1.0f;
            return;
        }

        const float theta = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f;
        gl = std::cos (theta) * 1.41421356f;
        gr = std::sin (theta) * 1.41421356f;
    }

    // Master soft clip: unity slope at low level, hard ceiling at ±1.
    //
    // NOTE: this shapes *every* sample, so the gain it applies to one line moves
    // with whatever else is in the sum. Never put it across a mix — see
    // MasterLimiter. Kept for the offline tools that measure a single voice.
    inline float softClip (float x)
    {
        return std::tanh (x);
    }

    inline void softClipBlock (float* data, int n)
    {
        for (int i = 0; i < n; ++i)
            data[i] = softClip (data[i]);
    }

    // Final safety curve. Exactly the identity below the knee — not approximately,
    // bit-for-bit — so in normal use it contributes nothing at all. Above the knee
    // it bends with a continuous slope onto an asymptotic ±1 ceiling, which is
    // only ever reached by the limiter's attack-time overshoot.
    inline float safetyClip (float x, float knee = 0.9f)
    {
        const float a = std::abs (x);
        if (a <= knee)
            return x;

        const float s = x < 0.0f ? -1.0f : 1.0f;
        return s * (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)));
    }

    // Stereo-linked peak limiter for the master bus.
    //
    // The point of this over a waveshaper is *where* the nonlinearity lands. A
    // waveshaper redraws each sample, so a kick transient bends the bass waveform
    // sitting underneath it and sprays non-harmonic sidebands across it. A
    // limiter instead computes one smooth gain and multiplies — the bass keeps
    // its shape, and below the threshold the gain is exactly 1.0 and the mix
    // passes through untouched.
    //
    // Detection is linked across the pair so a peak on one side moves both by the
    // same amount and the stereo image stays put.
    class MasterLimiter
    {
    public:
        void prepare (double sampleRate)
        {
            attackCoef  = (float) std::exp (-1.0 / (0.0015 * sampleRate));   // 1.5 ms
            releaseCoef = (float) std::exp (-1.0 / (0.120  * sampleRate));   // 120 ms
            reset();
        }

        void reset() { gain = 1.0f; }

        void process (float* left, float* right, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const float detect = std::max (std::abs (left[i]), std::abs (right[i]));
                const float target = detect > threshold ? threshold / detect : 1.0f;

                // Down fast so a transient is caught, back up slowly so the gain
                // change is too gradual to hear as a waveform change.
                gain = target < gain ? target + (gain - target) * attackCoef
                                     : target + (gain - target) * releaseCoef;

                // Release is asymptotic and would leave a gain of 0.99999… riding
                // on the signal forever after one loud moment. Snapping it lands
                // the quiet passages back on exact unity, which is what makes the
                // untouched case genuinely untouched.
                if (gain > 0.9999f)
                    gain = 1.0f;

                left[i]  = safetyClip (left[i]  * gain);
                right[i] = safetyClip (right[i] * gain);
            }
        }

    private:
        static constexpr float threshold = 0.9f;

        float gain = 1.0f;
        float attackCoef = 0.0f, releaseCoef = 0.0f;
    };
}
