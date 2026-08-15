// Offline test for the three gestures a drum lane's end handle carries, driven
// through the real DrumGrid rather than by setting the flags directly.
//
// The point is that they stay distinct. Length and FIT are one control's worth
// of screen and two entirely different musical results — a drag that turned FIT
// on, or a double-click that moved the end, would silently swap polymeter for
// polyrhythm on a pattern the user thought they were only resizing.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok)
            ++failures;
    }

    template <typename T>
    void collect (juce::Component& c, std::vector<T*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* hit = dynamic_cast<T*> (child))
                out.push_back (hit);
            collect (*child, out);
        }
    }

    juce::MouseEvent eventAt (juce::Component& c, juce::Point<int> pos,
                              bool rightButton, int clicks)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 pos.toFloat(),
                 rightButton ? juce::ModifierKeys::rightButtonModifier
                             : juce::ModifierKeys::leftButtonModifier,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &c, &c, juce::Time::getCurrentTime(),
                 pos.toFloat(), juce::Time::getCurrentTime(), clicks, false };
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    base->setVisible (true);
    base->setSize (1466, 848);

    std::vector<DrumGrid*> grids;
    collect (*base, grids);
    if (grids.empty())
    {
        std::printf ("FIT-GESTURE-TEST FAILED (no drum grid)\n");
        return 1;
    }

    auto& grid = *grids.front();
    auto& drums = proc.drumSequencer;
    constexpr int lane = DrumMachine::CH;
    const int masterLen = proc.sequencer.length.load();

    // Where the handle sits for a given length, which is what all three
    // gestures are aimed at.
    auto handleCentre = [&] (int len) {
        return grid.laneEndHandle (lane, len).getCentre();
    };

    // --- a drag sets the length and leaves the clock alone ------------------
    {
        grid.mouseDown (eventAt (grid, handleCentre (masterLen), false, 1));
        grid.mouseDrag (eventAt (grid, handleCentre (3), false, 1));

        check (drums.lengthOf (lane, masterLen) == 3, "dragging the handle set the lane to 3 steps");
        check (! drums.laneFit[lane].load(), "dragging the handle did not turn FIT on");
    }

    // --- a double-click toggles FIT and leaves the length alone -------------
    {
        // The press that opens a double-click arms a length drag; it must not
        // survive as a move of the end.
        grid.mouseDown (eventAt (grid, handleCentre (3), false, 1));
        grid.mouseDoubleClick (eventAt (grid, handleCentre (3), false, 2));

        check (drums.laneFit[lane].load(), "double-clicking the handle turned FIT on");
        check (drums.lengthOf (lane, masterLen) == 3, "double-clicking left the length at 3");

        grid.mouseDoubleClick (eventAt (grid, handleCentre (3), false, 2));
        check (! drums.laneFit[lane].load(), "double-clicking again turned FIT back off");
    }

    // --- a double-click away from the handle does nothing -------------------
    // The grid is mostly cells, and a double-click on one is just two clicks.
    {
        drums.laneFit[lane].store (true);
        const auto cell = grid.playheadCellBounds (lane, 8).getCentre();
        grid.mouseDoubleClick (eventAt (grid, cell, false, 2));
        check (drums.laneFit[lane].load(), "a double-click on a cell left FIT alone");
    }

    // --- right-click puts the lane fully back on the master -----------------
    // Including its clock: a lane that follows the master and is also fitted is
    // the identity case, so leaving FIT set would only show up as a handle
    // coloured for a mode the lane is no longer meaningfully in.
    {
        drums.laneLength[lane].store (3);
        drums.laneFit[lane].store (true);
        grid.mouseDown (eventAt (grid, handleCentre (3), true, 1));

        check (drums.laneLength[lane].load() == DrumSequencer::followMaster,
               "right-clicking put the lane back on the master");
        check (! drums.laneFit[lane].load(), "right-clicking cleared FIT too");
    }

    // --- the bass line's end handle carries the same three gestures ---------
    std::vector<StepGrid*> stepGrids;
    collect (*base, stepGrids);
    if (stepGrids.empty())
    {
        std::printf ("FIT-GESTURE-TEST FAILED (no step grid)\n");
        return 1;
    }

    auto& bass = *stepGrids.front();
    auto& seq = proc.sequencer;

    auto bassHandle = [&] (int len) {
        return bass.patternEndHandle (len).getCentre();
    };

    // --- dragging the bass end must not drag the bar with it ----------------
    // This is the whole point of the handle setting the line rather than LENGTH:
    // every drum lane on followMaster takes its length from the bar, so a handle
    // wired to LENGTH moved all five of them at once.
    {
        seq.patternLength.store (Sequencer303::followBar);
        seq.patternFit.store (false);
        drums.laneLength[lane].store (DrumSequencer::followMaster);

        const int barBefore = seq.length.load();
        const int laneBefore = drums.lengthOf (lane, barBefore);

        bass.mouseDown (eventAt (bass, bassHandle (barBefore), false, 1));
        bass.mouseDrag (eventAt (bass, bassHandle (6), false, 1));
        bass.mouseUp   (eventAt (bass, bassHandle (6), false, 1));

        check (seq.lengthOf (seq.length.load()) == 6, "dragging the bass end set the line to 6");
        check (seq.length.load() == barBefore, "dragging the bass end moved the bar");
        check (drums.lengthOf (lane, seq.length.load()) == laneBefore,
               "dragging the bass end dragged a drum lane with it");
    }

    // --- and the other two gestures behave as the drums' do -----------------
    {
        bass.mouseDown (eventAt (bass, bassHandle (6), false, 1));
        bass.mouseDoubleClick (eventAt (bass, bassHandle (6), false, 2));
        check (seq.patternFit.load(), "double-clicking the bass end turned FIT on");
        check (seq.lengthOf (seq.length.load()) == 6, "double-clicking left the line at 6");

        bass.mouseDown (eventAt (bass, bassHandle (6), true, 1));
        check (seq.patternLength.load() == Sequencer303::followBar,
               "right-clicking put the line back on the bar");
        check (! seq.patternFit.load(), "right-clicking cleared the line's FIT too");
        check (seq.length.load() == 16, "right-clicking the bass end moved the bar");
    }

    if (failures == 0)
        std::printf ("FIT-GESTURE-TEST OK\n");
    else
        std::printf ("FIT-GESTURE-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
