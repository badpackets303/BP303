// Renders the editor once per XY pad mode with the pad held and pushed off
// centre, so the handle, the crosshairs and the axis readout can be checked
// without opening a host. Writes padtab_<mode>.png. Development aid; not part of
// the plugin, and the counterpart to fxtab_snapshot, drumtab_snapshot and
// eqtab_snapshot.
//
// It exists for the same reason eqtab_snapshot does: at rest the pad draws a
// handle in the middle of an empty square and nothing else, so the default
// snapshot shows none of the states worth looking at. The pad is therefore
// held, moved, and only then drawn.

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

    void setParam (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    XyPad* findPad (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* pad = dynamic_cast<XyPad*> (child))
                return pad;
            if (auto* found = findPad (*child))
                return found;
        }
        return nullptr;
    }

    juce::MouseEvent eventAt (juce::Component& c, juce::Point<float> p)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 p, juce::ModifierKeys::leftButtonModifier,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                 juce::Time::getCurrentTime(), p,
                 juce::Time::getCurrentTime(), 1, false };
    }

    // Writing the file is the same four lines every time.
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

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    // The tab follows `padmode`, so the mode is set on the parameter rather than
    // by clicking the bar — which also checks that the attachment moves the tab,
    // the path a preset load or a host automation lane takes.
    static const char* names[] = { "padtab_acid.png", "padtab_grit.png",
                                   "padtab_space.png", "padtab_kit.png" };

    // One position for all four, up and to the right: both axes off centre and
    // neither at an edge, so the readout shows two different numbers and the
    // handle is somewhere the crosshairs can be seen crossing.
    setParam (proc, "padon", 1.0f);
    setParam (proc, "padx", 0.62f);
    setParam (proc, "pady", 0.38f);

    for (int mode = 0; mode < (int) std::size (names); ++mode)
    {
        setParam (proc, "padmode", (float) mode);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        if (findSection (*editor, "PAD") == nullptr)
        {
            std::printf ("no PAD section found\n");
            return 1;
        }

        if (! writePng (*editor, names[mode]))
            return 1;
    }

    // --- a gesture actually in flight ---------------------------------------
    // The sparks are thrown by the pad's own 25 Hz timer off how far the handle
    // moved since the last tick, so they cannot be posed the way the four
    // snapshots above are: the drag has to be driven with the message loop
    // running between the moves. This is the only way to see them without a
    // host, and it exercises the whole mouse path on the way.
    {
        setParam (proc, "padmode", (float) macropad::Acid);
        setParam (proc, "padon", 0.0f);
        setParam (proc, "padx", 0.0f);
        setParam (proc, "pady", 0.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto* pad = findPad (*editor);
        if (pad == nullptr)
        {
            std::printf ("no XyPad found\n");
            return 1;
        }

        const auto square = pad->padArea();
        const auto from = square.getRelativePoint (0.12f, 0.80f).toFloat();
        const auto to   = square.getRelativePoint (0.78f, 0.24f).toFloat();

        pad->mouseDown (eventAt (*pad, from));

        // Eight steps at 40 ms, which is one timer tick each — enough travel a
        // tick to throw a full burst, and long enough for the earliest sparks to
        // have started fading by the time the last ones are spawned.
        constexpr int steps = 8;
        for (int i = 1; i <= steps; ++i)
        {
            const float t = (float) i / (float) steps;
            pad->mouseDrag (eventAt (*pad, from + (to - from) * t));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }

        if (! writePng (*editor, "padtab_drag.png"))
            return 1;

        pad->mouseUp (eventAt (*pad, to));
    }

    return 0;
}
