#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DrumMachine.h"
#include "DrumSequencer.h"
#include "Sequencer303.h"

// Renders the live patterns to standard MIDI files so they can be dragged into
// the host. The timing mirrors the sequencers exactly (shuffle, hold, slide and
// dynamics), and the note/velocity conventions match what the plugin itself
// reads back in, so a dragged-out region re-imported onto a BP303 track plays
// the same pattern: velocities on both lines come straight from dyn303's soft /
// normal / accented mapping, overlapping bass notes are a slide, and drums are
// GM notes on channel 10.
namespace bp303
{
    inline constexpr int ticksPerQuarter = 960;
    inline constexpr int ticksPer16th    = ticksPerQuarter / 4;


    // GM drum notes per DrumMachine voice (BD, SD, CP, CH, OH), matching the
    // channel-10 mapping the processor accepts.
    inline constexpr int gmDrumNote[DrumMachine::numVoices] = { 36, 38, 39, 42, 46 };

    // The sequencers delay odd steps by shuffle * step / 3.
    inline double shuffleOffsetTicks (int step, float shuffle)
    {
        return (step & 1) ? (double) shuffle * (double) ticksPer16th / 3.0 : 0.0;
    }

    inline juce::MidiMessageSequence bassSequence (const Sequencer303& seq, float shuffle)
    {
        juce::MidiMessageSequence s;
        const int len = juce::jlimit (1, Sequencer303::maxSteps, seq.length.load());
        const double patternEnd = (double) len * ticksPer16th;

        // Walk the steps the same greedy way playback does: a gated step owns the
        // next hold-1 steps, and an owned step never starts a note of its own.
        for (int step = 0, covered = 0; step < len; ++step)
        {
            if (covered > 0)
            {
                --covered;
                continue;
            }

            const auto& st = seq.steps[step];
            if (! st.gate.load())
                continue;

            const int hold = juce::jlimit (1, len - step, st.hold.load());
            covered = hold - 1;

            const int note = Sequencer303::baseNote + Sequencer303::loadPitch (st);
            const int vel  = dyn303::velocityForDyn (st.dyn.load());
            const double start = (double) step * ticksPer16th + shuffleOffsetTicks (step, shuffle);

            // A slid note ties into the next step (kept overlapping so it reads
            // back as a slide); otherwise it releases partway through its last
            // step, like the sequencer's own gate.
            const int lastStep = step + hold - 1;
            double end;
            if (st.slide.load())
            {
                const int next = lastStep + 1;
                end = (double) next * ticksPer16th + shuffleOffsetTicks (next, shuffle)
                        + (double) ticksPer16th / 8.0;
            }
            else
            {
                end = (double) lastStep * ticksPer16th + shuffleOffsetTicks (lastStep, shuffle)
                        + (double) ticksPer16th * 0.55;
            }
            end = juce::jmin (end, patternEnd);

            if (end > start)
            {
                s.addEvent (juce::MidiMessage::noteOn  (1, note, (juce::uint8) vel), start);
                s.addEvent (juce::MidiMessage::noteOff (1, note), end);
            }
        }

        s.updateMatchedPairs();
        return s;
    }

    inline juce::MidiMessageSequence drumSequence (const DrumSequencer& drums, int len, float shuffle)
    {
        juce::MidiMessageSequence s;
        len = juce::jlimit (1, DrumSequencer::maxSteps, len);

        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            const uint32_t hits    = drums.stepMask[lane].load();


            for (int step = 0; step < len; ++step)
            {
                const uint32_t bit = 1u << step;
                if ((hits & bit) == 0)
                    continue;

                const int note = gmDrumNote[lane];
                const int vel  = dyn303::velocityForDyn (drums.dynAt (lane, step));
                const double start = (double) step * ticksPer16th + shuffleOffsetTicks (step, shuffle);

                // Drum voices are one-shots, so the gate length is nominal.
                s.addEvent (juce::MidiMessage::noteOn  (10, note, (juce::uint8) vel), start);
                s.addEvent (juce::MidiMessage::noteOff (10, note), start + ticksPer16th * 0.5);
            }
        }

        s.updateMatchedPairs();
        return s;
    }

    // Copies one sequence onto the end of another at a tick offset. Used to lay
    // a song out: each row's patterns are built once by the same exporters that
    // serve a single-pattern drag, then stamped down the timeline at the tick the
    // row starts on, so an arrangement and a pattern can never disagree about
    // how a pattern is voiced.
    inline void appendAt (juce::MidiMessageSequence& dest,
                          const juce::MidiMessageSequence& src, double tickOffset)
    {
        for (int i = 0; i < src.getNumEvents(); ++i)
        {
            auto message = src.getEventPointer (i)->message;
            message.setTimeStamp (message.getTimeStamp() + tickOffset);
            dest.addEvent (message);
        }
    }

    // Writes the given tracks to a type-0/1 MIDI file. No tempo event is written,
    // so the region simply follows the host's project tempo.
    inline bool writeMidiFile (const juce::File& file,
                               const std::vector<juce::MidiMessageSequence>& tracks,
                               int lengthSteps)
    {
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ticksPerQuarter);

        const double endTick = (double) juce::jmax (1, lengthSteps) * ticksPer16th;
        for (auto track : tracks)
        {
            // Pad to the full pattern so the host sizes the region to the loop.
            track.addEvent (juce::MidiMessage::endOfTrack(), endTick);
            mf.addTrack (track);
        }

        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk())
            return false;

        return mf.writeTo (out);
    }
}
