#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Chorus.h"
#include "Compressor.h"
#include "Distortion.h"
#include "DrumMachine.h"
#include "DrumSequencer.h"
#include "DspUtil.h"
#include "Fx303.h"
#include "Metronome.h"
#include "Pcf.h"
#include "Reverb.h"
#include "Sequencer303.h"
#include "SongPlayer.h"
#include "Synth303.h"

class BP303AudioProcessor : public juce::AudioProcessor
{
public:
    BP303AudioProcessor();
    ~BP303AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    // Long enough for the reverb at full size; the delay alone needed ~2 s.
    double getTailLengthSeconds() const override           { return 8.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- independent bass & drum pattern banks (3 banks A/B/C x 9 keys) ---
    static constexpr int numBassPatterns = 27;
    static constexpr int numDrumPatterns = 27;

    struct BassPattern
    {
        struct BassStep
        {
            int  pitch = -3;   // A1
            bool gate = false, slide = false;
            int  dyn = dyn303::Normal;   // soft / normal / accented
            int  hold = 1;     // note length in steps
        };
        BassStep bass[Sequencer303::maxSteps];
        int length = 16;

        bool isEmpty() const
        {
            for (const auto& b : bass)
                if (b.gate)
                    return false;
            return true;
        }
    };

    struct DrumPattern
    {
        uint32_t drumSteps[DrumSequencer::numLanes] = {};
        uint32_t drumAccents[DrumSequencer::numLanes] = {};
        uint32_t drumSofts[DrumSequencer::numLanes] = {};

        bool isEmpty() const
        {
            for (auto m : drumSteps)
                if (m != 0)
                    return false;
            return true;
        }
    };

    // Queue a switch to a slot; happens at the next pattern boundary while that
    // line runs, immediately otherwise. Bass and drums switch independently.
    void requestBassPattern (int idx);
    void requestDrumPattern (int idx);
    int getCurrentBassPattern() const  { return currentBassPattern.load(); }
    int getPendingBassPattern() const  { return pendingBassPattern.load(); }
    int getCurrentDrumPattern() const  { return currentDrumPattern.load(); }
    int getPendingDrumPattern() const  { return pendingDrumPattern.load(); }

    // A slot's contents, for reading a pattern the sequencers aren't playing
    // (the editor exports these as MIDI). The loaded slot is read back off the
    // live sequencer, because edits aren't written into the bank until the
    // pattern is switched away from.
    BassPattern snapshotBassPattern (int slot) const;
    DrumPattern snapshotDrumPattern (int slot) const;

    // Editor width in pixels; the height follows from the fixed aspect ratio.
    // Persisted so a project reopens the window at the size it was left.
    std::atomic<int> editorWidth { 1466 };

    // UI skin choice (see ui303::numSkins); persisted with the project state and
    // also remembered globally so new instances open on the last-used skin.
    //
    // This initialiser is a placeholder, not the default: the constructor always
    // overwrites it with the global preference, falling back to ui303::defaultSkin.
    // That constant lives in PluginEditor.h, which includes *this* header, so it
    // can't be named here — hence a bare value, and hence this note, so nobody
    // reads the number as the default and changes it expecting an effect.
    std::atomic<int> uiSkin { 0 };

    void setSkinGlobally (int skin);   // updates uiSkin and the global preference
    static int loadGlobalSkin (int fallback);

    // The key-colour dial (the editor's easter egg) is an app-wide preference
    // like the last-used skin, not project state: the palette it tints is one
    // per process, so every open window shows the same look, and a per-project
    // value would have two instances fighting over it.
    static int  loadGlobalKeyHue (int fallback);
    static void saveGlobalKeyHue (int stop);

    // Audition a note through the live-monitor voice (called from the editor's
    // on-screen keyboard on the message thread; picked up on the audio thread).
    void postAudition (int midiNote) noexcept { pendingAudition.store (midiNote); }

    // HOLD: a latching live-write mode. Armed, a note held down — on the editor's
    // keyboard or on anything sending MIDI in — is written at the playhead and
    // sustained over every step it spans, overwriting what it runs across, until
    // it is released. Deliberately not a parameter: it overwrites steps, so it
    // must never be automated or come back armed when a session is reopened.
    // It does outlive the editor, since MIDI keeps playing into it either way.
    std::atomic<bool> holdArmed { false };

    // The note the editor's keyboard is holding down (-1 for none). It sounds
    // for as long as it is held and drives HOLD while armed. Held as the note
    // *wanted* rather than as on/off edges, so a block that misses a change
    // still lands on the right state and can never strand a note sounding.
    // MIDI needs no equivalent: its note-offs are read straight from the block.
    void postHeldKey (int midiNote) noexcept { heldKey.store (midiNote); }
    void releaseHeldKey() noexcept { heldKey.store (-1); }

    // Folds `playingStep` into the note HOLD is currently writing. Called every
    // block from processBlock; public so the hold test can step the write head
    // directly instead of rendering a step's worth of audio per move.
    void updateHoldLatch (int playingStep);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handleMidiEvent (const juce::MidiMessage& msg);

    // The parameter ids behind one line's distortion. The enable and the
    // drive/color pair predate the type selector and were never named to a
    // shared scheme, so every id is spelled out rather than built from a line
    // prefix — concatenating strings on the audio thread would allocate.
    struct DistIds
    {
        const char* on;    const char* type;
        const char* drive; const char* color;
        const char* bits;  const char* rate;
        const char* foldAmount; const char* foldSym;
        const char* rectAmount; const char* rectTone;
        const char* lows;
    };

    static constexpr DistIds bassDistIds {
        "diston", "bdisttype", "distdrive", "distcolor",
        "bcrbits", "bcrrate", "bfoldamt", "bfoldsym",
        "brectamt", "brecttone", "bdistlows"
    };

    static constexpr DistIds drumDistIds {
        "ddiston", "ddisttype", "drumdrive", "ddistcolor",
        "dcrbits", "dcrrate", "dfoldamt", "dfoldsym",
        "drectamt", "drecttone", "ddistlows"
    };

    Distortion::Params distParams (const DistIds&) const;

    Synth303 synth;

public:
    Sequencer303  sequencer;       // exposed for the editor
    DrumSequencer drumSequencer;
    SongPlayer    song;

    // Song step currently playing, or -1 when song mode is off / the song is
    // empty. For the editor's playing-row highlight.
    int getSongStep() const { return songStepPlaying.load(); }

    // Drop the playhead on a song step. Only meaningful while free-running: a
    // host-synced song follows the host's timeline, so this is ignored there.
    void jumpSongToStep (int index);
    bool isHostSynced() const { return hostSyncedNow.load(); }

    // Back to the top of the song — what the transport's STOP does, since a
    // cued position otherwise survives stopping.
    void resetSongPosition() { songOffsetBeats.store (0.0); }

    // Song mode has its own transport, so selecting it never inherits a RUN
    // that was left on for pattern work: the song starts only when PLAY is
    // pressed (or when a host transport rolls).
    bool isSongMode() const { return apvts.getRawParameterValue ("playmode")->load() >= 1.5f; }
    bool isSongPlaying() const { return songPlaying.load(); }
    void startSong() { songPlaying.store (true); }
    void stopSong()  { songPlaying.store (false); resetSongPosition(); }

    // --- the song library ---------------------------------------------------
    // Song files are self-contained: they carry the pattern slots the
    // arrangement references, because a chain of slot numbers means nothing
    // without the patterns those numbers point at.

    static juce::File songLibraryFolder();          // ~/Music/BP303/Songs
    static constexpr const char* songFileSuffix = ".bp303song";

    bool saveSongToFile (const juce::File&);
    bool loadSongFromFile (const juce::File&);

    juce::String getSongName() const           { return songName; }
    void setSongName (const juce::String& n)   { songName = n; }

    // Pattern length (16th steps) of a bass slot — the song's step lengths come
    // from these, since the bass pattern owns the loop length.
    int slotLengthSteps (int slot) const
    {
        return bassPatterns[(size_t) juce::jlimit (0, numBassPatterns - 1, slot)].length;
    }

private:
    Synth303 monitorSynth;   // live-play voice heard over the sequence in Seq mode
    DrumMachine drums;
    Fx303 fx;          // bass delay
    Fx303 drumFx;      // drum delay
    Distortion bassDist, drumDist;   // per-line drive, ahead of each delay
    Pcf bassFilter, drumFilter;    // per-line envelope-followed BP/LP filter
    Compressor bassComp, drumComp; // per-line glue compressor
    Chorus bassChorus, drumChorus; // per-line modulated thickening
    Reverb bassReverb, drumReverb; // per-line space, last in each chain
    Metronome metronome;
    dsp303::MasterLimiter masterLimiter;   // safety ceiling on the summed mix
    std::vector<float>     drumBuffer, drumBufferR;
    std::vector<float>     monBuffer;
    // stand-in right channel for a host that hands us a mono block anyway
    std::vector<float>     spareRight;
    std::vector<SeqEvent>  seqEvents;
    std::vector<DrumEvent> drumEvents;
    bool lastSeqMode = false;

    BassPattern bassPatterns[numBassPatterns];
    DrumPattern drumPatterns[numDrumPatterns];
    std::atomic<int> currentBassPattern { 0 }, pendingBassPattern { 0 };
    std::atomic<int> currentDrumPattern { 0 }, pendingDrumPattern { 0 };
    std::atomic<int>  songStepPlaying { -1 };
    std::atomic<bool> songPlaying { false };

    // Song playback reads the transport phase shifted by songOffsetBeats, which
    // is how a row-jump moves the playhead without disturbing the transport.
    // The last two are published for the editor / jumpSongToStep.
    std::atomic<double> songOffsetBeats { 0.0 };
    std::atomic<bool>   songJumped { false };   // jump: switch patterns at once
    std::atomic<double> transportPhaseNow { 0.0 };
    std::atomic<bool>   hostSyncedNow { false };

    double sampleRateHz = 44100.0;   // captured in prepareToPlay

    // Live step-record: internal transport phase (beats), used to quantize
    // played notes when there is no host ppq. Also the master clock the bass and
    // drum sequencers snap to when free-running, so both lines and the click stay
    // locked to one phase however they are toggled on and off.
    double recPpq = 0.0;

    // On-screen keyboard audition: pendingAudition is posted by the UI; the audio
    // thread starts it on monitorSynth and releases it after auditionSamplesLeft.
    std::atomic<int> pendingAudition { -1 };
    int auditionActive = -1;
    int auditionSamplesLeft = 0;

    // The note the editor is holding down (-1 for none), and the one the monitor
    // voice is actually sounding for it. Reconciled every block.
    std::atomic<int> heldKey { -1 };
    int sustainHeld = -1;

    // HOLD's write head, all audio-thread state. `midiHeld` is the note MIDI is
    // holding, tracked here because note-offs only exist in the block; `latchNote`
    // is whichever source is winning; `latchHead` the step the run being written
    // started on (-1 when nothing is being written) and `latchStep` the last step
    // already folded into it.
    int midiHeld  = -1;
    int latchNote = -1;
    int latchHead = -1;
    int latchStep = -1;

    void trackHeldMidi (const juce::MidiBuffer& midi);
    void absorbIntoLatch (int step, int len);

    void recordNotes (juce::MidiBuffer& midi, int numSamples, double bpm,
                      double basePhaseBeats);

    void saveBassPatternTo (BassPattern&) const;
    void loadBassPatternFrom (const BassPattern&);
    void saveDrumPatternTo (DrumPattern&) const;
    void loadDrumPatternFrom (const DrumPattern&);

    // Pattern / song serialisation, shared by the plugin state and song files.
    static void writeBassPatXml (juce::XmlElement& pat, const BassPattern&);
    static void readBassPatXml  (const juce::XmlElement& pat, BassPattern&);
    static void writeDrumPatXml (juce::XmlElement& pat, const DrumPattern&);
    static void readDrumPatXml  (const juce::XmlElement& pat, DrumPattern&);
    void writeSongXml (juce::XmlElement& songXml) const;
    void readSongXml (const juce::XmlElement& songXml);

    juce::String songName;   // message thread only; shown in the editor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BP303AudioProcessor)
};
