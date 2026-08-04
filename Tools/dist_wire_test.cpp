// Offline test that the distortion TYPE and its per-type controls are actually
// wired from the parameter tree through to the audio. dist_test covers the
// shapers themselves; this one drives the real processor, which is the only
// place the ids, the defaults and the routing can be checked together.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    int failures = 0;
    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    constexpr int blockSize = 512;

    // Plays one held note through the synth and returns the rendered output.
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

    double rms (const std::vector<float>& v)
    {
        double s = 0.0;
        for (float x : v) s += (double) x * x;
        return std::sqrt (s / (double) v.size());
    }

    bool allFinite (const std::vector<float>& v)
    {
        for (float x : v) if (! std::isfinite (x)) return false;
        return true;
    }

    double difference (const std::vector<float>& a, const std::vector<float>& b)
    {
        double s = 0.0;
        const size_t n = std::min (a.size(), b.size());
        for (size_t i = 0; i < n; ++i) s += std::abs ((double) a[i] - b[i]);
        return s / (double) n;
    }

    void setP (BP303AudioProcessor& proc, const char* id, float norm)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (norm);
        else
        {
            std::printf ("missing parameter %s: FAIL\n", id);
            ++failures;
        }
    }

    // The synth is stateful — oscillator phase and envelopes carry across a
    // render — so each comparison gets its own processor and every run starts
    // from exactly the same place.
    template <typename Configure>
    std::vector<float> renderFresh (Configure&& configure)
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);

        setP (proc, "playmode", 0.0f);   // Ext: the synth plays incoming notes
        setP (proc, "drumson", 0.0f);    // bass line only
        configure (proc);

        return render (proc, 8);
    }
}

int main()
{
    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, blockSize);

    // --- every new parameter exists under the id the editor and DSP expect ---
    for (const char* id : { "bdisttype", "bcrbits", "bcrrate", "bfoldamt", "bfoldsym",
                            "brectamt", "brecttone", "bdistlows",
                            "ddisttype", "dcrbits", "dcrrate", "dfoldamt", "dfoldsym",
                            "drectamt", "drecttone", "ddistlows" })
        if (proc.apvts.getParameter (id) == nullptr)
        {
            std::printf ("missing parameter %s: FAIL\n", id);
            ++failures;
        }
    check (failures == 0, "all distortion parameters exist");

    // --- defaults keep each line sounding as it always has ---
    check ((int) proc.apvts.getRawParameterValue ("bdisttype")->load() == Distortion::Soft,
           "bass defaults to the SOFT overdrive it always used");
    check ((int) proc.apvts.getRawParameterValue ("ddisttype")->load() == Distortion::Fuzz,
           "drums default to the industrial FUZZ they always used");
    check (proc.apvts.getRawParameterValue ("bdistlows")->load() == 0.0f
           && proc.apvts.getRawParameterValue ("ddistlows")->load() == 0.0f,
           "LOWS defaults to driving the full range");

    const float typeNorm[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    static const char* names[] = { "SOFT", "FUZZ", "CRUSH", "FOLD", "RECT" };

    // --- with the distortion off, the type must make no difference at all ---
    {
        auto bypassed = [&] (int type) {
            return renderFresh ([&] (BP303AudioProcessor& p) {
                setP (p, "diston", 0.0f);
                setP (p, "bdisttype", typeNorm[type]);
            });
        };
        check (difference (bypassed (Distortion::Soft), bypassed (Distortion::Crush)) == 0.0,
               "type does nothing while the distortion is bypassed");
    }

    // --- each type renders, and renders differently: the parameter is wired ---
    std::vector<std::vector<float>> rendered;
    int renderFailures = 0;
    for (int t = 0; t < Distortion::numTypes; ++t)
    {
        auto out = renderFresh ([&] (BP303AudioProcessor& p) {
            setP (p, "diston", 1.0f);
            setP (p, "bdisttype", typeNorm[t]);
            setP (p, "distdrive", 0.8f);
            setP (p, "bcrbits", 0.1f);     // coarse
            setP (p, "bfoldamt", 0.9f);
            setP (p, "brectamt", 1.0f);
        });
        std::printf ("  %-5s rms=%.4f\n", names[t], rms (out));

        if (! allFinite (out)) { ++renderFailures; std::printf ("  %s produced NaNs\n", names[t]); }
        if (rms (out) < 1.0e-4) { ++renderFailures; std::printf ("  %s rendered silence\n", names[t]); }
        rendered.push_back (std::move (out));
    }
    check (renderFailures == 0, "every type renders finite, audible output");

    bool allDistinct = true;
    for (size_t i = 0; i < rendered.size(); ++i)
        for (size_t j = i + 1; j < rendered.size(); ++j)
            if (difference (rendered[i], rendered[j]) < 1.0e-5)
            {
                allDistinct = false;
                std::printf ("  %s and %s render identically\n", names[i], names[j]);
            }
    check (allDistinct, "each type sounds different from the others");

    // --- the CRUSH knobs must both read as "up is cleaner" ---
    // RATE used to be the hold length in samples, so turning it up held each
    // sample longer and *lowered* the rate — backwards from its label, and
    // opposite to BITS sitting next to it. Both are now measured against the
    // bypassed signal: turning either knob up has to move toward it.
    {
        const auto clean = renderFresh ([&] (BP303AudioProcessor& p) {
            setP (p, "diston", 0.0f);
        });

        auto crushed = [&] (const char* knob, float norm) {
            return renderFresh ([&] (BP303AudioProcessor& p) {
                setP (p, "diston", 1.0f);
                setP (p, "bdisttype", typeNorm[Distortion::Crush]);
                // pin the other knob wide open so this one is what moves
                setP (p, std::strcmp (knob, "bcrrate") == 0 ? "bcrbits" : "bcrrate", 1.0f);
                setP (p, knob, norm);
            });
        };

        for (const char* knob : { "bcrrate", "bcrbits" })
        {
            const double up   = difference (crushed (knob, 1.0f), clean);
            const double down = difference (crushed (knob, 0.0f), clean);
            std::printf ("  %s: up=%.5f from clean, down=%.5f\n", knob, up, down);
            check (up < down, "turning this crush knob up must move toward clean");
        }
    }

    // --- LOWS is wired: keeping the lows changes what comes out ---
    {
        auto withLows = [&] (float lows) {
            return renderFresh ([&] (BP303AudioProcessor& p) {
                setP (p, "diston", 1.0f);
                setP (p, "bdisttype", typeNorm[Distortion::Fold]);   // mangles lows hard
                setP (p, "bfoldamt", 0.9f);
                setP (p, "bdistlows", lows);
            });
        };
        check (difference (withLows (0.0f), withLows (1.0f)) > 1.0e-4,
               "LOWS changes the rendered output");
    }

    std::printf (failures == 0 ? "DIST-WIRE-TEST OK\n" : "DIST-WIRE-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
