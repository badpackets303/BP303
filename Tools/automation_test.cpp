// Offline test that every FX unit's controls are actually wired from the
// parameter tree through to the audio — the thing a host's automation lane is
// driving. dist_wire_test does this for the distortion's type selector; this
// one walks all six units on both lines plus the EQ.
//
// It exists because "I automated Bass Filter Cutoff and nothing happened" has
// two very different causes and they look identical from the host: a parameter
// that isn't plumbed through, or a parameter whose unit is switched off. Every
// FX unit here defaults to ACTIVE off, and a bypassed unit ignores its controls
// completely — so this pins both halves, that the knobs are inert with ACTIVE
// off and live with it on. If a future refactor drops a parameter on the floor,
// the second half fails; if someone "helpfully" makes a bypassed unit keep
// filtering, the first half does.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;
    constexpr int blockSize = 512;

    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    void setP (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
        {
            std::printf ("missing parameter %s: FAIL\n", id);
            ++failures;
        }
    }

    std::vector<float> render (BP303AudioProcessor& proc, int numBlocks)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);

        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0);
            proc.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0),
                        buf.getReadPointer (0) + blockSize);
        }
        return out;
    }

    // The synth is stateful, so every comparison gets a fresh processor and
    // starts from exactly the same place.
    template <typename Configure>
    std::vector<float> renderFresh (Configure&& configure)
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);
        setP (proc, "playmode", 0.0f);   // Ext: the synth plays incoming notes
        setP (proc, "drumson", 0.0f);    // bass line only
        configure (proc);

        // Long enough for the delay to have returned its first echo. At the
        // default 3/16 and 120 BPM that is ~16.5k samples, so a short render
        // makes FEEDBACK and MIX look unwired when they are simply not audible
        // yet — which is exactly the false alarm this test exists to avoid
        // raising.
        return render (proc, 64);
    }

    double difference (const std::vector<float>& a, const std::vector<float>& b)
    {
        double s = 0.0;
        const size_t n = std::min (a.size(), b.size());
        for (size_t i = 0; i < n; ++i)
            s += std::abs ((double) a[i] - (double) b[i]);
        return n > 0 ? s / (double) n : 0.0;
    }

    // Renders `id` at two settings, with the unit's ACTIVE switch held at
    // `active`, and says whether the two differ.
    bool movesAudio (const char* onId, bool active,
                     const char* id, float lo, float hi)
    {
        auto a = renderFresh ([&] (BP303AudioProcessor& p) {
            setP (p, onId, active ? 1.0f : 0.0f);
            setP (p, id, lo);
        });
        auto b = renderFresh ([&] (BP303AudioProcessor& p) {
            setP (p, onId, active ? 1.0f : 0.0f);
            setP (p, id, hi);
        });
        return difference (a, b) > 1.0e-7;
    }

    struct Control { const char* id; float lo, hi; };

    void unit (const char* label, const char* onId, std::vector<Control> controls)
    {
        bool allLive = true, allInert = true;

        for (const auto& c : controls)
        {
            if (! movesAudio (onId, true, c.id, c.lo, c.hi))
            {
                std::printf ("  %s does nothing even with %s on\n", c.id, onId);
                allLive = false;
            }
            if (movesAudio (onId, false, c.id, c.lo, c.hi))
            {
                std::printf ("  %s still changes the audio with %s off\n", c.id, onId);
                allInert = false;
            }
        }

        std::printf ("%-22s ", label);
        check (allLive && allInert, "controls live when active, inert when not");
    }
}

