// Offline test for Sequencer303: event-stream invariants + a rendered WAV
// of the default pattern through the Synth303 engine.
// Build: clang++ -std=c++17 -O2 Tools/seq_test.cpp -o seq_test

#include "../Source/Sequencer303.h"
#include "../Source/Synth303.h"

#include <cstdio>

int main()
{
    const int sr = 44100;
    const int blockSize = 480;   // deliberately awkward size
    const double bpm = 130.0;

    Sequencer303 seq;
    seq.prepare (sr);

    Synth303 synth;
    synth.prepare (sr);
    synth.setParams (Synth303::Wave::Saw, 0.0f, 400.0f, 0.85f, 0.7f, 250.0f, 0.8f, 0.0f, 5.0f, 0.0f);

    std::vector<SeqEvent> events;
    events.reserve (128);
    std::vector<float> block (blockSize);
    std::vector<float> out;

    const int totalSamples = sr * 8;   // ~4 bars at 130
    int noteOns = 0, noteOffs = 0, slides = 0, accents = 0;
    int failures = 0;
    bool noteHeld = false;
    double ppq = 0.0;
    const double ppqPerBlock = bpm / 60.0 * blockSize / sr;

    for (int rendered = 0; rendered < totalSamples; rendered += blockSize)
    {
        // emulate a host: playing, ppq advancing
        seq.process (blockSize, bpm, true, true, ppq, 0.3f, events);
        ppq += ppqPerBlock;

        int lastOffset = 0;
        for (const auto& e : events)
        {
            if (e.offset < lastOffset || e.offset >= blockSize)
            {
                std::printf ("BAD OFFSET %d (last %d)\n", e.offset, lastOffset);
                ++failures;
            }
            lastOffset = e.offset;

            if (e.noteOn)
            {
                ++noteOns;
                if (e.slide) ++slides;
                if (e.dyn > 0) ++accents;
                if (e.slide && ! noteHeld)
                {
                    std::printf ("SLIDE WITHOUT HELD NOTE at on #%d\n", noteOns);
                    ++failures;
                }
                noteHeld = true;
            }
            else
            {
                ++noteOffs;
            }
        }
        // track whether anything is held across blocks (mono: ons/offs interleave)
        noteHeld = noteOns > noteOffs;

        int pos = 0;
        for (const auto& e : events)
        {
            synth.render (block.data() + pos, e.offset - pos);
            pos = e.offset;
            if (e.noteOn) synth.noteOn (e.note, e.dyn, e.slide);
            else          synth.noteOff (e.note);
        }
        synth.render (block.data() + pos, blockSize - pos);
        out.insert (out.end(), block.begin(), block.end());
    }

    // stop: sequencer must release any held note
    seq.process (blockSize, bpm, false, true, ppq, 0.3f, events);
    for (const auto& e : events)
        if (! e.noteOn) ++noteOffs;

    float peak = 0.0f;
    int nans = 0;
    for (float v : out)
    {
        if (! std::isfinite (v)) ++nans;
        peak = std::max (peak, std::abs (v));
    }

    // default pattern: 12 gated steps per 16, 4 with slide flag, 3 accented
    std::printf ("noteOns=%d noteOffs=%d slides=%d accents=%d peak=%.3f nans=%d failures=%d\n",
                 noteOns, noteOffs, slides, accents, peak, nans, failures);

    const bool ok = failures == 0 && nans == 0
                 && noteOns > 40 && noteOns == noteOffs
                 && slides > 0 && accents > 0
                 && peak > 0.05f && peak < 1.5f;
    std::printf (ok ? "SEQ-TEST OK\n" : "SEQ-TEST FAILED\n");
    return ok ? 0 : 1;
}
