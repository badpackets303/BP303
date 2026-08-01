// Offline test for DrumMachine + DrumSequencer.
// Renders each voice in both kits (checking per-voice signal), then a full
// groove (303 + drums) to groove_test.wav.
// Build: clang++ -std=c++17 -O2 Tools/drum_test.cpp -o drum_test

#include "../Source/DrumMachine.h"
#include "../Source/DrumSequencer.h"
#include "../Source/DspUtil.h"
#include "../Source/Sequencer303.h"
#include "../Source/Synth303.h"

#include <cstdio>
#include <fstream>
#include <vector>

static void writeWav (const char* path, const std::vector<float>& samples, int sr)
{
    std::ofstream f (path, std::ios::binary);
    auto w32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto w16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t dataBytes = (uint32_t) samples.size() * 2;
    f.write ("RIFF", 4); w32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); w32 (16); w16 (1); w16 (1);
    w32 ((uint32_t) sr); w32 ((uint32_t) sr * 2); w16 (2); w16 (16);
    f.write ("data", 4); w32 (dataBytes);
    for (float s : samples)
        w16 ((uint16_t) (int16_t) (std::clamp (s, -1.0f, 1.0f) * 32767.0f));
}

int main()
{
    const int sr = 44100;
    const float levels[5] = { 0.9f, 0.8f, 0.7f, 0.6f, 0.6f };
    int failures = 0;

    // --- per-voice sanity, both kits ---
    const char* voiceNames[] = { "BD", "SD", "CP", "CH", "OH" };
    const DrumMachine::Kit kits[] = { DrumMachine::Kit::K808, DrumMachine::Kit::K909,
                                      DrumMachine::Kit::K606 };
    const char* kitNames[] = { "808", "909", "606" };
    for (int kit = 0; kit < 3; ++kit)
    {
        for (int v = 0; v < DrumMachine::numVoices; ++v)
        {
            DrumMachine dm;
            dm.prepare (sr);
            dm.setParams (kits[kit], 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, levels, 0.0f);
            std::vector<float> buf (sr, 0.0f);   // 1 second
            dm.trigger (v, false);
            dm.render (buf.data(), (int) buf.size());

            float peak = 0.0f;
            int nans = 0;
            float tailPeak = 0.0f;   // last 100 ms — should have decayed
            for (size_t i = 0; i < buf.size(); ++i)
            {
                if (! std::isfinite (buf[i])) ++nans;
                peak = std::max (peak, std::abs (buf[i]));
                if (i > buf.size() - 4410)
                    tailPeak = std::max (tailPeak, std::abs (buf[i]));
            }
            const bool ok = nans == 0 && peak > 0.02f && peak < 1.6f
                         && tailPeak < peak * 0.2f;
            std::printf ("%s %s: peak=%.3f tail=%.4f nans=%d %s\n",
                         kitNames[kit], voiceNames[v],
                         peak, tailPeak, nans, ok ? "ok" : "FAIL");
            if (! ok) ++failures;
        }
    }

    // --- full groove: 303 + drums, 4 bars at 128 ---
    const double bpm = 128.0;
    Synth303 synth;         synth.prepare (sr);
    Sequencer303 seq;       seq.prepare (sr);
    DrumMachine dm;         dm.prepare (sr);
    DrumSequencer dseq;     dseq.prepare (sr);
    synth.setParams (Synth303::Wave::Saw, 0, 400, 0.85f, 0.7f, 250, 0.8f, 0, 5.0f, 0.0f);
    dm.setParams (DrumMachine::Kit::K909, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, levels, 0.0f);

    std::vector<SeqEvent> ev;    ev.reserve (64);
    std::vector<DrumEvent> dev;  dev.reserve (64);
    const int blockSize = 512;
    std::vector<float> block (blockSize);
    std::vector<float> out;
    double ppq = 0.0;
    const double ppqPerBlock = bpm / 60.0 * blockSize / sr;
    const int totalBlocks = (int) (sr * 7.5 / blockSize);
    int drumHits = 0;

    for (int b = 0; b < totalBlocks; ++b)
    {
        std::fill (block.begin(), block.end(), 0.0f);

        seq.process (blockSize, bpm, true, true, ppq, 0.25f, ev);
        int pos = 0;
        for (const auto& e : ev)
        {
            synth.render (block.data() + pos, e.offset - pos);
            pos = e.offset;
            if (e.noteOn) synth.noteOn (e.note, e.dyn, e.slide);
            else          synth.noteOff (e.note);
        }
        synth.render (block.data() + pos, blockSize - pos);

        dseq.process (blockSize, bpm, true, true, ppq, 0.25f, 16, dev);
        drumHits += (int) dev.size();
        int dpos = 0;
        for (const auto& e : dev)
        {
            dm.render (block.data() + dpos, e.offset - dpos);
            dpos = e.offset;
            dm.trigger (e.voice, e.dyn);
        }
        dm.render (block.data() + dpos, blockSize - dpos);

        dsp303::softClipBlock (block.data(), blockSize);
        ppq += ppqPerBlock;
        out.insert (out.end(), block.begin(), block.end());
    }

    float peak = 0.0f;
    int nans = 0;
    for (float v : out)
    {
        if (! std::isfinite (v)) ++nans;
        peak = std::max (peak, std::abs (v));
    }
    writeWav ("groove_test.wav", out, sr);
    std::printf ("groove: drumHits=%d peak=%.3f nans=%d\n", drumHits, peak, nans);
    // default pattern = 11 hits/bar × 4 bars
    if (! (drumHits == 44 && nans == 0 && peak > 0.1f && peak <= 1.0f))
        ++failures;

    std::printf (failures == 0 ? "DRUM-TEST OK\n" : "DRUM-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
