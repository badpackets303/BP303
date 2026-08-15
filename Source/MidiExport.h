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

    // The sequencers delay odd steps by shuffle * step / 3. The span is the
    // step's own length, which is a sixteenth everywhere except on a drum lane
    // fitted to the bar — there the swing rides that lane's clock, as it does in
    // DrumSequencer::onTime.
    inline double shuffleOffsetTicks (int step, float shuffle,
                                      double span = (double) ticksPer16th)
    {
        return (step & 1) ? (double) shuffle * span / 3.0 : 0.0;
    }

    inline juce::MidiMessageSequence bassSequence (const Sequencer303& seq, float shuffle)
    {
        juce::MidiMessageSequence s;

        // The bar, then the line's own cycle within it — the same split the drum
        // lanes have. A region is one bar long whatever the line runs, so a short
        // line repeats *inside* it and a fitted one spreads across it.
        const int barLen = juce::jlimit (1, Sequencer303::maxSteps, seq.length.load());
        const int lineLen = seq.lengthOf (barLen);
        const bool fit = seq.patternFit.load();
        const double span = fit ? (double) ticksPer16th * (double) barLen / (double) lineLen
                                : (double) ticksPer16th;
        const int emitted = fit ? lineLen : barLen;
        const double patternEnd = (double) barLen * ticksPer16th;

        // Walk the positions the same greedy way playback does: a gated step owns
        // the next hold-1 of them, and an owned one never starts a note of itself.
        //
        // Position and step come apart once the line runs short: `i` is where the
        // note goes in the bar, `step` is which of the line's steps it is. Swing
        // parity comes off `step`, because the sequencer takes it off its own
        // wrapped index — so an odd-length line's swing flips each cycle here
        // exactly as it does there.
        for (int i = 0, covered = 0; i < emitted; ++i)
        {
            if (covered > 0)
            {
                --covered;
                continue;
            }

            const int step = i % lineLen;
            const auto& st = seq.steps[step];
            if (! st.gate.load())
                continue;

            const int hold = juce::jlimit (1, emitted - i, st.hold.load());
            covered = hold - 1;

            const int note = Sequencer303::baseNote + Sequencer303::loadPitch (st);
            const int vel  = dyn303::velocityForDyn (st.dyn.load());
            const double tOn = shuffleOffsetTicks (step, shuffle, span);
            double start = (double) i * span + tOn;

            // A slid note ties into the next step (kept overlapping so it reads
            // back as a slide); otherwise it releases partway through its last
            // step, like the sequencer's own gate.
            const int lastPos = i + hold - 1;
            double end;
            if (st.slide.load())
            {
                end = (double) (lastPos + 1) * span
                        + shuffleOffsetTicks ((lastPos + 1) % lineLen, shuffle, span)
                        + span / 8.0;
            }
            else
            {
                end = (double) lastPos * span
                        + shuffleOffsetTicks (lastPos % lineLen, shuffle, span)
                        + span * 0.55;
            }

            // A split gate retriggers inside its own slot, so the repeats before
            // the last are short notes spaced through what the swing left of the
            // step, and the last one starts late enough that the plain gate's end
            // would fall *before* it. Hold and slide still carry it further, so
            // the later of the two wins rather than the split simply overriding.
            const int count = seq.ratchetAt (step);
            if (count > 1)
            {
                const double spacing = (span - tOn) / (double) count;

                for (int r = 0; r < count - 1; ++r)
                {
                    const double at = start + (double) r * spacing;
                    s.addEvent (juce::MidiMessage::noteOn  (1, note, (juce::uint8) vel), at);
                    s.addEvent (juce::MidiMessage::noteOff (1, note), at + spacing * 0.55);
                }
                start += (double) (count - 1) * spacing;
                end = juce::jmax (end, start + spacing * 0.55);
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
            const uint32_t hits = drums.stepMask[lane].load();

            // A lane with a clock of its own has to be rendered on that clock or
            // the region is not the pattern: a short lane repeats *inside* the
            // bar rather than stopping at its own end, and a fitted lane's steps
            // are not sixteenths at all. Iterating the bar and taking the lane
            // step as a modulo is what covers both — and is why the swing parity
            // comes off the position in the bar rather than off the mask index.
            const int laneLen = drums.lengthOf (lane, len);
            const bool fit = drums.laneFit[lane].load();
            const double span = fit ? (double) ticksPer16th * (double) len / (double) laneLen
                                    : (double) ticksPer16th;
            const int emitted = fit ? laneLen : len;

            for (int i = 0; i < emitted; ++i)
            {
                const int step = i % laneLen;
                if ((hits & (1u << step)) == 0)
                    continue;

                const int note = gmDrumNote[lane];
                const int vel  = dyn303::velocityForDyn (drums.dynAt (lane, step));
                const double start = (double) i * span + shuffleOffsetTicks (i, shuffle, span);

                // Ratchets are repeats inside the step, spaced through what the
                // swing left of it, exactly as the sequencer fires them.
                const int count = drums.ratchetAt (lane, step);
                const double spacing = (span - shuffleOffsetTicks (i, shuffle, span))
                                         / (double) count;

                for (int r = 0; r < count; ++r)
                {
                    // Drum voices are one-shots, so the gate length is nominal.
                    const double at = start + (double) r * spacing;
                    s.addEvent (juce::MidiMessage::noteOn  (10, note, (juce::uint8) vel), at);
                    s.addEvent (juce::MidiMessage::noteOff (10, note), at + spacing * 0.5);
                }
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

    inline bool hasNotes (const juce::MidiMessageSequence& s)
    {
        for (int i = 0; i < s.getNumEvents(); ++i)
            if (s.getEventPointer (i)->message.isNoteOn())
                return true;
        return false;
    }

    // Writes the given tracks to a type-0/1 MIDI file. No tempo event is written,
    // so the region simply follows the host's project tempo.
    inline bool writeMidiFile (const juce::File& file,
                               const std::vector<juce::MidiMessageSequence>& tracks,
                               int lengthSteps)
    {
        // Nothing to say, so say nothing. A noteless MIDI file is valid, and the
        // drag used to succeed with one — but a host makes no region from it, so
        // the drop just failed silently and looked like a bug in the drag. Two
        // ways in: a pattern with no gated steps, and a pattern whose notes all
        // sit past its own LENGTH, which still looks full on the 16-step grid.
        bool anyNotes = false;
        for (const auto& t : tracks)
            anyNotes = anyNotes || hasNotes (t);

        if (! anyNotes)
            return false;

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
