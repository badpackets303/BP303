#pragma once

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include "DspUtil.h"

// Monophonic 303-style voice: PolyBLEP saw/pulse oscillator into a
// zero-delay-feedback 4-pole ladder filter, with the classic accent and
// slide behaviour. Deliberately JUCE-free so it can be unit-tested standalone.
//
// UNISON runs several of those oscillators at once, detuned symmetrically about
// the played pitch and placed across the image in the same order, so the voices
// furthest out of tune are also furthest apart. It is the only thing on the bass
// line that produces width: with one voice the two channels are identical and
// nothing downstream separates them.
//
// The width is genuinely two different signals rather than one signal phase-
// tricked into sounding wide, which is what makes it safe on a bass — summed
// back to mono the detuned copies beat against each other exactly as they did
// in stereo instead of partially cancelling. That is worth stating because the
// cheap alternatives (Haas, mid-side) do cancel, and on a bass line played out
// on a system that sums the low end that is the whole sound gone.
//
// Each channel gets its own ladder rather than one filter on a summed signal.
// Two ladders fed slightly different mixes land their resonant peaks fractionally
// apart, and that difference is most of what makes unison sound big rather than
// merely doubled.
class Synth303
{
public:
    // Order matters: the wave parameter stores this index, so new waves go on
    // the end or every saved project changes what it plays.
    enum class Wave { Saw, Square, Pulse };
    static constexpr int numWaves = 3;

