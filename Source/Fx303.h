#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Bass/drum-channel tempo-synced feedback delay. The distortion that used to sit
// in front of it now lives in Distortion.h, which owns every drive flavour and
// runs just ahead of this in the chain — so the echoes still repeat a distorted
// signal, exactly as before. JUCE-free for standalone testing.
//
// Two delay lines run side by side, one per channel, and a TYPE picks how they
// are wired:
//
//   MONO   - each channel is its own independent feedback line, which is what
//            this always did. Fed the same signal on both channels (everything
//            upstream in the plugin is mono) the two lines evolve identically,
//            so the output is the single-line sound the plugin has always had.
//   STEREO - ping-pong. The dry goes into the left line only, and each line's
//            output feeds the other, so echo 1 lands left, echo 2 right, echo 3
//            left again. `feedback` is applied on every hop, so an echo decays
//            at the same rate per repeat as it does in MONO.
//
// TIME, FEEDBACK and MIX mean the same thing either way: the type only changes
// the routing between the two lines, never what the controls do.
class Fx303
{
public:
    enum Type { Mono = 0, Stereo, numTypes };

    void prepare (double sr)
    {
        sampleRate = sr;
        const size_t len = (size_t) (sr * 2.0) + 8;
        bufL.assign (len, 0.0f);
        bufR.assign (len, 0.0f);
        writePos = 0;
        delaySmooth = 0.0f;
        fbLpL = fbLpR = 0.0f;
        prevDelayOn = false;
        prevType = Mono;

        cFb = lpCoef (5000.0);
    }

    void setParams (bool delayOnIn, int typeIn, int delaySixteenths, float fbIn,
                    float mixIn, double bpm)
    {
        delayOn = delayOnIn;
        type = std::clamp (typeIn, 0, (int) numTypes - 1);
        const double sixteenth = sampleRate * 15.0 / std::clamp (bpm, 20.0, 400.0);
        delayTarget = (float) std::min (sixteenth * std::max (1, delaySixteenths),
                                        (double) bufL.size() - 4.0);
        feedback = std::clamp (fbIn, 0.0f, 0.95f);
        mix = mixIn;

        // Fresh lines on enable: no stale audio, no glide chirp from zero. Also
        // on a type change, because the two routings leave completely different
        // material in the lines — crossing over would smear one into the other.
        if ((delayOn && ! prevDelayOn) || type != prevType)
        {
            std::fill (bufL.begin(), bufL.end(), 0.0f);
            std::fill (bufR.begin(), bufR.end(), 0.0f);
            delaySmooth = delayTarget;
            fbLpL = fbLpR = 0.0f;
        }
        prevDelayOn = delayOn;
        prevType = type;
    }

    void process (float* l, float* r, int n)
    {
        if (! delayOn)
            return;

        const int size = (int) bufL.size();

        for (int i = 0; i < n; ++i)
        {
            delaySmooth += (delayTarget - delaySmooth) * 0.0005f;

            const float wetL = readAt (bufL, delaySmooth);
            const float wetR = readAt (bufR, delaySmooth);

            if (type == Stereo)
            {
                // Ping-pong: each line's output darkens and feeds the *other*
                // line. The dry enters on the left only — summed, so a genuinely
                // stereo input still sends both sides, and a mono one (which is
                // all the plugin ever feeds it) sends at unchanged level.
                fbLpL += (wetR - fbLpL) * cFb;
                fbLpR += (wetL - fbLpR) * cFb;
                bufL[(size_t) writePos] = (l[i] + r[i]) * 0.5f + fbLpL * feedback;
                bufR[(size_t) writePos] = fbLpR * feedback;
            }
            else
            {
                // two independent lines, each darkening its own feedback path
                fbLpL += (wetL - fbLpL) * cFb;
                fbLpR += (wetR - fbLpR) * cFb;
                bufL[(size_t) writePos] = l[i] + fbLpL * feedback;
                bufR[(size_t) writePos] = r[i] + fbLpR * feedback;
            }

            writePos = (writePos + 1) % size;

            l[i] += wetL * mix;
            r[i] += wetR * mix;
        }
    }

private:
    // fractional read, delaySamples back from the shared write head
    float readAt (const std::vector<float>& buf, float delaySamples) const
    {
        const auto size = (float) buf.size();
        float rp = (float) writePos - delaySamples;
        while (rp < 0.0f)
            rp += size;

        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % (int) buf.size();
        const float frac = rp - (float) i0;
        return buf[(size_t) i0] * (1.0f - frac) + buf[(size_t) i1] * frac;
    }

    float lpCoef (double freqHz) const
    {
        return (float) (1.0 - std::exp (-6.2831853 * freqHz / sampleRate));
    }

    double sampleRate = 44100.0;

    bool  delayOn = false, prevDelayOn = false;
    int   type = Mono, prevType = Mono;
    float delayTarget = 0.0f, delaySmooth = 0.0f;
    float feedback = 0.4f, mix = 0.25f, cFb = 0.4f;
    float fbLpL = 0.0f, fbLpR = 0.0f;
    std::vector<float> bufL, bufR;
    int writePos = 0;
};
