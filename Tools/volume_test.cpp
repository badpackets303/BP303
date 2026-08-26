// Offline test that the bass VOLUME is a level control, not a drive into the
// distortion.
//
// It used to be applied inside the voice, ahead of the whole FX chain, so it set
// the level going into the fuzz. The fuzz is a fixed-threshold hard clipper, and
// a clipper sustains whatever crosses its threshold — so turning VOLUME down
// pulled the decaying tail of a note below the threshold sooner and the note got
// *shorter* as well as quieter. That is a drive control wearing a volume label,
// and DRIVE already exists for driving the shaper.
//
// The fix moves VOLUME to the end of the bass line, after the FX. The property
// that proves it worked: with the fuzz driven hard, the whole bass line at one
// volume is the line at another volume scaled by the ratio of the two, sample
// for sample. A linear scaling cannot change where a note crosses a threshold,
// so it cannot change the note's length — which is the whole complaint. On the
// old code this failed, because the clipper's output was not a linear function
// of its input level.
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
        else { std::printf ("missing parameter %s: FAIL\n", id); ++failures; }
    }

    // A note struck, held a while, then released, through a hard-driven fuzz on
    // the bass line — the exact patch the complaint was about. Drums off, so the
    // captured buffer is the bass line alone.
    std::vector<float> render (BP303AudioProcessor& proc, float volDb)
    {
        proc.prepareToPlay (44100.0, blockSize);
        setP (proc, "playmode", 0.0f);   // Ext: play incoming notes
        setP (proc, "drumson", 0.0f);
        setP (proc, "basson", 1.0f);
        setP (proc, "diston", 1.0f);
        setP (proc, "bdisttype", 1.0f);  // FUZZ
        setP (proc, "distdrive", 0.95f); // hard into the clipper
        setP (proc, "decay", 300.0f);    // a tail long enough to matter
        setP (proc, "volume", volDb);

        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);
        for (int b = 0; b < 40; ++b)     // ~460 ms: strike, hold, release
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)  midi.addEvent (juce::MidiMessage::noteOn  (1, 45, (juce::uint8) 110), 0);
            if (b == 24) midi.addEvent (juce::MidiMessage::noteOff (1, 45), 0);
            proc.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0),
                        buf.getReadPointer (0) + blockSize);
        }
        return out;
    }

    int soundingSamples (const std::vector<float>& x, float floorLevel)
    {
        int n = 0;
        for (float v : x) if (std::abs (v) > floorLevel) ++n;
        return n;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto full = render (*std::make_unique<BP303AudioProcessor>(), 0.0f);

    // --- VOLUME is a linear trim on the finished line ------------------------
    // At -12 dB every sample must be the 0 dB sample times the gain, within a
    // hair. If VOLUME still drove the fuzz, the quiet render would be a
    // *different shape*, not a scaled one.
    {
        BP303AudioProcessor proc;
        const auto quiet = render (proc, -12.0f);
        const float gain = juce::Decibels::decibelsToGain (-12.0f);

        double worst = 0.0;
        for (size_t i = 0; i < full.size(); ++i)
            worst = std::max (worst, std::abs ((double) quiet[i] - full[i] * gain));

        check (worst < 1.0e-4,
               "VOLUME scales the fuzzed bass line linearly, it does not reshape it");
    }

    // --- ...so the note is the same length, only quieter ---------------------
    // The complaint, in its own terms. Count how long the line stays above a
    // floor set *relative to each render's own level*, so the comparison is of
    // duration and not of loudness. They must match.
    {
        BP303AudioProcessor proc;
        const auto quiet = render (proc, -12.0f);
        const float gain = juce::Decibels::decibelsToGain (-12.0f);

        const int lenFull  = soundingSamples (full,  0.02f);
        const int lenQuiet = soundingSamples (quiet, 0.02f * gain);

        check (std::abs (lenFull - lenQuiet) <= blockSize,
               "the note lasts as long quiet as it does loud");
    }

    std::printf (failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
