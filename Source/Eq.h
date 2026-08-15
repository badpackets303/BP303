#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

// Ten-band graphic EQ across a stereo pair, one instance per line. Octave-spaced
// peaking biquads at the ISO centres, ±12 dB each, plus a bandpass tap per band
// that feeds the editor's meters. JUCE-free for standalone testing.
//
// Two things about it are load-bearing.
//
// **Flat is nothing, not almost nothing.** A peaking filter at 0 dB has b == a,
// so its transfer function is exactly 1 — but only in exact arithmetic. Run in
// float it still rounds every sample, and this instrument's tests demand that a
// width or tone control at its default leaves the output bit-identical to what
// it was before the control existed. So flat is a branch, not a coefficient: all
// ten gains at zero and the samples are not touched at all. The filter states
// are cleared on the way into that branch, because a frozen state resumed from
// later is a click, and clearing them costs nothing at the moment the filters
// are passing signal through unchanged anyway.
//
// **Both channels share one set of coefficients.** Nothing here may widen: the
// pair arrives identical unless UNISON, the drum pans or the ping-pong delay
// made it otherwise, and an EQ that computed its coefficients twice — or drifted
// between two smoothers — would quietly turn a mono line into a slightly-stereo
// one. One coefficient set, applied to two independent state pairs.
class GraphicEq
{
public:
    static constexpr int numBands = 10;

