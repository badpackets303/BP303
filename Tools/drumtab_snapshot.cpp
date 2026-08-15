// Renders the editor once per DRUMS tab, so MIX / TUNE / DECAY / BALANCE can each be
// checked visually. Writes drumtab_<name>.png. Development aid; not part of the
// plugin, and the counterpart to fxtab_snapshot for the other tabbed panel.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <iterator>

namespace
{
    FxSection* findDrums (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* fx = dynamic_cast<FxSection*> (child))
                if (fx->sectionTitle() == "DRUMS")
                    return fx;
            if (auto* found = findDrums (*child))
                return found;
        }
        return nullptr;
    }

    // tabBarArea() is private, but its geometry is fixed: the bar sits under the
    // 18px title, and tabs run from the left at a capped width. numTabs has to
    // match what the section was built with — on a full-width section every
    // segment is capped at maxTabW and the count makes no difference, but the
    // narrow case divides by it.
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
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    static const char* names[] = { "drumtab_mix.png", "drumtab_tune.png",
                                   "drumtab_decay.png", "drumtab_balance.png" };

    for (int tab = 0; tab < (int) std::size (names); ++tab)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto* drums = findDrums (*editor);
        if (drums == nullptr)
        {
            std::printf ("no DRUMS section found\n");
            return 1;
        }

        clickTab (*drums, tab, (int) std::size (names));
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
