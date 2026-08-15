#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "DspUtil.h"
#include "StepDyn.h"

// Synthesized 606 / 808 / 909 drum voices: kick, snare, clap, closed & open
// hats. No samples — everything is generated. JUCE-free for standalone testing.
//
// The stereo overload places each voice across the pair. The width is real —
// separate voices at separate positions, not a widener across the sum — so it
// survives being summed back to mono instead of phasing away. SPREAD at zero
// puts every voice back in the centre and both channels carry the mono sum.
//
// Keeping the voices apart costs the old sum its last bit: it used to accumulate
// straight into one running total, which the compiler was free to fuse into an
// FMA, and a per-voice store then summed cannot be. The arithmetic is otherwise
// identical (proved by building both ways with -ffp-contract=off), so existing
// patterns move by at most one ULP — around -145 dBFS, below the noise floor of
// any converter this will ever reach. Nothing that matters rides on it: the
// master stage's guarantee is that the drums don't modulate the *bass*, and that
// is a property of the mix stage, not of this sum.
class DrumMachine
{
public:
    enum Voice { BD, SD, CP, CH, OH, numVoices };
    enum class Kit { K808, K909, K606 };

    // The default place for each voice at full spread: -1 hard left, +1 hard
    // right. These are where the BALANCE page starts, not a limit — setPan takes
    // whatever the player asks for.
    //
    // The kick starts centred and is worth leaving there: panning it throws away
    // half its power on one side, and it is the first thing to collapse on a
    // system that sums the low end to mono. The two hats start opposite each
    // other because a closed hat chokes an open one, so a hat pattern alternates
    // across the image instead of piling up on one side.
    static constexpr float voicePan[numVoices] = { 0.0f, -0.15f, 0.55f, -0.60f, 0.50f };

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
        lp1 = lp2 = hp1 = hp2 = 0.0f;
        for (auto& s : chHp) s = 0.0f;
        for (auto& s : ohHp) s = 0.0f;
        sdLp = sdHp = 0.0f;
        for (auto& p : metalPhase) p = 0.0f;
        rng = 0x12345678u;
    }

    // Per-voice tune multipliers (1.0 = original pitch). The two hats share one
    // metal oscillator bank, so a single hatTuneAmt moves both.
    // The three decay multipliers scale each voice's own per-kit decay rather
    // than setting an absolute time the way BD DECAY does. Two reasons: the
    // snare has *two* decays (tone and noise) at different lengths, which one
    // absolute number cannot express without flattening their ratio; and the
    // hats are where the kits differ most — 28 ms of closed hat on a 909
    // against 45 ms on a 606 — so an absolute time would make switching kits
    // stop changing the envelope at all. 1.0 is the kit as it was.
    void setParams (Kit k, float bdTuneAmt, float sdTuneAmt, float cpTuneAmt,
                    float hatTuneAmt, float bdDecaySec,
                    const float* laneLevels, float volDb,
                    float sdDecayMul = 1.0f, float chDecayMul = 1.0f,
                    float ohDecayMul = 1.0f)
    {
        kit = k;
        bdTune = bdTuneAmt;
        sdTune = sdTuneAmt;
        cpTune = cpTuneAmt;
        hatTune = hatTuneAmt;
        bdDecay = bdDecaySec;
        sdDecayScale = sdDecayMul;
        chDecayScale = chDecayMul;
        ohDecayScale = ohDecayMul;
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
                sdToneCoef  = decayCoef (sel3 (kit, 0.13f, 0.10f, 0.09f) * sdDecayScale);
                sdNoiseCoef = decayCoef (sel3 (kit, 0.20f, 0.16f, 0.12f) * sdDecayScale);
                break;

            case CP:
                cpAmp = acc;
                cpT = 0.0f;
                cpKit = kit;
                break;

            case CH:
                chAmp = acc;
                chKit = kit;
                chCoef = decayCoef (sel3 (kit, 0.045f, 0.045f, 0.028f) * chDecayScale);
                ohAmp = 0.0f;          // closed hat chokes the open hat
                break;

            case OH:
                ohAmp = acc;
                ohKit = kit;
                ohCoef = decayCoef (sel3 (kit, 0.40f, 0.30f, 0.18f) * ohDecayScale);
                break;
        }
    }

    // Slides every voice out to `spread01` of its position in voicePan. Only the
    // stereo render reads this; the mono one sums the voices whatever it says.
    void setSpread (float spread01)
    {
        const float s = std::clamp (spread01, 0.0f, 1.0f);
        float pans[numVoices];
        for (int i = 0; i < numVoices; ++i)
            pans[i] = voicePan[i] * s;
        setPan (pans);
    }

    // Per-voice positions, for a caller that wants its own layout rather than
    // voicePan scaled.
    void setPan (const float* pans)
    {
        for (int i = 0; i < numVoices; ++i)
            dsp303::panGains (pans[i], panL[i], panR[i]);
    }

    // Adds into out
    void render (float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float v[numVoices];
            renderVoices (v);
            out[i] += (sum (v) * headroom) * masterGain;
        }
    }

    // Adds into the channel pair, each voice at its own position.
    void render (float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float v[numVoices];
            renderVoices (v);

            float sl = 0.0f, sr = 0.0f;
            for (int k = 0; k < numVoices; ++k)
            {
                sl += v[k] * panL[k];
                sr += v[k] * panR[k];
            }

            l[i] += (sl * headroom) * masterGain;
            r[i] += (sr * headroom) * masterGain;
        }
    }

