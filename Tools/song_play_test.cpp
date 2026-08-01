// Offline test for song mode driving the real processor: verifies that the
// arrangement selects patterns at the right moments, that holds and per-step
// mutes behave, that looping wraps, and that a pattern whose length differs
// from the previous step's starts from its own step 0.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

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

    constexpr double sampleRate = 44100.0;
    constexpr int    blockSize  = 256;
    constexpr double bpm        = 120.0;

    // beats advanced per processBlock at the settings above
    constexpr double blockBeats = (double) blockSize / sampleRate * bpm / 60.0;

    void pump (BP303AudioProcessor& proc, int blocks = 1)
    {
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i)
        {
            buf.clear();
            midi.clear();
            proc.processBlock (buf, midi);
        }
    }

    void setP (BP303AudioProcessor& proc, const char* id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (v);
    }

    // Writes a recognisable one-note bass line and a one-hit drum beat into a
    // slot, so we can tell from the live sequencers which slot is loaded.
    void fillSlot (BP303AudioProcessor& proc, int slot, int pitch, int drumStep,
                   int lengthSteps = 16)
    {
        proc.requestBassPattern (slot);
        proc.requestDrumPattern (slot);
        pump (proc);   // not running -> the switch lands immediately

        for (int i = 0; i < Sequencer303::maxSteps; ++i)
        {
            proc.sequencer.steps[i].gate.store (i == 0);
            Sequencer303::storePitch (proc.sequencer.steps[i], pitch);
            proc.sequencer.steps[i].hold.store (1);
        }
        proc.sequencer.length.store (lengthSteps);

        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
            proc.drumSequencer.stepMask[lane].store (0);
        proc.drumSequencer.stepMask[DrumMachine::BD].store (1u << drumStep);
    }

    SongPlayer::Step mk (int bass, int drum, int reps = 1,
                         bool bassMute = false, bool drumMute = false)
    {
        SongPlayer::Step s;
        s.bassSlot = bass;
        s.drumSlot = drum;
        s.repeats  = reps;
        s.bassMute = bassMute;
        s.drumMute = drumMute;
        return s;
    }

    void append (SongPlayer& song, const SongPlayer::Step& s)
    {
        song.insertStep (song.getCount(), s);
    }

    struct Change { double beat; int slot; int playingStep; };

    // Runs the song for a number of beats, recording every bass-slot change:
    // the beat it happened on, and which step of the new pattern was playing at
    // that moment (0 if the pattern started from its beginning).
    std::vector<Change> runRecordingBassChanges (BP303AudioProcessor& proc, double beats)
    {
        std::vector<Change> changes;
        int last = proc.getCurrentBassPattern();
        const int blocks = (int) (beats / blockBeats);

        for (int i = 0; i < blocks; ++i)
        {
            pump (proc);
            const int now = proc.getCurrentBassPattern();
            if (now != last)
            {
                changes.push_back ({ (double) (i + 1) * blockBeats, now,
                                     proc.sequencer.playingStep.load() });
                last = now;
            }
        }
        return changes;
    }

    bool nearBeat (double actual, double expected)
    {
        return std::abs (actual - expected) <= blockBeats * 1.5;
    }

    // A processor set up in Seq mode, free-running, with a three-step song.
    void prepare (BP303AudioProcessor& proc)
    {
        proc.prepareToPlay (sampleRate, blockSize);
        setP (proc, "playmode", 0.5f);   // Seq (Ext / Seq / Song -> index 1)
        setP (proc, "run", 0.0f);

        // Parameter values are set normalised, so pin the internal tempo to the
        // rate the beat arithmetic above assumes (it defaults to 130).
        if (auto* p = proc.apvts.getParameter ("intbpm"))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) bpm));

        setP (proc, "basson", 1.0f);
        setP (proc, "drumson", 1.0f);
    }
}

