// Each FX tab shows a lamp when its effect is switched on. The tab labels and
// the enable-parameter IDs are two parallel lists in the editor, so a single
// mis-ordered entry would light the wrong tab and nothing would complain.
//
// Rather than repeat that list here — which would agree with itself and prove
// nothing — this drives the ACTIVE toggle on each open page and requires the
// lamp on *that* tab, and no other, to follow it.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

namespace
{
    int failures = 0;
    constexpr int numTabs = 6;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    void collect (juce::Component& c, std::vector<FxSection*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* fx = dynamic_cast<FxSection*> (child))
                out.push_back (fx);
            collect (*child, out);
        }
    }

    juce::ToggleButton* firstToggle (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<juce::ToggleButton*> (child))
                return t;
            if (auto* t = firstToggle (*child))
                return t;
        }
        return nullptr;
    }

    // The page the section is currently showing is its one visible child.
    juce::Component* openPage (FxSection& fx)
    {
        for (auto* child : fx.getChildren())
            if (child->isVisible())
                return child;
        return nullptr;
    }

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

    juce::String litTabs (FxSection& fx)
    {
        juce::String s;
        for (int i = 0; i < numTabs; ++i)
            s += fx.tabEnabled (i) ? "*" : ".";
        return s;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (48000.0, 128);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    std::vector<FxSection*> sections;
    collect (*editor, sections);
    check (sections.size() == 2, "both FX sections found");
    if (sections.size() != 2)
        return 1;

    static const char* sectionNames[] = { "BASS FX", "DRUM FX" };

    for (size_t s = 0; s < sections.size(); ++s)
    {
        auto& fx = *sections[s];
        const juce::String name = sectionNames[s];

        check (litTabs (fx) == "......", name + ": nothing lit before anything is enabled");

        for (int tab = 0; tab < numTabs; ++tab)
        {
            clickTab (fx, tab);

            auto* page = openPage (fx);
            auto* active = page != nullptr ? firstToggle (*page) : nullptr;
            if (active == nullptr)
            {
                check (false, name + " tab " + juce::String (tab) + ": no ACTIVE toggle found");
                continue;
            }

            juce::String expected;
            for (int i = 0; i < numTabs; ++i)
                expected += i == tab ? "*" : ".";

            active->setToggleState (true, juce::sendNotificationSync);
            check (litTabs (fx) == expected,
                   name + " tab " + juce::String (tab) + ": its own lamp lights ("
                       + litTabs (fx) + ", wanted " + expected + ")");

            // The other section must not have moved with it.
            auto& other = *sections[1 - s];
            check (litTabs (other) == "......",
                   name + " tab " + juce::String (tab) + ": the other line is untouched");

            active->setToggleState (false, juce::sendNotificationSync);
            check (litTabs (fx) == "......",
                   name + " tab " + juce::String (tab) + ": switching off clears the lamp");
        }
    }

    std::printf (failures == 0 ? "\nall FX tab checks passed\n"
                               : "\n%d FX tab check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
