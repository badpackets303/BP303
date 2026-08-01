// Offline test for the two grids' playing-step highlight. Each line has its own
// ON switch, so each grid has to follow its own sequencer: the drum grid used to
// read the bass sequencer's step and went dark whenever the bass was switched
// off, even though the drums were still running.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>

namespace
{
    int failures = 0;
    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    void pump (BP303AudioProcessor& proc, int blocks = 1)
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i)
        {
            buf.clear();
            proc.processBlock (buf, midi);
        }
    }

    // Runs a while and reports whether the step ever advanced past its start —
    // a lit grid moves, a dark one sits on -1.
    struct Motion { bool lit; bool advanced; };

    Motion watch (BP303AudioProcessor& proc, std::atomic<int>& step)
    {
        Motion m { false, false };
        const int first = step.load();
        for (int i = 0; i < 200; ++i)
        {
            pump (proc);
            const int s = step.load();
            if (s >= 0)  m.lit = true;
            if (s >= 0 && s != first) m.advanced = true;
        }
        return m;
    }

    template <typename T>
    T* findChild (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* hit = dynamic_cast<T*> (child))
                return hit;
            if (auto* hit = findChild<T> (*child))
                return hit;
        }
        return nullptr;
    }

    // The highlight is the only thing in the grid that moves with the transport,
    // so comparing renders is enough to say whether one was drawn — and it goes
    // through the real paint path, which asserting on the processor alone does not.
    bool sameImage (const juce::Image& a, const juce::Image& b)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return false;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return false;
        return true;
    }

    juce::Image render (juce::Component& c)
    {
        return c.createComponentSnapshot (c.getLocalBounds());
    }

    // Runs until the given line's step changes, so two renders can be compared
    // at genuinely different playhead positions.
    void advanceStep (BP303AudioProcessor& proc, std::atomic<int>& step)
    {
        const int from = step.load();
        for (int i = 0; i < 400 && step.load() == from; ++i)
            pump (proc);
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
    setP ("intbpm", 1.0f);     // fast, so steps advance within the test's blocks
    setP ("run", 1.0f);

    // --- both lines on: both grids follow along ---
    {
        setP ("basson", 1.0f);
        setP ("drumson", 1.0f);
        const auto bass  = watch (proc, proc.sequencer.playingStep);
        const auto drums = watch (proc, proc.drumSequencer.playingStep);
        check (bass.lit && bass.advanced, "both on: the bass grid follows the playhead");
        check (drums.lit && drums.advanced, "both on: the drum grid follows the playhead");
    }

    // --- the reported bug: drums only ---
    {
        setP ("basson", 0.0f);
        setP ("drumson", 1.0f);
        const auto drums = watch (proc, proc.drumSequencer.playingStep);
        check (drums.lit, "drums only: the drum grid still highlights");
        check (drums.advanced, "drums only: the highlight still advances");
        check (proc.sequencer.playingStep.load() < 0,
               "drums only: the bass grid goes dark");
    }

    // --- and the mirror case: bass only ---
    {
        setP ("basson", 1.0f);
        setP ("drumson", 0.0f);
        const auto bass = watch (proc, proc.sequencer.playingStep);
        check (bass.lit && bass.advanced, "bass only: the bass grid still highlights");
        check (proc.drumSequencer.playingStep.load() < 0,
               "bass only: the drum grid goes dark");
    }

    // --- stopped: neither grid shows a playhead ---
    {
        setP ("basson", 1.0f);
        setP ("drumson", 1.0f);
        setP ("run", 0.0f);
        pump (proc, 4);
        check (proc.sequencer.playingStep.load() < 0, "stopped: the bass grid is dark");
        check (proc.drumSequencer.playingStep.load() < 0, "stopped: the drum grid is dark");
    }

    // --- the same thing through the real grid, which is where the bug lived ---
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* drumGrid = findChild<DrumGrid> (*ed);
        auto* stepGrid = findChild<StepGrid> (*ed);
        if (drumGrid == nullptr || stepGrid == nullptr)
        {
            std::printf ("could not find the grids in the editor\n");
            return 1;
        }

        setP ("run", 0.0f);
        setP ("basson", 1.0f);
        setP ("drumson", 1.0f);
        pump (proc, 4);
        const auto drumsStopped = render (*drumGrid);
        const auto bassStopped  = render (*stepGrid);

        // drums running with the bass switched off — the reported case
        setP ("basson", 0.0f);
        setP ("run", 1.0f);
        pump (proc, 4);
        const auto drumsBassOff = render (*drumGrid);
        check (! sameImage (drumsStopped, drumsBassOff),
               "drum grid draws a playhead with the bass off");
        check (sameImage (bassStopped, render (*stepGrid)),
               "bass grid stays dark with the bass off");

        // and it tracks the drum line rather than sitting on one step
        advanceStep (proc, proc.drumSequencer.playingStep);
        check (! sameImage (drumsBassOff, render (*drumGrid)),
               "drum grid's playhead moves with the drum line");

        // the mirror: bass running, drums off
        setP ("basson", 1.0f);
        setP ("drumson", 0.0f);
        pump (proc, 4);
        check (sameImage (drumsStopped, render (*drumGrid)),
               "drum grid goes dark with the drums off");
        check (! sameImage (bassStopped, render (*stepGrid)),
               "bass grid draws a playhead with the drums off");
    }

    std::printf (failures == 0 ? "PLAYHEAD-TEST OK\n" : "PLAYHEAD-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
