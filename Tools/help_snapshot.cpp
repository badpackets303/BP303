// Renders every step of the help tour to PNG, so the text can be proofread and
// — the part arithmetic will not tell you — checked for fitting. Writes
// help_NN.png. Development aid; not part of the plugin, and the counterpart to
// the other snapshot tools.
//
// The callout grows to fit its body but is capped at the window height, so a
// step written a paragraph too long does not error: it silently loses its last
// lines behind the NEXT button. It also picks its own position — below the
// thing it points at, above it, or centred if neither fits — and a step can
// therefore end up covering the control it is describing. Neither shows up
// anywhere except by looking.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

namespace
{
    template <typename T>
    T* findChild (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* found = dynamic_cast<T*> (child))
                return found;
            if (auto* found = findChild<T> (*child))
                return found;
        }
        return nullptr;
    }

    juce::TextButton* findHelpButton (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (child))
                if (b->getButtonText() == "HELP")
                    return b;
            if (auto* found = findHelpButton (*child))
                return found;
        }
        return nullptr;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    auto* button = findHelpButton (*editor);
    auto* help   = findChild<HelpOverlay> (*editor);

    if (button == nullptr || help == nullptr)
    {
        std::printf ("no HELP button or overlay found\n");
        return 1;
    }

    // Called directly rather than through triggerClick, which would post a
    // message this tool never dispatches.
    if (button->onClick)
        button->onClick();

    // The tour has no public count, so it is walked until the overlay closes
    // itself on the last NEXT. The cap is only there to stop a bug in that
    // logic turning into an infinite loop of PNGs.
    for (int step = 0; step < 40; ++step)
    {
        if (! help->isVisible())
        {
            std::printf ("%d steps written\n", step);
            return 0;
        }

        auto image = editor->createComponentSnapshot (editor->getLocalBounds());

        const auto name = "help_" + juce::String (step + 1).paddedLeft ('0', 2) + ".png";
        auto file = juce::File::getCurrentWorkingDirectory().getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (! stream.openedOk())
            return 1;

        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, stream))
            return 1;
        std::printf ("written %s\n", name.toRawUTF8());

        help->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    }

    std::printf ("the tour did not end after 40 steps\n");
    return 1;
}
