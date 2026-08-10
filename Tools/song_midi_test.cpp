// Offline test for dragging the whole SONG out as one MIDI region. Drives the
// real editor's shouldDropFilesWhenDraggedExternally() with the description the
// MIDI button produces, then reads the written .mid file back.
//
// The arrangement walk has to agree with the one SongPlayer does at playback:
// repeats expanded, holds carrying the previous pattern on, a *leading* hold
// staying silent, and per-line mutes dropping their line. Each of those is a way
// the exported region could disagree with what you hear, so each is checked
// against tick positions rather than just note counts.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/MidiExport.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& msg)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", msg.toRawUTF8());
        if (! ok)
            ++failures;
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

    int countOn (const std::vector<Note>& notes, int channel)
    {
        int n = 0;
        for (const auto& x : notes)
            if (x.channel == channel)
                ++n;
        return n;
    }

    bool hasNoteAt (const std::vector<Note>& notes, int channel, int note, double tick)
    {
        for (const auto& x : notes)
            if (x.channel == channel && x.note == note
                && std::abs (x.tick - tick) < 1.0)
                return true;
        return false;
    }

    void pointerOutside (BP303AudioProcessorEditor& editor)
    {
        const auto away = editor.getScreenBounds().getBottomRight() + juce::Point<int> (400, 400);
        editor.pointerPosition = [away] { return away; };
    }

    juce::File dragSongOut (BP303AudioProcessorEditor& editor)
    {
        juce::StringArray files;
        bool canMove = true;
        const juce::DragAndDropTarget::SourceDetails details { "BP303SONG", nullptr, {} };

        if (! editor.shouldDropFilesWhenDraggedExternally (details, files, canMove))
            return {};
        if (files.size() != 1 || canMove)
            return {};
        return juce::File (files[0]);
    }

    // Writes a one-note bass pattern into a slot: a single gated step 0 at the
    // given pitch, so each slot is identifiable by note number in the export.
    void writeBassSlot (BP303AudioProcessor& proc, int slot, int pitch, int length)
    {
        proc.requestBassPattern (slot);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        buf.clear();
        proc.processBlock (buf, midi);      // not running: the switch lands now

        for (int i = 0; i < Sequencer303::maxSteps; ++i)
            proc.sequencer.steps[i].gate.store (false);
        proc.sequencer.steps[0].gate.store (true);
        Sequencer303::storePitch (proc.sequencer.steps[0], pitch);
        proc.sequencer.steps[0].hold.store (1);
        proc.sequencer.steps[0].slide.store (false);
        proc.sequencer.steps[0].dyn.store (dyn303::Normal);
        proc.sequencer.length.store (length);
    }

    void writeDrumSlot (BP303AudioProcessor& proc, int slot)
    {
        proc.requestDrumPattern (slot);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        buf.clear();
        proc.processBlock (buf, midi);

        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
            proc.drumSequencer.stepMask[lane].store (0);
        proc.drumSequencer.stepMask[0].store (1u);   // kick on step 0 only
    }

    SongPlayer::Step row (int bass, int drum, int repeats,
                          bool bassMute = false, bool drumMute = false)
    {
        SongPlayer::Step s;
        s.bassSlot = bass;
        s.drumSlot = drum;
        s.repeats  = repeats;
        s.bassMute = bassMute;
        s.drumMute = drumMute;
        return s;
    }

    void setSong (BP303AudioProcessor& proc, const std::vector<SongPlayer::Step>& rows)
    {
        proc.song.clear();
        for (size_t i = 0; i < rows.size(); ++i)
            proc.song.insertStep ((int) i, rows[i]);
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
    setP ("run", 0.0f);        // not running, so pattern switches land at once
    setP ("shuffle", 0.0f);    // straight 16ths, so the tick maths is exact

    // Three identifiable 16-step slots: A1 -> note 45, A2 -> 46, A3 -> 47.
    writeBassSlot (proc, 0, 9, 16);
    writeBassSlot (proc, 1, 10, 16);
    writeBassSlot (proc, 2, 11, 16);
    writeDrumSlot (proc, 0);
    writeBassSlot (proc, 0, 9, 16);        // leave slot 0 current

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<BP303AudioProcessorEditor*> (ed.get());
    check (editor != nullptr, "the editor is a BP303 editor");
    if (editor == nullptr)
        return 1;
    pointerOutside (*editor);

    const double bar = 16.0 * bp303::ticksPer16th;   // one 16-step pattern

    // --- an empty song hands over nothing -------------------------------------
    {
        proc.song.clear();
        check (! dragSongOut (*editor).existsAsFile(),
               "an empty song refuses the drag rather than writing an empty region");
    }

    // --- repeats are expanded, not collapsed ----------------------------------
    {
        setSong (proc, { row (0, 0, 3) });
        const auto file = dragSongOut (*editor);
        check (file.existsAsFile(), "a song drag writes a file");

        const auto notes = readNotes (file);
        check (countOn (notes, 1) == 3, "three repeats give three bass notes");
        for (int r = 0; r < 3; ++r)
            check (hasNoteAt (notes, 1, 45, r * bar),
                   "repeat " + juce::String (r) + " lands one pattern later");
    }

    // --- rows follow each other, and each names its own pattern ---------------
    {
        setSong (proc, { row (0, 0, 1), row (1, 0, 1), row (2, 0, 1) });
        const auto notes = readNotes (dragSongOut (*editor));

        check (countOn (notes, 1) == 3, "three rows give three bass notes");
        check (hasNoteAt (notes, 1, 45, 0.0),        "row 0 plays A1 at the top");
        check (hasNoteAt (notes, 1, 46, bar),        "row 1 plays A2 a pattern in");
        check (hasNoteAt (notes, 1, 47, 2.0 * bar),  "row 2 plays A3 after that");
    }

    // --- a hold carries the previous pattern on -------------------------------
    {
        setSong (proc, { row (1, 0, 1), row (SongPlayer::hold, SongPlayer::hold, 2) });
        const auto notes = readNotes (dragSongOut (*editor));

        check (countOn (notes, 1) == 3, "a hold row still sounds its carried pattern");
        check (hasNoteAt (notes, 1, 46, bar) && hasNoteAt (notes, 1, 46, 2.0 * bar),
               "and it is the pattern the row above named, twice over");
    }

    // --- a *leading* hold stays silent ----------------------------------------
    // Nothing has named the line yet, so there is nothing to carry on from.
    // Sounding whatever happened to be loaded would make the same song play
    // differently from run to run, which is the call SongPlayer already makes.
    {
        setSong (proc, { row (SongPlayer::hold, SongPlayer::hold, 1), row (0, 0, 1) });
        const auto notes = readNotes (dragSongOut (*editor));

        check (! hasNoteAt (notes, 1, 45, 0.0), "a leading hold sounds nothing");
        check (hasNoteAt (notes, 1, 45, bar),
               "and the row that does name a pattern still lands in its own bar");
    }

    // --- mutes drop their own line and leave the other alone ------------------
    {
        setSong (proc, { row (0, 0, 1, true, false), row (0, 0, 1, false, true) });
        const auto notes = readNotes (dragSongOut (*editor));

        check (! hasNoteAt (notes, 1, 45, 0.0), "a bass-muted row writes no bass");
        check (hasNoteAt (notes, 10, bp303::gmDrumNote[0], 0.0),
               "but its drums still play");
        check (hasNoteAt (notes, 1, 45, bar), "a drum-muted row still writes bass");
        check (! hasNoteAt (notes, 10, bp303::gmDrumNote[0], bar),
               "and no drums");
    }

    // --- both lines share one region ------------------------------------------
    // The file is meant to drop back onto a BP303, which reads bass on channel 1
    // and drums on channel 10, so one region has to carry the whole arrangement.
    {
        setSong (proc, { row (0, 0, 2) });
        const auto file = dragSongOut (*editor);

        juce::FileInputStream in (file);
        juce::MidiFile mf;
        check (mf.readFrom (in), "the file parses as MIDI");
        check (mf.getNumTracks() == 1, "the song is one track, so it drops as one region");

        const auto notes = readNotes (file);
        check (countOn (notes, 1) > 0 && countOn (notes, 10) > 0,
               "carrying bass on channel 1 and drums on channel 10 together");
    }

    // --- a row's length comes from its own bass pattern ------------------------
    // Patterns of different lengths must not overlap: an 8-step row is half a
    // 16-step one, and the row after it has to start where it actually ends.
    {
        writeBassSlot (proc, 1, 10, 8);        // A2 is now 8 steps long
        writeBassSlot (proc, 0, 9, 16);        // leave a 16-step pattern current

        setSong (proc, { row (1, 0, 1), row (0, 0, 1) });
        const auto notes = readNotes (dragSongOut (*editor));

        const double shortBar = 8.0 * bp303::ticksPer16th;
        check (hasNoteAt (notes, 1, 46, 0.0), "the 8-step row starts at the top");
        check (hasNoteAt (notes, 1, 45, shortBar),
               "and the next row starts a *short* pattern later, not a full one");
    }

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