    // Seven is where adding voices stops changing the sound much and starts
    // just costing oscillators.
    static constexpr int maxUnison = 7;

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
        for (auto& l : lad)
            l = {};
        // Scattered across the cycle rather than all starting together: stacked
        // oscillators leaving zero in step sum coherently into one loud edge
        // before the detuning pulls them apart.
        //
        // Scattered, though, and specifically *not* evenly spaced. N saws at even
        // k/N spacing sum to a saw at N times the frequency with 1/N the
        // amplitude — the fundamental cancels almost exactly, and measured on the
        // real voice that cost nearly 5 dB at seven voices, turning VOICES into a
        // volume control pointing the wrong way. These offsets come off a hash
        // instead, which is uneven enough to have no such identity and still the
        // same every run, so a render stays reproducible. Voice 0 lands on zero,
        // where the single oscillator has always started.
        for (int v = 0; v < maxUnison; ++v)
        {
            uint32_t h = (uint32_t) v * 2654435761u;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            uniPhase[v] = (double) h / 4294967296.0;
        }
        vibPhase = 0.0;
        env = accEnv = accEnvSmoothed = amp = 0.0f;
        envRising = false;
        gate = false;
        heldNotes.clear();
        currentFreq = targetFreq = midiToFreq (45.0f);
    }

    // attackMs defaults to zero — the 303 has no attack stage at all, and that
    // instant snap is what every existing caller (and every saved project) is
    // asking for. Only the plugin passes anything else.
    void setParams (Wave w, float tuningSemis, float cutoffHz, float res,
                    float envModAmt, float decayMs, float accentAmt, float volDb,
                    float vibSpeedHz, float vibDepthSemis, float attackMs = 0.0f)
    {
        wave = w;
        tuning = tuningSemis;
        cutoff = cutoffHz;
        resonance = res;
        envMod = envModAmt;
        decaySeconds = decayMs * 0.001f;
        attackSeconds = attackMs * 0.001f;
        accent = accentAmt;
        gain = std::pow (10.0f, volDb / 20.0f);
        vibSpeed = vibSpeedHz;
        vibDepth = vibDepthSemis;

        if (! heldNotes.empty())
            targetFreq = midiToFreq ((float) heldNotes.back() + tuning);
    }

    // voices:      1 is the plain 303 — one oscillator, no width, and the output
    //              both channels carry is what this voice has always produced.
    // detuneCents: half-width of the spread, so the outermost pair sits this far
    //              either side of the played pitch.
    // spread01:    how far across the image that same spread is placed.
    void setUnison (int voices, float detuneCents, float spread01)
    {
        const int n = std::clamp (voices, 1, maxUnison);
        const float cents = std::max (0.0f, detuneCents);
        const float spread = std::clamp (spread01, 0.0f, 1.0f);

        for (int v = 0; v < n; ++v)
        {
            // -1 .. +1 across the stack; the lone voice of a 1-voice unison sits
            // dead centre, which is what keeps it bit-identical to no unison.
            const float t = n == 1 ? 0.0f
                                   : 2.0f * (float) v / (float) (n - 1) - 1.0f;

            uniRatio[v] = std::exp2 (t * cents / 1200.0f);
            dsp303::panGains (t * spread, uniL[v], uniR[v]);
        }

        // Detuned voices are uncorrelated within a few milliseconds, so their
        // power adds rather than their amplitude: 1/sqrt(n) holds the level as
        // voices come in. Anything else and VOICES would double as a gain knob,
        // and on this voice that means driving the ladder's tanh harder — a
        // timbre change dressed up as a width change.
        uniNorm = 1.0f / std::sqrt ((float) n);
        unisonVoices = n;
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
            envPeak = soft ? softEnv : 1.0f;
            noteLevel = soft ? softLevel : 1.0f;

            // ATTACK is an addition, not a 303 behaviour: the original jumps the
            // filter envelope straight to the top. Accents keep that jump — the
            // snap is the whole point of an accent, and sweeping into one reads
            // as a mistake rather than as emphasis. A soft step rises from zero
            // to its own lower peak, so it stays the lighter-struck note.
            if (attackSeconds > 0.0f && ! accented)
            {
                env = 0.0f;
                envRising = true;
                // Linear rather than the one-pole used elsewhere in this file: a
                // one-pole only reaches 63% in its time constant, so a dial set
                // to 100 ms would take most of a second to actually open. A ramp
                // makes the number on the knob the time you hear.
                envRise = envPeak / std::max (1.0f, attackSeconds * (float) sampleRate);
            }
            else
            {
                env = envPeak;
                envRising = false;
            }
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
        // Pop the *newest* matching note rather than every copy of it. The
        // sequencer fires a step's note-on before releasing the step before it,
        // so the gate never drops and the voice glides; when both name the same
        // number — a pattern wrapping from a note straight back onto itself —
        // erasing every copy took the note that had just started with it, and
        // the line went silent until some other pitch came round.
        const auto last = std::find (heldNotes.rbegin(), heldNotes.rend(), note);
        if (last != heldNotes.rend())
            heldNotes.erase (std::next (last).base());

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

    // The mono render is the fold-down of the pair, not a separate path: with
    // one voice (or SPREAD at zero) the channels are identical, and l + l halved
    // is exactly l, so this stays sample-for-sample what it always was.
    void render (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float l, r;
            renderSample (l, r);
            out[i] = (l + r) * 0.5f;
        }
    }

    void render (float* l, float* r, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            renderSample (l[i], r[i]);
    }

