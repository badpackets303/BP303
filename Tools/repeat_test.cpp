// A pattern that lands back on the note it is already playing.
//
// The sequencer fires a step's note-on *before* releasing the step before it, so
// the voice's gate never drops and consecutive steps glide instead of
// retriggering. That ordering means the voice sees a note-on and a note-off for
// the same number at the same sample whenever a note is followed by another at
// the same pitch — a long note running to the end of a pattern and wrapping back
// onto its own first step, or a tie onto the same key. Releasing every copy of
// the number took the note that had just started with it and the line went
// silent, so the release has to pop one note, not all of them.
//
// Measured as level out of the real voice rather than as note-stack state: what
// went wrong was audible, and the gate is only one of the things that has to
// survive the re-fire.
//
// JUCE-free: Sequencer303 and Synth303 only.
// Build: clang++ -std=c++17 -O2 Tools/repeat_test.cpp -o repeat_test

#include "../Source/Sequencer303.h"
#include "../Source/Synth303.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok)
            ++failures;
    }

    constexpr double sr = 44100.0;
    constexpr double bpm = 120.0;
    constexpr int blockSize = 256;

    Synth303 makeVoice()
    {
        Synth303 s;
        s.prepare (sr);
        //         wave              tune  cutoff  res   envMod decay  accent vol   vib
        s.setParams (Synth303::Wave::Saw, 0.0f, 400.0f, 0.85f, 0.7f, 250.0f, 0.8f, 0.0f, 5.0f, 0.0f);
        return s;
    }

    float renderPeak (Synth303& synth, int numSamples)
    {
        std::vector<float> buf ((size_t) numSamples);
        synth.render (buf.data(), numSamples);
        float peak = 0.0f;
        for (float v : buf)
            peak = std::max (peak, std::fabs (v));
        return peak;
    }

    void clearPattern (Sequencer303& seq, int len)
    {
        for (auto& s : seq.steps)
        {
            s.gate.store (false);
            s.hold.store (1);
            s.slide.store (false);
            s.dyn.store (dyn303::Normal);
            s.key.store (0);
            s.octave.store (0);
        }
        seq.length.store (len);
    }

    // Play `cycles` passes of the pattern through the voice and return the peak
    // level of each one, so a cycle that falls silent shows up on its own.
    std::vector<float> peakPerCycle (Sequencer303& seq, int cycles)
    {
        Synth303 synth = makeVoice();
        std::vector<SeqEvent> events;
        std::vector<float> buf ((size_t) blockSize);
        std::vector<float> peaks;

        const int len = seq.length.load();
        const double samplesPerStep = sr * 15.0 / bpm;
        const int cycleSamples = (int) (samplesPerStep * len);

        double ppq = 0.0;
        int sample = 0, cycle = 0;
        float peak = 0.0f;

        for (int done = 0; done < cycleSamples * cycles; done += blockSize)
        {
            seq.process (blockSize, bpm, true, true, ppq, 0.0f, events);

            int pos = 0;
            for (const auto& e : events)
            {
                synth.render (buf.data() + pos, e.offset - pos);
                pos = e.offset;
                if (e.noteOn)
                    synth.noteOn (e.note, e.dyn, e.slide);
                else
                    synth.noteOff (e.note);
            }
            synth.render (buf.data() + pos, blockSize - pos);

            for (float v : buf)
                peak = std::max (peak, std::fabs (v));

            sample += blockSize;
            ppq += bpm / 60.0 * blockSize / sr;

            if (sample / cycleSamples != cycle)
            {
                cycle = sample / cycleSamples;
                peaks.push_back (peak);
                peak = 0.0f;
            }
        }
        return peaks;
    }

    void report (const char* what, const std::vector<float>& peaks)
    {
        std::printf ("      [%s:", what);
        for (float p : peaks)
            std::printf (" %.3f", p);
        std::printf ("]\n");
    }
}

int main()
{
    // --- the voice: a re-fire onto the note already sounding -------------------
    {
        Synth303 synth = makeVoice();
        synth.noteOn (36, dyn303::Normal, false);
        const float alone = renderPeak (synth, 4410);

        // Exactly what the sequencer emits when a step lands on the pitch that is
        // still down: the new note first, then the release of the old one.
        synth.noteOn (36, dyn303::Normal, false);
        synth.noteOff (36);
        const float again = renderPeak (synth, 4410);

        std::printf ("      [alone %.3f  re-fired %.3f]\n", alone, again);
        check (again > alone * 0.8f,
               "re-firing the note already sounding leaves it sounding");
        check (synth.hasHeldNotes(), "and leaves the voice holding it");

        // The other side of the same stack: one release per note-on, so the note
        // still ends when the pattern moves off it.
        synth.noteOff (36);
        check (! synth.hasHeldNotes(), "a second release lets it go");
    }

    // --- the pattern the bug was reported against ------------------------------
    // A four-step pattern whose one note is held across all four: its last step
    // is the note itself, so the wrap fires step 0's note over a copy of it that
    // is still down. It went silent from the second cycle on.
    {
        Sequencer303 seq;
        seq.prepare (sr);
        clearPattern (seq, 4);
        seq.steps[0].gate.store (true);
        seq.steps[0].hold.store (4);

        const auto peaks = peakPerCycle (seq, 6);
        report ("held note, wraps onto itself", peaks);

        bool everyCycleSounds = ! peaks.empty();
        for (float p : peaks)
            everyCycleSounds = everyCycleSounds && p > 0.05f;
        check (everyCycleSounds,
               "a held note that fills the pattern sounds on every cycle, not just the first");
    }

    // --- a tie onto the same key ----------------------------------------------
    // Slide holds the gate open deliberately, which is the other way the voice
    // gets a note-on and a note-off for one number at the same sample.
    {
        Sequencer303 seq;
        seq.prepare (sr);
        clearPattern (seq, 4);
        for (int i = 0; i < 4; ++i)
        {
            seq.steps[i].gate.store (true);
            seq.steps[i].key.store (5);      // every step the same pitch
            seq.steps[i].slide.store (true); // tied all the way round
        }

        const auto peaks = peakPerCycle (seq, 6);
        report ("one pitch, tied all the way round", peaks);

        bool everyCycleSounds = ! peaks.empty();
        for (float p : peaks)
            everyCycleSounds = everyCycleSounds && p > 0.05f;
        check (everyCycleSounds, "a pattern tied onto its own pitch keeps sounding");
    }

    // --- the ordering it was there for in the first place ----------------------
    // Stepping to a different pitch must still glide rather than retrigger: the
    // gate stays up across the change, which is what the on-before-off ordering
    // buys and what a narrower fix would have cost.
    {
        Synth303 synth = makeVoice();
        synth.noteOn (36, dyn303::Normal, false);
        renderPeak (synth, 2205);
        synth.noteOn (43, dyn303::Normal, true);
        synth.noteOff (36);
        check (synth.hasHeldNotes(),
               "stepping to a different pitch still keeps the gate up");
        check (renderPeak (synth, 4410) > 0.05f, "and still sounds");
    }

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
