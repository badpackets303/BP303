// Offline test for live step-recording: drives the real processor with MIDI
// while armed + running, and checks notes quantize into the expected steps.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

#include <cstdio>

int main()
{
    const double sr = 44100.0;
    const int block = 512;

    BP303AudioProcessor proc;
    proc.prepareToPlay (sr, block);

    auto setP = [&] (const char* id, float v) {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (v);
    };
    setP ("playmode", 0.5f);   // Seq (choices Ext=0 / Seq=1 / Song=2)
    setP ("run", 1.0f);        // internal transport (no host)
    setP ("rec", 1.0f);        // armed

    // start from an empty pattern so recorded steps are unambiguous
    for (int i = 0; i < Sequencer303::maxSteps; ++i)
        proc.sequencer.steps[i].gate.store (false);
    for (int l = 0; l < DrumSequencer::numLanes; ++l)
    {
        proc.drumSequencer.stepMask[l].store (0);
        proc.drumSequencer.accentMask[l].store (0);
    }

    juce::AudioBuffer<float> buf (2, block);
    int failures = 0;

    const double bpm = 130.0;   // intbpm default, no host
    const double blockBeats = (double) block / sr * bpm / 60.0;
    const int len = 16;

    auto expectedStep = [&] (int blockIndex, int off) {
        const double beats = blockIndex * blockBeats + (double) off / sr * bpm / 60.0;
        long long s16 = (long long) std::llround (beats * 4.0);
        return (int) (((s16 % len) + len) % len);
    };

    auto runBlock = [&] (const juce::MidiBuffer& m) {
        buf.clear();
        juce::MidiBuffer mm (m);
        proc.processBlock (buf, mm);
    };

    // Block 0: bass note A2 (MIDI 45) at offset 0 -> step 0, pitch 45-36=9
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), 0);
        runBlock (m);
        const int st = expectedStep (0, 0);
        const bool ok = proc.sequencer.steps[st].gate.load()
                     && Sequencer303::loadPitch (proc.sequencer.steps[st]) == 9
                     && proc.sequencer.steps[st].dyn.load() == dyn303::Hard;
        std::printf ("bass note (vel 100) -> step %d, accented: %s\n", st, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    // Advance to a later block, then record a kick on ch 10 -> a non-zero step
    int blk = 1;
    for (; blk < 10; ++blk)
        runBlock ({});
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 80), 0);   // kick, no accent
        const int st = expectedStep (blk, 0);
        runBlock (m);
        const bool ok = st != 0
                     && (proc.drumSequencer.stepMask[DrumMachine::BD].load() & (1u << st))
                     && ! (proc.drumSequencer.accentMask[DrumMachine::BD].load() & (1u << st));
        std::printf ("kick note -> step %d (non-zero, no accent): %s\n", st, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
        ++blk;
    }

    // A gently played bass note records as a soft step, not just a quiet normal
    // one — the same velocity thresholds the grid and the MIDI export use.
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 47, (juce::uint8) 50), 0);
        const int st = expectedStep (blk, 0);
        runBlock (m);
        const bool ok = st != 0
                     && proc.sequencer.steps[st].gate.load()
                     && proc.sequencer.steps[st].dyn.load() == dyn303::Soft;
        std::printf ("soft bass note (vel 50) -> step %d, soft: %s\n", st, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
        ++blk;
    }

    // Additive: original bass step 0 must still be set
    {
        const bool ok = proc.sequencer.steps[0].gate.load();
        std::printf ("additive (step 0 still set): %s\n", ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    // Disarm -> further notes are ignored
    setP ("rec", 0.0f);
    {
        const auto before = proc.drumSequencer.stepMask[DrumMachine::SD].load();
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), 0);   // snare
        runBlock (m);
        const bool ok = proc.drumSequencer.stepMask[DrumMachine::SD].load() == before;
        std::printf ("disarmed (no write): %s\n", ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    std::printf (failures == 0 ? "RECORD-TEST OK\n" : "RECORD-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
