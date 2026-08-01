#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "StepDyn.h"

// Synthesized 606 / 808 / 909 drum voices: kick, snare, clap, closed & open
// hats. No samples — everything is generated. JUCE-free for standalone testing.
class DrumMachine
{
public:
    enum Voice { BD, SD, CP, CH, OH, numVoices };
    enum class Kit { K808, K909, K606 };

    void prepare (double sr)
    {
        sampleRate = (float) sr;
        twoPiOverSr = 6.2831853f / sampleRate;
        decayCoefClick = decayCoef (0.004f);
        reset();
    }

    void reset()
    {
        bdAmp = sdAmp = cpAmp = chAmp = ohAmp = 0.0f;
        bdPitchEnv = sdNoiseAmp = clickEnv = 0.0f;
        bdPhase = sdPhase1 = sdPhase2 = 0.0f;
        cpT = 0.0f;
        lp1 = lp2 = hp1 = hp2 = chLp = chHp1 = chHp2 = ohLp = ohHp1 = ohHp2 = 0.0f;
        sdLp = sdHp = 0.0f;
        for (auto& p : metalPhase) p = 0.0f;
        rng = 0x12345678u;
    }

    // Per-voice tune multipliers (1.0 = original pitch). The two hats share one
    // metal oscillator bank, so a single hatTuneAmt moves both.
    void setParams (Kit k, float bdTuneAmt, float sdTuneAmt, float cpTuneAmt,
                    float hatTuneAmt, float bdDecaySec,
                    const float* laneLevels, float volDb)
    {
        kit = k;
        bdTune = bdTuneAmt;
        sdTune = sdTuneAmt;
        cpTune = cpTuneAmt;
        hatTune = hatTuneAmt;
        bdDecay = bdDecaySec;
        for (int i = 0; i < numVoices; ++i)
            levels[i] = laneLevels[i];
        masterGain = std::pow (10.0f, volDb / 20.0f);
    }

    // An accent is the 1.3x it always was; a soft hit sits roughly as far under
    // a normal one as an accent sits over it, so the three read as even steps.
    static constexpr float accentLevel = 1.3f, softLevel = 0.55f;

    // dyn is a dyn303::Dyn. Every voice scales its whole hit by this, so a soft
    // step is simply a lighter strike — a ghost note under the pattern rather
    // than a differently-voiced one.
    void trigger (int voice, int dyn)
    {
        const float acc = dyn > 0 ? accentLevel : dyn < 0 ? softLevel : 1.0f;

        switch (voice)
        {
            case BD:
                bdAmp = acc;
                bdPitchEnv = 1.0f;
                clickEnv = 1.0f;
                bdPhase = 0.0f;
                bdKit = kit;
                bdAmpCoef = decayCoef (sel3 (kit, bdDecay,
                                             std::min (bdDecay, 0.5f),
                                             std::min (bdDecay, 0.35f)));
                bdPitchCoef = decayCoef (sel3 (kit, 0.11f, 0.045f, 0.06f));
                break;

            case SD:
                sdAmp = acc;
                sdNoiseAmp = acc;
                sdPhase1 = sdPhase2 = 0.0f;
                sdKit = kit;
                sdToneCoef  = decayCoef (sel3 (kit, 0.13f, 0.10f, 0.09f));
                sdNoiseCoef = decayCoef (sel3 (kit, 0.20f, 0.16f, 0.12f));
                break;

            case CP:
                cpAmp = acc;
                cpT = 0.0f;
                cpKit = kit;
                break;

            case CH:
                chAmp = acc;
                chKit = kit;
                chCoef = decayCoef (sel3 (kit, 0.045f, 0.045f, 0.028f));
                ohAmp = 0.0f;          // closed hat chokes the open hat
                break;

            case OH:
                ohAmp = acc;
                ohKit = kit;
                ohCoef = decayCoef (sel3 (kit, 0.40f, 0.30f, 0.18f));
                break;
        }
    }

