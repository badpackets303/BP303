// Offline test that the delay TYPE is wired from the parameter tree through to
// the plugin's two output channels. fx_test covers the delay lines themselves;
// this one drives the real processor, which is the only place the ids, the
// defaults and the stereo routing of the whole chain can be checked together.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

#include <cstdio>
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

    struct Rendered
    {
        std::vector<float> l, r;
    };

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

    // The synth and every delay line are stateful, so each comparison gets its
    // own processor and every run starts from exactly the same place. `drums`
    // picks which line is auditioned: the bass note, or a kick on channel 10.
    template <typename Configure>
    Rendered renderFresh (bool drums, Configure&& configure)
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);

        setP (proc, "playmode", 0.0f);   // Ext: the lines play incoming notes only
        setP (proc, "basson", drums ? 0.0f : 1.0f);
        setP (proc, "drumson", drums ? 1.0f : 0.0f);
        configure (proc);

        Rendered out;
        juce::AudioBuffer<float> buf (2, blockSize);

        // long enough for several repeats of the default 1/8 - 1/4 note delay
        for (int b = 0; b < 64; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (drums ? juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120)
                                     : juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0);

            proc.processBlock (buf, midi);
            out.l.insert (out.l.end(), buf.getReadPointer (0),
                          buf.getReadPointer (0) + blockSize);
            out.r.insert (out.r.end(), buf.getReadPointer (1),
                          buf.getReadPointer (1) + blockSize);
        }
        return out;
    }

    double rms (const std::vector<float>& v)
    {
        double s = 0.0;
        for (float x : v) s += (double) x * x;
        return std::sqrt (s / (double) v.size());
    }

    bool allFinite (const Rendered& x)
    {
        for (float v : x.l) if (! std::isfinite (v)) return false;
        for (float v : x.r) if (! std::isfinite (v)) return false;
        return true;
    }

    // mean |L - R|: 0 means a dead-centre mono image
    double width (const Rendered& x)
    {
        double s = 0.0;
        for (size_t i = 0; i < x.l.size(); ++i) s += std::abs ((double) x.l[i] - x.r[i]);
        return s / (double) x.l.size();
    }

    double difference (const std::vector<float>& a, const std::vector<float>& b)
    {
        double s = 0.0;
        const size_t n = std::min (a.size(), b.size());
        for (size_t i = 0; i < n; ++i) s += std::abs ((double) a[i] - b[i]);
        return s / (double) n;
    }

    // MONO = 0, STEREO = 1, over a two-choice parameter
    constexpr float mono = 0.0f, stereo = 1.0f;
}