    // ISO octave centres. The two ends earn their place by what they remove
    // rather than what they add: nothing in a 303 or an 808 kit wants lifting at
    // 31 Hz, but plenty of it wants the rumble pulled out, and 16 k is where hat
    // hiss gets tamed rather than boosted.
    static constexpr float centreHz[numBands] = {
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f,
        2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    static constexpr float maxGainDb = 12.0f;

    // Meter range. The tap sits before the master headroom trim, so a mix at a
    // sensible level reads in the top half rather than pinned; -48 dB of range
    // is what keeps the quiet bands off the floor without flattening the loud
    // ones together.
    static constexpr float meterFloorDb = -48.0f;

    void prepare (double sr)
    {
        sampleRate = sr;

        for (int b = 0; b < numBands; ++b)
        {
            auto& s = bands[(size_t) b];

            // A centre that has run past the usable range — 16 k at a 32 kHz
            // sample rate — is switched out rather than folded down onto some
            // other frequency while still wearing its label.
            s.active = centreHz[b] < 0.45f * (float) sr;

            const double w0 = 6.283185307179586 * (double) centreHz[b] / sr;
            s.cosW0 = (float) std::cos (w0);
            s.alpha = (float) (std::sin (w0) / (2.0 * octaveQ));

            // Constant-peak-gain bandpass on the same centre and Q, for the
            // meter. Fixed, so it is computed once here and never again.
            //
            // Two of them in series, not one. A single biquad's skirts fall at
            // only 6 dB an octave, which puts the neighbouring bar within a few
            // dB of the driven one and turns ten meters into one wide blob that
            // rises and falls together. Cascading a second doubles the slope and
            // is what makes the display read as a spectrum instead of a level.
            // Each is unity at the centre, so the pair still is.
            const float inv = 1.0f / (1.0f + s.alpha);
            s.meterFilter.b0 =  s.alpha * inv;
            s.meterFilter.b1 =  0.0f;
            s.meterFilter.b2 = -s.alpha * inv;
            s.meterFilter.a1 = -2.0f * s.cosW0 * inv;
            s.meterFilter.a2 = (1.0f - s.alpha) * inv;
            s.meterFilter2 = s.meterFilter;

            s.gainDb = 0.0f;
            s.targetDb = 0.0f;
            s.setUnity();
        }

        // Glide is per chunk rather than per block: a host handing us 1024
        // samples at a time would otherwise step the coefficients once every
        // 23 ms, which a resonant band rings on audibly.
        smoothCoef   = (float) std::exp (-(double) chunk / (0.030 * sr));
        meterAttack  = (float) std::exp (-(double) chunk / (0.005 * sr));
        meterRelease = (float) std::exp (-(double) chunk / (0.250 * sr));

        reset();
    }

    void reset()
    {
        for (auto& s : bands)
        {
            s.left.reset();
            s.right.reset();
            s.meterFilter.reset();
            s.meterFilter2.reset();
            s.env = 0.0f;
            s.out.store (0.0f, std::memory_order_relaxed);
        }
    }

    // `gainsDb` is numBands entries. Switched off, every band glides to zero
    // rather than dropping out, so ON and OFF are the same click-free move as
    // dragging the faders back to the centre line — and once the glide arrives
    // the whole unit falls into the bypass branch on its own.
    void setParams (bool onIn, const float* gainsDb)
    {
        for (int b = 0; b < numBands; ++b)
        {
            auto& s = bands[(size_t) b];
            s.targetDb = (onIn && s.active)
                       ? std::clamp (gainsDb[b], -maxGainDb, maxGainDb)
                       : 0.0f;
        }
    }

    // Meters cost a bandpass and a follower per band, so they run only while
    // something is looking at them. Independent of the audio path: metering a
    // flat EQ still reports what is passing through it, and still changes
    // nothing about what comes out.
    void setMetering (bool shouldMeter)
    {
        metering.store (shouldMeter, std::memory_order_relaxed);
    }

    void process (float* l, float* r, int n)
    {
        for (int off = 0; off < n; off += chunk)
        {
            const int len = std::min (chunk, n - off);

            if (advance())
                for (auto& s : bands)
                {
                    if (s.gainDb == 0.0f)
                        continue;

                    s.left .process (l + off, len);
                    s.right.process (r + off, len);
                }

            if (metering.load (std::memory_order_relaxed))
                meterChunk (l + off, r + off, len);
        }
    }

    // Post-EQ level in band `b`, 0..1 across meterFloorDb..0 dBFS. Called from
    // the message thread.
    float bandLevel (int b) const
    {
        return bands[(size_t) b].out.load (std::memory_order_relaxed);
    }

    // What the ten bands add up to at `hz`, in dB. This is what the editor
    // draws its curve from — built here, off the same coefficients the audio
    // path runs, so the line on screen cannot drift from what is being heard.
    // Static and JUCE-free, so the test can hold it against a measured sweep.
    static float responseDb (const float* gainsDb, float hz, double sr)
    {
        double total = 0.0;

        for (int b = 0; b < numBands; ++b)
        {
            const double g = (double) gainsDb[b];

            // Skipped on exactly the same terms the audio path skips them, or
            // the curve would promise a band the filters aren't running.
            if (g == 0.0 || centreHz[b] >= 0.45f * (float) sr)
                continue;

            const double A  = std::pow (10.0, g / 40.0);
            const double w0 = 6.283185307179586 * (double) centreHz[b] / sr;
            const double al = std::sin (w0) / (2.0 * octaveQ);
            const double cw0 = std::cos (w0);

            const double b0 = 1.0 + al * A, b1 = -2.0 * cw0, b2 = 1.0 - al * A;
            const double a0 = 1.0 + al / A, a1 = -2.0 * cw0, a2 = 1.0 - al / A;

            const double w  = 6.283185307179586 * (double) hz / sr;
            const double c1 = std::cos (w),       s1 = std::sin (w);
            const double c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);

            const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
            const double dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);

            total += 20.0 * std::log10 (std::hypot (nr, ni) / std::hypot (dr, di));
        }

        return (float) total;
    }