int main()
{
    // The EQ's bands are read as a group, so one band standing in for the ten
    // proves the wiring; eq_test covers what each one does to the signal.
    const char* eqBand = BP303AudioProcessor::eqBandIds (0)[5];   // bass 1 kHz

    unit ("bass filter", "bflton", { { "bfltcut", 200.0f, 8000.0f },
                                     { "bfltres", 0.0f, 1.0f },
                                     { "bfltenv", 0.0f, 1.0f } });
    unit ("bass delay",  "delayon", { { "delayfb", 0.1f, 0.8f },
                                      { "delaymix", 0.05f, 0.9f } });
    // Values have to sit inside each parameter's own range: convertTo0to1
    // clamps, so two out-of-range endpoints land on the same setting and the
    // control looks dead. THRESHOLD is in dB, not 0..1.
    unit ("bass comp",   "bcompon", { { "bcompthr", -40.0f, -3.0f },
                                      { "bcomprat", 1.5f, 12.0f },
                                      { "bcompmk", 0.0f, 12.0f } });
    unit ("bass chorus", "bchron", { { "bchrrate", 0.1f, 6.0f },
                                     { "bchrdepth", 0.05f, 1.0f },
                                     { "bchrmix", 0.05f, 1.0f } });
    unit ("bass reverb", "brevon", { { "brevsize", 0.1f, 1.0f },
                                     { "brevdamp", 0.0f, 1.0f },
                                     { "brevmix", 0.05f, 1.0f } });
    unit ("bass dist",   "diston", { { "distdrive", 0.1f, 1.0f },
                                     { "distcolor", 0.0f, 1.0f },
                                     { "bdistlows", 0.0f, 1.0f } });
    unit ("bass EQ",     "beqon", { { eqBand, -12.0f, 12.0f } });

    // The 303's own filter has no ACTIVE to hide behind — it is always live.
    // Worth pinning next to the above, because "Cut Off" (this one, the 303's)
    // and "Bass Filter Cutoff" (the per-line PCF on the FX row) are different
    // parameters, and the one that needs switching on first is not the one
    // anybody reaching for a filter sweep means.
    //
    // Driven as normalised positions, since these ranges differ wildly.
    for (const char* id : { "cutoff", "resonance", "envmod", "decay" })
    {
        auto viaNorm = [&] (float n) {
            return renderFresh ([&] (BP303AudioProcessor& p) {
                if (auto* prm = p.apvts.getParameter (id))
                    prm->setValueNotifyingHost (n);
            });
        };

        std::printf ("%-22s ", id);
        check (difference (viaNorm (0.15f), viaNorm (0.85f)) > 1.0e-7,
               "always live (no ACTIVE switch)");
    }

    // --- the XY pad ---------------------------------------------------------
    // The pad is the deliberate exception to everything above: it engages a
    // mode's units for the length of a gesture without writing their ACTIVE
    // parameters, which is what stops a pad driving REVERB MIX being silent
    // until the user has gone hunting for a switch. pad_test pins the mapping
    // arithmetic; this pins that the mapping reaches the audio at all.
    {
        // Held or not, `padmode` decides what the axes reach — so the pad is
        // swept in each mode with HOLD off first. Exactly zero difference, not
        // "small": the pad sits across the 303's own cutoff, and a pad parked
        // off-centre with HOLD clear must be as inert as one never touched.
        const char* modeNames[] = { "pad ACID", "pad GRIT", "pad SPACE" };

        for (int mode = 0; mode < 3; ++mode)
        {
            auto sweep = [&] (bool held, float axis) {
                return renderFresh ([&] (BP303AudioProcessor& p) {
                    setP (p, "padmode", (float) mode);
                    setP (p, "padon", held ? 1.0f : 0.0f);
                    setP (p, "padx", axis);
                    setP (p, "pady", axis);
                });
            };

            const bool inertUnheld = difference (sweep (false, -0.9f),
                                                 sweep (false, 0.9f)) == 0.0;
            const bool liveHeld    = difference (sweep (true, -0.9f),
                                                 sweep (true, 0.9f)) > 1.0e-7;

            if (! inertUnheld)
                std::printf ("  the axes changed the audio with PAD HOLD off\n");
            if (! liveHeld)
                std::printf ("  the axes did nothing with PAD HOLD on\n");

            std::printf ("%-22s ", modeNames[mode]);
            check (inertUnheld && liveHeld, "axes live when held, inert when not");
        }

        // SPACE reaches the delay and the reverb, both of which are off. The
        // gesture has to be audible anyway, and the switches have to still read
        // off when it is over — the pad borrows the units, it does not claim
        // them.
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);
        setP (proc, "playmode", 0.0f);
        setP (proc, "drumson", 0.0f);
        setP (proc, "padmode", 2.0f);   // SPACE
        setP (proc, "padon", 1.0f);
        setP (proc, "padx", 0.9f);
        setP (proc, "pady", 0.9f);
        render (proc, 8);

        const bool untouched =
            proc.apvts.getRawParameterValue ("delayon")->load() < 0.5f
            && proc.apvts.getRawParameterValue ("brevon")->load() < 0.5f;

        std::printf ("%-22s ", "pad borrows units");
        check (untouched, "engaged its units without writing their ACTIVE switches");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