    // Adds into out
    void render (float* out, int n)
    {
        for (int i = 0; i < n; ++i)
            out[i] += renderSample() * masterGain;
    }

private:
    static float sel3 (Kit k, float a808, float a909, float a606)
    {
        return k == Kit::K808 ? a808 : (k == Kit::K909 ? a909 : a606);
    }

    float decayCoef (float seconds) const
    {
        return std::exp (-1.0f / (std::max (0.001f, seconds) * sampleRate));
    }

    float noise()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (int32_t) rng * 4.656613e-10f;
    }

    float renderSample()
    {
        float mix = 0.0f;
        const float twoPi = 6.2831853f;

        // ---- Kick ----
        if (bdAmp > 1.0e-4f)
        {
            const float base  = sel3 (bdKit, 52.0f, 48.0f, 58.0f) * bdTune;
            const float sweep = sel3 (bdKit, 2.5f, 5.0f, 3.4f);
            const float freq = base * (1.0f + sweep * bdPitchEnv);
            bdPhase += freq / sampleRate;
            if (bdPhase >= 1.0f) bdPhase -= 1.0f;

            float s = std::sin (twoPi * bdPhase);
            if (bdKit == Kit::K909)
            {
                s = std::tanh (2.5f * s);
                s += noise() * 0.4f * clickEnv;    // 909 attack click
            }
            else if (bdKit == Kit::K606)
            {
                s = std::tanh (1.7f * s);
                s += noise() * 0.22f * clickEnv;   // 606 has a tight click too
            }
            mix += s * bdAmp * levels[BD];

            bdAmp *= bdAmpCoef;
            bdPitchEnv *= bdPitchCoef;
            clickEnv *= decayCoefClick;
        }

        // ---- Snare ----
        if (sdAmp > 1.0e-4f || sdNoiseAmp > 1.0e-4f)
        {
            const float f1 = sel3 (sdKit, 180.0f, 185.0f, 238.0f) * sdTune;
            const float f2 = sel3 (sdKit, 330.0f, 330.0f, 476.0f) * sdTune;
            sdPhase1 += f1 / sampleRate; if (sdPhase1 >= 1.0f) sdPhase1 -= 1.0f;
            sdPhase2 += f2 / sampleRate; if (sdPhase2 >= 1.0f) sdPhase2 -= 1.0f;

            const float tone = (std::sin (twoPi * sdPhase1) * 0.7f
                              + std::sin (twoPi * sdPhase2) * 0.3f) * sdAmp;

            float sn = noise();
            sdLp += (sn - sdLp) * onePoleC (sel3 (sdKit, 6000.0f, 8000.0f, 9000.0f));
            sdHp += (sdLp - sdHp) * onePoleC (sel3 (sdKit, 1200.0f, 900.0f, 1500.0f));
            const float snare = (sdLp - sdHp) * sdNoiseAmp;

            const float noiseMix = sel3 (sdKit, 0.6f, 0.75f, 0.72f);
            mix += (tone * (1.0f - noiseMix) + snare * noiseMix) * 1.4f * levels[SD];

            sdAmp *= sdToneCoef;
            sdNoiseAmp *= sdNoiseCoef;
        }

        // ---- Clap ----
        if (cpAmp > 1.0e-4f)
        {
            // three fast retriggered bursts, then a tail
            float env;
            if (cpT < 0.03f)
                env = std::exp (-std::fmod (cpT, 0.01f) / 0.004f);
            else
                env = std::exp (-(cpT - 0.03f) / sel3 (cpKit, 0.14f, 0.10f, 0.08f));

            float cn = noise();
            lp1 += (cn - lp1) * onePoleC (sel3 (cpKit, 2500.0f, 3500.0f, 3800.0f) * cpTune);
            hp1 += (lp1 - hp1) * onePoleC (sel3 (cpKit, 700.0f, 900.0f, 1100.0f) * cpTune);
            mix += (lp1 - hp1) * env * cpAmp * 2.2f * levels[CP];

            cpT += 1.0f / sampleRate;
            if (cpT > 0.5f) cpAmp = 0.0f;
        }

        // ---- Hats (shared metal bank; 909 uses noise instead) ----
        const bool hatsActive = chAmp > 1.0e-4f || ohAmp > 1.0e-4f;
        if (hatsActive)
        {
            float metal = 0.0f;
            static constexpr float metalFreqs[6] = { 263.5f, 400.0f, 421.0f,
                                                     474.0f, 587.3f, 845.0f };
            for (int o = 0; o < 6; ++o)
            {
                metalPhase[o] += metalFreqs[o] * 4.0f * hatTune / sampleRate;
                if (metalPhase[o] >= 1.0f) metalPhase[o] -= 1.0f;
                metal += metalPhase[o] < 0.5f ? 1.0f : -1.0f;
            }
            metal *= 0.16f;

            if (chAmp > 1.0e-4f)
            {
                // two cascaded one-pole highpasses; 606 sits brighter
                const float src = chKit == Kit::K909 ? noise() : metal;
                const float hp  = sel3 (chKit, 6500.0f, 6500.0f, 8500.0f) * hatTune;
                chLp += (src - chLp) * onePoleC (hp);
                const float h1 = src - chLp;
                chHp1 += (h1 - chHp1) * onePoleC (hp);
                mix += (h1 - chHp1) * chAmp * 2.5f * levels[CH];
                chAmp *= chCoef;
            }
            if (ohAmp > 1.0e-4f)
            {
                const float src = ohKit == Kit::K909 ? noise() : metal;
                const float hp  = sel3 (ohKit, 5500.0f, 5500.0f, 7000.0f) * hatTune;
                ohLp += (src - ohLp) * onePoleC (hp);
                const float h1 = src - ohLp;
                ohHp1 += (h1 - ohHp1) * onePoleC (hp);
                mix += (h1 - ohHp1) * ohAmp * 2.5f * levels[OH];
                ohAmp *= ohCoef;
            }
        }

        return mix * 0.7f;   // headroom against the bass line
    }

    float onePoleC (float freqHz) const
    {
        return 1.0f - std::exp (-twoPiOverSr * freqHz);
    }

    float sampleRate = 44100.0f;
    float twoPiOverSr = 6.2831853f / 44100.0f;

    Kit   kit = Kit::K808;
    float bdTune = 1.0f, bdDecay = 0.5f;
    float sdTune = 1.0f, cpTune = 1.0f, hatTune = 1.0f;
    float levels[numVoices] = { 0.9f, 0.8f, 0.7f, 0.6f, 0.6f };
    float masterGain = 1.0f;

    // kick
    float bdPhase = 0, bdAmp = 0, bdPitchEnv = 0, clickEnv = 0;
    float bdAmpCoef = 0.999f, bdPitchCoef = 0.999f, decayCoefClick = 0.99f;
    Kit   bdKit = Kit::K808;
    // snare
    float sdPhase1 = 0, sdPhase2 = 0, sdAmp = 0, sdNoiseAmp = 0;
    float sdToneCoef = 0.999f, sdNoiseCoef = 0.999f, sdLp = 0, sdHp = 0;
    Kit   sdKit = Kit::K808;
    // clap
    float cpAmp = 0, cpT = 0, lp1 = 0, lp2 = 0, hp1 = 0, hp2 = 0;
    Kit   cpKit = Kit::K808;
    // hats
    float metalPhase[6] = {};
    float chAmp = 0, ohAmp = 0, chCoef = 0.99f, ohCoef = 0.99f;
    float chLp = 0, chHp1 = 0, chHp2 = 0, ohLp = 0, ohHp1 = 0, ohHp2 = 0;
    Kit   chKit = Kit::K808, ohKit = Kit::K808;

    uint32_t rng = 0x12345678u;
};
