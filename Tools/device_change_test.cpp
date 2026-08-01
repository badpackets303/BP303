// Reproduces what a DAW does on an audio-device / sample-rate change: it stops
// the audio callback, calls releaseResources()/prepareToPlay() again at the new
// rate, then restarts — all while the editor is open and its timers run. If the
// plugin has a re-prepare bug, this crashes headlessly.
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

static void runBlocks (BP303AudioProcessor& proc, double sr, int block, int count)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i < count; ++i)
    {
        buf.clear();
        midi.clear();
        proc.processBlock (buf, midi);
    }
    juce::ignoreUnused (sr);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;

    // initial device
    proc.setRateAndBufferSizeDetails (44100.0, 512);
    proc.prepareToPlay (44100.0, 512);

    // open the editor and let its 25 Hz timers spin
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setBounds (0, 0, editor->getWidth(), editor->getHeight());

    // a mix of rates a Mac throws at you: built-in, pro interface, AirPods SCO
    struct Dev { double sr; int block; };
    const Dev devices[] = { { 48000.0, 512 }, { 44100.0, 256 }, { 96000.0, 1024 },
                            { 16000.0, 128 }, { 44100.0, 512 } };

    for (const auto& d : devices)
    {
        runBlocks (proc, d.sr, d.block, 20);       // audio running on old device

        // ---- device change ----
        proc.releaseResources();
        proc.setRateAndBufferSizeDetails (d.sr, d.block);
        proc.prepareToPlay (d.sr, d.block);

        // pump the message thread so the editor's timers fire against the new state
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        // force a repaint the way a host re-expose would
        auto img = editor->createComponentSnapshot (editor->getLocalBounds());
        juce::ignoreUnused (img);

        runBlocks (proc, d.sr, d.block, 20);       // audio running on new device
        std::printf ("survived %.0f Hz / %d spb\n", d.sr, d.block);
    }

    // some hosts destroy and recreate the editor on the reset, too
    editor.reset();
    editor.reset (proc.createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    (void) editor->createComponentSnapshot (editor->getLocalBounds());

    // A host rebuilding the editor can size it to nothing on the way out. That
    // must not be remembered as the size to reopen at, or every device change
    // would shrink the window a little further.
    {
        int reopenFailures = 0;

        editor.reset (proc.createEditor());
        const int chosen = editor->getWidth();

        editor->setSize (0, 0);          // what a teardown can look like
        editor.reset();
        editor.reset (proc.createEditor());

        if (editor->getWidth() != chosen)
        {
            ++reopenFailures;
            std::printf ("FAIL: editor reopened at %d, expected %d\n",
                         editor->getWidth(), chosen);
        }
        if (reopenFailures != 0)
            return 1;
        std::printf ("reopened at %d px after a zero-sized teardown\n", editor->getWidth());
    }

    std::printf ("PASS: plugin survived re-prepare with a live editor\n");
    return 0;
}
