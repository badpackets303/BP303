// Offline test for dragging a pattern key out of the plugin as MIDI. Drives the
// real editor's shouldDropFilesWhenDraggedExternally() with the drag description
// a pattern key produces, then reads the written .mid file back and checks it
// carries that slot's pattern — including a slot the sequencers aren't playing,
// which is the thing the old DRAG MIDI button couldn't do.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/MidiExport.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;
    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    // Not running in Seq mode: a queued switch lands on the next processBlock.
    void pump (BP303AudioProcessor& proc)
    {
        juce::AudioBuffer<float> buf (2, 256);
        buf.clear();
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
    }

    void setBass (BP303AudioProcessor& proc, int step, bool gate, int pitch)
    {
        proc.sequencer.steps[step].gate.store (gate);
        Sequencer303::storePitch (proc.sequencer.steps[step], pitch);
    }

    struct Note { int channel, note; double tick; };

    std::vector<Note> readNotes (const juce::File& file)
    {
        std::vector<Note> notes;
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return notes;

        juce::MidiFile mf;
        if (! mf.readFrom (in))
            return notes;

        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto& track = *mf.getTrack (t);
            for (int i = 0; i < track.getNumEvents(); ++i)
            {
                const auto& msg = track.getEventPointer (i)->message;
                if (msg.isNoteOn())
                    notes.push_back ({ msg.getChannel(), msg.getNoteNumber(),
                                       msg.getTimeStamp() });
            }
        }
        return notes;
    }

    // Puts the pointer well clear of the editor, which is what a drag that has
    // actually left the plugin window looks like.
    void pointerOutside (BP303AudioProcessorEditor& editor)
    {
        const auto away = editor.getScreenBounds().getBottomRight() + juce::Point<int> (400, 400);
        editor.pointerPosition = [away] { return away; };
    }

    // Runs one drag out of the plugin and returns the file the editor handed the
    // host, or a non-existent File if it refused the drag.
    juce::File dragOut (BP303AudioProcessorEditor& editor, const juce::var& description)
    {
        juce::StringArray files;
        bool canMove = true;
        const juce::DragAndDropTarget::SourceDetails details (description, nullptr, {});

        // Bracketed the way a real gesture is: startDragging tells the editor a
        // drag has begun, which is what arms the export for it. Calling the drop
        // on its own would leave the previous drag's "already handed over" flag
        // standing and quietly export nothing.
        editor.dragOperationStarted (details);
        const bool ok = editor.shouldDropFilesWhenDraggedExternally (details, files, canMove);
        editor.dragOperationEnded (details);

        if (! ok)
            return {};
        if (files.size() != 1)
            return {};
        if (canMove)     // our temp file must be copied, never moved away
            return {};

        return juce::File (files[0]);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 256);

    auto setP = [&] (const char* id, float v) {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (v);
    };
    setP ("playmode", 0.5f);   // Seq
    setP ("run", 0.0f);        // not running -> immediate switches
    setP ("shuffle", 0.0f);    // straight 16ths, so the tick maths is exact

    // --- slot A1 (0): bass on step 0, drums on every 4th step ---
    for (int i = 0; i < Sequencer303::maxSteps; ++i)
        setBass (proc, i, false, -3);
    setBass (proc, 0, true, 9);          // note 36 + 9 = 45
    for (int l = 0; l < DrumSequencer::numLanes; ++l)
        proc.drumSequencer.stepMask[l].store (0);
    proc.drumSequencer.stepMask[DrumMachine::BD].store (0x1111u);   // steps 0,4,8,12

    // --- slot A2 (1): a different bass line, then hop back to A1 ---
    proc.requestBassPattern (1);
    pump (proc);
    setBass (proc, 4, true, 5);          // note 36 + 5 = 41
    proc.requestBassPattern (0);
    pump (proc);
    check (proc.getCurrentBassPattern() == 0, "back on bass slot A1");

    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<BP303AudioProcessorEditor*> (base.get());
    if (editor == nullptr)
    {
        std::printf ("could not create the editor\n");
        return 1;
    }

    pointerOutside (*editor);

    // --- a drag still inside the window belongs to the SONG list, not the host.
    //     Accepting it here would destroy the internal drag, which is what broke
    //     dragging patterns into the song. ---
    {
        const auto centre = editor->getScreenBounds().getCentre();
        editor->pointerPosition = [centre] { return centre; };
        check (! dragOut (*editor, SongList::dragDescription (true, 0)).existsAsFile(),
               "a drag inside the editor is left to the song list");

        pointerOutside (*editor);
        check (dragOut (*editor, SongList::dragDescription (true, 0)).existsAsFile(),
               "the same drag outside the editor does go to the host");
    }

    // --- the loaded bass slot exports its live edits ---
    {
        const auto file = dragOut (*editor, SongList::dragDescription (true, 0));
        check (file.existsAsFile(), "dragging bass A1 out produced a file");
        check (file.getFileName() == "BP303 Bass A1.mid", "the file is named for the slot");

        const auto notes = readNotes (file);
        check (notes.size() == 1, "bass A1 carries one note");
        check (! notes.empty() && notes[0].note == 45, "bass A1 note is the one on step 0");
        check (! notes.empty() && notes[0].tick == 0.0, "bass A1 note sits on step 0");
    }

    // --- a slot that is NOT loaded exports its own pattern, not the live one ---
    {
        const auto file = dragOut (*editor, SongList::dragDescription (true, 1));
        check (file.existsAsFile(), "dragging bass A2 out produced a file");
        check (file.getFileName() == "BP303 Bass A2.mid", "the A2 file is named for its slot");

        const auto notes = readNotes (file);
        check (notes.size() == 1, "bass A2 carries one note");
        check (! notes.empty() && notes[0].note == 41, "bass A2 exported the stored slot");
        check (! notes.empty() && notes[0].tick == 4.0 * bp303::ticksPer16th,
               "bass A2 note sits on step 4");
    }

    // --- drums go out on channel 10 with GM notes ---
    {
        const auto file = dragOut (*editor, SongList::dragDescription (false, 0));
        check (file.existsAsFile(), "dragging drums A1 out produced a file");

        const auto notes = readNotes (file);
        check (notes.size() == 4, "drums A1 carries the four kicks");
        bool allKick = ! notes.empty();
        for (const auto& n : notes)
            if (n.channel != 10 || n.note != bp303::gmDrumNote[DrumMachine::BD])
                allKick = false;
        check (allKick, "drum notes are GM kicks on channel 10");
    }

    // --- a lane on its own clock exports on that clock ---
    // The region has to be what the plugin plays, and for these two lanes that
    // is not "the mask, laid out in sixteenths": a short lane repeats inside the
    // bar rather than stopping at its own end, and a fitted lane's steps are not
    // sixteenths at all.
    {
        for (int l = 0; l < DrumSequencer::numLanes; ++l)
            proc.drumSequencer.stepMask[l].store (0);

        // three steps fitted across the bar — 3 against 4
        proc.drumSequencer.stepMask[DrumMachine::CH].store (0b111);
        proc.drumSequencer.laneLength[DrumMachine::CH].store (3);
        proc.drumSequencer.laneFit[DrumMachine::CH].store (true);

        // and a 6-step lane on the grid, which goes round twice plus four in a
        // 16-step bar: hits on its own step 0 land at 0, 6 and 12
        proc.drumSequencer.stepMask[DrumMachine::OH].store (1u << 0);
        proc.drumSequencer.laneLength[DrumMachine::OH].store (6);

        const auto file = dragOut (*editor, SongList::dragDescription (false, 0));
        check (file.existsAsFile(), "dragging a polyrhythmic pattern out produced a file");

        std::vector<double> hat, open;
        for (const auto& n : readNotes (file))
        {
            if (n.note == bp303::gmDrumNote[DrumMachine::CH]) hat.push_back (n.tick);
            if (n.note == bp303::gmDrumNote[DrumMachine::OH]) open.push_back (n.tick);
        }
        std::sort (hat.begin(), hat.end());
        std::sort (open.begin(), open.end());

        const double span = 16.0 * bp303::ticksPer16th / 3.0;
        check (hat.size() == 3, "the fitted lane exported three hits");
        bool spaced = hat.size() == 3;
        for (size_t i = 0; spaced && i < hat.size(); ++i)
            if (std::abs (hat[i] - (double) i * span) > 1.0)
                spaced = false;
        check (spaced, "the fitted lane's hits are not evenly spread across the bar");

        const std::vector<double> wantOpen { 0.0, 6.0 * bp303::ticksPer16th,
                                             12.0 * bp303::ticksPer16th };
        check (open.size() == 3 && open == wantOpen,
               "the 6-step lane did not repeat on its own cycle inside the bar");
    }

    // --- a split gate exports as repeats, not as one note ---
    {
        auto& st = proc.sequencer.steps[0];
        st.gate.store (true);
        st.slide.store (false);
        st.hold.store (1);
        st.ratchet.store (3);

        const auto file = dragOut (*editor, SongList::dragDescription (true, 0));
        check (file.existsAsFile(), "dragging a split gate out produced a file");

        std::vector<double> ons;
        for (const auto& n : readNotes (file))
            ons.push_back (n.tick);
        std::sort (ons.begin(), ons.end());

        check (ons.size() == 3, "a 3-way split exported three notes");
        bool spaced = ons.size() == 3;
        for (size_t r = 0; spaced && r < ons.size(); ++r)
            if (std::abs (ons[r] - (double) r * bp303::ticksPer16th / 3.0) > 1.0)
                spaced = false;
        check (spaced, "the split's repeats are not evenly spread through the step");

        st.ratchet.store (1);
    }

    // --- an empty slot ---
    // Slot 26 has never been written to, so it has no gated steps at all.
    {
        const auto file = dragOut (*editor, SongList::dragDescription (true, 26));
        std::printf ("      [empty bass slot handed over a file: %d]\n",
                     (int) file.existsAsFile());
        if (file.existsAsFile())
            std::printf ("      [and it contains %d notes]\n", (int) readNotes (file).size());
        check (! file.existsAsFile(),
               "an empty pattern refuses the drag instead of writing a noteless region");
    }

    // --- notes that sit past the pattern's own LENGTH -------------------------
    // The grid holds 16 steps whatever LENGTH says, so a pattern can look full
    // while playing none of it. The exporter walks the same steps playback does,
    // which is right, but it means a pattern with every note past the loop point
    // exports nothing at all.
    {
        proc.requestBassPattern (5);
        pump (proc);
        for (int i = 0; i < Sequencer303::maxSteps; ++i)
            setBass (proc, i, false, -3);
        for (int i = 8; i < 16; ++i)          // notes only in the second half
            setBass (proc, i, true, 9);
        proc.sequencer.length.store (4);      // ...but the loop stops at step 4

        const auto file = dragOut (*editor, SongList::dragDescription (true, 5));
        std::printf ("      [notes-past-length handed over a file: %d, notes %d]\n",
                     (int) file.existsAsFile(),
                     file.existsAsFile() ? (int) readNotes (file).size() : -1);
        check (! file.existsAsFile(),
               "a pattern whose notes all sit past LENGTH refuses the drag too");
    }

    // --- anything that isn't a pattern-key drag is refused ---
    {
        check (! dragOut (*editor, juce::var ("something else")).existsAsFile(),
               "a foreign drag hands the host nothing");
    }

    std::printf (failures == 0 ? "MIDI-DRAG-TEST OK\n" : "MIDI-DRAG-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
