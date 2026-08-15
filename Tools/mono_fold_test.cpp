// Offline test that a mono host buffer gets the whole instrument folded down
// rather than just its left channel.
//
// The plugin reports both a stereo and a mono output layout, and everything that
// makes width — the ping-pong delay, the drum spread, the bass unison — lives on
// a channel pair. For a long time the right channel of a mono instance was
// written into a scratch vector and thrown away, which cost the ping-pong delay
// every second echo and would now cost half of both lines. This drives the real
// processor at both channel counts and checks the mono one is the sum of the
// stereo one, not the left of it.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

#include <cmath>
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
    constexpr double sr = 44100.0;

    void setParam (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // Renders the sequencer for a while and returns each channel.
    std::vector<std::vector<float>> render (int numChannels, bool wide)
    {
        BP303AudioProcessor proc;
        proc.setPlayConfigDetails (0, numChannels, sr, blockSize);
        proc.prepareToPlay (sr, blockSize);

        // Everything that can produce width, turned on together.
        setParam (proc, "run", 1.0f);
        setParam (proc, "drumspread", wide ? 1.0f : 0.0f);
        setParam (proc, "unisonvoices", wide ? 7.0f : 1.0f);
        setParam (proc, "unisondetune", 12.0f);
        setParam (proc, "unisonspread", wide ? 1.0f : 0.0f);
        setParam (proc, "delayon", 1.0f);
        setParam (proc, "delaytype", wide ? 1.0f : 0.0f);   // STEREO = ping-pong

        std::vector<std::vector<float>> out ((size_t) numChannels);
        juce::AudioBuffer<float> buf (numChannels, blockSize);

        for (int b = 0; b < 120; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            for (int c = 0; c < numChannels; ++c)
                out[(size_t) c].insert (out[(size_t) c].end(),
                                        buf.getReadPointer (c),
                                        buf.getReadPointer (c) + blockSize);
        }
        return out;
    }

    double rms (const std::vector<float>& v)
    {
        double s = 0.0;
        for (float x : v) s += (double) x * x;
        return std::sqrt (s / (double) v.size());
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
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- with nothing widening, mono is the left channel unchanged ----------
    {
        const auto stereo = render (2, false);
        const auto mono   = render (1, false);

        check (identical (stereo[0], stereo[1]),
               "narrow: the two channels are identical");
        check (identical (stereo[0], mono[0]),
               "narrow: mono output matches the stereo left channel");
    }

    // ---- with everything widening, mono is the fold, not the left -----------
    {
        const auto stereo = render (2, true);
        const auto mono   = render (1, true);

        check (! identical (stereo[0], stereo[1]),
               "wide: the two channels actually differ");

        std::vector<float> folded (stereo[0].size());
        for (size_t i = 0; i < folded.size(); ++i)
            folded[i] = (stereo[0][i] + stereo[1][i]) * 0.5f;

        // The limiter runs after the fold and is fed a different signal in the
        // two cases, so this is a level comparison rather than a sample one.
        const double foldRms = rms (folded), monoRms = rms (mono[0]);
        const double leftRms = rms (stereo[0]);
        std::printf ("wide: left=%.5f folded=%.5f mono-out=%.5f\n",
                     leftRms, foldRms, monoRms);

        check (std::abs (20.0 * std::log10 (monoRms / foldRms)) < 0.5,
               "wide: mono output is the fold-down of the pair");

        // and the thing this is really guarding: the right channel is not simply
        // being dropped on the floor
        const double dropped = std::abs (20.0 * std::log10 (monoRms / leftRms));
        check (dropped > 0.05 || std::abs (leftRms - foldRms) < 1.0e-9,
               "wide: mono output looks like the discarded-right-channel case");
    }

    if (failures == 0)
        std::printf ("MONO-FOLD-TEST OK\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