int main()
{
    // --- pattern selection follows the arrangement --------------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);

        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        fillSlot (proc, 2, 7, 8);
        proc.requestBassPattern (0);
        proc.requestDrumPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0, 2));   // 2 x 4 beats
        append (proc.song, mk (1, 1));      // 4 beats
        append (proc.song, mk (2, 2));      // 4 beats -> 16 beats total

        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        const auto changes = runRecordingBassChanges (proc, 15.5);

        check (changes.size() == 2, "two bass changes in the first pass");
        if (changes.size() == 2)
        {
            check (changes[0].slot == 1 && nearBeat (changes[0].beat, 8.0),
                   "slot 1 arrives after the 2 repeats of step 0");
            check (changes[1].slot == 2 && nearBeat (changes[1].beat, 12.0),
                   "slot 2 arrives at beat 12");
        }
        check (proc.getCurrentDrumPattern() == 2, "drums followed the arrangement too");
        check (proc.getSongStep() == 2, "song reports the playing step");
    }

    // --- looping ------------------------------------------------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        proc.requestBassPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0));
        append (proc.song, mk (1, 1));   // 8 beats total
        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        const auto changes = runRecordingBassChanges (proc, 17.0);
        check (changes.size() == 4, "song looped twice");
        if (changes.size() == 4)
            check (nearBeat (changes[1].beat, 8.0) && changes[1].slot == 0
                   && nearBeat (changes[2].beat, 12.0) && changes[2].slot == 1,
                   "loop wraps back to step 0 at the end");
    }

    // --- looping off: the song holds its last step, silent -------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        proc.requestBassPattern (0);
        pump (proc);

        proc.song.setLooping (false);
        append (proc.song, mk (0, 0));
        append (proc.song, mk (1, 1));   // ends at beat 8
        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        runRecordingBassChanges (proc, 7.0);
        check (proc.sequencer.playingStep.load() >= 0, "still playing before the end");

        runRecordingBassChanges (proc, 3.0);   // past beat 8
        check (proc.getCurrentBassPattern() == 1, "holds the last step's pattern");
        check (proc.sequencer.playingStep.load() == -1, "bass is silent past the end");
    }

    // --- holds keep one line while the other changes -------------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        proc.requestBassPattern (0);
        proc.requestDrumPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0));
        append (proc.song, mk (SongPlayer::hold, 1));
        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        runRecordingBassChanges (proc, 5.0);
        check (proc.getCurrentBassPattern() == 0, "held bass stayed on slot 0");
        check (proc.getCurrentDrumPattern() == 1, "drums moved to slot 1");
    }

    // --- a leading hold is silent, not leftover state ------------------------
    // Dragging only bass patterns into a song leaves every DRUM cell a hold. No
    // row ever names the drums, so there is nothing for them to carry on from:
    // they must stay silent rather than sound whichever beat happened to be
    // loaded when PLAY was pressed, which would make the song play differently
    // from run to run. Once a row does name the line, it starts normally.
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        proc.requestBassPattern (0);
        proc.requestDrumPattern (1);   // leftover beat loaded before playback
        pump (proc);

        append (proc.song, mk (0, SongPlayer::hold));   // bass named, drums never
        append (proc.song, mk (0, 1));                  // ... until here
        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        runRecordingBassChanges (proc, 2.0);
        check (proc.sequencer.playingStep.load() >= 0,
               "the named bass line plays under a leading hold");
        check (proc.drumSequencer.playingStep.load() == -1,
               "a leading hold leaves the drums silent, not on leftover state");

        runRecordingBassChanges (proc, 4.0);
        check (proc.drumSequencer.playingStep.load() >= 0,
               "the drums start once a row names them");
        check (proc.getCurrentDrumPattern() == 1, "and on the slot that row named");
    }

    // --- per-step mutes ------------------------------------------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        proc.requestBassPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0));                       // both lines
        append (proc.song, mk (0, 0, 1, true, false));       // bass dropped
        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        runRecordingBassChanges (proc, 2.0);
        check (proc.sequencer.playingStep.load() >= 0, "bass plays on the unmuted step");

        runRecordingBassChanges (proc, 4.0);
        check (proc.sequencer.playingStep.load() == -1, "bass is muted on the muted step");
        check (proc.getCurrentBassPattern() == 0, "a muted line keeps its pattern loaded");
    }

    // --- a shorter pattern starts from its own step 0 ------------------------
    // Step 0 is a 16-step pattern (4 beats); step 1 is an 8-step pattern, so it
    // begins at beat 4 — halfway through a bar. Without a phase origin it would
    // be entered at its step 4 instead of its step 0.
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0, 16);
        fillSlot (proc, 1, 5, 0, 8);
        proc.requestBassPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0));
        append (proc.song, mk (1, 1, 3));   // 3 x 2 beats, so the song is 10 long
        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        const auto changes = runRecordingBassChanges (proc, 4.2);
        check (changes.size() == 1 && changes[0].slot == 1
               && nearBeat (changes[0].beat, 4.0),
               "the 8-step pattern arrives at beat 4");
        check (proc.sequencer.length.load() == 8, "its length came with it");
        check (! changes.empty() && changes[0].playingStep == 0,
               "the short pattern starts at its own step 0");

        // ...and it loops every 2 beats within its step rather than the switch
        // re-firing or the song drifting.
        const auto more = runRecordingBassChanges (proc, 2.0);
        check (more.empty(), "the short pattern loops within its step");
    }

    // --- row-jump moves the playhead, and lands at once ----------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        fillSlot (proc, 2, 7, 8);
        proc.requestBassPattern (0);
        proc.requestDrumPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0));
        append (proc.song, mk (1, 1));
        append (proc.song, mk (2, 2));
        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();

        runRecordingBassChanges (proc, 1.0);   // a beat into step 0
        check (proc.getSongStep() == 0, "playing step 0 before the jump");
        check (! proc.isHostSynced(), "free-running, so a jump applies");

        proc.jumpSongToStep (2);
        pump (proc, 2);
        check (proc.getSongStep() == 2, "jumped to step 2");
        check (proc.getCurrentBassPattern() == 2 && proc.getCurrentDrumPattern() == 2,
               "the jump's patterns land immediately, not at the next boundary");

        // and the song carries on from there: step 2 is last, so it wraps to 0
        const auto changes = runRecordingBassChanges (proc, 4.2);
        check (changes.size() == 1 && changes[0].slot == 0,
               "playback continues from the jumped-to step");
    }

    // --- selecting Song mode does not start playback -------------------------
    // RUN may well be on from pattern work; entering song mode must not inherit
    // it, and leaving song mode must drop the song's own transport.
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        proc.requestBassPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0));
        append (proc.song, mk (1, 1));

        setP (proc, "run", 1.0f);          // pattern work, transport rolling
        runRecordingBassChanges (proc, 5.0);
        check (proc.getCurrentBassPattern() == 0, "seq mode ignores the arrangement");

        setP (proc, "playmode", 1.0f);     // now select Song, with RUN still on
        runRecordingBassChanges (proc, 9.0);
        check (! proc.isSongPlaying(), "selecting song mode leaves it stopped");
        check (proc.getSongStep() == 0, "and parked at the top of the song");
        check (proc.getCurrentBassPattern() == 0, "no arrangement changes without PLAY");
        check (proc.sequencer.playingStep.load() == -1, "nothing sounds until PLAY");

        proc.startSong();
        runRecordingBassChanges (proc, 5.0);
        check (proc.getSongStep() == 1, "PLAY starts the song");

        // back to Seq: the song's transport drops, RUN drives the pattern again
        setP (proc, "playmode", 0.5f);
        pump (proc, 2);
        check (! proc.isSongPlaying(), "leaving song mode stops the song");
        check (proc.sequencer.playingStep.load() >= 0, "and RUN plays the pattern again");

        setP (proc, "playmode", 1.0f);
        pump (proc, 2);
        check (! proc.isSongPlaying(), "returning to song mode is still stopped");
    }

    // --- cueing while stopped -------------------------------------------------
    // The transport's FF/RW work with the transport stopped, so a step can be
    // cued up and played from. STOP is what returns to the top.
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        proc.requestBassPattern (0);
        pump (proc);

        append (proc.song, mk (0, 0));
        append (proc.song, mk (1, 1));
        setP (proc, "playmode", 1.0f);   // Song

        proc.jumpSongToStep (1);
        pump (proc);
        check (proc.getSongStep() == 1, "a cued step shows while stopped");
        check (proc.getCurrentBassPattern() == 1,
               "and its patterns are pre-loaded, ready to play");

        proc.startSong();
        pump (proc, 2);
        check (proc.getSongStep() == 1, "starting plays from the cued step");

        // STOP: back to the top, and starting again begins at step 0
        proc.stopSong();
        pump (proc);
        check (proc.getSongStep() == 0, "stop returns to the top of the song");

        proc.startSong();
        pump (proc, 2);
        check (proc.getSongStep() == 0, "and playing again starts from step 0");
    }

    // --- song off leaves manual pattern control alone ------------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        proc.requestBassPattern (0);
        pump (proc);

        append (proc.song, mk (1, 1));
        setP (proc, "playmode", 0.5f);   // Seq: no song
        setP (proc, "run", 1.0f);

        runRecordingBassChanges (proc, 6.0);
        check (proc.getCurrentBassPattern() == 0, "song off: the arrangement is ignored");
        check (proc.getSongStep() == -1, "song off: no playing step reported");
        check (proc.sequencer.phaseOrigin.load() == 0.0,
               "song off: patterns stay locked to the bar grid");
    }

    // --- an empty song is inert ---------------------------------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        proc.requestBassPattern (0);
        pump (proc);

        setP (proc, "playmode", 1.0f);   // Song
        proc.startSong();
        runRecordingBassChanges (proc, 5.0);
        check (proc.getCurrentBassPattern() == 0, "empty song leaves the pattern alone");
        check (proc.getSongStep() == -1, "empty song reports no step");
        check (proc.sequencer.playingStep.load() >= 0, "empty song still plays");
    }

    // --- the arrangement survives a state round-trip -------------------------
    {
        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 0, 0);
        fillSlot (proc, 1, 5, 4);
        fillSlot (proc, 2, 7, 8);
        proc.requestBassPattern (0);
        proc.requestDrumPattern (0);
        pump (proc);

        proc.song.setLooping (false);
        append (proc.song, mk (0, 0, 4, true, false));
        append (proc.song, mk (SongPlayer::hold, 1, 1, false, true));
        append (proc.song, mk (2, 2, 16));
        setP (proc, "playmode", 1.0f);   // Song

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        BP303AudioProcessor proc2;
        proc2.prepareToPlay (sampleRate, blockSize);
        proc2.setStateInformation (state.getData(), (int) state.getSize());

        check (proc2.song.getCount() == 3, "step count restored");
        check (! proc2.song.isLooping(), "loop flag restored");

        const auto a = proc2.song.getStep (0);
        check (a.bassSlot == 0 && a.drumSlot == 0 && a.repeats == 4
               && a.bassMute && ! a.drumMute,
               "step 0 restored, mute and all");

        const auto b = proc2.song.getStep (1);
        check (b.bassSlot == SongPlayer::hold && b.drumSlot == 1 && ! b.bassMute
               && b.drumMute,
               "a held slot survives as a hold, not as slot 0");

        const auto c = proc2.song.getStep (2);
        check (c.bassSlot == 2 && c.drumSlot == 2 && c.repeats == 16, "step 2 restored");

        check (proc2.apvts.getRawParameterValue ("playmode")->load() >= 1.5f,
               "song mode is the play-mode parameter, so it restores too");

        // and it plays: 4 repeats of step 0 then the held step
        if (auto* p = proc2.apvts.getParameter ("intbpm"))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) bpm));
        proc2.startSong();
        const auto changes = runRecordingBassChanges (proc2, 17.0);
        check (proc2.getSongStep() == 1, "the restored song plays from the top");
        check (changes.empty(), "a held bass slot means no pattern change at step 1");
    }

    // --- a state with no song in it clears whatever was loaded ---------------
    {
        BP303AudioProcessor plain;
        plain.prepareToPlay (sampleRate, blockSize);
        juce::MemoryBlock state;
        plain.getStateInformation (state);

        BP303AudioProcessor proc;
        prepare (proc);
        append (proc.song, mk (0, 0));
        append (proc.song, mk (1, 1));
        proc.song.setLooping (false);

        proc.setStateInformation (state.getData(), (int) state.getSize());
        check (proc.song.getCount() == 0, "loading a song-less state empties the song");
        check (proc.song.isLooping(), "and restores the default loop flag");
    }

    // --- song files are self-contained --------------------------------------
    // The point of embedding patterns: a song saved from one instance must play
    // the same in another instance whose banks hold completely different
    // patterns, because slot numbers alone mean nothing.
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("BP303SongTest")
                        .getChildFile (juce::String ("roundtrip")
                                       + BP303AudioProcessor::songFileSuffix);
        file.deleteFile();

        BP303AudioProcessor proc;
        prepare (proc);
        fillSlot (proc, 0, 3, 0);     // bass pitch 3, kick on step 0
        fillSlot (proc, 5, 9, 4, 8);  // pitch 9, kick on step 4, 8 steps long
        proc.requestBassPattern (0);
        proc.requestDrumPattern (0);
        pump (proc);

        proc.song.setLooping (false);
        append (proc.song, mk (0, 0, 2));
        append (proc.song, mk (5, 5, 3, false, true));

        check (proc.saveSongToFile (file), "song file written");
        check (file.existsAsFile(), "song file is on disk");
        check (proc.getSongName() == "roundtrip", "saving names the song after the file");

        // A fresh instance with unrelated bank contents.
        BP303AudioProcessor proc2;
        prepare (proc2);
        fillSlot (proc2, 0, -5, 12);
        fillSlot (proc2, 5, -7, 15);
        proc2.requestBassPattern (0);
        pump (proc2);

        check (proc2.loadSongFromFile (file), "song file read back");
        check (proc2.song.getCount() == 2, "arrangement restored from the file");
        check (! proc2.song.isLooping(), "loop flag restored from the file");
        check (proc2.song.getStep (1).drumMute, "step mutes restored from the file");
        check (proc2.getSongName() == "roundtrip", "song name follows the file");

        // The referenced patterns came with it, replacing proc2's own.
        check (proc2.slotLengthSteps (5) == 8, "an embedded pattern's length arrived");
        check (Sequencer303::loadPitch (proc2.sequencer.steps[0]) == 3,
               "the live sequencer picked up the reloaded current slot");

        proc2.requestBassPattern (5);
        pump (proc2);
        check (Sequencer303::loadPitch (proc2.sequencer.steps[0]) == 9,
               "the other embedded pattern replaced the local one too");
        check (proc2.drumSequencer.stepMask[DrumMachine::BD].load() == (1u << 4),
               "embedded drum patterns arrived as well");

        // A file that isn't ours is refused rather than half-loaded.
        auto junk = file.getSiblingFile ("junk.bp303song");
        junk.replaceWithText ("<NotASong/>");
        check (! proc2.loadSongFromFile (junk), "a foreign file is rejected");
        check (proc2.song.getCount() == 2, "and the loaded song is left alone");

        file.getParentDirectory().deleteRecursively();
    }

    std::printf (failures == 0 ? "SONG-PLAY-TEST OK\n" : "SONG-PLAY-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
