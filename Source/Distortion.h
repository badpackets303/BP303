#pragma once

#include <algorithm>
#include <cmath>

#include "DrumFuzz.h"

// Per-line distortion with a selectable character. One master enable (off is a
// transparent bypass) and a TYPE that picks the shaper; each type has its own
// pair of controls, so switching back and forth keeps each one's settings.
//
//   SOFT  - symmetric tanh overdrive with a pre-emphasis tilt. The original 303
//           voicing, moved here out of Fx303 so every flavour lives in one place.
//   FUZZ  - the industrial asymmetric hard clip (see DrumFuzz).
//   CRUSH - bit quantisation plus sample-and-hold decimation. Deliberately NOT
//           oversampled: the aliasing is the effect.
//   FOLD  - triangle wavefolder. Peaks fold back instead of flattening, so the
//           harmonic content tracks level and the timbre moves with the envelope.
//   RECT  - rectifier, for the octave-up ghost of a classic octave fuzz.
//
// FOLD and RECT throw off harmonics far above the band, so they run through a
// 2x oversampler; the others run at the host rate.
//
// LOWS splits a low band off before the shaper and adds it back clean, which is
// what keeps a distorted bass line's sub intact. At zero the split is skipped
// entirely, so the signal path is exactly what it would be without the feature.
// JUCE-free for standalone testing.
class Distortion
{
public:
    enum Type { Soft = 0, Fuzz, Crush, Fold, Rect, numTypes };

    struct Params
    {
        bool  on = false;
        int   type = Soft;
        float drive = 0.4f, color = 0.5f;        // SOFT / FUZZ
        float bits = 8.0f, rateSamples = 4.0f;   // CRUSH
        float foldAmount = 0.4f, foldSym = 0.5f; // FOLD
        float rectAmount = 0.5f, rectTone = 0.5f;// RECT
        float lowsKept = 0.0f;                   // 0 = drive the full range
    };

    void prepare (double sr)
    {
        sampleRate = sr;
        fuzz.prepare (sr);

        cTilt = lpCoef (800.0);
        cPost = lpCoef (7500.0);
        cSplit = lpCoef (splitHz);
        tiltLp = postLp = splitLp = 0.0f;

        held = 0.0f;
        holdPhase = 0.0f;

        dcX1 = dcY1 = 0.0f;
        rectLp = 0.0f;
        setRectTone (0.5f);

        up.prepare();
        down.prepare();
        prevOn = false;
        prevType = Soft;
    }

    void setParams (const Params& newParams)
    {
        const bool typeChanged = newParams.type != p.type;
        p = newParams;
        p.type = std::clamp (p.type, 0, (int) numTypes - 1);

        // SOFT: matches the voicing Fx303 used to apply
        const float drive01 = std::clamp (p.drive, 0.0f, 1.0f);
        softGain = 1.0f + drive01 * 19.0f;
        softMakeup = 0.9f / std::tanh (softGain * 0.9f);

        fuzz.setParams (true, drive01, p.color);

        // CRUSH: bits -> quantisation step, rate -> samples per sample-and-hold
        crushLevels = std::pow (2.0f, std::clamp (p.bits, 1.0f, 16.0f) - 1.0f);
        holdLength = std::max (1.0f, p.rateSamples);

        // FOLD: more gain drives the signal through more folds; SYM offsets it
        // so the folds land unevenly and even harmonics appear
        const float fold01 = std::clamp (p.foldAmount, 0.0f, 1.0f);
        foldGain = 1.0f + fold01 * 7.0f;
        foldBias = (std::clamp (p.foldSym, 0.0f, 1.0f) - 0.5f) * 2.0f * fold01;
        foldMakeup = 1.0f / (1.0f + fold01 * 0.6f);

        rectMix = std::clamp (p.rectAmount, 0.0f, 1.0f);
        setRectTone (p.rectTone);

        lowsKept = std::clamp (p.lowsKept, 0.0f, 1.0f);

        // A fresh start on enable, or when the shaper underneath changes, so no
        // state from the previous setting bleeds into the new sound.
        if ((p.on && ! prevOn) || typeChanged)
            reset();
        prevOn = p.on;
        prevType = p.type;
    }

    void process (float* x, int n)
    {
        if (! p.on)
            return;

        const bool split = lowsKept > 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float s = x[i];
            float clean = 0.0f;

            if (split)
            {
                // 1st-order crossover: low + high sums back to the input exactly,
                // so the band that isn't kept re-joins the shaper untouched.
                splitLp += (s - splitLp) * cSplit;
                clean = splitLp * lowsKept;
                s -= clean;
            }

            x[i] = shape (s) + clean;
        }
    }

