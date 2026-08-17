// HOLD: the latching live-write mode in the BASS PATTERN strip.
//
// Armed, a note held down — on the on-screen keyboard or over MIDI — is written
// at the playhead and sustained over each step the sequencer reaches,
// overwriting what it runs across, until it is released.
//
// The keyboard cases drive the real editor and move the playhead by hand, so the
// hit-testing and the write path are the ones a user gets and each step lands
// exactly where the test says. The MIDI cases run the whole plugin instead —
// real transport, real blocks, no editor — since that is the only way to prove
// note-offs arrive and that HOLD doesn't quietly depend on REC.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>

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

    juce::MouseEvent eventAt (juce::Component& c, juce::Point<int> pos)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 pos.toFloat(), juce::ModifierKeys::leftButtonModifier,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &c, &c, juce::Time::getCurrentTime(),
                 pos.toFloat(), juce::Time::getCurrentTime(), 1, false };
    }

    // The white keys start at the bottom-left of the keyboard region, one per
    // 1/15th of the grid's width. Pressing low down the key avoids the black
    // keys, which are hit-tested first over the top of it.
    juce::Point<int> whiteKeyPoint (StepGrid& grid, int whiteIndex)
    {
        const int labelW = grid.ledCellBounds (0).getX();
        const int kbTop  = grid.ledCellBounds (0).getBottom() + 20 + 22 + 15 + 15;
        const float whiteW = (float) (grid.getWidth() - labelW) / 15.0f;
        return { labelW + (int) ((float) whiteIndex * whiteW + whiteW * 0.5f),
                 kbTop + (grid.getHeight() - kbTop) * 3 / 4 };
    }

    // The write head runs on the audio thread, so a press is only picked up on
    // the next block. These helpers pump it once, standing in for that block.
    void pumpLatch (BP303AudioProcessor& proc)
    {
        proc.updateHoldLatch (proc.sequencer.playingStep.load());
    }

    void pressKey (BP303AudioProcessor& proc, StepGrid& grid, int whiteIndex)
    {
        grid.mouseDown (eventAt (grid, whiteKeyPoint (grid, whiteIndex)));
        pumpLatch (proc);
    }

    void releaseKey (BP303AudioProcessor& proc, StepGrid& grid)
    {
        grid.mouseUp (eventAt (grid, whiteKeyPoint (grid, 0)));
        pumpLatch (proc);
    }

    // Move the playhead one step on and let the write head follow it, the way a
    // block of audio does while the sequencer is running.
    void stepPlayheadTo (BP303AudioProcessor& proc, int step)
    {
        proc.sequencer.playingStep.store (step);
        proc.updateHoldLatch (step);
    }

    void setParam (BP303AudioProcessor& proc, const juce::String& id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // Render blocks until the sequencer reaches `target`, feeding `midi` into the
    // first one only — a MIDI buffer is consumed by the block it arrives in.
    // Returns false if it never got there.
    bool renderUntilStep (BP303AudioProcessor& proc, int target, juce::MidiBuffer midi)
    {
        juce::AudioBuffer<float> buf (2, 256);
        for (int i = 0; i < 4000; ++i)
        {
            buf.clear();
            proc.processBlock (buf, midi);
            midi.clear();
            if (proc.sequencer.playingStep.load() == target)
                return true;
        }
        return false;
    }

    juce::MidiBuffer noteOn (int note, int channel = 1)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) 100), 0);
        return m;
    }

    juce::MidiBuffer noteOff (int note, int channel = 1)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOff (channel, note), 0);
        return m;
    }

    juce::TextButton* findButton (juce::Component& root, const juce::String& text)
    {
        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            auto* child = root.getChildComponent (i);
            if (auto* b = dynamic_cast<juce::TextButton*> (child))
                if (b->getButtonText() == text)
                    return b;
            if (auto* found = findButton (*child, text))
                return found;
        }
        return nullptr;
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

    void clearPattern (Sequencer303& seq, int length)
    {
        for (auto& s : seq.steps)
        {
            s.gate.store (false);
            s.hold.store (1);
            s.slide.store (false);
            s.dyn.store (dyn303::Normal);
            s.key.store (0);
            s.octave.store (0);
        }
        seq.length.store (length);
        seq.playingStep.store (-1);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* grid = findDescendant<StepGrid> (*ed);
    check (grid != nullptr, "the step grid is in the editor");
    if (grid == nullptr)
        return 1;

    auto& seq = proc.sequencer;

    // --- disarmed, the keyboard behaves exactly as it always did ---------------
    {
        clearPattern (seq, 16);
        grid->setHoldLatch (false);
        check (! grid->isHoldArmed(), "HOLD starts disarmed");

        seq.playingStep.store (9);      // running, but it should be ignored
        pressKey (proc, *grid, 0);
        releaseKey (proc, *grid);

        check (seq.steps[0].gate.load() && ! seq.steps[9].gate.load(),
               "disarmed, a key still writes at the cursor and not at the playhead");
    }

    // --- armed, a held key writes at the playhead and grows with it ------------
    {
        clearPattern (seq, 16);
        grid->setHoldLatch (true);
        check (grid->isHoldArmed(), "HOLD arms");

        seq.playingStep.store (2);
        pressKey (proc, *grid, 0);

        check (seq.steps[2].gate.load(), "armed, a key press writes at the playhead");
        check (seq.steps[2].hold.load() == 1, "starting one step long");
        check (! seq.steps[0].gate.load(), "and not at the cursor");

        const int note = Sequencer303::loadPitch (seq.steps[2]);

        stepPlayheadTo (proc, 3);
        check (seq.steps[2].hold.load() == 2, "the note grows as the playhead moves on");
        stepPlayheadTo (proc, 4);
        stepPlayheadTo (proc, 5);
        check (seq.steps[2].hold.load() == 4, "and keeps growing while the key is down");
        check (Sequencer303::loadPitch (seq.steps[2]) == note, "without changing pitch");
        check (! seq.steps[3].gate.load() && ! seq.steps[4].gate.load()
                   && ! seq.steps[5].gate.load(),
               "the steps it covers are swallowed, not left with notes of their own");

        // --- releasing stops it there -----------------------------------------
        releaseKey (proc, *grid);
        stepPlayheadTo (proc, 6);
        stepPlayheadTo (proc, 7);
        check (seq.steps[2].hold.load() == 4, "letting go stops the note growing");
    }

    // --- it is destructive: a held note erases what it runs over ---------------
    {
        clearPattern (seq, 16);
        for (int i = 0; i < 8; ++i)
        {
            seq.steps[i].gate.store (true);       // a full pattern to write over
            seq.steps[i].key.store (i);
        }
        grid->setHoldLatch (true);

        seq.playingStep.store (0);
        pressKey (proc, *grid, 2);
        for (int i = 1; i <= 4; ++i)
            stepPlayheadTo (proc, i);
        releaseKey (proc, *grid);

        check (seq.steps[0].gate.load() && seq.steps[0].hold.load() == 5,
               "one held note replaces the notes it runs across");
        check (! seq.steps[1].gate.load() && ! seq.steps[4].gate.load(),
               "and their gates are cleared");
        check (seq.steps[5].gate.load(), "steps past the end of the run are untouched");
    }

    // --- a key held through the loop point starts again at the top -------------
    {
        clearPattern (seq, 8);
        grid->setHoldLatch (true);

        seq.playingStep.store (6);
        pressKey (proc, *grid, 3);
        const int note = Sequencer303::loadPitch (seq.steps[6]);

        stepPlayheadTo (proc, 7);
        check (seq.steps[6].hold.load() == 2, "the note runs up to the last step");

        stepPlayheadTo (proc, 0);        // the pattern wraps
        check (seq.steps[6].hold.load() == 2, "the run stops at the end of the pattern");
        check (seq.steps[0].gate.load() && Sequencer303::loadPitch (seq.steps[0]) == note,
               "and the same note opens a fresh run at the top");

        stepPlayheadTo (proc, 1);
        check (seq.steps[0].hold.load() == 2, "which then grows like any other");
        releaseKey (proc, *grid);
    }

    // --- a late timer tick leaves no hole -------------------------------------
    {
        clearPattern (seq, 16);
        grid->setHoldLatch (true);

        seq.playingStep.store (1);
        pressKey (proc, *grid, 4);
        seq.steps[3].gate.store (true);         // something for the run to swallow

        stepPlayheadTo (proc, 4);        // three steps at once: a missed poll
        check (seq.steps[1].hold.load() == 4, "a skipped poll still covers every step");
        check (! seq.steps[3].gate.load(), "including clearing the ones it jumped over");
        releaseKey (proc, *grid);
    }

    // --- disarming mid-note stops it ------------------------------------------
    {
        clearPattern (seq, 16);
        grid->setHoldLatch (true);

        seq.playingStep.store (0);
        pressKey (proc, *grid, 5);
        stepPlayheadTo (proc, 1);
        grid->setHoldLatch (false);
        stepPlayheadTo (proc, 2);

        check (seq.steps[0].hold.load() == 2, "turning HOLD off stops the note growing");
        check (! grid->isHoldArmed(), "and leaves it disarmed");
    }

    // --- nothing playing: fall back to writing a step at a time ----------------
    {
        clearPattern (seq, 16);
        grid->setHoldLatch (true);
        seq.playingStep.store (-1);      // stopped

        pressKey (proc, *grid, 0);
        releaseKey (proc, *grid);

        check (seq.steps[0].gate.load(),
               "with nothing playing, an armed key still writes at the cursor");
        check (seq.steps[0].hold.load() == 1, "as a single step");
    }

    // --- a held key sounds, and keeps sounding, until it is let go -------------
    // The monitor voice is driven from the audio thread, so what the editor asks
    // for is checked by rendering blocks and listening to the output rather than
    // by inspecting state. A 303's amp follows the gate, so a sustained note
    // stays up while only its filter envelope falls away.
    {
        clearPattern (seq, 16);
        grid->setHoldLatch (true);
        seq.playingStep.store (0);

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        const auto renderPeak = [&] (int blocks)
        {
            float peak = 0.0f;
            for (int i = 0; i < blocks; ++i)
            {
                buf.clear();
                proc.processBlock (buf, midi);
                peak = juce::jmax (peak, buf.getMagnitude (0, buf.getNumSamples()));
            }
            return peak;
        };

        renderPeak (4);                      // settle, and flush any earlier audition
        pressKey (proc, *grid, 0);

        const float atStart = renderPeak (8);
        check (atStart > 0.001f, "a key held with HOLD armed makes a sound");

        // The one-shot audition would have expired by now: 0.20s is ~34 blocks of
        // 256 at 44.1k, so a note still up past that is genuinely held. Level is
        // the telling part — a blip would be well into its release, whereas a
        // sustained note sits at the same amplitude with only its filter moving.
        const float muchLater = renderPeak (60);
        check (muchLater > atStart * 0.8f,
               "and it is still at full level long after a blip would have died");

        releaseKey (proc, *grid);
        const float tail = renderPeak (300);
        const float settled = renderPeak (8);
        std::printf ("      [start %.4f  later %.4f  tail %.4f  settled %.4f]\n",
                     atStart, muchLater, tail, settled);
        check (settled < atStart * 0.02f, "letting go releases it");
    }

    // --- the note sounds even with nothing playing ------------------------------
    {
        clearPattern (seq, 16);
        grid->setHoldLatch (true);
        seq.playingStep.store (-1);          // stopped: no playhead to write along

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        const auto renderPeak = [&] (int blocks)
        {
            float peak = 0.0f;
            for (int i = 0; i < blocks; ++i)
            {
                buf.clear();
                proc.processBlock (buf, midi);
                peak = juce::jmax (peak, buf.getMagnitude (0, buf.getNumSamples()));
            }
            return peak;
        };

        renderPeak (4);
        pressKey (proc, *grid, 0);
        const float held = renderPeak (60);
        check (held > 0.001f, "stopped, a held key still sustains rather than blipping");

        releaseKey (proc, *grid);
        renderPeak (300);
        const float settled = renderPeak (8);
        std::printf ("      [held %.4f  settled %.4f]\n", held, settled);
        check (settled < held * 0.02f, "and still releases on let-go");
    }

    // --- an editor torn down mid-note doesn't leave one hanging ----------------
    {
        clearPattern (seq, 16);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;

        {
            std::unique_ptr<juce::AudioProcessorEditor> temp (proc.createEditor());
            auto* tempGrid = findDescendant<StepGrid> (*temp);
            tempGrid->setHoldLatch (true);
            seq.playingStep.store (0);
            pressKey (proc, *tempGrid, 0);         // still held as the editor goes away
        }

        float peak = 0.0f;
        for (int i = 0; i < 90; ++i)
        {
            buf.clear();
            proc.processBlock (buf, midi);
            peak = juce::jmax (peak, buf.getMagnitude (0, buf.getNumSamples()));
        }
        check (peak < 0.01f, "closing the editor with a key down doesn't strand the note on");
    }

    // --- the button reads as lit when armed, on every skin ---------------------
    // The skinned button drawing paints from the palette rather than the colour
    // JUCE hands it, so "it lights up" has to be checked per skin or a skin that
    // quietly ignores the toggle would look exactly like a disarmed one.
    for (int skin = 0; skin < ui303::numSkins; ++skin)
    {
        proc.uiSkin.store (skin);
        std::unique_ptr<juce::AudioProcessorEditor> skinEd (proc.createEditor());

        auto* hold = findButton (*skinEd, "HOLD");
        check (hold != nullptr, juce::String ("skin ") + juce::String (skin)
                                    + ": the HOLD button is in the pattern strip");
        if (hold == nullptr)
            continue;

        hold->setToggleState (false, juce::sendNotification);
        // Mapped into the editor's own space rather than passed straight in.
        // `getBounds()` is in the button's *parent's* coordinates, and the
        // editor scales its content to whatever size the host gives it — so the
        // two only agree while that scale happens to be 1. It was, until the
        // window grew tall enough to be scaled down to fit, at which point this
        // sampled the wrong pixels and reported that arming HOLD lit nothing.
        const auto shotArea = skinEd->getLocalArea (hold, hold->getLocalBounds());

        const auto off = skinEd->createComponentSnapshot (shotArea);
        hold->setToggleState (true, juce::sendNotification);
        const auto on = skinEd->createComponentSnapshot (shotArea);

        const int changed = pixelsDiffering (off, on);
        const int area = juce::jmax (1, off.getWidth() * off.getHeight());
        check (changed > area / 4,
               juce::String ("skin ") + juce::String (skin)
                   + ": arming HOLD lights the button up (" + juce::String (changed * 100 / area)
                   + "% of it changes)");

        // HOLD outlives the editor — MIDI plays into it with the window shut — so
        // reopening must show what is really armed rather than resetting the
        // button to off and lying about the mode.
        std::unique_ptr<juce::AudioProcessorEditor> reopened (proc.createEditor());
        auto* reopenedButton = findButton (*reopened, "HOLD");
        check (reopenedButton != nullptr && reopenedButton->getToggleState(),
               juce::String ("skin ") + juce::String (skin)
                   + ": a reopened editor shows HOLD still armed");

        hold->setToggleState (false, juce::sendNotification);
    }

    check (! proc.holdArmed.load(), "and disarming through the button clears it");

    // --- HOLD from MIDI, end to end -------------------------------------------
    // No editor, no REC, the real sequencer running on its own clock: a MIDI note
    // held down should lay in one sustained run exactly as a key does.
    {
        clearPattern (seq, 16);
        for (int i = 0; i < 16; ++i)
            seq.steps[i].gate.store (true);        // a full pattern to write over

        setParam (proc, "playmode", 1.0f);         // Seq
        setParam (proc, "run", 1.0f);              // internal transport
        setParam (proc, "rec", 0.0f);              // HOLD is not a flavour of REC
        setParam (proc, "intbpm", 120.0f);
        proc.holdArmed.store (true);

        check (renderUntilStep (proc, 0, {}), "the sequencer runs on its own clock");

        // Note-on while step 0 is playing, held until step 4 has been reached.
        check (renderUntilStep (proc, 1, noteOn (Sequencer303::baseNote + 7)),
               "and keeps running with a note held");
        check (renderUntilStep (proc, 4, {}), "on through the steps it covers");

        check (seq.steps[0].gate.load() && seq.steps[0].hold.load() == 5,
               "a held MIDI note writes one sustained run from where it landed");
        check (Sequencer303::loadPitch (seq.steps[0]) == 7,
               "at the pitch that was played");
        check (! seq.steps[1].gate.load() && ! seq.steps[3].gate.load(),
               "swallowing the steps it ran across, REC or no REC");

        // Note-off: the run stops where it is.
        check (renderUntilStep (proc, 6, noteOff (Sequencer303::baseNote + 7)),
               "the transport carries on after the note is released");
        check (seq.steps[0].hold.load() == 5, "releasing the key stops the run growing");
        check (seq.steps[6].gate.load(), "and later steps are left alone");

        setParam (proc, "run", 0.0f);
        proc.holdArmed.store (false);
    }

    // --- disarmed, MIDI writes nothing ----------------------------------------
    {
        clearPattern (seq, 16);
        setParam (proc, "run", 1.0f);
        proc.holdArmed.store (false);

        renderUntilStep (proc, 0, {});
        renderUntilStep (proc, 4, noteOn (Sequencer303::baseNote + 7));

        bool anyGate = false;
        for (int i = 0; i < 16; ++i)
            anyGate = anyGate || seq.steps[i].gate.load();
        check (! anyGate, "with HOLD off, a played MIDI note writes nothing");

        renderUntilStep (proc, 6, noteOff (Sequencer303::baseNote + 7));
        setParam (proc, "run", 0.0f);
    }

    // --- drums on channel 10 are not bass notes -------------------------------
    {
        clearPattern (seq, 16);
        setParam (proc, "run", 1.0f);
        proc.holdArmed.store (true);

        renderUntilStep (proc, 0, {});
        renderUntilStep (proc, 4, noteOn (36, 10));      // a kick, not a bass note

        bool anyGate = false;
        for (int i = 0; i < 16; ++i)
            anyGate = anyGate || seq.steps[i].gate.load();
        check (! anyGate, "a drum note on channel 10 doesn't drive HOLD");

        renderUntilStep (proc, 6, noteOff (36, 10));
        setParam (proc, "run", 0.0f);
        proc.holdArmed.store (false);
    }

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
