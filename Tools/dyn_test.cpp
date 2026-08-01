// Offline test for the three-way step dynamics on the bass ACC row: the click
// cycle through the real grid, what a soft step actually does to the audio, and
// that the value survives every road out of the plugin and back — the state, the
// pattern banks, and an exported MIDI clip.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/MidiExport.h"
#include "../Source/PluginEditor.h"
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

    template <typename T>
    void collect (juce::Component& c, std::vector<T*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* hit = dynamic_cast<T*> (child))
                out.push_back (hit);
            collect (*child, out);
        }
    }

    juce::MouseEvent eventAt (juce::Component& c, juce::Point<int> pos, bool rightButton)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 pos.toFloat(),
                 rightButton ? juce::ModifierKeys::rightButtonModifier
                             : juce::ModifierKeys::leftButtonModifier,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &c, &c, juce::Time::getCurrentTime(),
                 pos.toFloat(), juce::Time::getCurrentTime(), 1, false };
    }

    const char* dynName (int d)
    {
        return d > 0 ? "HARD" : d < 0 ? "SOFT" : "normal";
    }

    constexpr int blockSize = 512;

    // Renders one held bass note at the given dynamics and returns the output.
    std::vector<float> renderNote (int dyn)
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);

        auto setP = [&] (const char* id, float v) {
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (v);
        };
        setP ("playmode", 0.5f);   // Seq: the pattern plays
        setP ("run", 1.0f);
        setP ("drumson", 0.0f);    // bass only

        // one note on step 0, nothing else, so the render is that step alone
        for (auto& s : proc.sequencer.steps)
        {
            s.gate.store (false);
            s.slide.store (false);
            s.dyn.store (dyn303::Normal);
            s.hold.store (1);
        }
        auto& s0 = proc.sequencer.steps[0];
        s0.gate.store (true);
        s0.dyn.store (dyn);
        Sequencer303::storePitch (s0, 9);

        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);
        for (int b = 0; b < 8; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0),
                        buf.getReadPointer (0) + blockSize);
        }
        return out;
    }

    double peak (const std::vector<float>& v)
    {
        double m = 0.0;
        for (float x : v) m = std::max (m, (double) std::abs (x));
        return m;
    }

    // Energy above ~2 kHz, as a share of the total: how open the filter got.
    // A one-pole difference is enough to rank three renders of the same note.
    double brightness (const std::vector<float>& v)
    {
        double hi = 0.0, all = 0.0, lp = 0.0;
        for (float x : v)
        {
            lp += ((double) x - lp) * 0.25;
            const double h = (double) x - lp;
            hi += h * h;
            all += (double) x * x;
        }
        return all > 0.0 ? hi / all : 0.0;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // --- the velocity mapping round-trips both ways ---------------------------
    {
        for (int d : { dyn303::Soft, dyn303::Normal, dyn303::Hard })
        {
            const int v = dyn303::velocityForDyn (d);
            if (dyn303::dynFromVelocity (v) != d)
            {
                std::printf ("  %s -> vel %d -> %s\n", dynName (d), v,
                             dynName (dyn303::dynFromVelocity (v)));
                ++failures;
            }
        }
        check (failures == 0, "every dynamic survives a trip through velocity");
        check (dyn303::dynFromVelocity (127) == dyn303::Hard
               && dyn303::dynFromVelocity (1) == dyn303::Soft
               && dyn303::dynFromVelocity (90) == dyn303::Normal,
               "the velocity thresholds land where the docs say");
    }

    // --- clicking the ACC cell cycles, through the real grid -------------------
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);
        if (auto* c = editor->getConstrainer())
            editor->setSize (juce::jmax (c->getMinimumWidth(), 1466),
                             juce::jmax (c->getMinimumHeight(), 848));
        else
            editor->setSize (1466, 848);

        std::vector<StepGrid*> grids;
        collect (*editor, grids);
        if (grids.empty())
        {
            std::printf ("no StepGrid found: FAIL\n");
            return 1;
        }
        auto& grid = *grids.front();

        // middle of column 0's ACC cell, in the grid's own coordinates
        const int cellW = (grid.getWidth() - 44) / 16;
        const juce::Point<int> accCell { 44 + cellW / 2, 14 + 20 + 22 + 7 };

        auto& step = proc.sequencer.steps[0];
        step.dyn.store (dyn303::Normal);

        auto clickAndRead = [&] (bool rightButton) {
            grid.mouseDown (eventAt (grid, accCell, rightButton));
            return dyn303::clampDyn (step.dyn.load());
        };

        const int a = clickAndRead (false);
        const int b = clickAndRead (false);
        const int c = clickAndRead (false);
        std::printf ("  left clicks from normal: %s -> %s -> %s\n",
                     dynName (a), dynName (b), dynName (c));
        check (a == dyn303::Hard, "one click accents the step, as it always did");
        check (b == dyn303::Soft, "a second click makes it soft");
        check (c == dyn303::Normal, "a third click clears it");

        // and the ring runs backwards on right-click, so undoing is one click
        step.dyn.store (dyn303::Hard);
        const int back = clickAndRead (true);
        std::printf ("  right click from HARD: %s\n", dynName (back));
        check (back == dyn303::Normal, "right-click steps back off an accent");

        // the ACC row must not disturb the step's gate or slide
        step.dyn.store (dyn303::Normal);
        step.gate.store (true);
        step.slide.store (true);
        clickAndRead (false);
        check (step.gate.load() && step.slide.load(),
               "cycling ACC leaves gate and slide alone");

    }

    // --- a picture of the row, per skin ---------------------------------------
    // A soft cell has to read as "less" rather than as an empty one, and each
    // skin draws lit cells differently (the retro one glows), so every palette
    // gets a snapshot to eyeball.
    {
        static const char* skinFiles[] = { "classic", "retro", "studio", "bad", "neon" };
        static_assert (juce::numElementsInArray (skinFiles) == ui303::numSkins,
                       "add a file name when a skin is added");

        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);
        for (int i = 0; i < Sequencer303::maxSteps; ++i)
        {
            auto& s = proc.sequencer.steps[i];
            s.gate.store (true);
            s.slide.store (false);
            s.hold.store (1);
            s.dyn.store (i % 3 == 0 ? dyn303::Hard
                       : i % 3 == 1 ? dyn303::Soft
                                    : dyn303::Normal);
        }

        // and the same three states across every drum lane, plus empty cells to
        // check a soft hit doesn't read as one
        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            proc.drumSequencer.stepMask[lane].store (0);
            proc.drumSequencer.accentMask[lane].store (0);
            proc.drumSequencer.softMask[lane].store (0);
            for (int i = 0; i < Sequencer303::maxSteps; ++i)
                if (i % 4 != 3)
                    proc.drumSequencer.setDynAt (lane, i,
                        (i + lane) % 3 == 0 ? dyn303::Hard
                      : (i + lane) % 3 == 1 ? dyn303::Soft
                                            : dyn303::Normal);
        }

        int written = 0;
        for (int skin = 0; skin < ui303::numSkins; ++skin)
        {
            proc.uiSkin.store (skin);
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setVisible (true);
            editor->setSize (1466, 848);

            std::vector<StepGrid*> grids;
            std::vector<DrumGrid*> drumGrids;
            collect (*editor, grids);
            collect (*editor, drumGrids);
            if (grids.empty() || drumGrids.empty())
                continue;

            // both grids in one image, so the two can be compared side by side
            auto& grid = *grids.front();
            auto& drumGrid = *drumGrids.front();
            const auto gridShot = grid.createComponentSnapshot (grid.getLocalBounds());
            const auto drumShot = drumGrid.createComponentSnapshot (drumGrid.getLocalBounds());

            juce::Image shot (juce::Image::ARGB,
                              juce::jmax (gridShot.getWidth(), drumShot.getWidth()),
                              gridShot.getHeight() + drumShot.getHeight(), true);
            {
                juce::Graphics ig (shot);
                ig.drawImageAt (gridShot, 0, 0);
                ig.drawImageAt (drumShot, 0, gridShot.getHeight());
            }

            const auto out = juce::File::getCurrentWorkingDirectory()
                                 .getChildFile (juce::String ("acc_dyn_") + skinFiles[skin] + ".png");
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream());
                stream != nullptr && stream->openedOk())
            {
                stream->setPosition (0);
                stream->truncate();
                juce::PNGImageFormat png;
                if (png.writeImageToStream (shot, *stream))
                    ++written;
            }
        }
        check (written == ui303::numSkins, "wrote an ACC row snapshot for every skin");
    }

    // --- a soft step is quieter and duller; an accent is louder and brighter ---
    {
        const auto soft   = renderNote (dyn303::Soft);
        const auto normal = renderNote (dyn303::Normal);
        const auto hard   = renderNote (dyn303::Hard);

        std::printf ("  peak  soft=%.4f normal=%.4f hard=%.4f\n",
                     peak (soft), peak (normal), peak (hard));
        std::printf ("  bright soft=%.4f normal=%.4f hard=%.4f\n",
                     brightness (soft), brightness (normal), brightness (hard));

        check (peak (soft) > 1.0e-4, "a soft step still sounds");
        check (peak (soft) < peak (normal) && peak (normal) < peak (hard),
               "the three dynamics get louder in order");
        check (brightness (soft) < brightness (normal),
               "a soft step is duller than a normal one");
    }

    // --- the value survives the state, and old states still load --------------
    {
        BP303AudioProcessor saved;
        saved.prepareToPlay (44100.0, blockSize);
        saved.sequencer.steps[0].dyn.store (dyn303::Soft);
        saved.sequencer.steps[1].dyn.store (dyn303::Hard);
        saved.sequencer.steps[2].dyn.store (dyn303::Normal);

        juce::MemoryBlock state;
        saved.getStateInformation (state);

        BP303AudioProcessor loaded;
        loaded.prepareToPlay (44100.0, blockSize);
        loaded.setStateInformation (state.getData(), (int) state.getSize());
        check (loaded.sequencer.steps[0].dyn.load() == dyn303::Soft
               && loaded.sequencer.steps[1].dyn.load() == dyn303::Hard
               && loaded.sequencer.steps[2].dyn.load() == dyn303::Normal,
               "all three dynamics survive a state round-trip");

        // Drop the "dyn" attributes to stand in for a project written before soft
        // steps existed. Its accents have to come back as accents.
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        int stripped = 0;
        if (xml != nullptr)
            for (auto* bank : xml->getChildWithTagNameIterator ("BASSPATTERNS"))
                for (auto* pat : bank->getChildWithTagNameIterator ("PAT"))
                    for (auto* st : pat->getChildWithTagNameIterator ("STEP"))
                        if (st->hasAttribute ("dyn"))
                        {
                            st->removeAttribute ("dyn");
                            ++stripped;
                        }
        check (stripped > 0, "the saved state carried dyn attributes to strip");

        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);

        BP303AudioProcessor legacy;
        legacy.prepareToPlay (44100.0, blockSize);
        legacy.setStateInformation (old.getData(), (int) old.getSize());
        std::printf ("  legacy load: step0=%s step1=%s\n",
                     dynName (legacy.sequencer.steps[0].dyn.load()),
                     dynName (legacy.sequencer.steps[1].dyn.load()));
        check (legacy.sequencer.steps[1].dyn.load() == dyn303::Hard,
               "an old state's accents still load as accents");
        check (legacy.sequencer.steps[0].dyn.load() == dyn303::Normal,
               "an old state has no soft steps to load, and doesn't invent any");
    }

    // --- an exported MIDI clip carries the dynamics back in -------------------
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);

        const int want[] = { dyn303::Soft, dyn303::Normal, dyn303::Hard };
        for (int i = 0; i < 3; ++i)
        {
            auto& s = proc.sequencer.steps[i];
            s.gate.store (true);
            s.slide.store (false);
            s.hold.store (1);
            s.dyn.store (want[i]);
        }
        for (int i = 3; i < Sequencer303::maxSteps; ++i)
            proc.sequencer.steps[i].gate.store (false);

        const auto seq = bp303::bassSequence (proc.sequencer, 0.0f);

        int found = 0, matched = 0;
        for (int e = 0; e < seq.getNumEvents(); ++e)
        {
            const auto& msg = seq.getEventPointer (e)->message;
            if (! msg.isNoteOn())
                continue;

            const double step = msg.getTimeStamp() / (double) bp303::ticksPer16th;
            const int idx = (int) std::llround (step);
            if (idx < 0 || idx > 2)
                continue;

            ++found;
            const int readBack = dyn303::dynFromVelocity (msg.getVelocity());
            std::printf ("  step %d exported at vel %d -> %s (wanted %s)\n",
                         idx, msg.getVelocity(), dynName (readBack), dynName (want[idx]));
            if (readBack == want[idx])
                ++matched;
        }
        check (found == 3 && matched == 3,
               "an exported clip re-imports as the dynamics it was written from");
    }

    // --- the drum lanes carry the same three states --------------------------
    {
        std::printf ("\n--- drum lanes ---\n");

        BP303AudioProcessor proc;
        proc.prepareToPlay (44100.0, blockSize);
        auto& drums = proc.drumSequencer;

        // the two level masks can never disagree, whatever order you set them in
        drums.setDynAt (0, 3, dyn303::Hard);
        drums.setDynAt (0, 3, dyn303::Soft);
        check ((drums.accentMask[0].load() & (1u << 3)) == 0
               && (drums.softMask[0].load() & (1u << 3)) != 0
               && drums.dynAt (0, 3) == dyn303::Soft,
               "setting soft over an accent leaves only the soft bit");

        drums.setDynAt (0, 3, dyn303::Hard);
        check ((drums.softMask[0].load() & (1u << 3)) == 0
               && drums.dynAt (0, 3) == dyn303::Hard,
               "and setting an accent over soft leaves only the accent bit");

        check (drums.hasHit (0, 3), "setting a level turns the hit on");

        drums.clearStep (0, 3);
        check (! drums.hasHit (0, 3) && drums.dynAt (0, 3) == dyn303::Normal,
               "clearing a step drops its level with it");

        // a step with no hit under it has no dynamics to report
        drums.softMask[1].store (0xffffu);
        drums.accentMask[1].store (0xffffu);
        drums.stepMask[1].store (0);
        drums.normalise();
        check (drums.softMask[1].load() == 0 && drums.accentMask[1].load() == 0,
               "normalise drops level bits that no hit stands under");

        // and where a hand-edited file claims both, the accent wins
        drums.stepMask[2].store (1u << 5);
        drums.accentMask[2].store (1u << 5);
        drums.softMask[2].store (1u << 5);
        drums.normalise();
        check (drums.dynAt (2, 5) == dyn303::Hard
               && (drums.softMask[2].load() & (1u << 5)) == 0,
               "normalise lets the accent win a step that claims both");
    }

    // --- a soft drum hit is quieter, and it survives the state ----------------
    {
        auto renderKick = [] (int dyn) {
            BP303AudioProcessor proc;
            proc.prepareToPlay (44100.0, blockSize);

            auto setP = [&] (const char* id, float v) {
                if (auto* p = proc.apvts.getParameter (id))
                    p->setValueNotifyingHost (v);
            };
            setP ("playmode", 0.5f);
            setP ("run", 1.0f);
            setP ("basson", 0.0f);

            for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
            {
                proc.drumSequencer.stepMask[lane].store (0);
                proc.drumSequencer.accentMask[lane].store (0);
                proc.drumSequencer.softMask[lane].store (0);
            }
            proc.drumSequencer.setDynAt (DrumMachine::BD, 0, dyn);

            std::vector<float> out;
            juce::AudioBuffer<float> buf (2, blockSize);
            for (int b = 0; b < 8; ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
                out.insert (out.end(), buf.getReadPointer (0),
                            buf.getReadPointer (0) + blockSize);
            }
            return out;
        };

        const auto soft   = renderKick (dyn303::Soft);
        const auto normal = renderKick (dyn303::Normal);
        const auto hard   = renderKick (dyn303::Hard);
        std::printf ("  kick peak soft=%.4f normal=%.4f hard=%.4f\n",
                     peak (soft), peak (normal), peak (hard));

        check (peak (soft) > 1.0e-4, "a soft kick still sounds");
        check (peak (soft) < peak (normal) && peak (normal) < peak (hard),
               "the three drum dynamics get louder in order");
    }

    {
        BP303AudioProcessor saved;
        saved.prepareToPlay (44100.0, blockSize);
        saved.drumSequencer.setDynAt (DrumMachine::CH, 2, dyn303::Soft);
        saved.drumSequencer.setDynAt (DrumMachine::CH, 6, dyn303::Hard);

        juce::MemoryBlock state;
        saved.getStateInformation (state);

        BP303AudioProcessor loaded;
        loaded.prepareToPlay (44100.0, blockSize);
        loaded.setStateInformation (state.getData(), (int) state.getSize());
        check (loaded.drumSequencer.dynAt (DrumMachine::CH, 2) == dyn303::Soft
               && loaded.drumSequencer.dynAt (DrumMachine::CH, 6) == dyn303::Hard,
               "drum dynamics survive a state round-trip");

        // an old file has no "softs" attribute at all, which reads as no soft hits
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        int stripped = 0;
        if (xml != nullptr)
            for (auto* bank : xml->getChildWithTagNameIterator ("DRUMPATTERNS"))
                for (auto* pat : bank->getChildWithTagNameIterator ("PAT"))
                    for (auto* lane : pat->getChildWithTagNameIterator ("LANE"))
                        if (lane->hasAttribute ("softs"))
                        {
                            lane->removeAttribute ("softs");
                            ++stripped;
                        }
        check (stripped > 0, "the saved state carried softs attributes to strip");

        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);

        BP303AudioProcessor legacy;
        legacy.prepareToPlay (44100.0, blockSize);
        legacy.setStateInformation (old.getData(), (int) old.getSize());
        check (legacy.drumSequencer.dynAt (DrumMachine::CH, 6) == dyn303::Hard
               && legacy.drumSequencer.hasHit (DrumMachine::CH, 2)
               && legacy.drumSequencer.dynAt (DrumMachine::CH, 2) == dyn303::Normal,
               "an old drum state keeps its accents and its hits, with no soft ones");
    }

    std::printf (failures == 0 ? "\nDYN-TEST OK\n" : "\nDYN-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
