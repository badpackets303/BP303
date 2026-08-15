// Renders the editor once per EQ tab with a curve dialled in and the band
// meters live, so the faders and the meters can be checked without opening a
// host. Writes eqtab_<name>.png. Development aid; not part of the plugin, and
// the counterpart to fxtab_snapshot and drumtab_snapshot.
//
// The meters only move if audio has actually run, and they only run at all
// while an editor exists — so the editor is built first, then a second or so of
// a pattern is rendered through it, and only then is the snapshot taken.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <iterator>

namespace
{
    FxSection* findSection (juce::Component& c, const juce::String& title)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* fx = dynamic_cast<FxSection*> (child))
                if (fx->sectionTitle() == title)
                    return fx;
            if (auto* found = findSection (*child, title))
                return found;
        }
        return nullptr;
    }

    // As in drumtab_snapshot: tabBarArea() is private but its geometry is fixed,
    // 18px of title above an 18px bar of tabs running from the left.
    void clickTab (FxSection& fx, int tab, int numTabs)
    {
        const int segW = juce::jmin (150, fx.getWidth() / numTabs);
        const juce::Point<float> p { (float) (8 + tab * segW + segW / 2), 27.0f };
        fx.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(),
                        p, juce::ModifierKeys::leftButtonModifier,
                        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &fx, &fx,
                        juce::Time::getCurrentTime(), p,
                        juce::Time::getCurrentTime(), 1, false });
    }

    void setParam (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    BP303AudioProcessor proc;
    proc.prepareToPlay (sampleRate, blockSize);

    // Something playing on both lines, so both sets of meters have signal.
    setParam (proc, "playmode", 1.0f);      // Seq
    setParam (proc, "run", 1.0f);
    for (int s = 0; s < 16; ++s)
        proc.sequencer.steps[(size_t) s].gate.store (true);
    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        proc.drumSequencer.stepMask[lane].store (0xffff);

    // A smile and a scoop, so the two tabs are visibly different and boost and
    // cut both get drawn.
    setParam (proc, "beqon", 1.0f);
    setParam (proc, "deqon", 1.0f);
    const float bassCurve[GraphicEq::numBands] = {
        -6.0f, 9.0f, 6.0f, 0.0f, -4.0f, -6.0f, -2.0f, 3.0f, 5.0f, 2.0f
    };
    const float drumCurve[GraphicEq::numBands] = {
        2.0f, 7.0f, 0.0f, -5.0f, -8.0f, -3.0f, 1.0f, 6.0f, 9.0f, 4.0f
    };
    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        setParam (proc, BP303AudioProcessor::eqBandIds (0)[b], bassCurve[b]);
        setParam (proc, BP303AudioProcessor::eqBandIds (1)[b], drumCurve[b]);
    }

    static const char* names[] = { "eqtab_bass.png", "eqtab_drums.png" };

    for (int tab = 0; tab < (int) std::size (names); ++tab)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto* eq = findSection (*editor, "EQ");
        if (eq == nullptr)
        {
            std::printf ("no EQ section found\n");
            return 1;
        }

        // The editor exists now, so metering is on. Render enough audio for the
        // followers to settle before asking the panel to draw itself.
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            for (int i = 0; i < 90; ++i)      // ~1 second
            {
                buffer.clear();
                proc.processBlock (buffer, midi);
            }
        }

        clickTab (*eq, tab, (int) std::size (names));
        auto image = editor->createComponentSnapshot (editor->getLocalBounds());

        auto file = juce::File::getCurrentWorkingDirectory().getChildFile (names[tab]);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (! stream.openedOk())
            return 1;

        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, stream))
            return 1;
        std::printf ("written %s\n", names[tab]);
    }
    return 0;
}
