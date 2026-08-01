// The playhead in both grids repaints only the cell it moved out of and the one
// it moved into, instead of the whole grid. That is only correct if those two
// cells are genuinely all that changed — anything else and the old marker would
// be left painted on screen.
//
// So: render the editor at one step, apply a *clipped* redraw at the next step
// over that image, and require the result to be identical to a full render at
// the next step.

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

    template <typename T>
    T* findDescendant (juce::Component& root)
    {
        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            auto* child = root.getChildComponent (i);
            if (auto* found = dynamic_cast<T*> (child))
                return found;
            if (auto* found = findDescendant<T> (*child))
                return found;
        }
        return nullptr;
    }

    void renderInto (juce::Image& img, juce::AudioProcessorEditor& editor,
                     juce::Rectangle<int> clip)
    {
        juce::Graphics g (img);
        g.reduceClipRegion (clip);
        editor.paintEntireComponent (g, false);
    }

    juce::Image fullRender (juce::AudioProcessorEditor& editor)
    {
        juce::Image img (juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
        renderInto (img, editor, editor.getLocalBounds());
        return img;
    }

    int pixelsDiffering (const juce::Image& a, const juce::Image& b)
    {
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    ++n;
        return n;
    }

    // Move the playhead from `from` to `to`, and check that redrawing only the
    // cells the grid would invalidate lands on the same pixels as a full redraw.
    void checkPartialRepaint (const char* name,
                              juce::AudioProcessorEditor& editor,
                              std::atomic<int>& playingStep,
                              juce::Rectangle<int> cellFrom,
                              juce::Rectangle<int> cellTo,
                              int from, int to)
    {
        playingStep.store (from);
        auto partial = fullRender (editor);

        playingStep.store (to);
        renderInto (partial, editor, cellFrom);
        renderInto (partial, editor, cellTo);

        const auto full = fullRender (editor);
        const int differing = pixelsDiffering (partial, full);

        check (differing == 0,
               juce::String (name) + ": redrawing two cells matches a full repaint"
                   + (differing == 0 ? "" : " (" + juce::String (differing) + " px differ)"));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (48000.0, 128);

    // Content worth redrawing around: gates, notes and drum hits on every step.
    for (int s = 0; s < 16; ++s)
    {
        proc.sequencer.steps[(size_t) s].gate.store (s % 3 != 0);
        // all three dynamics represented, so the ACC row has every cell state on it
        proc.sequencer.steps[(size_t) s].dyn.store (s % 4 == 0 ? dyn303::Hard
                                                  : s % 4 == 2 ? dyn303::Soft
                                                               : dyn303::Normal);
        proc.sequencer.steps[(size_t) s].slide.store (s % 5 == 0);
        proc.sequencer.steps[(size_t) s].key.store (s % 12);
    }
    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        proc.drumSequencer.stepMask[lane].store (0x9249u >> lane);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setVisible (true);
    // Native size, so `content`'s scale transform is the identity and the clipped
    // redraw lands on exactly the pixels the grid asked for.
    if (auto* c = editor->getConstrainer())
        editor->setSize (c->getMaximumWidth(), c->getMaximumHeight());

    auto* stepGrid = findDescendant<StepGrid> (*editor);
    auto* drumGrid = findDescendant<DrumGrid> (*editor);

    check (stepGrid != nullptr && drumGrid != nullptr, "both grids found in the editor");
    if (stepGrid == nullptr || drumGrid == nullptr)
        return 1;

    for (const auto move : { std::pair { 0, 1 }, std::pair { 7, 8 }, std::pair { 15, 0 } })
    {
        checkPartialRepaint (
            "step grid", *editor, proc.sequencer.playingStep,
            editor->getLocalArea (stepGrid, stepGrid->ledCellBounds (move.first)),
            editor->getLocalArea (stepGrid, stepGrid->ledCellBounds (move.second)),
            move.first, move.second);

        checkPartialRepaint (
            "drum grid", *editor, proc.drumSequencer.playingStep,
            editor->getLocalArea (drumGrid, drumGrid->playheadCellBounds (move.first)),
            editor->getLocalArea (drumGrid, drumGrid->playheadCellBounds (move.second)),
            move.first, move.second);
    }

    // And leaving the pattern entirely (transport stopped) has to clear it.
    checkPartialRepaint (
        "step grid, stopping", *editor, proc.sequencer.playingStep,
        editor->getLocalArea (stepGrid, stepGrid->ledCellBounds (9)),
        editor->getLocalArea (stepGrid, stepGrid->ledCellBounds (-1)),
        9, -1);

    std::printf (failures == 0 ? "\nall repaint checks passed\n"
                               : "\n%d repaint check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
