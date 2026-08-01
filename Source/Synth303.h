#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Monophonic 303-style voice: PolyBLEP saw/pulse oscillator into a
// zero-delay-feedback 4-pole ladder filter, with the classic accent and
// slide behaviour. Deliberately JUCE-free so it can be unit-tested standalone.
class Synth303
{
public:
    // Order matters: the wave parameter stores this index, so new waves go on
    // the end or every saved project changes what it plays.
    enum class Wave { Saw, Square, Pulse };
    static constexpr int numWaves = 3;

    void prepare (double sr)
    {
        sampleRate = sr;
        glideCoef   = onePoleCoef (0.06);
        attackCoef  = onePoleCoef (0.003);
        releaseCoef = onePoleCoef (0.012);
        accSmoothCoef = onePoleCoef (0.03);
        // 5 Hz: below anything the voice can play, so it takes off DC without
        // touching the bottom octave.
        dcCoef = (float) std::exp (-2.0 * 3.14159265358979 * 5.0 / sampleRate);
        reset();
    }

    void reset()
    {
        s1 = s2 = s3 = s4 = 0.0f;
        dcX1 = dcY1 = 0.0f;
        phase = 0.0;
        vibPhase = 0.0;
        env = accEnv = accEnvSmoothed = amp = 0.0f;
        gate = false;
        heldNotes.clear();
        currentFreq = targetFreq = midiToFreq (45.0f);
    }

    void setParams (Wave w, float tuningSemis, float cutoffHz, float res,
                    float envModAmt, float decayMs, float accentAmt, float volDb,
                    float vibSpeedHz, float vibDepthSemis)
    {
        wave = w;
        tuning = tuningSemis;
        cutoff = cutoffHz;
        resonance = res;
        envMod = envModAmt;
        decaySeconds = decayMs * 0.001f;
        accent = accentAmt;
        gain = std::pow (10.0f, volDb / 20.0f);
        vibSpeed = vibSpeedHz;
        vibDepth = vibDepthSemis;

        if (! heldNotes.empty())
            targetFreq = midiToFreq ((float) heldNotes.back() + tuning);
    }

    // dyn is a Sequencer303::Dyn: below zero soft, zero normal, above accented.
    void noteOn (int note, int dyn, bool slide)
    {
        const bool accented = dyn > 0;
        const bool soft     = dyn < 0;

        heldNotes.push_back (note);
        targetFreq = midiToFreq ((float) note + tuning);

        if (! slide)
        {
            currentFreq = targetFreq;
            // A soft step starts its filter envelope short of the top, so the
            // note opens up less — quieter and duller together, the way a
            // lightly played note behaves. Level is applied separately below,
            // since env only drives the filter here.
            env = soft ? softEnv : 1.0f;
            noteLevel = soft ? softLevel : 1.0f;
            // Restart the vibrato with each new note. 303 notes are short (often
            // less than one LFO cycle), so a free-running LFO would give each note
            // an arbitrary slice of the waveform — audible as random detuning
            // rather than vibrato. Slid notes leave it running, like the glide.
            vibPhase = 0.0;
            // Accented notes use a fixed, snappy filter decay regardless of
            // the decay knob — a defining 303 quirk.
            const float d = accented ? std::min (decaySeconds, 0.2f) : decaySeconds;
            decayCoef = std::exp (-1.0f / (d * (float) sampleRate));
            accDecayCoef = std::exp (-1.0f / (0.2f * (float) sampleRate));
            if (accented)
                accEnv = 1.0f;
        }

        gate = true;
    }

    void noteOff (int note)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note),
                         heldNotes.end());

        if (heldNotes.empty())
            gate = false;
        else
            targetFreq = midiToFreq ((float) heldNotes.back() + tuning);
    }

    void allNotesOff()
    {
        heldNotes.clear();
        gate = false;
    }

    bool hasHeldNotes() const { return ! heldNotes.empty(); }

    void render (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = renderSample();
    }