private:
    // Octave-wide skirts: Q = sqrt(2^N)/(2^N - 1) at N = 1. Wider and the bands
    // smear into each other so a single fader stops meaning one frequency;
    // narrower and ten of them no longer cover the spectrum between them.
    static constexpr double octaveQ = 1.4142135623730951;

    // Coefficients are recomputed at most once per chunk, and only for bands
    // that actually moved — which in steady state is none of them. 32 samples is
    // short enough that a full-travel fader move sounds like a move rather than
    // a series of steps, and long enough that the check costs nothing.
    static constexpr int chunk = 32;

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float s1 = 0.0f, s2 = 0.0f;

        void reset() { s1 = s2 = 0.0f; }

        // Transposed direct form II: fewer state slots than DF-I and better
        // behaved in float at the low bands, where the poles sit very close to
        // the unit circle.
        void process (float* x, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i];
                const float out = b0 * in + s1;
                s1 = b1 * in - a1 * out + s2;
                s2 = b2 * in - a2 * out;
                x[i] = out;
            }
        }

        float tick (float in)
        {
            const float out = b0 * in + s1;
            s1 = b1 * in - a1 * out + s2;
            s2 = b2 * in - a2 * out;
            return out;
        }
    };

    struct Band
    {
        bool  active = true;
        float cosW0 = 0.0f, alpha = 0.0f;
        float gainDb = 0.0f, targetDb = 0.0f;

        Biquad left, right;

        // meter tap: a bandpass pair fixed at prepare time, plus a peak follower
        Biquad meterFilter, meterFilter2;
        float env = 0.0f;
        std::atomic<float> out { 0.0f };

        void setUnity()
        {
            left.b0 = right.b0 = 1.0f;
            left.b1 = right.b1 = 0.0f;
            left.b2 = right.b2 = 0.0f;
            left.a1 = right.a1 = 0.0f;
            left.a2 = right.a2 = 0.0f;
        }

        // RBJ peaking EQ. Only the gain moves, so the frequency-dependent parts
        // were computed once in prepare and all that is left here is one pow.
        void updateCoeffs()
        {
            if (gainDb == 0.0f)
            {
                setUnity();
                return;
            }

            const float A      = std::pow (10.0f, gainDb * 0.025f);   // 10^(dB/40)
            const float alphaA = alpha * A;
            const float alphaD = alpha / A;
            const float inv    = 1.0f / (1.0f + alphaD);

            left.b0 = right.b0 = (1.0f + alphaA) * inv;
            left.b1 = right.b1 = -2.0f * cosW0 * inv;
            left.b2 = right.b2 = (1.0f - alphaA) * inv;
            left.a1 = right.a1 = -2.0f * cosW0 * inv;
            left.a2 = right.a2 = (1.0f - alphaD) * inv;
        }
    };

    // Glide every band one chunk along, recomputing only what moved. Returns
    // whether any band is doing anything at all.
    bool advance()
    {
        bool anyLive = false;

        for (auto& s : bands)
        {
            // Exact float comparison on purpose, here and on the zero tests
            // below: "settled" has to mean landed on the target bit for bit,
            // because that is what the bypass branch keys off. An epsilon here
            // would leave a band a hair off zero, running filters forever and
            // costing the whole unit its transparency.
            if (s.gainDb != s.targetDb)
            {
                s.gainDb = s.targetDb + (s.gainDb - s.targetDb) * smoothCoef;

                // An asymptotic glide never actually arrives, and "never quite
                // reaches zero" is the difference between a flat EQ costing
                // nothing and it costing ten biquads that almost cancel. Snap
                // the last hair of it, well below what is audible as a step.
                if (std::abs (s.gainDb - s.targetDb) < 0.0005f)
                    s.gainDb = s.targetDb;

                s.updateCoeffs();

                // A band that has just landed on zero stops being processed at
                // all, so its state would freeze there and be resumed from on
                // the next move off zero — which is a click. At unity the
                // filter contributes nothing, so clearing it here is silent,
                // and it is the one moment where that is true.
                if (s.gainDb == 0.0f)
                {
                    s.left.reset();
                    s.right.reset();
                }
            }

            if (s.gainDb != 0.0f)
                anyLive = true;
        }

        return anyLive;
    }

    void meterChunk (const float* l, const float* r, int n)
    {
        for (auto& s : bands)
        {
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float mid = (l[i] + r[i]) * 0.5f;
                peak = std::max (peak, std::abs (s.meterFilter2.tick (s.meterFilter.tick (mid))));
            }

            // Up fast so a hat registers, down slowly so the bar is readable at
            // a 25 Hz repaint instead of strobing.
            const float coef = peak > s.env ? meterAttack : meterRelease;
            s.env = peak + (s.env - peak) * coef;

            const float db = s.env > 1.0e-6f ? 20.0f * std::log10 (s.env) : meterFloorDb;
            s.out.store (std::clamp ((db - meterFloorDb) / -meterFloorDb, 0.0f, 1.0f),
                         std::memory_order_relaxed);
        }
    }

    Band bands[numBands];
    double sampleRate = 44100.0;
    float smoothCoef = 0.0f, meterAttack = 0.0f, meterRelease = 0.0f;

    // Toggled from the message thread as editors open and close, read on the
    // audio thread every chunk.
    std::atomic<bool> metering { false };
};
