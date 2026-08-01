// Offline test for dragging a pattern key into the song list: the drop position
// decides whether the pattern is assigned into an existing row or inserted as a
// new one, and which column it lands in.
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

    // Geometry mirrored from SongList: a 15px header then 21px rows.
    constexpr int headerH = 15, rowH = 21;

    juce::Point<int> onRow (int row)          { return { 60, headerH + row * rowH + rowH / 2 }; }
    juce::Point<int> atRowTop (int row)       { return { 60, headerH + row * rowH + 1 }; }
    juce::Point<int> belowAllRows (int count) { return { 60, headerH + count * rowH + rowH / 2 }; }

    // Simulates a real drag: the target tracks the pointer, then takes the drop.
    void dropAt (SongList& list, bool bass, int slot, juce::Point<int> pos)
    {
        const juce::DragAndDropTarget::SourceDetails details (
            SongList::dragDescription (bass, slot), nullptr, pos);
        list.itemDragEnter (details);
        list.itemDragMove (details);
        list.itemDropped (details);
    }

    void appendStep (BP303AudioProcessor& proc, int bass, int drum)
    {
        SongPlayer::Step s;
        s.bassSlot = bass;
        s.drumSlot = drum;
        proc.song.insertStep (proc.song.getCount(), s);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // --- the drag description round-trips ------------------------------------
    {
        bool bass = false;
        int slot = -1;

        check (SongList::parseDrag (SongList::dragDescription (true, 7), bass, slot)
               && bass && slot == 7,
               "a bass drag round-trips");
        check (SongList::parseDrag (SongList::dragDescription (false, 26), bass, slot)
               && ! bass && slot == 26,
               "a drum drag round-trips");
        check (! SongList::parseDrag (juce::var ("something else"), bass, slot),
               "a foreign description is rejected");
        check (! SongList::parseDrag (juce::var (), bass, slot),
               "an empty description is rejected");
    }

    // --- dropping on the body of a row assigns into it -----------------------
    {
        BP303AudioProcessor proc;
        SongList list (proc);
        list.setBounds (0, 0, 260, 400);

        appendStep (proc, 0, 0);
        appendStep (proc, 1, 1);
        appendStep (proc, 2, 2);

        const juce::DragAndDropTarget::SourceDetails details (
            SongList::dragDescription (true, 5), nullptr, onRow (1));
        check (list.isInterestedInDragSource (details), "the list accepts a pattern drag");

        dropAt (list, true, 5, onRow (1));
        check (proc.song.getCount() == 3, "assigning does not add a row");
        check (proc.song.getStep (1).bassSlot == 5, "the bass slot was assigned");
        check (proc.song.getStep (1).drumSlot == 1, "the drum slot was left alone");
        check (list.getSelectedRow() == 1, "the assigned row is selected");

        dropAt (list, false, 9, onRow (1));
        check (proc.song.getStep (1).drumSlot == 9, "a drum drag assigns the drum slot");
        check (proc.song.getStep (1).bassSlot == 5, "and leaves the bass slot alone");

        check (proc.song.getStep (0).bassSlot == 0 && proc.song.getStep (2).bassSlot == 2,
               "neighbouring rows are untouched");
    }

    // --- dropping on the band at the top of a row inserts before it ----------
    {
        BP303AudioProcessor proc;
        SongList list (proc);
        list.setBounds (0, 0, 260, 400);

        appendStep (proc, 0, 0);
        appendStep (proc, 1, 1);

        dropAt (list, true, 4, atRowTop (1));
        check (proc.song.getCount() == 3, "inserting adds a row");
        check (proc.song.getStep (1).bassSlot == 4, "the new row carries the dragged pattern");
        check (proc.song.getStep (1).drumSlot == SongPlayer::hold,
               "the other line holds, so the drums carry on");
        check (proc.song.getStep (2).bassSlot == 1, "the displaced row moved down");
        check (list.getSelectedRow() == 1, "the new row is selected");
    }

    // --- dropping past the last row appends ---------------------------------
    {
        BP303AudioProcessor proc;
        SongList list (proc);
        list.setBounds (0, 0, 260, 400);

        appendStep (proc, 0, 0);
        appendStep (proc, 1, 1);

        dropAt (list, false, 12, belowAllRows (2));
        check (proc.song.getCount() == 3, "dropping past the end appends");
        check (proc.song.getStep (2).drumSlot == 12, "the appended row carries the drum pattern");
        check (proc.song.getStep (2).bassSlot == SongPlayer::hold,
               "and holds the bass line");
    }

    // --- dropping into an empty song starts it ------------------------------
    {
        BP303AudioProcessor proc;
        SongList list (proc);
        list.setBounds (0, 0, 260, 400);

        dropAt (list, true, 3, onRow (0));
        check (proc.song.getCount() == 1, "a drop into an empty song creates the first row");
        check (proc.song.getStep (0).bassSlot == 3, "with the dragged pattern");
    }

    // --- a rejected drop leaves the song alone -------------------------------
    {
        BP303AudioProcessor proc;
        SongList list (proc);
        list.setBounds (0, 0, 260, 400);
        appendStep (proc, 0, 0);

        const juce::DragAndDropTarget::SourceDetails junk (juce::var ("nope"), nullptr, onRow (0));
        list.itemDragEnter (junk);
        list.itemDropped (junk);
        check (proc.song.getCount() == 1 && proc.song.getStep (0).bassSlot == 0,
               "a foreign drop changes nothing");
    }

    std::printf (failures == 0 ? "SONG-DRAG-TEST OK\n" : "SONG-DRAG-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