private:
    static float midiToFreq (float note)
    {
        return 440.0f * std::exp2 ((note - 69.0f) / 12.0f);
    }

    float onePoleCoef (double seconds) const
    {
        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    static float stage (float x, float& s, float G)
    {
        const float v = (x - s) * G;
        const float y = v + s;
        s = y + v;
        return y;
    }

    float renderSample()
    {
        // Pitch glide (slide)
        currentFreq += (targetFreq - currentFreq) * glideCoef;

        // Envelopes
        env *= decayCoef;
        accEnv *= accDecayCoef;
        // The accent "wow" comes from the accent envelope being smeared in
        // time before it hits the filter.
        accEnvSmoothed += (accEnv - accEnvSmoothed) * accSmoothCoef;

        const float ampTarget = gate ? 1.0f : 0.0f;
        amp += (ampTarget - amp) * (gate ? attackCoef : releaseCoef);

        // Oscillator. A sine LFO adds pitch vibrato on top of the glided pitch;
        // it modulates the playback frequency only (not the glide state), so it
        // never accumulates.
        vibPhase += vibSpeed / sampleRate;
        if (vibPhase >= 1.0)
            vibPhase -= 1.0;
        const float vibSemis = vibDepth * std::sin ((float) (2.0 * 3.14159265358979 * vibPhase));
        const double freq = currentFreq * std::exp2 (vibSemis / 12.0f);
        const double dt = freq / sampleRate;
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;

        const float t = (float) phase;
        const float fdt = (float) dt;
        float osc;
        if (wave == Wave::Saw)
        {
            osc = 2.0f * t - 1.0f - polyBlep (t, fdt);
        }
        else
        {
            // A square is a 50% pulse; PULSE just narrows it. Narrowing costs
            // fundamental — a pulse's fundamental follows sin(pi * width) — and
            // brings in the even harmonics a square has none of. That trade is
            // the thinner, reedier tone, and it hands the ladder a denser set of
            // partials to sweep through.
            const float width = wave == Wave::Pulse ? pulseWidth : 0.5f;

            osc = (t < width ? 1.0f : -1.0f);
            osc += polyBlep (t, fdt);
            osc -= polyBlep (std::fmod (t + 1.0f - width, 1.0f), fdt);

            // Any width but 50% leaves the wave sitting off centre by its own
            // mean. Left in, the amp envelope would sweep that DC into a click at
            // every note edge and it would push the ladder's tanh permanently to
            // one side, so it comes straight back off. At 50% this is a no-op.
            osc -= 2.0f * width - 1.0f;
        }

        // Filter cutoff: base cutoff pushed up by the filter envelope and the
        // (smoothed) accent envelope, in octaves.
        const float envOctaves = envMod * 5.0f * env
                               + accent * 2.5f * accEnvSmoothed;
        float fc = cutoff * std::exp2 (envOctaves);
        fc = std::clamp (fc, 20.0f, (float) (sampleRate * 0.45));

        // Zero-delay-feedback 4-pole ladder
        const float g = std::tan (3.14159265f * fc / (float) sampleRate);
        const float G = g / (1.0f + g);
        const float k = resonance * 4.2f;

        const float S = (G * G * G * s1 + G * G * s2 + G * s3 + s4) / (1.0f + g);
        float u = (osc - k * S) / (1.0f + k * G * G * G * G);
        u = std::tanh (u);
        float y = stage (stage (stage (stage (u, s1, G), s2, G), s3, G), s4, G);
        y *= 1.0f + 0.5f * k;   // keep loudness roughly constant as resonance rises

        // Accent also punches the volume
        y *= 1.0f + accent * accEnv;

        const float out = y * amp * gain * noteLevel * 0.5f;

        // The ladder's tanh is an odd function, but a pulse is not symmetric
        // about zero, so saturating one leaves DC behind even though the
        // oscillator itself is centred (a 25% pulse picks up about -0.12). The
        // amp envelope would sweep that into a thump at every note edge, and it
        // eats headroom the rest of the time, so it comes off here — after the
        // filter, which is where it appears.
        dcY1 = out - dcX1 + dcCoef * dcY1;
        dcX1 = out;
        return dcY1;
    }

    static float polyBlep (float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    // Narrow enough to read as clearly apart from the square, wide enough that
    // the fundamental doesn't thin away to nothing (a 25% pulse keeps about
    // -3 dB of it).
    static constexpr float pulseWidth = 0.25f;

    double sampleRate = 44100.0;
    float dcCoef = 0.9993f;      // DC blocker on the voice output
    float dcX1 = 0.0f, dcY1 = 0.0f;

    Wave  wave = Wave::Saw;
    float tuning = 0.0f;
    float cutoff = 500.0f;
    float resonance = 0.5f;
    float envMod = 0.5f;
    float decaySeconds = 0.3f;
    float accent = 0.5f;
    float gain = 1.0f;

    // How far a soft step sits under a normal one. Fixed rather than scaled by
    // the ACCENT knob: that knob sets how hard an accent hits, and a player
    // reaching for it doesn't expect their ghost notes to move too. Measured on
    // one note, these put a soft step ~4 dB under a normal one and ~7 dB under
    // an accent, with the filter opening less on the way (see dyn_test).
    static constexpr float softLevel = 0.6f;   // amplitude
    static constexpr float softEnv   = 0.5f;   // filter envelope at note-on
    float noteLevel = 1.0f;                    // per-note level, set at note-on
    float vibSpeed = 5.0f;   // Hz
    float vibDepth = 0.0f;   // semitones (0 = vibrato off)

    double phase = 0.0;
    double vibPhase = 0.0;
    float currentFreq = 110.0f, targetFreq = 110.0f;
    float glideCoef = 0.001f, attackCoef = 0.01f, releaseCoef = 0.005f, accSmoothCoef = 0.002f;
    float decayCoef = 0.9999f, accDecayCoef = 0.9999f;
    float env = 0.0f, accEnv = 0.0f, accEnvSmoothed = 0.0f, amp = 0.0f;
    float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    bool  gate = false;

    std::vector<int> heldNotes;
};
