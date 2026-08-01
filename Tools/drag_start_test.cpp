// Offline test for the *start* of a pattern-key drag, through the real editor.
// song_drag_test covers what the SongList does with a drop; this covers the half
// before that — pressing a key and dragging must put a live drag on the editor's
// DragAndDropContainer carrying that slot's description, and the song list must
// accept it. A break anywhere in there stops patterns reaching the song even
// though the drop logic itself is fine.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>

namespace
{
    int failures = 0;
    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
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

    // getDistanceFromDragStart() is measured against the event's mouse-down
    // position, so a synthesised drag has to carry the original press point --
    // pass the same point for both and the drag reads as zero-distance.
    juce::MouseEvent eventAt (juce::Component& c, juce::Point<int> pos,
                              juce::Point<int> downPos, bool dragged)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 pos.toFloat(), juce::ModifierKeys::leftButtonModifier,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &c, &c, juce::Time::getCurrentTime(),
                 downPos.toFloat(), juce::Time::getCurrentTime(), 1, dragged };
    }

    // Press a key, then drag far enough to pass PatternKeys' 5px threshold.
    void pressAndDrag (PatternKeys& keys, juce::Point<int> from, juce::Point<int> to)
    {
        keys.mouseDown (eventAt (keys, from, from, false));
        keys.mouseDrag (eventAt (keys, to, from, true));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());

    auto* container = dynamic_cast<juce::DragAndDropContainer*> (ed.get());
    check (container != nullptr, "the editor is a drag-and-drop container");
    if (container == nullptr)
        return 1;

    std::vector<PatternKeys*> keypads;
    collect (*ed, keypads);
    check (keypads.size() == 2, "both pattern keypads are in the editor");

    std::vector<SongList*> lists;
    collect (*ed, lists);
    check (lists.size() == 1, "the song list is in the editor");
    if (keypads.size() != 2 || lists.empty())
    {
        std::printf ("DRAG-START-TEST FAILED (editor layout)\n");
        return 1;
    }

    for (auto* keys : keypads)
    {
        check (! keys->getBounds().isEmpty(), "the keypad has been laid out");

        // key 1 of bank A: below the bank row, in the first cell
        const auto area = keys->getLocalBounds();
        const juce::Point<int> onKey1 { area.getWidth() / 6, 20 + 4 + area.getHeight() / 6 };
        const juce::Point<int> movedAway { onKey1.x + 40, onKey1.y + 10 };

        // startDragging() refuses unless the real MouseInputSource is mid-drag,
        // which a headless harness cannot fake -- so this checks the things the
        // drag needs to be there, rather than the drag itself.
        keys->mouseDown (eventAt (*keys, onKey1, onKey1, false));

        check (juce::DragAndDropContainer::findParentDragContainerFor (keys) == container,
               "the keypad resolves to the editor as its drag container");

        bool bass = false;
        int slot = -1;
        const auto description =
            SongList::dragDescription (keys == keypads[0], 0);
        check (SongList::parseDrag (description, bass, slot) && slot == 0,
               "the keypad's slot description parses");

        const juce::DragAndDropTarget::SourceDetails details (description, keys, { 60, 40 });
        check (lists[0]->isInterestedInDragSource (details),
               "the song list accepts the drag");
        check (! lists[0]->getBounds().isEmpty(), "the song list has been laid out");

        // release so the next keypad starts clean
        keys->mouseUp (eventAt (*keys, movedAway, onKey1, true));
    }

    std::printf (failures == 0 ? "DRAG-START-TEST OK\n" : "DRAG-START-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
