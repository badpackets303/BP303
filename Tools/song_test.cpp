// Offline test for the song chain (SongPlayer). Verifies that song position is
// a pure function of transport phase: step boundaries land where the pattern
// lengths say they should, holds carry the previous slot (and its length),
// repeats and looping wrap correctly, and arranging edits behave.
// JUCE-free — built as a console app target (see CMakeLists.txt).

#include "../Source/SongPlayer.h"

#include <cstdio>

namespace
{
    int failures = 0;
    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    bool nearly (double a, double b) { return std::abs (a - b) < 1.0e-9; }

    // Pattern lengths per bass slot: slot 0 = 16 steps (4 beats), slot 1 = 8
    // steps (2 beats), slot 2 = 4 steps (1 beat), everything else 16.
    int slotLen (int slot)
    {
        switch (slot)
        {
            case 1:  return 8;
            case 2:  return 4;
            default: return 16;
        }
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
}

int main()
{
    // --- empty song ---------------------------------------------------------
    {
        SongPlayer song;
        check (song.getCount() == 0, "new song is empty");
        check (song.locate (0.0, slotLen).stepIndex == -1, "empty song locates nowhere");
        check (nearly (song.totalBeats (slotLen), 0.0), "empty song has zero length");
    }

    // --- a plain chain: three 16-step patterns, one repeat each --------------
    {
        SongPlayer song;
        append (song, mk (0, 0));
        append (song, mk (3, 1));
        append (song, mk (5, 2));

        check (song.getCount() == 3, "three steps appended");
        check (nearly (song.totalBeats (slotLen), 12.0), "3 x 4 beats = 12 beats");

        auto p = song.locate (0.0, slotLen);
        check (p.stepIndex == 0 && p.bassSlot == 0 && p.drumSlot == 0, "phase 0 -> step 0");

        p = song.locate (3.999, slotLen);
        check (p.stepIndex == 0, "just before the boundary is still step 0");

        p = song.locate (4.0, slotLen);
        check (p.stepIndex == 1 && p.bassSlot == 3 && p.drumSlot == 1, "phase 4 -> step 1");

        p = song.locate (8.0, slotLen);
        check (p.stepIndex == 2 && p.bassSlot == 5 && p.drumSlot == 2, "phase 8 -> step 2");

        p = song.locate (9.5, slotLen);
        check (p.stepIndex == 2 && nearly (p.beatsIntoRepeat, 1.5), "beats into the repeat");

        // startBeats feeds the UI's row-jump
        check (nearly (song.startBeats (0, slotLen), 0.0)
               && nearly (song.startBeats (1, slotLen), 4.0)
               && nearly (song.startBeats (2, slotLen), 8.0),
               "startBeats matches the boundaries");
        check (nearly (song.startBeats (3, slotLen), 12.0), "startBeats past the end = total");
    }

    // --- repeats ------------------------------------------------------------
    {
        SongPlayer song;
        append (song, mk (0, 0, 4));   // 4 x 4 beats = 16
        append (song, mk (1, 1, 2));   // slot 1 is 8 steps -> 2 x 2 beats = 4

        check (nearly (song.totalBeats (slotLen), 20.0), "repeats extend the song");

        auto p = song.locate (0.0, slotLen);
        check (p.stepIndex == 0 && p.repeatIndex == 0, "first repeat");
        p = song.locate (4.0, slotLen);
        check (p.stepIndex == 0 && p.repeatIndex == 1, "second repeat, same step");
        p = song.locate (12.5, slotLen);
        check (p.stepIndex == 0 && p.repeatIndex == 3 && nearly (p.beatsIntoRepeat, 0.5),
               "fourth repeat");
        p = song.locate (16.0, slotLen);
        check (p.stepIndex == 1 && p.repeatIndex == 0, "repeats exhausted -> next step");
        p = song.locate (18.0, slotLen);
        check (p.stepIndex == 1 && p.repeatIndex == 1, "short pattern's second repeat");
    }

    // --- a short pattern shortens its step ----------------------------------
    {
        SongPlayer song;
        append (song, mk (2, 0));      // slot 2 is 4 steps -> 1 beat
        append (song, mk (0, 0));      // 4 beats

        check (nearly (song.totalBeats (slotLen), 5.0), "step length follows the bass pattern");
        check (song.locate (0.5, slotLen).stepIndex == 0, "inside the short step");
        check (song.locate (1.0, slotLen).stepIndex == 1, "short step ends after one beat");
    }

    // --- holds --------------------------------------------------------------
    {
        SongPlayer song;
        append (song, mk (4, 0));                          // bass 4, drums 0
        append (song, mk (SongPlayer::hold, 1));           // same bass, drums 1
        append (song, mk (6, SongPlayer::hold));           // bass 6, drums still 1

        auto p = song.locate (4.0, slotLen);
        check (p.stepIndex == 1 && p.bassSlot == 4 && p.drumSlot == 1,
               "held bass carries the previous slot");
        p = song.locate (8.0, slotLen);
        check (p.stepIndex == 2 && p.bassSlot == 6 && p.drumSlot == 1,
               "held drums carry the previous slot");
    }

    // --- a hold takes its length from the pattern it holds -------------------
    {
        SongPlayer song;
        append (song, mk (1, 0));                    // slot 1 = 8 steps -> 2 beats
        append (song, mk (SongPlayer::hold, 1));     // holds slot 1, so also 2 beats

        check (nearly (song.totalBeats (slotLen), 4.0), "hold inherits the held length");
        check (song.locate (2.0, slotLen).stepIndex == 1, "hold starts at 2 beats");
    }

    // --- a leading hold falls back to the live length ------------------------
    {
        SongPlayer song;
        append (song, mk (SongPlayer::hold, SongPlayer::hold));
        append (song, mk (0, 0));

        auto p = song.locate (0.0, slotLen, 8);
        check (p.stepIndex == 0 && p.bassSlot == SongPlayer::hold
               && p.drumSlot == SongPlayer::hold,
               "leading hold leaves both lines alone");
        check (nearly (song.totalBeats (slotLen, 8), 6.0), "leading hold uses the fallback length");
        check (song.locate (2.0, slotLen, 8).stepIndex == 1, "fallback-length step ends at 2 beats");
    }

    // --- mutes are carried through ------------------------------------------
    {
        SongPlayer song;
        append (song, mk (0, 0, 1, true, false));
        append (song, mk (0, 0, 1, false, true));

        auto p = song.locate (0.0, slotLen);
        check (p.bassMute && ! p.drumMute, "bass mute on step 0");
        p = song.locate (4.0, slotLen);
        check (! p.bassMute && p.drumMute, "drum mute on step 1");
    }

    // --- looping ------------------------------------------------------------
    {
        SongPlayer song;
        append (song, mk (0, 0));      // 0..4
        append (song, mk (1, 1));      // 4..6  (slot 1 = 2 beats)
        // total 6 beats
        check (song.isLooping(), "songs loop by default");

        auto p = song.locate (6.0, slotLen);
        check (p.stepIndex == 0 && ! p.finished, "phase == total wraps to the start");
        p = song.locate (10.0, slotLen);
        check (p.stepIndex == 1 && nearly (p.beatsIntoRepeat, 0.0), "wraps into step 1");
        p = song.locate (60.0, slotLen);
        check (p.stepIndex == 0, "many loops later, still correct");

        // A host that reports a phase before the song's start (loop point ahead
        // of the transport) must still land somewhere sensible.
        p = song.locate (-2.0, slotLen);   // 6 - 2 = 4 beats in, the start of step 1
        check (p.stepIndex == 1 && nearly (p.beatsIntoRepeat, 0.0), "negative phase wraps");
    }

    // --- looping off: hold the final step ------------------------------------
    {
        SongPlayer song;
        song.setLooping (false);
        append (song, mk (0, 0));
        append (song, mk (7, 3, 2));   // 2 repeats x 4 beats, ends at 12

        auto p = song.locate (11.0, slotLen);
        check (p.stepIndex == 1 && ! p.finished, "still inside the last step");

        p = song.locate (12.0, slotLen);
        check (p.stepIndex == 1 && p.finished && p.bassSlot == 7 && p.drumSlot == 3,
               "past the end -> finished, holding the last step");
        check (p.repeatIndex == 1, "finished on the final repeat");

        p = song.locate (500.0, slotLen);
        check (p.stepIndex == 1 && p.finished, "far past the end stays finished");

        p = song.locate (-5.0, slotLen);
        check (p.stepIndex == 0 && ! p.finished, "negative phase clamps to the start");
    }

    // --- arranging: insert, remove, clear ------------------------------------
    {
        SongPlayer song;
        append (song, mk (0, 0));
        append (song, mk (1, 1));
        append (song, mk (2, 2));

        song.insertStep (1, mk (9, 9));
        check (song.getCount() == 4, "insert grew the song");
        check (song.getStep (0).bassSlot == 0 && song.getStep (1).bassSlot == 9
               && song.getStep (2).bassSlot == 1 && song.getStep (3).bassSlot == 2,
               "insert shifted the tail down");

        song.removeStep (1);
        check (song.getCount() == 3, "remove shrank the song");
        check (song.getStep (0).bassSlot == 0 && song.getStep (1).bassSlot == 1
               && song.getStep (2).bassSlot == 2,
               "remove closed the gap");

        song.setStep (1, mk (4, 5, 3));
        const auto s = song.getStep (1);
        check (s.bassSlot == 4 && s.drumSlot == 5 && s.repeats == 3, "setStep replaced a row");

        // out-of-range access is a no-op, not a crash
        song.removeStep (99);
        song.setStep (-1, mk (0, 0));
        check (song.getCount() == 3 && song.getStep (99).bassSlot == 0,
               "out-of-range edits are ignored");

        song.clear();
        check (song.getCount() == 0, "clear empties the song");
    }

    // --- clamping and capacity ------------------------------------------------
    {
        SongPlayer song;
        append (song, mk (0, 0, 999));
        check (song.getStep (0).repeats == SongPlayer::maxRepeats, "repeats clamp to the max");

        song.setStep (0, mk (0, 0, 0));
        check (song.getStep (0).repeats == 1, "repeats clamp to at least one");

        song.clear();
        for (int i = 0; i < SongPlayer::maxSteps + 10; ++i)
            append (song, mk (0, 0));
        check (song.getCount() == SongPlayer::maxSteps, "song fills to capacity and stops");
        check (song.insertStep (0, mk (1, 1)) == -1, "insert into a full song is refused");
    }

    std::printf (failures == 0 ? "SONG-TEST OK\n" : "SONG-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
