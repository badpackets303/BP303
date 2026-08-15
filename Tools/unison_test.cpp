// Offline tests for UNISON on the bass voice.
//
// Four things have to hold, and the first two are what let this ship without
// disturbing anything that already exists:
//
//   1. One voice is the 303. Not close to it — the same samples the single
//      oscillator produced before unison was written, on every wave.
//   2. VOICES is a width control, not a gain control. Stacking oscillators must
//      not push the ladder's tanh harder, or the knob quietly changes the timbre.
//   3. SPREAD opens a real image, and one that survives a fold to mono. These
//      are separate detuned oscillators rather than a phase trick on one signal,
//      so summing has to keep the level — the failure a Haas or mid-side
//      widener would show here, and the reason neither is used.
//   4. Detune actually beats: the sound of unison is the amplitude moving, so a
//      detuned stack must vary where a single oscillator holds steady.
//
// Build: clang++ -std=c++17 -O2 Tools/unison_test.cpp -o unison_test

#include "../Source/Synth303.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr int sr = 44100;

    struct Render { std::vector<float> l, r; };

    // Holds one note long enough for the beating to develop.
    Render play (int voices, float detune, float spread,
                 Synth303::Wave wave = Synth303::Wave::Saw, int note = 45)
    {
        const int len = sr;
        Render out { std::vector<float> ((size_t) len, 0.0f),
                     std::vector<float> ((size_t) len, 0.0f) };

        Synth303 s;
        s.prepare (sr);
        s.setParams (wave, 0.0f, 800.0f, 0.6f, 0.5f, 900.0f, 0.5f, 0.0f, 5.0f, 0.0f);
        s.setUnison (voices, detune, spread);
        s.noteOn (note, 0, false);
        s.render (out.l.data(), out.r.data(), len);
        return out;
    }

    double rms (const std::vector<float>& v, size_t from = 0)
    {
        double s = 0.0;
        for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
        return std::sqrt (s / (double) (v.size() - from));
    }

    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }

    int countNans (const std::vector<float>& v)
    {
        int n = 0;
        for (float x : v) if (! std::isfinite (x)) ++n;
        return n;
    }

    // How much the envelope moves over the note, as a fraction of its mean.
    // A single oscillator on a steady note barely moves; detuned copies beat.
    double envelopeSwing (const std::vector<float>& v)
    {
        const size_t win = (size_t) sr / 50;        // 20 ms
        double lo = 1.0e9, hi = 0.0, sum = 0.0;
        int blocks = 0;

        // skip the first tenth: the filter envelope is still falling there
        for (size_t i = v.size() / 10; i + win < v.size(); i += win)
        {
            double e = 0.0;
            for (size_t j = i; j < i + win; ++j)
                e += (double) v[j] * v[j];
            e = std::sqrt (e / (double) win);
            lo = std::min (lo, e);
            hi = std::max (hi, e);
            sum += e;
            ++blocks;
        }

        const double mean = sum / (double) blocks;
        return mean > 0.0 ? (hi - lo) / mean : 0.0;
    }

    const char* waveName (Synth303::Wave w)
    {
        return w == Synth303::Wave::Saw ? "Saw"
             : w == Synth303::Wave::Square ? "Square" : "Pulse";
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- 1. One voice is the plain 303, on every wave -----------------------
    for (auto wave : { Synth303::Wave::Saw, Synth303::Wave::Square, Synth303::Wave::Pulse })
    {
        // A voice that was never told about unison at all, against one explicitly
        // set to a single voice with detune and spread dialled up. With one
        // oscillator there is nothing to detune and nowhere to spread it, so the
        // two have to agree exactly, and both channels with them.
        const int len = sr;
        std::vector<float> bare ((size_t) len, 0.0f);
        Synth303 s;
        s.prepare (sr);
        s.setParams (wave, 0.0f, 800.0f, 0.6f, 0.5f, 900.0f, 0.5f, 0.0f, 5.0f, 0.0f);
        s.noteOn (45, 0, false);
        s.render (bare.data(), len);

        const auto one = play (1, 25.0f, 1.0f, wave);

        char msg[96];
        std::snprintf (msg, sizeof msg, "%s: 1 voice differs from no unison", waveName (wave));
        check (identical (bare, one.l) && identical (bare, one.r), msg);
    }

    // ---- 2. VOICES holds its level -----------------------------------------
    {
        const double one = rms (play (1, 8.0f, 0.6f).l);
        std::printf ("level by voice count (1 = %.4f):", one);

        for (int v = 2; v <= Synth303::maxUnison; ++v)
        {
            const auto out = play (v, 8.0f, 0.6f);
            // fold to mono, so this measures the level of the whole line rather
            // than of whichever side the voices happened to land on
            std::vector<float> folded (out.l.size());
            for (size_t i = 0; i < out.l.size(); ++i)
                folded[i] = (out.l[i] + out.r[i]) * 0.5f;

            const double db = 20.0 * std::log10 (rms (folded) / one);
            std::printf (" %d=%+.2fdB", v, db);

            char msg[96];
            std::snprintf (msg, sizeof msg, "%d voices moved the level more than 3 dB", v);
            check (std::abs (db) < 3.0, msg);
            std::snprintf (msg, sizeof msg, "%d voices produced non-finite samples", v);
            check (countNans (out.l) == 0 && countNans (out.r) == 0, msg);
        }
        std::printf ("\n");
    }

    // ---- 3. SPREAD opens an image that survives a fold to mono --------------
    {
        const auto narrow = play (5, 8.0f, 0.0f);
        check (identical (narrow.l, narrow.r), "spread 0 did not leave the channels identical");

        const auto wide = play (5, 8.0f, 1.0f);
        check (! identical (wide.l, wide.r), "spread 1 left the channels identical");

        // SPREAD is a width control, so it must not move the level on the way
        // out. Constant-power panning is what buys this: the stereo level is
        // what it was centred, whatever the voices are doing across the image.
        const double narrowPower = std::hypot (rms (narrow.l), rms (narrow.r));
        const double widePower   = std::hypot (rms (wide.l), rms (wide.r));
        std::printf ("stereo level: centred=%.4f spread=%.4f (%.2f dB)\n",
                     narrowPower, widePower, 20.0 * std::log10 (widePower / narrowPower));
        check (std::abs (20.0 * std::log10 (widePower / narrowPower)) < 1.0,
               "spread changed the stereo level");

        // And the fold to mono must not null anything. Constant-power panning
        // costs a hard-panned source 3 dB when the sides are summed — that is
        // the price of the line above and it is a known, bounded loss. What is
        // being ruled out here is the unbounded one: a Haas or mid-side widener
        // cancels rather than attenuating, and takes the note with it.
        std::vector<float> folded (wide.l.size());
        for (size_t i = 0; i < wide.l.size(); ++i)
            folded[i] = (wide.l[i] + wide.r[i]) * 0.5f;

        const double foldedRms = rms (folded), narrowRms = rms (narrow.l);
        std::printf ("bass fold-down: centred=%.4f folded=%.4f (%.2f dB)\n",
                     narrowRms, foldedRms, 20.0 * std::log10 (foldedRms / narrowRms));
        check (foldedRms > narrowRms * 0.707, "spread cancelled when summed to mono");
    }

    // ---- 4. Detune beats ----------------------------------------------------
    {
        const double steady = envelopeSwing (play (1, 0.0f, 0.0f).l);
        const double beating = envelopeSwing (play (5, 12.0f, 0.0f).l);
        std::printf ("envelope swing: 1 voice=%.3f  5 voices detuned=%.3f\n",
                     steady, beating);
        check (beating > steady * 2.0, "detuned unison did not beat");

        // and with the detune closed the stack should settle back down
        const double flat = envelopeSwing (play (5, 0.0f, 0.0f).l);
        check (flat < beating, "zero detune beat as much as a detuned stack");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
