// Offline test for the independent bass/drum pattern banks: drives the real
// processor to verify (1) switching one line's pattern leaves the other line
// untouched, and (2) both banks + current slots survive a state round-trip.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <functional>

namespace
{
    int failures = 0;
    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    // While not running (run=0) in Seq mode, a queued switch lands on the very
    // next processBlock — makes switching deterministic for the test.
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
}

int main()
{
    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 256);

    auto setP = [&] (const char* id, float v) {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (v);
    };
    setP ("playmode", 0.5f);   // Seq (Ext / Seq / Song)
    setP ("run", 0.0f);        // not running -> immediate switches

    using BD = DrumMachine;

    // --- Known starting state in the current slots (bass 0 / drum 0) ---
    for (int i = 0; i < Sequencer303::maxSteps; ++i)
        setBass (proc, i, false, -3);
    setBass (proc, 0, true, 9);           // bass line "A": step 0, pitch 9
    for (int l = 0; l < DrumSequencer::numLanes; ++l)
        proc.drumSequencer.stepMask[l].store (0);
    proc.drumSequencer.stepMask[BD::BD].store (0x1111u);   // drum beat "A"

    // --- Switching the DRUM pattern must not disturb the bass line ---
    proc.requestDrumPattern (1);
    pump (proc);
    check (proc.getCurrentDrumPattern() == 1, "drum switched to slot 1");
    check (proc.drumSequencer.stepMask[BD::BD].load() == 0u, "drum slot 1 is empty");
    check (proc.sequencer.steps[0].gate.load()
           && Sequencer303::loadPitch (proc.sequencer.steps[0]) == 9,
           "bass line untouched by drum switch");

    proc.requestDrumPattern (0);
    pump (proc);
    check (proc.drumSequencer.stepMask[BD::BD].load() == 0x1111u, "drum beat A restored");

    // --- Switching the BASS pattern must not disturb the drums ---
    proc.requestBassPattern (1);
    pump (proc);
    check (proc.getCurrentBassPattern() == 1, "bass switched to slot 1");
    check (! proc.sequencer.steps[0].gate.load(), "bass slot 1 is empty");
    check (proc.drumSequencer.stepMask[BD::BD].load() == 0x1111u,
           "drum beat untouched by bass switch");

    // edit bass line "B" into slot 1, then hop back to slot 0 -> line A returns
    setBass (proc, 4, true, 5);
    proc.requestBassPattern (0);
    pump (proc);
    check (proc.sequencer.steps[0].gate.load()
           && Sequencer303::loadPitch (proc.sequencer.steps[0]) == 9
           && ! proc.sequencer.steps[4].gate.load(),
           "bass line A restored on return to slot 0");

    // --- State round-trip: both banks + currents + a second bass slot ---
    // Save on PULSE, the last wave in the list. A choice parameter stored as a
    // fraction of its list would write 1.0 for whatever is last, so a stored 2.0
    // is the proof that the *index* is what travels — which is the whole reason
    // a wave can be appended without changing what saved projects play.
    proc.apvts.getParameter ("wave")->setValueNotifyingHost (
        proc.apvts.getParameter ("wave")->convertTo0to1 (2.0f));   // Pulse

    juce::MemoryBlock state;
    proc.getStateInformation (state);

    // The parameters sit inside APVTS's own element, so hunt for the entry
    // rather than assuming where APVTS chose to put it.
    std::function<juce::XmlElement*(juce::XmlElement&)> findWave;
    findWave = [&findWave] (juce::XmlElement& e) -> juce::XmlElement*
    {
        for (auto* c : e.getChildIterator())
        {
            if (c->getStringAttribute ("id") == "wave")
                return c;
            if (auto* found = findWave (*c))
                return found;
        }
        return nullptr;
    };

    if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(),
                                                           (int) state.getSize()))
    {
        auto* waveXml = findWave (*xml);
        const juce::String stored = waveXml != nullptr
                                        ? waveXml->getStringAttribute ("value")
                                        : juce::String ("<missing>");
        std::printf ("      [wave stored in state as \"%s\"]\n", stored.toRawUTF8());
        check (waveXml != nullptr && stored.getFloatValue() == 2.0f,
               "the wave choice is saved as its index, not as a fraction of the list");

        // The case that actually matters: a project saved before PULSE existed
        // carried wave = 1 for Square. Stand one in by rewriting the value, and
        // it has to still open on Square rather than sliding onto whatever is
        // last in the list now.
        if (waveXml != nullptr)
        {
            waveXml->setAttribute ("value", 1.0);
            juce::MemoryBlock older;
            juce::AudioProcessor::copyXmlToBinary (*xml, older);

            BP303AudioProcessor proc3;
            proc3.prepareToPlay (44100.0, 256);
            proc3.setStateInformation (older.getData(), (int) older.getSize());
            check ((int) proc3.apvts.getRawParameterValue ("wave")->load() == 1,
                   "a project saved before PULSE existed still opens on Square");
        }
    }
    else
    {
        check (false, "plugin state is readable as XML");
    }

    BP303AudioProcessor proc2;
    proc2.prepareToPlay (44100.0, 256);
    proc2.setStateInformation (state.getData(), (int) state.getSize());

    check (proc2.getCurrentBassPattern() == 0 && proc2.getCurrentDrumPattern() == 0,
           "currents restored");
    check ((int) proc2.apvts.getRawParameterValue ("wave")->load() == 2,
           "and the wave comes back as the Pulse it was saved on");
    check (proc2.sequencer.steps[0].gate.load()
           && Sequencer303::loadPitch (proc2.sequencer.steps[0]) == 9,
           "live bass line A restored from state");
    check (proc2.drumSequencer.stepMask[BD::BD].load() == 0x1111u,
           "live drum beat A restored from state");

    // the second bass slot (line B, step 4) must have persisted too
    proc2.apvts.getParameter ("run")->setValueNotifyingHost (0.0f);
    proc2.requestBassPattern (1);
    pump (proc2);
    check (proc2.sequencer.steps[4].gate.load()
           && Sequencer303::loadPitch (proc2.sequencer.steps[4]) == 5,
           "bass slot 1 (line B) persisted through state");

    std::printf (failures == 0 ? "PATTERN-TEST OK\n" : "PATTERN-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