int main()
{
    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, blockSize);

    // --- the parameters exist under the ids the editor and DSP expect ---
    for (const char* id : { "delaytype", "ddelaytype" })
        if (proc.apvts.getParameter (id) == nullptr)
        {
            std::printf ("missing parameter %s: FAIL\n", id);
            ++failures;
        }
    check (failures == 0, "both delay type parameters exist");

    // --- the default keeps every existing project sounding as it did ---
    check ((int) proc.apvts.getRawParameterValue ("delaytype")->load() == Fx303::Mono
           && (int) proc.apvts.getRawParameterValue ("ddelaytype")->load() == Fx303::Mono,
           "both lines default to the MONO delay they always had");

    struct Line { const char* label; bool drums; const char* on; const char* type;
                  const char* mix; const char* fb; const char* rev; };
    const Line lines[] = {
        { "bass",  false, "delayon",  "delaytype",  "delaymix",  "delayfb",  "brevon" },
        { "drums", true,  "ddelayon", "ddelaytype", "ddelaymix", "ddelayfb", "drevon" },
    };

    for (const auto& ln : lines)
    {
        std::printf ("\n--- %s line ---\n", ln.label);

        auto withDelay = [&] (float type, bool on) {
            return renderFresh (ln.drums, [&] (BP303AudioProcessor& p) {
                setP (p, ln.on, on ? 1.0f : 0.0f);
                setP (p, ln.type, type);
                setP (p, ln.mix, 0.7f);
                setP (p, ln.fb, 0.7f);
            });
        };

        const auto offMono   = withDelay (mono,   false);
        const auto offStereo = withDelay (stereo, false);
        const auto onMono    = withDelay (mono,   true);
        const auto onStereo  = withDelay (stereo, true);

        std::printf ("  rms  mono=%.4f stereo=%.4f\n", rms (onMono.l), rms (onStereo.l));
        std::printf ("  width off=%.6f mono=%.6f stereo=%.6f\n",
                     width (offMono), width (onMono), width (onStereo));

        check (allFinite (onMono) && allFinite (onStereo),
               "both types render finite output");
        check (rms (onStereo.l) > 1.0e-4 && rms (onStereo.r) > 1.0e-4,
               "STEREO puts audible signal on both channels");

        // With the delay bypassed the type is inert — nothing else in the chain
        // reads it, so switching it must change nothing at all.
        check (difference (offMono.l, offStereo.l) == 0.0
               && difference (offMono.r, offStereo.r) == 0.0,
               "type does nothing while the delay is bypassed");

        // MONO is the old behaviour: one line, dead centre, both channels equal.
        check (width (offMono) == 0.0, "a bypassed delay leaves the line centred");
        check (width (onMono) == 0.0, "MONO leaves the line centred");

        // ...and STEREO is the whole point: the two channels must differ.
        check (width (onStereo) > 1.0e-3, "STEREO drives the channels apart");
        check (difference (onMono.l, onStereo.l) > 1.0e-4,
               "STEREO changes what the left channel plays");
    }

    // --- the image survives the rest of the chain -----------------------------
    // Comp, chorus and reverb all sit after the delay. None of them may collapse
    // a ping-pong back to the middle, and none may widen a mono line on its own.
    {
        std::printf ("\n--- downstream chain ---\n");

        auto withTail = [&] (float type) {
            return renderFresh (false, [&] (BP303AudioProcessor& p) {
                setP (p, "delayon", 1.0f);
                setP (p, "delaytype", type);
                setP (p, "delaymix", 0.7f);
                setP (p, "delayfb", 0.7f);
                setP (p, "bcompon", 1.0f);    // all three downstream units on
                setP (p, "bchron", 1.0f);
                setP (p, "brevon", 1.0f);
            });
        };

        const auto tailMono   = withTail (mono);
        const auto tailStereo = withTail (stereo);
        std::printf ("  width mono=%.6f stereo=%.6f\n",
                     width (tailMono), width (tailStereo));

        check (allFinite (tailMono) && allFinite (tailStereo),
               "comp + chorus + reverb render finite output on both channels");
        check (width (tailMono) == 0.0,
               "comp, chorus and reverb add no width of their own");
        check (width (tailStereo) > 1.0e-3,
               "the ping-pong image survives comp, chorus and reverb");
    }

    // --- the type round-trips, and a project saved before it existed loads MONO -
    {
        std::printf ("\n--- state ---\n");

        BP303AudioProcessor saved;
        saved.prepareToPlay (44100.0, blockSize);
        setP (saved, "delaytype", stereo);
        setP (saved, "ddelaytype", stereo);

        juce::MemoryBlock state;
        saved.getStateInformation (state);

        {
            BP303AudioProcessor loaded;
            loaded.prepareToPlay (44100.0, blockSize);
            loaded.setStateInformation (state.getData(), (int) state.getSize());
            check ((int) loaded.apvts.getRawParameterValue ("delaytype")->load() == Fx303::Stereo
                   && (int) loaded.apvts.getRawParameterValue ("ddelaytype")->load() == Fx303::Stereo,
                   "STEREO survives a state round-trip");
        }

        // Strip the two ids back out to stand in for a project written by a build
        // that predates them — that state has to load, and load as it sounded.
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        int stripped = 0;
        if (xml != nullptr)
            if (auto* params = xml->getChildByName (saved.apvts.state.getType()))
            {
                // collect first: removing while iterating walks a freed node
                juce::Array<juce::XmlElement*> doomed;
                for (auto* p : params->getChildWithTagNameIterator ("PARAM"))
                {
                    const auto id = p->getStringAttribute ("id");
                    if (id == "delaytype" || id == "ddelaytype")
                        doomed.add (p);
                }
                for (auto* p : doomed)
                {
                    params->removeChildElement (p, true);
                    ++stripped;
                }
            }
        check (stripped == 2, "both ids were found in the saved state to strip");

        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);

        BP303AudioProcessor legacy;
        legacy.prepareToPlay (44100.0, blockSize);
        legacy.setStateInformation (old.getData(), (int) old.getSize());
        check ((int) legacy.apvts.getRawParameterValue ("delaytype")->load() == Fx303::Mono
               && (int) legacy.apvts.getRawParameterValue ("ddelaytype")->load() == Fx303::Mono,
               "a state without the ids loads as MONO");
    }

    std::printf (failures == 0 ? "\nSTEREO-DELAY-TEST OK\n"
                               : "\nSTEREO-DELAY-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
