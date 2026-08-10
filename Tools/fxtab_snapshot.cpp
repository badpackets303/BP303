// Renders the editor once per FX tab, so every page in BASS FX / DRUM FX can be
// checked visually. Clicks the tab on both sections at the same index, then
// writes fxtab_<n>.png. Development aid; not part of the plugin.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

namespace
{
    void collect (juce::Component& c, std::vector<FxSection*>& out)
    {
        for (auto* child : c.getChildren())
        {
            // DRUMS is an FxSection as well now; this tool means the FX ones.
            if (auto* fx = dynamic_cast<FxSection*> (child))
                if (fx->sectionTitle().contains ("FX"))
                    out.push_back (fx);
            collect (*child, out);
        }
    }

    constexpr int numTabs = 6;

    // tabBarArea() is private; the bar sits under the title and the tabs divide
    // it evenly, so aim at the centre of the wanted segment.
    void clickTab (FxSection& fx, int tab)
    {
        const auto bounds = fx.getLocalBounds();
        const int barY = 18 + 9;
        const int barX = 8 + (bounds.getWidth() - 16) * (2 * tab + 1) / (2 * numTabs);
        const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(),
                                  juce::Point<float> ((float) barX, (float) barY),
                                  juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &fx, &fx, juce::Time::getCurrentTime(),
                                  juce::Point<float> ((float) barX, (float) barY),
                                  juce::Time::getCurrentTime(), 1, false);
        fx.mouseDown (e);
    }

    bool writeSnapshot (juce::AudioProcessorEditor& editor, const juce::String& name)
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

        std::printf ("written %s\n", file.getFileName().toRawUTF8());
        return true;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    static const char* tabNames[] = { "dist", "delay", "filter", "comp", "chorus", "reverb" };

    for (int tab = 0; tab < numTabs; ++tab)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

        std::vector<FxSection*> sections;
        collect (*editor, sections);
        if (sections.size() != 2)
        {
            std::printf ("expected 2 FxSections, found %d\n", (int) sections.size());
            return 1;
        }

        for (auto* fx : sections)
            clickTab (*fx, tab);

        if (! writeSnapshot (*editor, juce::String ("fxtab_") + tabNames[tab] + ".png"))
            return 1;
    }

    // The DIST tab swaps its knobs with the type, so sweep both lines through
    // every shaper and check each layout lands.
    static const char* distNames[] = { "soft", "fuzz", "crush", "fold", "rect" };
    for (int type = 0; type < Distortion::numTypes; ++type)
    {
        const float norm = (float) type / (float) (Distortion::numTypes - 1);
        for (const char* id : { "bdisttype", "ddisttype" })
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (norm);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

        std::vector<FxSection*> sections;
        collect (*editor, sections);
        for (auto* fx : sections)
            clickTab (*fx, 0);

        if (! writeSnapshot (*editor, juce::String ("fxdist_") + distNames[type] + ".png"))
            return 1;
    }

    // The DELAY tab keeps the same three controls for both types, so one extra
    // snapshot is enough — it's there to check the longer word still fits the
    // selector, and that adding it hasn't squeezed the knobs out of shape.
    {
        for (const char* id : { "delaytype", "ddelaytype" })
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (1.0f);   // STEREO

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

        std::vector<FxSection*> sections;
        collect (*editor, sections);
        for (auto* fx : sections)
            clickTab (*fx, 1);

        if (! writeSnapshot (*editor, "fxdelay_stereo.png"))
            return 1;

        for (const char* id : { "delaytype", "ddelaytype" })
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (0.0f);   // back to MONO
    }

    // The tab bar shows which effects are switched on without their tab being
    // opened, so render a mixed state: a different set on each line, and on both
    // the selected tab and unselected ones.
    {
        for (const char* id : { "diston", "delayon", "brevon", "dcompon", "dchron", "ddiston" })
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (1.0f);
        for (const char* id : { "bflton", "bcompon", "bchron", "ddelayon", "dflton", "drevon" })
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (0.0f);

        // Once per skin: a wash that reads on a near-black chassis can vanish on
        // a cream one, so every palette gets checked.
        static const char* skinFiles[] = { "classic", "retro", "studio", "bad", "neon" };
        static_assert (juce::numElementsInArray (skinFiles) == ui303::numSkins,
                       "add a file name when a skin is added");

        const int userSkin = proc.uiSkin.load();
        for (int skin = 0; skin < ui303::numSkins; ++skin)
        {
            proc.uiSkin.store (skin);
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

            std::vector<FxSection*> sections;
            collect (*editor, sections);
            for (auto* fx : sections)
                clickTab (*fx, 1);   // DELAY: on for bass, off for drums

            if (! writeSnapshot (*editor, juce::String ("fxtab_enabled_") + skinFiles[skin] + ".png"))
                return 1;
        }
        proc.uiSkin.store (userSkin);
    }
    return 0;
}
