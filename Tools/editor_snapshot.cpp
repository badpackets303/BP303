// Renders the plugin editor to editor_snapshot.png for visual verification
// without needing screen-recording permission.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    // A few arrangement rows so the SONG panel renders with content.
    auto songStep = [] (int bass, int drum, int reps, bool bassMute = false)
    {
        SongPlayer::Step s;
        s.bassSlot = bass;
        s.drumSlot = drum;
        s.repeats  = reps;
        s.bassMute = bassMute;
        return s;
    };
    proc.song.insertStep (0, songStep (0, 0, 4, true));
    proc.song.insertStep (1, songStep (1, 0, 8));
    proc.song.insertStep (2, songStep (1, 2, 8));
    proc.song.insertStep (3, songStep (SongPlayer::hold, 9, 4));
    proc.song.insertStep (4, songStep (11, 9, 16));

    static const char* names[] = { "editor_snapshot_classic.png", "editor_snapshot_retro.png",
                                   "editor_snapshot_studio.png", "editor_snapshot_bad.png",
                                   "editor_snapshot_neon.png" };
    static_assert (juce::numElementsInArray (names) == ui303::numSkins,
                   "add a file name when a skin is added");
    for (int skin = 0; skin < ui303::numSkins; ++skin)
    {
        proc.uiSkin.store (skin);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto image = editor->createComponentSnapshot (editor->getLocalBounds());

        auto file = juce::File::getCurrentWorkingDirectory().getChildFile (names[skin]);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (! stream.openedOk())
            return 1;

        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, stream))
            return 1;
        std::printf ("written %s %dx%d\n", names[skin], image.getWidth(), image.getHeight());
    }
    return 0;
}
