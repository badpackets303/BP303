// Renders one knob swept across its travel, for every skin and every hot-end
// setting, to knob_snapshot.png.
//
// The editor snapshots draw each knob wherever its parameter happens to sit,
// which is no use for judging anything that varies *along* the travel — the
// value tint, the warning at an extreme end, the highlight drifting round with
// the cap. This lays the whole sweep out side by side instead.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr int cell = 92;
    const float positions[] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
    const int   nPos = juce::numElementsInArray (positions);

    const int   hots[]    = { ui303::HotNone, ui303::HotTop, ui303::HotBottom };
    const char* hotName[] = { "none", "top", "bottom" };
    const int   nHot = juce::numElementsInArray (hots);

    juce::Image img (juce::Image::ARGB, cell * nPos + 130,
                     cell * ui303::numSkins * nHot + 30, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colour (0xff2a2a2e));

    Look303 look;
    juce::Slider probe { juce::Slider::RotaryHorizontalVerticalDrag,
                         juce::Slider::NoTextBox };

    // the same sweep the editor's knobs use
    const float a0 = juce::MathConstants<float>::pi * 1.2f;
    const float a1 = juce::MathConstants<float>::pi * 2.8f;

    int row = 0;
    for (int skin = 0; skin < ui303::numSkins; ++skin)
    {
        look.setSkin (skin);

        for (int hi = 0; hi < nHot; ++hi)
        {
            probe.getProperties().set ("hotEnd", hots[hi]);
            const int y = 15 + row * cell;

            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (ui303::skinName (skin)) + "  /  " + hotName[hi],
                        4, y + cell / 2 - 8, 122, 16, juce::Justification::centredLeft);

            for (int i = 0; i < nPos; ++i)
                look.drawRotarySlider (g, 130 + i * cell, y, cell - 8, cell - 8,
                                       positions[i], a0, a1, probe);
            ++row;
        }
    }

    const auto out = juce::File::getCurrentWorkingDirectory()
                         .getChildFile ("knob_snapshot.png");
    juce::FileOutputStream fs (out);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, fs);

    std::printf ("written %s %dx%d\n", out.getFullPathName().toRawUTF8(),
                 img.getWidth(), img.getHeight());
    return 0;
}
