// Renders the editor with LFO 1 running, so the parts of the row that only
// exist while it is doing something can be checked without opening a host.
// Writes lfo_<shape>.png plus lfo_rings.png. Development aid; not part of the
// plugin, and the counterpart to padtab_snapshot and eqtab_snapshot.
//
// It exists for the reason those two do: at rest the LFO row draws a greyed
// wave, no dot and no rings — none of the states worth looking at. The failure
// it is meant to catch is a shape drawn wrong (sample & hold in particular,
// which is steps rather than a curve, and the one most likely to come out as a
// smear) and a scope whose dot has drifted off the wave it is supposed to ride.
//
// The phase comes from `processBlock` publishing it, so a block has to be
// rendered before the dot has anywhere to be — the same reason eqtab_snapshot
// renders a second of audio before drawing its meters.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <iterator>

namespace
{
    void setParam (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
            std::printf ("missing parameter %s\n", id);
    }

    // Runs the LFO forward so `lfoPhaseNow` is somewhere other than zero — at
    // phase 0 every shape but the square sits at the centre line, which is the
    // one position that shows least.
    void runTo (BP303AudioProcessor& proc, int blocks)
    {
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0);
            proc.processBlock (buf, midi);
        }
    }

    bool writePng (juce::Component& editor, const char* name)
    {
        auto image = editor.createComponentSnapshot (editor.getLocalBounds());

        auto file = juce::File::getCurrentWorkingDirectory().getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (! stream.openedOk())
            return false;

        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, stream))
            return false;

        std::printf ("written %s\n", name);
        return true;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    static const char* names[] = { "lfo_sine.png", "lfo_tri.png", "lfo_saw.png",
                                   "lfo_square.png", "lfo_sh.png", "lfo_draw.png" };
    static_assert ((int) std::size (names) == lfo::numShapes,
                   "a shape was added without a snapshot to check it draws");

    for (int shape = 0; shape < lfo::numShapes; ++shape)
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, 512);

        // Free-running, because there is no host transport here and a synced LFO
        // would fall back to free anyway — this just makes the rate explicit.
        setParam (proc, "lfo1on", 1.0f);
        setParam (proc, "lfo1sync", 0.0f);
        setParam (proc, "lfo1rate", 2.0f);
        setParam (proc, "lfo1amt", 0.75f);
        setParam (proc, "lfo1dest", 0.0f);   // CUT OFF
        setParam (proc, "lfo1shape", (float) shape);

        // Pose the drawn shape with a shortened loop, so its snapshot shows the
        // end marker and the dimmed steps beyond it rather than the full table.
        if (shape == lfo::Custom)
            setParam (proc, "lfo1len", 11.0f);

        runTo (proc, 12);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        if (! writePng (*editor, names[shape]))
            return 1;
    }

    // --- the rings, on a destination that is visible without changing tab ----
    // CUT OFF is on the synth row and always on screen, so the ring round it is
    // the one that can be checked in a single frame. The editor asks the same
    // `lfo::Lfo::apply` the audio thread does, clamping included, so a ring that
    // disagrees with the sound is a bug this catches.
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, 512);

        setParam (proc, "lfo1on", 1.0f);
        setParam (proc, "lfo1sync", 0.0f);
        setParam (proc, "lfo1rate", 0.4f);     // slow, so the pose is not a blur
        setParam (proc, "lfo1amt", 0.9f);
        setParam (proc, "lfo1dest", 0.0f);

        // A quarter cycle in, which is a sine's peak — the offset is at its
        // largest, so the ring is at its most visible.
        runTo (proc, 27);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

        // The rings come off the scope's 25 Hz tick, so the message loop has to
        // run for one before there is anything to see. The constructor primes
        // them once as well, but only from the phase at that instant.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        if (! writePng (*editor, "lfo_rings.png"))
            return 1;
    }

    std::printf ("OK\n");
    return 0;
}