private:
    // --- the shapers ------------------------------------------------------
    float shape (float s)
    {
        switch (p.type)
        {
            case Fuzz:  fuzz.process (&s, 1); return s;
            case Crush: return crush (s);
            case Fold:  return overSampled (s, [this] (float v) { return fold (v); });
            case Rect:  return overSampled (s, [this] (float v) { return rect (v); });
            case Soft:
            default:    return soft (s);
        }
    }

    float soft (float s)
    {
        // color tilts the signal toward highs before the clipper, moving it from
        // smooth boom to aggressive buzz
        tiltLp += (s - tiltLp) * cTilt;
        const float tilted = s + p.color * 1.5f * (s - tiltLp);
        const float d = std::tanh (softGain * tilted) * softMakeup;
        postLp += (d - postLp) * cPost;   // tame the fizz
        return postLp;
    }

    float crush (float s)
    {
        // sample and hold first, then quantise what was held
        holdPhase += 1.0f;
        if (holdPhase >= holdLength)
        {
            holdPhase -= holdLength;
            // Mid-riser: the step straddles zero instead of sitting on it. A
            // plain round() puts a step *centred* on zero, which swallows
            // everything below half a step — at 1 bit that muted any line
            // peaking under 0.5 outright, while a hotter one came through as a
            // full-scale square. Same knob position, opposite results, decided
            // by input level. Offsetting by half a step is inaudible at 8 bits
            // and makes 1 bit an honest square at any level.
            held = (std::floor (s * crushLevels) + 0.5f) / crushLevels;
        }
        return held;
    }

    float fold (float s)
    {
        return triangleFold (s * foldGain + foldBias) * foldMakeup;
    }

    float rect (float s)
    {
        // |s| doubles the fundamental — the octave-up ghost. The DC it carries
        // is removed after the blend, which is also where the bias from an
        // asymmetric fold gets taken out.
        // the 0.8 keeps a fully rectified signal from overshooting the dry level
        const float rectified = (std::abs (s) * 2.0f - 1.0f) * 0.8f;
        const float mixed = s * (1.0f - rectMix) + rectified * rectMix;
        rectLp += (mixed - rectLp) * cRect;
        return rectLp;
    }

    // Triangle fold with period 4: the identity across [-1, 1], folding back on
    // itself beyond that, so the output can never leave [-1, 1].
    static float triangleFold (float v)
    {
        const float q = (v + 1.0f) * 0.25f;
        return 1.0f - 4.0f * std::abs ((q - std::floor (q)) - 0.5f);
    }

    // --- 2x oversampling --------------------------------------------------
    // Linear-phase windowed-sinc half-band pair. Zero-stuff, shape at twice the
    // rate, filter and drop every other sample.
    struct HalfBand
    {
        static constexpr int numTaps = 23;
        float h[numTaps] {};
        float z[numTaps] {};
        int pos = 0;

        void prepare()
        {
            double sum = 0.0;
            for (int i = 0; i < numTaps; ++i)
            {
                const double m = i - (numTaps - 1) / 2.0;
                const double sinc = m == 0.0 ? 0.5
                                             : std::sin (3.14159265358979 * m * 0.5)
                                                   / (3.14159265358979 * m);
                const double win = 0.54 - 0.46 * std::cos (2.0 * 3.14159265358979 * i
                                                           / (numTaps - 1));
                h[i] = (float) (sinc * win);
                sum += h[i];
            }
            for (auto& c : h)          // unity gain at DC
                c = (float) (c / sum);

            clear();
        }

        void clear()
        {
            for (auto& v : z)
                v = 0.0f;
            pos = 0;
        }

        float process (float in)
        {
            z[pos] = in;
            float acc = 0.0f;
            int idx = pos;
            for (int i = 0; i < numTaps; ++i)
            {
                acc += h[i] * z[idx];
                if (--idx < 0)
                    idx = numTaps - 1;
            }
            if (++pos >= numTaps)
                pos = 0;
            return acc;
        }
    };

    template <typename Shaper>
    float overSampled (float s, Shaper&& shaper)
    {
        // x2 on the inserted sample keeps the level through the zero-stuffing
        const float a = shaper (up.process (s * 2.0f));
        const float b = shaper (up.process (0.0f));

        const float out = down.process (a);
        down.process (b);            // the half we throw away
        return dcBlock (out);
    }

    float dcBlock (float v)
    {
        const float out = v - dcX1 + 0.9992f * dcY1;
        dcX1 = v;
        dcY1 = out;
        return out;
    }

    void setRectTone (float tone01)
    {
        // dull to open, over the shaper's own (doubled) sample rate
        const double hz = 1500.0 * std::pow (8.0, (double) std::clamp (tone01, 0.0f, 1.0f));
        cRect = (float) (1.0 - std::exp (-6.2831853 * hz / (sampleRate * 2.0)));
    }

    void reset()
    {
        tiltLp = postLp = splitLp = 0.0f;
        held = 0.0f;
        holdPhase = 0.0f;
        dcX1 = dcY1 = 0.0f;
        rectLp = 0.0f;
        up.clear();
        down.clear();
    }

    float lpCoef (double freqHz) const
    {
        return (float) (1.0 - std::exp (-6.2831853 * freqHz / sampleRate));
    }

    static constexpr double splitHz = 120.0;

    double sampleRate = 44100.0;
    Params p;
    bool prevOn = false;
    int  prevType = Soft;

    DrumFuzz fuzz;

    float softGain = 1.0f, softMakeup = 1.0f;
    float tiltLp = 0.0f, postLp = 0.0f, cTilt = 0.1f, cPost = 0.5f;

    float crushLevels = 128.0f, holdLength = 1.0f, held = 0.0f, holdPhase = 0.0f;

    float foldGain = 1.0f, foldBias = 0.0f, foldMakeup = 1.0f;

    float rectMix = 0.5f, rectLp = 0.0f, cRect = 0.2f;

    float lowsKept = 0.0f, splitLp = 0.0f, cSplit = 0.02f;

    float dcX1 = 0.0f, dcY1 = 0.0f;

    HalfBand up, down;
};
