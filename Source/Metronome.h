#pragma once

#include <cmath>

// Transport-synced click. Emits a short sine tick on every quarter-note beat,
// with an accented, higher-pitched tick on the downbeat (every 4th beat).
// JUCE-free for standalone testing.
class Metronome
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        decayCoef = std::exp (-1.0f / (0.018f * (float) sr));   // ~18 ms tick
        reset();
    }

    void reset() { env = 0.0f; phase = 0.0f; }

    // Adds clicks into out. basePhaseBeats is the beat position at the first
    // sample of the block. Does nothing (and stays silent) when disabled.
    void process (float* out, int n, double basePhaseBeats, double bpm, bool enabled)
    {
        process (out, nullptr, n, basePhaseBeats, bpm, enabled);
    }

    // Same click, added to both channels, so it stays dead centre whatever the
    // line's own effects are doing to the image.
    void process (float* l, float* r, int n, double basePhaseBeats, double bpm,
                  bool enabled)
    {
        if (! enabled)
        {
            env = 0.0f;
            return;
        }

        const double beatsPerSample = bpm / 60.0 / sampleRate;
        // next integer beat at or after the block start (handles a beat that
        // lands exactly on sample 0, e.g. the downbeat when transport starts)
        long k = (long) std::ceil (basePhaseBeats - 1.0e-9);
        double nextOffset = (k - basePhaseBeats) / beatsPerSample;

        for (int i = 0; i < n; ++i)
        {
            while (nextOffset <= (double) i + 1.0e-6 && nextOffset < (double) n)
            {
                if (k >= 0)
                    trigger (k);
                ++k;
                nextOffset = (k - basePhaseBeats) / beatsPerSample;
            }
            const float tick = renderTick();
            l[i] += tick;
            if (r != nullptr)
                r[i] += tick;
        }
    }

private:
    void trigger (long beatIndex)
    {
        env = 1.0f;
        phase = 0.0f;
        const bool downbeat = (beatIndex % 4) == 0;
        freq = downbeat ? 1600.0f : 1000.0f;
        amp  = downbeat ? 0.5f : 0.32f;
    }

    float renderTick()
    {
        if (env < 1.0e-4f)
            return 0.0f;
        phase += freq / (float) sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;
        const float s = std::sin (6.2831853f * phase) * env * amp;
        env *= decayCoef;
        return s;
    }

    double sampleRate = 44100.0;
    float env = 0.0f, phase = 0.0f, freq = 1000.0f, amp = 0.4f, decayCoef = 0.999f;
};