private:
    // One channel's filter: the four ladder integrators and the DC blocker that
    // follows them.
    struct Ladder
    {
        float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
        float dcX1 = 0.0f, dcY1 = 0.0f;
    };

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

    void renderSample (float& outL, float& outR)
    {
        // Pitch glide (slide)
        currentFreq += (targetFreq - currentFreq) * glideCoef;

        // Envelopes. The filter envelope only starts falling once it has run its
        // attack ramp, so ATTACK and DECAY are the two stages of one AD rather
        // than two things fighting over the same value.
        if (envRising)
        {
            env += envRise;
            if (env >= envPeak)
            {
                env = envPeak;
                envRising = false;
            }
        }
        else
        {
            env *= decayCoef;
        }
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

        // The unison bank. Every voice shares the glide, the vibrato and both
        // envelopes — this is one 303 voice played by several oscillators, not
        // several voices — so only the frequency and the position differ.
        float oscL = 0.0f, oscR = 0.0f;
        for (int v = 0; v < unisonVoices; ++v)
        {
            const double dt = freq * uniRatio[v] / sampleRate;
            uniPhase[v] += dt;
            if (uniPhase[v] >= 1.0)
                uniPhase[v] -= 1.0;

            const float o = oscillator ((float) uniPhase[v], (float) dt);
            oscL += o * uniL[v];
            oscR += o * uniR[v];
        }
        oscL *= uniNorm;
        oscR *= uniNorm;

        // Filter cutoff: base cutoff pushed up by the filter envelope and the
        // (smoothed) accent envelope, in octaves. Shared by both ladders — the
        // envelopes belong to the note, not to a side of the image.
        const float envOctaves = envMod * 5.0f * env
                               + accent * 2.5f * accEnvSmoothed;
        float fc = cutoff * std::exp2 (envOctaves);
        fc = std::clamp (fc, 20.0f, (float) (sampleRate * 0.45));

        // Zero-delay-feedback 4-pole ladder
        const float g = std::tan (3.14159265f * fc / (float) sampleRate);
        const float G = g / (1.0f + g);
        const float k = resonance * 4.2f;

        outL = ladder (lad[0], oscL, g, G, k);
        outR = ladder (lad[1], oscR, g, G, k);
    }

    // The oscillator, at one phase and one phase increment.
    float oscillator (float t, float fdt) const
    {
        if (wave == Wave::Saw)
            return 2.0f * t - 1.0f - polyBlep (t, fdt);

        // A square is a 50% pulse; PULSE just narrows it. Narrowing costs
        // fundamental — a pulse's fundamental follows sin(pi * width) — and
        // brings in the even harmonics a square has none of. That trade is
        // the thinner, reedier tone, and it hands the ladder a denser set of
        // partials to sweep through.
        const float width = wave == Wave::Pulse ? pulseWidth : 0.5f;

        float osc = (t < width ? 1.0f : -1.0f);
        osc += polyBlep (t, fdt);
        osc -= polyBlep (std::fmod (t + 1.0f - width, 1.0f), fdt);

        // Any width but 50% leaves the wave sitting off centre by its own
        // mean. Left in, the amp envelope would sweep that DC into a click at
        // every note edge and it would push the ladder's tanh permanently to
        // one side, so it comes straight back off. At 50% this is a no-op.
        osc -= 2.0f * width - 1.0f;
        return osc;
    }

    // One channel's ladder, and everything after it. The state is per channel;
    // every coefficient and envelope reaching it is shared.
    float ladder (Ladder& l, float osc, float g, float G, float k)
    {
        const float S = (G * G * G * l.s1 + G * G * l.s2 + G * l.s3 + l.s4) / (1.0f + g);
        float u = (osc - k * S) / (1.0f + k * G * G * G * G);
        u = std::tanh (u);
        float y = stage (stage (stage (stage (u, l.s1, G), l.s2, G), l.s3, G), l.s4, G);
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
        l.dcY1 = out - l.dcX1 + dcCoef * l.dcY1;
        l.dcX1 = out;
        return l.dcY1;
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
    float dcCoef = 0.9993f;      // DC blocker on the voice output, per channel

    Wave  wave = Wave::Saw;
    float tuning = 0.0f;
    float cutoff = 500.0f;
    float resonance = 0.5f;
    float envMod = 0.5f;
    float decaySeconds = 0.3f, attackSeconds = 0.0f;
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

    // Unison. One oscillator, no detune and dead centre until setUnison says
    // otherwise, so an untouched voice is the 303 it always was.
    int    unisonVoices = 1;
    float  uniNorm = 1.0f;
    double uniPhase[maxUnison] = {};
    float  uniRatio[maxUnison] = { 1.0f };            // frequency multiplier
    float  uniL[maxUnison] = { 1.0f }, uniR[maxUnison] = { 1.0f };

    double vibPhase = 0.0;
    float currentFreq = 110.0f, targetFreq = 110.0f;
    float glideCoef = 0.001f, attackCoef = 0.01f, releaseCoef = 0.005f, accSmoothCoef = 0.002f;
    float decayCoef = 0.9999f, accDecayCoef = 0.9999f;
    float env = 0.0f, accEnv = 0.0f, accEnvSmoothed = 0.0f, amp = 0.0f;
    float envPeak = 1.0f, envRise = 0.0f;   // filter-envelope attack ramp
    bool  envRising = false;
    Ladder lad[2];
    bool  gate = false;

    std::vector<int> heldNotes;
};
