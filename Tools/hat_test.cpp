// Headless, JUCE-free test for the hi-hat voicing.
//
// The 808 hat is six square oscillators, and the thing that decides whether that
// reads as a "tsss" or as pitched metal is where their energy ends up. It used to
// end up low: the oscillators ran at 4x, putting every fundamental in the 1-3.4
// kHz range, and a 12 dB/oct highpass could not remove them again — one partial
// at 3.4 kHz stood 35 dB above its neighbours and the hat clanged. The fix was
// four poles rather than two, at the real 808's frequencies. This pins the
// result, because the tempting simplifications (fewer poles, "tidier" round
// frequencies) both bring the clang straight back.
//
// Built as a console app target (see CMakeLists.txt).

#include "../Source/DrumMachine.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr float sr = 44100.0f;
    constexpr double pi = 3.14159265358979;

    int failures = 0;

    void check (bool ok, const char* msg, double value)
    {
        std::printf ("%s  %s (%.2f)\n", ok ? "ok  " : "FAIL", msg, value);
        if (! ok)
            ++failures;
    }

    std::vector<float> hit (DrumMachine::Kit kit, int voice)
    {
        const float levels[DrumMachine::numVoices] = { 1, 1, 1, 1, 1 };
        DrumMachine dm;
        dm.prepare (sr);
        dm.setParams (kit, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, levels, 0.0f);
        dm.trigger (voice, 0);

        std::vector<float> out (16384, 0.0f);
        dm.render (out.data(), (int) out.size());
        return out;
    }

    double magAt (const std::vector<float>& x, double freq)
    {
        double re = 0.0, im = 0.0;
        const double w = 2.0 * pi * freq / sr;
        for (size_t n = 0; n < x.size(); ++n)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * pi * (double) n / (double) x.size());
            re += x[n] * win * std::cos (w * (double) n);
            im -= x[n] * win * std::sin (w * (double) n);
        }
        return std::sqrt (re * re + im * im) / (double) x.size();
    }

    // Share of the hit's energy below 4 kHz — the band a ringing oscillator
    // fundamental lands in, and the one a hat should have almost nothing in.
    double lowShare (const std::vector<float>& x)
    {
        double lo = 0.0, total = 0.0;
        for (double f = 500; f < 20000; f *= 1.02)
        {
            const double e = magAt (x, f) * magAt (x, f);
            total += e;
            if (f < 4000) lo += e;
        }
        return 100.0 * lo / (total > 0 ? total : 1);
    }

    double rms (const std::vector<float>& x)
    {
        double s = 0.0;
        for (float v : x) s += (double) v * v;
        return std::sqrt (s / x.size());
    }
}

int main()
{
    // --- the metal kits put their energy up top, where a hat belongs ----------
    for (auto kit : { DrumMachine::Kit::K808, DrumMachine::Kit::K606 })
    {
        const char* name = kit == DrumMachine::Kit::K808 ? "808" : "606";
        for (int voice : { DrumMachine::CH, DrumMachine::OH })
        {
            const double low = lowShare (hit (kit, voice));
            std::printf ("  %s %s: ", name, voice == DrumMachine::CH ? "CH" : "OH");
            check (low < 3.0, "under 4 kHz is nearly empty, so the bank sizzles "
                              "rather than rings", low);
        }
    }

    // --- the 909 is noise and was never the problem, so it must not move ------
    // Its numbers are what the two-pole path has always produced. A change here
    // means the shared hat code stopped leaving the noise kit alone — most likely
    // by drawing from noise() a different number of times per sample, which
    // shifts the whole random stream.
    {
        const double chRms = rms (hit (DrumMachine::Kit::K909, DrumMachine::CH));
        const double ohRms = rms (hit (DrumMachine::Kit::K909, DrumMachine::OH));
        check (std::fabs (chRms - 0.062134) < 1.0e-5, "the 909 closed hat is untouched", chRms);
        check (std::fabs (ohRms - 0.192610) < 1.0e-5, "the 909 open hat is untouched", ohRms);
    }

    // --- the makeup gain keeps old patterns balanced --------------------------
    // Four poles throw away far more than two did. These are the levels the
    // voicing produced before it was changed, over this same buffer, which is
    // what a pattern anyone has already mixed was balanced against.
    {
        struct Expect { DrumMachine::Kit kit; int voice; const char* name; double was; };
        const Expect expected[] = {
            { DrumMachine::Kit::K808, DrumMachine::CH, "808 CH", 0.01739 },
            { DrumMachine::Kit::K808, DrumMachine::OH, "808 OH", 0.06294 },
            { DrumMachine::Kit::K606, DrumMachine::CH, "606 CH", 0.00800 },
            { DrumMachine::Kit::K606, DrumMachine::OH, "606 OH", 0.02998 },
        };

        for (const auto& e : expected)
        {
            const double now = rms (hit (e.kit, e.voice));
            const double dB = 20.0 * std::log10 (now / e.was);
            std::printf ("  %s: ", e.name);
            check (std::fabs (dB) < 1.0, "still sits where it did, within a dB", dB);
        }
    }

    std::printf (failures == 0 ? "\nHAT-TEST OK\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
