// Offline render test for the Synth303 engine.
// Renders a short acid pattern to a WAV file and prints signal stats.
// Build: clang++ -std=c++17 -O2 Tools/render_test.cpp -o render_test

#include "../Source/Sequencer303.h"   // for the Dyn values noteOn takes
#include "../Source/Synth303.h"

#include <cstdint>
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
    {
        const float c = std::clamp (s, -1.0f, 1.0f);
        w16 ((uint16_t) (int16_t) (c * 32767.0f));
    }
}

int main()
{
    const int sr = 44100;
    const double bpm = 130.0;
    const int stepLen = (int) (sr * 60.0 / bpm / 4.0);   // 16th notes
    const int gateLen = (int) (stepLen * 0.55);

    Synth303 synth;
    synth.prepare (sr);
    synth.setParams (Synth303::Wave::Saw,
                     0.0f,     // tuning
                     400.0f,   // cutoff
                     0.85f,    // resonance
                     0.7f,     // env mod
                     250.0f,   // decay ms
                     0.8f,     // accent
                     0.0f,     // volume dB
                     5.0f,     // vib speed Hz
                     0.0f);    // vib depth semis

    // note, accent, slide (-1 = rest)
    struct Step { int note; bool accent; bool slide; };
    const Step pattern[16] = {
        { 33, true,  false }, { 33, false, false }, { 45, false, true  }, { -1, false, false },
        { 33, false, false }, { 36, true,  false }, { -1, false, false }, { 33, false, false },
        { 40, false, true  }, { 38, false, true  }, { 33, false, false }, { -1, false, false },
        { 45, true,  false }, { 33, false, false }, { 31, false, true  }, { 33, false, false },
    };

    std::vector<float> out;
    out.reserve ((size_t) stepLen * 32);
    std::vector<float> block (256);

    int prevNote = -1;
    for (int loop = 0; loop < 2; ++loop)
    {
        for (int s = 0; s < 16; ++s)
        {
            const auto& st = pattern[s];
            int rendered = 0;
            bool noteOnSent = false;

            if (st.note >= 0)
            {
                synth.noteOn (st.note, st.accent ? dyn303::Hard : dyn303::Normal,
                              st.slide && prevNote >= 0);
                noteOnSent = true;
            }
            if (prevNote >= 0 && ! (st.note >= 0 && st.slide))
            {
                // previous note's gate already closed at gateLen (handled below)
            }

            while (rendered < stepLen)
            {
                const int n = std::min ((int) block.size(), stepLen - rendered);
                // close the gate partway through the step unless sliding into next
                if (noteOnSent && rendered >= gateLen)
                {
                    const bool nextSlides = pattern[(s + 1) % 16].slide
                                         && pattern[(s + 1) % 16].note >= 0;
                    if (! nextSlides)
                    {
                        synth.noteOff (st.note);
                        noteOnSent = false;
                    }
                }
                synth.render (block.data(), n);
                out.insert (out.end(), block.begin(), block.begin() + n);
                rendered += n;
            }

            if (noteOnSent)
                synth.noteOff (st.note);
            prevNote = st.note;
        }
    }

    // stats
    float peak = 0.0f;
    double sumSq = 0.0;
    int nans = 0;
    for (float v : out)
    {
        if (! std::isfinite (v)) ++nans;
        peak = std::max (peak, std::abs (v));
        sumSq += (double) v * v;
    }
    const double rms = std::sqrt (sumSq / (double) out.size());

    writeWav ("render_test.wav", out, sr);
    std::printf ("samples=%zu peak=%.3f rms=%.4f nans=%d\n", out.size(), peak, rms, nans);
    return (nans == 0 && peak > 0.01f && peak < 4.0f) ? 0 : 1;
}
