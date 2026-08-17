// Headless checks for the skin system and its unlabelled picker:
//   * every skin index names and paints something, and no two look alike
//   * the picker's hotspot is really clickable — nothing is laid out over it
//   * the chosen skin survives a project save/load and the global preference
//
// The global preference is a real file in Application Support, so the test
// puts the user's value back before it exits.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    // How many pixels differ between two renders of the same layout.
    int pixelsDiffering (const juce::Image& a, const juce::Image& b)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return -1;

        int n = 0;
        for (int y = 0; y < a.getHeight(); y += 3)
            for (int x = 0; x < a.getWidth(); x += 3)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    ++n;
        return n;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const int userSkin = BP303AudioProcessor::loadGlobalSkin (ui303::defaultSkin);

    // --- names ------------------------------------------------------------
    juce::StringArray names;
    for (int s = 0; s < ui303::numSkins; ++s)
        names.add (ui303::skinName (s));

    check (! names.contains (""), "every skin has a name");
    names.removeDuplicates (false);
    check (names.size() == ui303::numSkins, "skin names are unique");
    check (juce::isPositiveAndBelow (ui303::defaultSkin, ui303::numSkins),
           "the default skin is in range");

    // --- each skin renders, and renders differently -----------------------
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, 512);

        std::vector<juce::Image> shots;
        for (int s = 0; s < ui303::numSkins; ++s)
        {
            proc.uiSkin.store (s);
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (editor->getWidth(), editor->getHeight());
            shots.push_back (editor->createComponentSnapshot (editor->getLocalBounds()));
        }

        for (int a = 0; a < ui303::numSkins; ++a)
            for (int b = a + 1; b < ui303::numSkins; ++b)
                check (pixelsDiffering (shots[(size_t) a], shots[(size_t) b]) > 1000,
                       juce::String (ui303::skinName (a)) + " and "
                           + ui303::skinName (b) + " render differently");
    }

    // --- the picker's hotspot is not covered by a control -----------------
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);   // hit-testing ignores an invisible component

        // Native size, so the scaled content maps 1:1 onto the editor and the
        // hotspot's design coordinates hit-test where they say. Not the resize
        // maximum — that is now whatever fits the display.
        const auto native = BP303AudioProcessorEditor::nativeSize();
        editor->setSize (native.x, native.y);

        const auto hotspot = ui303::skinMenuHotspot();
        bool allReachContent = true;
        for (auto pt : { hotspot.getTopLeft() + juce::Point<int> (2, 2),
                         hotspot.getCentre(),
                         hotspot.getBottomRight() - juce::Point<int> (2, 2) })
        {
            auto* hit = editor->getComponentAt (pt);
            // The scaled content is the editor's own child; anything deeper is a
            // control sitting on the legend, which would eat the right-click.
            if (hit == nullptr || hit->getParentComponent() != editor.get())
                allReachContent = false;
        }
        check (allReachContent, "right-clicking the header legend reaches the picker");
    }

    // --- the skin survives a project save / load --------------------------
    {
        BP303AudioProcessor source;
        source.uiSkin.store (ui303::numSkins - 1);
        juce::MemoryBlock state;
        source.getStateInformation (state);

        BP303AudioProcessor loaded;
        loaded.uiSkin.store (0);
        loaded.setStateInformation (state.getData(), (int) state.getSize());
        check (loaded.uiSkin.load() == ui303::numSkins - 1,
               "a project reopens on the skin it was saved with");
    }

    // --- and out-of-range state does not push it out of range -------------
    {
        juce::XmlElement root ("BP303State");
        root.setAttribute ("skin", 99);
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (root, state);

        BP303AudioProcessor loaded;
        loaded.setStateInformation (state.getData(), (int) state.getSize());
        check (juce::isPositiveAndBelow (loaded.uiSkin.load(), ui303::numSkins),
               "a state naming an unknown skin is clamped");
    }

    // --- the global preference is what a new instance opens on ------------
    {
        const int wanted = ui303::numSkins - 1;
        BP303AudioProcessor setter;
        setter.setSkinGlobally (wanted);
        check (BP303AudioProcessor::loadGlobalSkin (0) == wanted,
               "choosing a skin is remembered globally");

        BP303AudioProcessor fresh;
        check (fresh.uiSkin.load() == wanted, "a new instance opens on the last skin");
    }

    // --- the key-colour easter egg ----------------------------------------
    const int userHue = BP303AudioProcessor::loadGlobalKeyHue (0);

    {
        // Rotating the dial moves the accent but never the chassis.
        ui303::setKeyHue (0, true);
        const auto orange0 = ui303::palette (4).orange;
        const auto panel0  = ui303::palette (4).panel1;
        const auto cell0   = ui303::palette (4).cellOff;

        ui303::setKeyHue (4, true);
        check (ui303::palette (4).orange != orange0, "the key colour moves with the dial");
        check (std::abs (ui303::palette (4).orange.getBrightness()
                         - orange0.getBrightness()) < 0.05f,
               "rotating changes the hue, not how bright the accent is");
        check (ui303::palette (4).panel1 == panel0 && ui303::palette (4).cellOff == cell0,
               "the chassis stays the skin's own");

        ui303::setKeyHue (0, true);
        check (ui303::palette (4).orange == orange0, "stop 0 is the skin as designed");

        ui303::setKeyHue (ui303::hueStops + 3, true);
        const bool wrapsUp = ui303::keyHue() == 3;
        ui303::setKeyHue (-1, true);
        check (wrapsUp && ui303::keyHue() == ui303::hueStops - 1, "the dial wraps both ways");

        // The fade eases over several frames and lands exactly on the stop.
        ui303::setKeyHue (6, true);
        const auto snapped = ui303::palette (4).orange;
        ui303::setKeyHue (0, true);
        ui303::setKeyHue (6);   // animated this time
        int ticks = 0;
        while (ui303::advanceKeyHueFade() && ticks < 200)
            ++ticks;
        check (ticks > 3 && ticks < 200, "the change fades over several frames");
        check (ui303::palette (4).orange == snapped, "and lands exactly where a snap does");
    }

    // --- the gesture, through the real editor -----------------------------
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);

        ScaledContent* content = nullptr;
        for (int i = 0; i < editor->getNumChildComponents(); ++i)
            if (auto* c = dynamic_cast<ScaledContent*> (editor->getChildComponent (i)))
                content = c;
        check (content != nullptr, "the editor's scaled content is reachable");

        if (content != nullptr)
        {
            ui303::setKeyHue (0, true);
            BP303AudioProcessor::saveGlobalKeyHue (0);

            auto eventAt = [&] (juce::Point<int> pos, juce::Point<int> downPos, bool dragged)
            {
                return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(),
                                          pos.toFloat(), juce::ModifierKeys::leftButtonModifier,
                                          1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          content, content, juce::Time::getCurrentTime(),
                                          downPos.toFloat(), juce::Time::getCurrentTime(),
                                          1, dragged };
            };

            // press the legend, sweep three stops' worth to the right
            const auto spot = ui303::skinMenuHotspot().getCentre();
            const auto to = spot + juce::Point<int> (26 * 3 + 4, 0);
            content->mouseDown (eventAt (spot, spot, false));
            content->mouseDrag (eventAt (to, spot, true));
            content->mouseUp   (eventAt (to, spot, true));

            check (ui303::keyHue() == 3, "sweeping the legend dials three stops");
            check (BP303AudioProcessor::loadGlobalKeyHue (-1) == 3,
                   "and the choice is remembered globally");

            // shift+arrow nudges one stop at a time
            editor->keyPressed (juce::KeyPress (juce::KeyPress::leftKey,
                                                juce::ModifierKeys::shiftModifier, 0));
            check (ui303::keyHue() == 2, "shift+left steps the dial back");

            // a press anywhere else must not start the drag
            ui303::setKeyHue (0, true);
            const juce::Point<int> offSpot { 600, 400 };
            content->mouseDown (eventAt (offSpot, offSpot, false));
            content->mouseDrag (eventAt (offSpot + juce::Point<int> (200, 0), offSpot, true));
            check (ui303::keyHue() == 0, "dragging elsewhere leaves the dial alone");
        }
    }

    // --- one snapshot per quarter-turn, for eyeballing the recolour --------
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        proc.uiSkin.store (4);   // neon: the strongest accent, so shifts show best

        int written = 0;
        for (int stop : { 0, 3, 6, 9 })
        {
            // through the preference, not the dial: opening the editor restores
            // the saved stop (snapped), which is the path a real window takes
            BP303AudioProcessor::saveGlobalKeyHue (stop);
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            if (ui303::keyHue() != stop)
            {
                check (false, "a new editor opens on the saved key colour");
                break;
            }
            editor->setSize (editor->getWidth(), editor->getHeight());
            const auto shot = editor->createComponentSnapshot (editor->getLocalBounds());

            const auto out = juce::File::getCurrentWorkingDirectory()
                                 .getChildFile ("hue_" + juce::String (stop) + ".png");
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream());
                stream != nullptr && stream->openedOk())
            {
                stream->setPosition (0);
                stream->truncate();
                juce::PNGImageFormat png;
                if (png.writeImageToStream (shot, *stream))
                    ++written;
            }
        }
        check (written == 4, "wrote a snapshot for every quarter-turn");
    }

    // Leave the user's preferences as we found them.
    {
        BP303AudioProcessor restore;
        restore.setSkinGlobally (userSkin);
        BP303AudioProcessor::saveGlobalKeyHue (userHue);
        ui303::setKeyHue (userHue, true);
    }

    std::printf (failures == 0 ? "\nall skin checks passed\n"
                               : "\n%d skin check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
