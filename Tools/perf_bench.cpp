// Where does BP303's CPU go? Times the two halves separately:
//
//   * processBlock, in a few realistic scenarios, as a percentage of the
//     realtime budget for one core at the block size being rendered
//   * a UI repaint, for each region that redraws on a timer, as a percentage
//     of one core at the rate that timer actually fires
//
// Run it with the editor benchmark alone (`perf_bench ui`) or the audio one
// (`perf_bench audio`); with no argument it runs both.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <chrono>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 128;

    using Clock = std::chrono::steady_clock;

    double secondsSince (Clock::time_point t0)
    {
        return std::chrono::duration<double> (Clock::now() - t0).count();
    }

    void setParam (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // Percentage of one core needed to keep up, given `elapsed` seconds of work
    // for `rendered` seconds of audio.
    double realtimePercent (double elapsed, double rendered)
    {
        return 100.0 * elapsed / rendered;
    }

    void runAudioCase (const char* name, BP303AudioProcessor& proc, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        // warm up: let the envelopes, delay lines and reverb tank fill
        for (int i = 0; i < 200; ++i)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
        }

        const auto t0 = Clock::now();
        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
        }
        const double elapsed  = secondsSince (t0);
        const double rendered = blocks * blockSize / sampleRate;

        std::printf ("  %-34s %7.3f us/block   %5.2f%% of a core\n",
                     name, elapsed / blocks * 1.0e6,
                     realtimePercent (elapsed, rendered));
    }

    void audioBench()
    {
        std::printf ("\naudio thread  (%d-sample blocks at %.0f kHz)\n",
                     blockSize, sampleRate / 1000.0);

        {
            BP303AudioProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, "playmode", 0.0f);          // Ext: nothing sequenced
            runAudioCase ("idle, no notes, no FX", proc, 4000);
        }

        {
            BP303AudioProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, "playmode", 1.0f);          // Seq
            setParam (proc, "run", 1.0f);
            for (int s = 0; s < 16; ++s)
                proc.sequencer.steps[(size_t) s].gate.store (true);
            for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
                proc.drumSequencer.stepMask[lane].store (0xffff);
            runAudioCase ("sequencer running, no FX", proc, 4000);
        }

        {
            BP303AudioProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, "playmode", 1.0f);
            setParam (proc, "run", 1.0f);
            for (int s = 0; s < 16; ++s)
                proc.sequencer.steps[(size_t) s].gate.store (true);
            for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
                proc.drumSequencer.stepMask[lane].store (0xffff);

            for (const char* id : { "bflton", "diston", "delayon", "bcompon",
                                    "bchron", "brevon",
                                    "dflton", "ddiston", "ddelayon", "dcompon",
                                    "dchron", "drevon", "metro" })
                setParam (proc, id, 1.0f);
            // FOLD is the oversampled shaper, i.e. the most expensive one
            setParam (proc, "bdisttype", 3.0f);
            setParam (proc, "ddisttype", 3.0f);
            runAudioCase ("running, every FX on (FOLD)", proc, 4000);
        }
    }

    // One repaint of `region`, the way the host's repaint of a dirty rectangle
    // would run it: the whole component tree, clipped. `pixelScale` stands in
    // for the display's backing scale — 2 is a Retina screen.
    double paintOnce (juce::AudioProcessorEditor& editor, juce::Image& into,
                      juce::Rectangle<int> region, int reps, float pixelScale = 1.0f)
    {
        const auto t0 = Clock::now();
        for (int i = 0; i < reps; ++i)
        {
            juce::Graphics g (into);
            if (pixelScale != 1.0f)
                g.addTransform (juce::AffineTransform::scale (pixelScale));
            g.reduceClipRegion (region);
            editor.paintEntireComponent (g, false);
        }
        return secondsSince (t0) / reps;
    }

    void uiCase (const char* name, juce::AudioProcessorEditor& editor,
                 juce::Image& into, juce::Rectangle<int> region, double hz,
                 float pixelScale = 1.0f)
    {
        const double perFrame = paintOnce (editor, into, region, 60, pixelScale);
        std::printf ("  %-34s %7.3f ms/frame  %5.1f%% of a core at %.0f Hz\n",
                     name, perFrame * 1000.0, 100.0 * perFrame * hz, hz);
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

    void uiBench()
    {
        BP303AudioProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);
        if (auto* c = editor->getConstrainer())
            editor->setSize (c->getMaximumWidth(), c->getMaximumHeight());

        const int w = editor->getWidth(), h = editor->getHeight();
        std::printf ("\neditor  (%dx%d, skin \"%s\")\n", w, h,
                     ui303::skinName (proc.uiSkin.load()));

        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Image retina (juce::Image::ARGB, w * 2, h * 2, true);

        auto* stepGrid = findDescendant<StepGrid> (*editor);
        auto* drumGrid = findDescendant<DrumGrid> (*editor);
        auto* songList = findDescendant<SongList> (*editor);
        auto* keys     = findDescendant<PatternKeys> (*editor);

        // A child's repaint invalidates that rectangle of the window, which
        // redraws the background under it too — so measure in window space.
        auto areaOf = [&editor] (juce::Component* c) {
            return c == nullptr ? juce::Rectangle<int>()
                                : editor->getLocalArea (c, c->getLocalBounds());
        };

        uiCase ("whole window", *editor, img, editor->getLocalBounds(), 25.0);
        uiCase ("step grid, whole", *editor, img, areaOf (stepGrid), 25.0);
        uiCase ("drum grid, whole", *editor, img, areaOf (drumGrid), 25.0);
        uiCase ("song list, whole", *editor, img, areaOf (songList), 25.0);
        uiCase ("pattern keys, whole", *editor, img, areaOf (keys), 8.0);

        // Fixed cost of *any* repaint: the component tree walk, plus whatever
        // the background costs under a clip that throws nearly all of it away.
        uiCase ("a 10x10 pixel repaint", *editor, img,
                areaOf (stepGrid).withSize (10, 10), 25.0);

        // The two cells a moving playhead actually invalidates, at the rate the
        // playhead actually moves: sixteenths at 120 BPM is 8 steps a second.
        const auto ledA  = editor->getLocalArea (stepGrid, stepGrid->ledCellBounds (7));
        const auto ledB  = editor->getLocalArea (stepGrid, stepGrid->ledCellBounds (8));
        const auto drumA = editor->getLocalArea (drumGrid, drumGrid->playheadCellBounds (7));
        const auto drumB = editor->getLocalArea (drumGrid, drumGrid->playheadCellBounds (8));

        auto playing = [&] (juce::Image& into, float scale)
        {
            return (paintOnce (*editor, into, ledA,  40, scale)
                  + paintOnce (*editor, into, ledB,  40, scale)
                  + paintOnce (*editor, into, drumA, 40, scale)
                  + paintOnce (*editor, into, drumB, 40, scale)) * 8.0;
        };

        auto allTimers = [&] (juce::Image& into, float scale)
        {
            return paintOnce (*editor, into, areaOf (stepGrid), 40, scale) * 25.0
                 + paintOnce (*editor, into, areaOf (drumGrid), 40, scale) * 25.0
                 + paintOnce (*editor, into, areaOf (songList), 40, scale) * 25.0
                 + paintOnce (*editor, into, areaOf (keys),     40, scale) * 8.0;
        };

        std::printf ("\n  steady state, as a share of one core:\n");
        std::printf ("  %-34s   %10s   %10s\n", "", "1x", "2x (Retina)");
        std::printf ("  %-34s   %9.1f%%   %9.1f%%\n",
                     "every region redrawn every frame",
                     100.0 * allTimers (img, 1.0f), 100.0 * allTimers (retina, 2.0f));
        std::printf ("  %-34s   %9.1f%%   %9.1f%%\n",
                     "playing: playhead cells at 8/s",
                     100.0 * playing (img, 1.0f), 100.0 * playing (retina, 2.0f));
        std::printf ("  %-34s   %9.1f%%   %9.1f%%\n",
                     "stopped and untouched", 0.0, 0.0);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String what = argc > 1 ? argv[1] : "";
    if (what.isEmpty() || what == "audio")
        audioBench();
    if (what.isEmpty() || what == "ui")
        uiBench();

    std::printf ("\n");
    return 0;
}