private:
    static float sum (const float* v)
    {
        float mix = 0.0f;
        for (int i = 0; i < numVoices; ++i)
            mix += v[i];
        return mix;
    }

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

    // Each voice's contribution kept apart rather than summed on the spot, so
    // the stereo render can place them. The headroom trim that used to ride on
    // the sum here now lives in render(), since applying it per voice and
    // summing is not the same arithmetic and would move every existing mix.
    void renderVoices (float* v)
    {
        for (int i = 0; i < numVoices; ++i)
            v[i] = 0.0f;

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
            v[BD] = s * bdAmp * levels[BD];

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
            v[SD] = (tone * (1.0f - noiseMix) + snare * noiseMix) * 1.4f * levels[SD];

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
            v[CP] = (lp1 - hp1) * env * cpAmp * 2.2f * levels[CP];

            cpT += 1.0f / sampleRate;
            if (cpT > 0.5f) cpAmp = 0.0f;
        }

        // ---- Hats (shared metal bank; 909 uses noise instead) ----
        const bool hatsActive = chAmp > 1.0e-4f || ohAmp > 1.0e-4f;
        if (hatsActive)
        {
            // The real 808's six oscillators, at twice their frequency. What you
            // are meant to hear is the thicket of their upper square harmonics,
            // not the oscillators themselves — the fundamentals sit far below the
            // highpass corner and the filter is supposed to remove them. These
            // used to run at 4x a different set, which put every fundamental in
            // the 1-3.4 kHz range the ear reads as pitched metal, and a 12 dB/oct
            // filter could not get them out again: one partial at 3.4 kHz stood
            // 35 dB above its neighbours, where the 909's noise hat manages 15.
            // That was the clang. Doubling keeps the harmonics dense up top
            // without dragging the fundamentals back into it.
            float metal = 0.0f;
            static constexpr float metalFreqs[6] = { 205.3f, 304.4f, 369.6f,
                                                     522.7f, 540.0f, 800.0f };
            for (int o = 0; o < 6; ++o)
            {
                metalPhase[o] += metalFreqs[o] * 2.0f * hatTune / sampleRate;
                if (metalPhase[o] >= 1.0f) metalPhase[o] -= 1.0f;
                metal += metalPhase[o] < 0.5f ? 1.0f : -1.0f;
            }
            metal *= 0.16f;

            // The makeup gains are measured, not chosen: four poles throw away
            // more than two, and more again at the higher corner a closed hat
            // uses, so each one is whatever puts that voice back at the RMS it had
            // before. Without them a saved pattern comes back with its hats
            // several dB down. The 909 keeps 2.5, having changed in no way.
            if (chAmp > 1.0e-4f)
            {
                const float hp = sel3 (chKit, 7000.0f, 6500.0f, 9000.0f) * hatTune;
                v[CH] = hatVoice (chKit, metal, chHp, hp,
                                  sel3 (chKit, 15.0f, 2.5f, 22.0f)) * chAmp * levels[CH];
                chAmp *= chCoef;
            }
            if (ohAmp > 1.0e-4f)
            {
                const float hp = sel3 (ohKit, 6000.0f, 5500.0f, 7500.0f) * hatTune;
                v[OH] = hatVoice (ohKit, metal, ohHp, hp,
                                  sel3 (ohKit, 12.5f, 2.5f, 16.0f)) * ohAmp * levels[OH];
                ohAmp *= ohCoef;
            }
        }

    }

    float onePoleC (float freqHz) const
    {
        return 1.0f - std::exp (-twoPiOverSr * freqHz);
    }

    // One hat, from the shared metal bank or from noise, through its own
    // highpass cascade.
    //
    // The metal kits get four poles where they used to get two. Twelve dB an
    // octave leaves the oscillator fundamentals audible as tones however they are
    // tuned, and no amount of moving them helps — drop them an octave and their
    // harmonics land in the same place. Steepness is what turns the bank into a
    // sheen instead of a chord.
    //
    // The 909's hat is noise, which is flat to begin with and needs none of that,
    // so it keeps the two poles and the gain it always had and comes out
    // unchanged. Its `noise()` draw stays one per hat per sample for the same
    // reason: the random stream must not shift under it.
    float hatVoice (Kit hatKit, float metal, float* state, float freqHz, float gain)
    {
        // Six squares are a chord however they are filtered. A little noise fills
        // between the partials, and that is what reads as air rather than ring.
        static constexpr float noiseMix = 0.15f;

        const bool noiseKit = hatKit == Kit::K909;
        float src = noiseKit ? noise()
                             : metal * (1.0f - noiseMix) + noise() * noiseMix;

        const int   poles = noiseKit ? 2 : maxHatPoles;
        const float c = onePoleC (freqHz);
        for (int p = 0; p < poles; ++p)
        {
            state[p] += (src - state[p]) * c;
            src -= state[p];
        }

        return src * gain;
    }

    static constexpr float headroom = 0.7f;   // against the bass line

    // unity on both sides until setSpread/setPan says otherwise, so an untouched
    // instance renders the mono sum into each channel
    float panL[numVoices] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float panR[numVoices] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    float sampleRate = 44100.0f;
    float twoPiOverSr = 6.2831853f / 44100.0f;

    Kit   kit = Kit::K808;
    float bdTune = 1.0f, bdDecay = 0.5f;
    float sdDecayScale = 1.0f, chDecayScale = 1.0f, ohDecayScale = 1.0f;
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
    static constexpr int maxHatPoles = 4;
    float chHp[maxHatPoles] = {}, ohHp[maxHatPoles] = {};
    Kit   chKit = Kit::K808, ohKit = Kit::K808;

    uint32_t rng = 0x12345678u;
};
