#include "PluginProcessor.h"

#include "DspUtil.h"
#include "PluginEditor.h"

namespace
{
    // Live drum triggering uses General-MIDI drum notes on MIDI channel 10, so
    // it never collides with the bass. Returns a DrumMachine voice or -1.
    int gmNoteToVoice (int note)
    {
        switch (note)
        {
            case 35: case 36: return DrumMachine::BD;   // kick
            case 38: case 40: return DrumMachine::SD;   // snare
            case 37: case 39: return DrumMachine::CP;   // rim / clap
            case 42: case 44: return DrumMachine::CH;   // closed hat
            case 46: return DrumMachine::OH;            // open hat
            default: return -1;
        }
    }

    juce::PropertiesFile::Options bp303PropsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "BP303";
        o.filenameSuffix      = ".settings";
        o.folderName          = "BP303";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }
}

BP303AudioProcessor::BP303AudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    // The picker is hidden (right-click the BADPACKETS logo) but it is reachable,
    // so a new instance opens on whatever skin was chosen last.
    uiSkin.store (juce::jlimit (0, ui303::numSkins - 1,
                                loadGlobalSkin (ui303::defaultSkin)));
}

int BP303AudioProcessor::loadGlobalSkin (int fallback)
{
    juce::PropertiesFile props (bp303PropsOptions());
    return props.getIntValue ("uiSkin", fallback);
}

void BP303AudioProcessor::setSkinGlobally (int skin)
{
    uiSkin.store (skin);
    juce::PropertiesFile props (bp303PropsOptions());
    props.setValue ("uiSkin", skin);
    props.saveIfNeeded();
}

int BP303AudioProcessor::loadGlobalKeyHue (int fallback)
{
    juce::PropertiesFile props (bp303PropsOptions());
    return props.getIntValue ("uiKeyHue", fallback);
}

void BP303AudioProcessor::saveGlobalKeyHue (int stop)
{
    juce::PropertiesFile props (bp303PropsOptions());
    props.setValue ("uiKeyHue", stop);
    props.saveIfNeeded();
}

juce::AudioProcessorValueTreeState::ParameterLayout BP303AudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "wave", 1 }, "Wave",
        StringArray { "Saw", "Square", "Pulse" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "tuning", 1 }, "Tuning",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "cutoff", 1 }, "Cut Off",
        NormalisableRange<float> (60.0f, 5000.0f, 0.0f, 0.3f), 500.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "resonance", 1 }, "Resonance",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "envmod", 1 }, "Env Mod",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "decay", 1 }, "Decay",
        NormalisableRange<float> (30.0f, 2000.0f, 0.0f, 0.4f), 300.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "accent", 1 }, "Accent",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "volume", 1 }, "Volume",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "vibspeed", 1 }, "Vib Speed",
        NormalisableRange<float> (0.1f, 20.0f, 0.0f, 0.5f), 5.0f));

    // Depth is in semitones. The range runs well past a "polite" vibrato so the
    // top of the knob is a dramatic warble; the skew keeps small values easy to
    // dial in (mid-knob is roughly a couple of semitones).
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "vibdepth", 1 }, "Vib Depth",
        NormalisableRange<float> (0.0f, 12.0f, 0.0f, 0.45f), 0.0f));

    // Defaults to Seq: a new instance should play its own pattern straight away
    // rather than sitting silent until the mode is switched off Ext.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "playmode", 1 }, "Play Mode",
        StringArray { "Ext", "Seq", "Song" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "shuffle", 1 }, "Shuffle",
        NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "run", 1 }, "Run (no host)", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "rec", 1 }, "Record", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "metro", 1 }, "Metronome", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "intbpm", 1 }, "Internal BPM",
        NormalisableRange<float> (40.0f, 220.0f, 0.1f), 130.0f));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "kit", 1 }, "Drum Kit", StringArray { "606", "808", "909" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "drumvol", 1 }, "Drums Volume",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "bdtune", 1 }, "Kick Tune",
        NormalisableRange<float> (0.5f, 2.0f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "bddecay", 1 }, "Kick Decay",
        NormalisableRange<float> (0.05f, 1.5f, 0.0f, 0.5f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "sdtune", 1 }, "Snare Tune",
        NormalisableRange<float> (0.5f, 2.0f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "cptune", 1 }, "Clap Tune",
        NormalisableRange<float> (0.5f, 2.0f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "hattune", 1 }, "Hat Tune",
        NormalisableRange<float> (0.5f, 2.0f), 1.0f));

    const char* laneIds[]   = { "bdlvl", "sdlvl", "cplvl", "chlvl", "ohlvl" };
    const char* laneNames[] = { "Kick Level", "Snare Level", "Clap Level",
                                "CH Level", "OH Level" };
    const float laneDefaults[] = { 0.9f, 0.8f, 0.7f, 0.6f, 0.6f };
    for (int i = 0; i < 5; ++i)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { laneIds[i], 1 }, laneNames[i],
            NormalisableRange<float> (0.0f, 1.0f), laneDefaults[i]));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "basson", 1 }, "Bass Seq Active", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "drumson", 1 }, "Drum Seq Active", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "diston", 1 }, "Dist Active", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "distdrive", 1 }, "Dist Drive",
        NormalisableRange<float> (0.0f, 1.0f), 0.4f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "distcolor", 1 }, "Dist Color",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "delayon", 1 }, "Delay Active", false));

    // MONO is the single feedback line the delay has always been, and stays the
    // default so a project saved before STEREO existed loads sounding the same.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "delaytype", 1 }, "Delay Type",
        StringArray { "MONO", "STEREO" }, Fx303::Mono));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "delaytime", 1 }, "Delay Time",
        StringArray { "1/16", "1/8", "3/16", "1/4" }, 2));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "delayfb", 1 }, "Delay Feedback",
        NormalisableRange<float> (0.0f, 0.95f), 0.45f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "delaymix", 1 }, "Delay Mix",
        NormalisableRange<float> (0.0f, 1.0f), 0.25f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "ddiston", 1 }, "Drum Dist Active", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "drumdrive", 1 }, "Drum Drive",
        NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ddistcolor", 1 }, "Drum Dist Color",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "ddelayon", 1 }, "Drum Delay Active", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "ddelaytype", 1 }, "Drum Delay Type",
        StringArray { "MONO", "STEREO" }, Fx303::Mono));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "ddelaytime", 1 }, "Drum Delay Time",
        StringArray { "1/16", "1/8", "3/16", "1/4" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ddelayfb", 1 }, "Drum Delay Feedback",
        NormalisableRange<float> (0.0f, 0.95f), 0.35f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ddelaymix", 1 }, "Drum Delay Mix",
        NormalisableRange<float> (0.0f, 1.0f), 0.2f));

    // --- per-line PCF filter (BP/LP, envelope-followed) + compressor
    //     + chorus + reverb + the distortion type and its per-type controls ---
    // defaultDist keeps each line sounding as it always has: the bass came up on
    // the tanh overdrive, the drums on the industrial fuzz.
    struct { const char* prefix; const char* label; int defaultDist; } lines[] = {
        { "b", "Bass", Distortion::Soft }, { "d", "Drum", Distortion::Fuzz }
    };
    for (const auto& ln : lines)
    {
        const juce::String pre (ln.prefix);
        const juce::String name (ln.label);

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { pre + "flton", 1 }, name + " Filter Active", false));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { pre + "fltmode", 1 }, name + " Filter Mode",
            StringArray { "LP", "BP" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "fltcut", 1 }, name + " Filter Cutoff",
            NormalisableRange<float> (60.0f, 12000.0f, 0.0f, 0.3f), 2000.0f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "fltres", 1 }, name + " Filter Res",
            NormalisableRange<float> (0.0f, 1.0f), 0.3f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "fltenv", 1 }, name + " Filter Env",
            NormalisableRange<float> (0.0f, 1.0f), 0.3f));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { pre + "compon", 1 }, name + " Comp Active", false));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "compthr", 1 }, name + " Comp Threshold",
            NormalisableRange<float> (-40.0f, 0.0f, 0.1f), -18.0f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "comprat", 1 }, name + " Comp Ratio",
            NormalisableRange<float> (1.0f, 20.0f, 0.0f, 0.5f), 4.0f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "compmk", 1 }, name + " Comp Makeup",
            NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { pre + "chron", 1 }, name + " Chorus Active", false));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "chrrate", 1 }, name + " Chorus Rate",
            NormalisableRange<float> (0.05f, 8.0f, 0.0f, 0.4f), 0.8f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "chrdepth", 1 }, name + " Chorus Depth",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "chrmix", 1 }, name + " Chorus Mix",
            NormalisableRange<float> (0.0f, 1.0f), 0.4f));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { pre + "revon", 1 }, name + " Reverb Active", false));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "revsize", 1 }, name + " Reverb Size",
            NormalisableRange<float> (0.0f, 1.0f), 0.55f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "revdamp", 1 }, name + " Reverb Damp",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "revmix", 1 }, name + " Reverb Mix",
            NormalisableRange<float> (0.0f, 1.0f), 0.25f));

        // --- distortion character. The enable and the SOFT/FUZZ drive+color
        //     controls keep their original ids, so old projects load unchanged;
        //     the types added later bring their own controls. ---
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { pre + "disttype", 1 }, name + " Dist Type",
            StringArray { "SOFT", "FUZZ", "CRUSH", "FOLD", "RECT" }, ln.defaultDist));

        // BITS tops out at 12, not 16: past about 10 the quantisation noise is
        // below -50 dB and the knob's remaining travel did nothing audible,
        // which made the control read as broken. The default sits at 6 so
        // selecting CRUSH sounds like a crusher rather than like a bypass.
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "crbits", 1 }, name + " Crush Bits",
            NormalisableRange<float> (1.0f, 12.0f, 1.0f), 6.0f));
        // RATE is the fraction of the host rate the crusher runs at, so turning
        // the knob up raises the rate the way the label says. It used to be the
        // hold length in samples, which ran backwards: up meant holding longer,
        // meaning a *lower* rate, opposite to BITS on the same page. 1.0 is the
        // host rate untouched; 1/64 holds each sample for 64 of them. The
        // default of 0.5 keeps the decimation light enough not to mask BITS.
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "crrate", 1 }, name + " Crush Rate",
            NormalisableRange<float> (1.0f / 64.0f, 1.0f, 0.0f, 0.3f), 0.5f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "foldamt", 1 }, name + " Fold Amount",
            NormalisableRange<float> (0.0f, 1.0f), 0.4f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "foldsym", 1 }, name + " Fold Symmetry",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "rectamt", 1 }, name + " Rect Amount",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "recttone", 1 }, name + " Rect Tone",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f));

        // 0 = drive the full range, which is what every existing project has
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pre + "distlows", 1 }, name + " Dist Lows Kept",
            NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    }

    // Multipliers on each voice's own per-kit decay, so the kits keep their
    // character — see the note on DrumMachine::setParams. Skewed so 1.0x, the
    // stock kit, sits at the centre of the knob's travel.
    {
        const NormalisableRange<float> decayMul { 0.25f, 4.0f, 0.0f, 0.43f };
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "sddecay", 1 }, "Snare Decay", decayMul, 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "chdecay", 1 }, "Closed Hat Decay", decayMul, 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "ohdecay", 1 }, "Open Hat Decay", decayMul, 1.0f));
    }

    // Appended rather than filed next to DECAY where it belongs on the panel:
    // a host stores automation against the parameter's index, so inserting one
    // in the middle would silently re-point every lane after it in projects that
    // already exist. New parameters go on the end for the same reason enums do.
    // 0 ms is the 303 — no attack stage — so old projects load unchanged.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Attack",
        NormalisableRange<float> (0.0f, 500.0f, 0.0f, 0.5f), 0.0f));

    return layout;
}

Distortion::Params BP303AudioProcessor::distParams (const DistIds& ids) const
{
    Distortion::Params p;
    p.on          = apvts.getRawParameterValue (ids.on)->load() >= 0.5f;
    p.type        = (int) apvts.getRawParameterValue (ids.type)->load();
    p.drive       = apvts.getRawParameterValue (ids.drive)->load();
    p.color       = apvts.getRawParameterValue (ids.color)->load();
    p.bits        = apvts.getRawParameterValue (ids.bits)->load();
    // The knob is a rate; the shaper wants the hold length that produces it.
    p.rateSamples = 1.0f / std::max (1.0e-3f, apvts.getRawParameterValue (ids.rate)->load());
    p.foldAmount  = apvts.getRawParameterValue (ids.foldAmount)->load();
    p.foldSym     = apvts.getRawParameterValue (ids.foldSym)->load();
    p.rectAmount  = apvts.getRawParameterValue (ids.rectAmount)->load();
    p.rectTone    = apvts.getRawParameterValue (ids.rectTone)->load();
    p.lowsKept    = apvts.getRawParameterValue (ids.lows)->load();
    return p;
}

void BP303AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    synth.prepare (sampleRate);
    monitorSynth.prepare (sampleRate);
    metronome.prepare (sampleRate);
    monBuffer.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    sequencer.prepare (sampleRate);
    drums.prepare (sampleRate);
    drumSequencer.prepare (sampleRate);
    fx.prepare (sampleRate);
    drumFx.prepare (sampleRate);
    bassDist.prepare (sampleRate);
    drumDist.prepare (sampleRate);
    bassFilter.prepare (sampleRate);
    drumFilter.prepare (sampleRate);
    bassComp.prepare (sampleRate);
    drumComp.prepare (sampleRate);
    bassChorus.prepare (sampleRate);
    drumChorus.prepare (sampleRate);
    bassReverb.prepare (sampleRate);
    drumReverb.prepare (sampleRate);
    masterLimiter.prepare (sampleRate);
    drumBuffer.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    drumBufferR.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    spareRight.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    seqEvents.reserve (128);
    drumEvents.reserve (256);

    // The monitor voice has just been reset, so nothing is sounding for a key
    // that was held across the restart, and nothing is still being written.
    heldKey.store (-1);
    sustainHeld = -1;
    midiHeld = latchNote = latchHead = latchStep = -1;
}

bool BP303AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void BP303AudioProcessor::requestBassPattern (int idx)
{
    pendingBassPattern.store (juce::jlimit (0, numBassPatterns - 1, idx));
}

void BP303AudioProcessor::requestDrumPattern (int idx)
{
    pendingDrumPattern.store (juce::jlimit (0, numDrumPatterns - 1, idx));
}

void BP303AudioProcessor::writeBassPatXml (juce::XmlElement& pat, const BassPattern& p)
{
    pat.setAttribute ("length", p.length);
    for (int i = 0; i < Sequencer303::maxSteps; ++i)
    {
        auto* step = pat.createNewChildElement ("STEP");
        step->setAttribute ("pitch",  p.bass[i].pitch);
        step->setAttribute ("gate",   p.bass[i].gate);
        // "accent" stays a plain flag so a build that predates soft steps still
        // reads the accents out of a file this one wrote; "dyn" carries the full
        // three-way value for builds that understand it.
        step->setAttribute ("accent", p.bass[i].dyn > 0);
        step->setAttribute ("dyn",    p.bass[i].dyn);
        step->setAttribute ("slide",  p.bass[i].slide);
        step->setAttribute ("hold",   p.bass[i].hold);
    }
}

void BP303AudioProcessor::readBassPatXml (const juce::XmlElement& pat, BassPattern& p)
{
    p.length = juce::jlimit (1, 16, pat.getIntAttribute ("length", 16));
    int i = 0;
    for (auto* step : pat.getChildWithTagNameIterator ("STEP"))
    {
        if (i >= Sequencer303::maxSteps)
            break;
        p.bass[i].pitch  = step->getIntAttribute ("pitch");
        p.bass[i].gate   = step->getBoolAttribute ("gate");
        // absent "dyn" means a file written before soft steps existed, where the
        // accent flag was the whole story
        p.bass[i].dyn    = dyn303::clampDyn (
            step->hasAttribute ("dyn") ? step->getIntAttribute ("dyn")
                                       : (step->getBoolAttribute ("accent")
                                              ? dyn303::Hard
                                              : dyn303::Normal));
        p.bass[i].slide  = step->getBoolAttribute ("slide");
        p.bass[i].hold   = juce::jlimit (1, Sequencer303::maxSteps,
                                         step->getIntAttribute ("hold", 1));
        ++i;
    }
}

void BP303AudioProcessor::writeDrumPatXml (juce::XmlElement& pat, const DrumPattern& p)
{
    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        auto* laneXml = pat.createNewChildElement ("LANE");
        laneXml->setAttribute ("steps",   (int) p.drumSteps[lane]);
        laneXml->setAttribute ("accents", (int) p.drumAccents[lane]);
        // absent in files written before soft hits existed, which is exactly what
        // an all-zero mask means, so no fallback is needed on the way back in
        laneXml->setAttribute ("softs", (int) p.drumSofts[lane]);
    }
}

void BP303AudioProcessor::readDrumPatXml (const juce::XmlElement& pat, DrumPattern& p)
{
    int lane = 0;
    for (auto* laneXml : pat.getChildWithTagNameIterator ("LANE"))
    {
        if (lane >= DrumSequencer::numLanes)
            break;
        p.drumSteps[lane]   = (uint32_t) laneXml->getIntAttribute ("steps");
        p.drumAccents[lane] = (uint32_t) laneXml->getIntAttribute ("accents");
        p.drumSofts[lane] = (uint32_t) laneXml->getIntAttribute ("softs");
        ++lane;
    }
}

void BP303AudioProcessor::writeSongXml (juce::XmlElement& songXml) const
{
    songXml.setAttribute ("loop", song.isLooping());
    songXml.setAttribute ("name", songName);

    for (int i = 0; i < song.getCount(); ++i)
    {
        const auto s = song.getStep (i);
        auto* stepXml = songXml.createNewChildElement ("STEP");
        stepXml->setAttribute ("bass",  s.bassSlot);   // may be SongPlayer::hold
        stepXml->setAttribute ("drum",  s.drumSlot);
        stepXml->setAttribute ("reps",  s.repeats);
        stepXml->setAttribute ("bmute", s.bassMute);
        stepXml->setAttribute ("dmute", s.drumMute);
    }
}

void BP303AudioProcessor::readSongXml (const juce::XmlElement& songXml)
{
    song.clear();
    song.setLooping (songXml.getBoolAttribute ("loop", true));
    songName = songXml.getStringAttribute ("name");

    for (auto* stepXml : songXml.getChildWithTagNameIterator ("STEP"))
    {
        SongPlayer::Step s;
        s.bassSlot = juce::jlimit (SongPlayer::hold, numBassPatterns - 1,
                                   stepXml->getIntAttribute ("bass", 0));
        s.drumSlot = juce::jlimit (SongPlayer::hold, numDrumPatterns - 1,
                                   stepXml->getIntAttribute ("drum", 0));
        s.repeats  = stepXml->getIntAttribute ("reps", 1);
        s.bassMute = stepXml->getBoolAttribute ("bmute");
        s.drumMute = stepXml->getBoolAttribute ("dmute");
        song.insertStep (song.getCount(), s);
    }
}

juce::File BP303AudioProcessor::songLibraryFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("BP303").getChildFile ("Songs");
}

bool BP303AudioProcessor::saveSongToFile (const juce::File& file)
{
    juce::XmlElement root ("BP303Song");
    root.setAttribute ("version", 1);

    songName = file.getFileNameWithoutExtension();
    writeSongXml (*root.createNewChildElement ("SONG"));

    // Capture live edits before copying slots out.
    saveBassPatternTo (bassPatterns[(size_t) currentBassPattern.load()]);
    saveDrumPatternTo (drumPatterns[(size_t) currentDrumPattern.load()]);

    // Only the slots the arrangement actually references travel with it.
    std::array<bool, (size_t) numBassPatterns> bassUsed {};
    std::array<bool, (size_t) numDrumPatterns> drumUsed {};
    for (int i = 0; i < song.getCount(); ++i)
    {
        const auto s = song.getStep (i);
        if (s.bassSlot >= 0 && s.bassSlot < numBassPatterns) bassUsed[(size_t) s.bassSlot] = true;
        if (s.drumSlot >= 0 && s.drumSlot < numDrumPatterns) drumUsed[(size_t) s.drumSlot] = true;
    }

    auto* bassBank = root.createNewChildElement ("BASSPATTERNS");
    for (int s = 0; s < numBassPatterns; ++s)
        if (bassUsed[(size_t) s])
        {
            auto* pat = bassBank->createNewChildElement ("PAT");
            pat->setAttribute ("index", s);
            writeBassPatXml (*pat, bassPatterns[(size_t) s]);
        }

    auto* drumBank = root.createNewChildElement ("DRUMPATTERNS");
    for (int s = 0; s < numDrumPatterns; ++s)
        if (drumUsed[(size_t) s])
        {
            auto* pat = drumBank->createNewChildElement ("PAT");
            pat->setAttribute ("index", s);
            writeDrumPatXml (*pat, drumPatterns[(size_t) s]);
        }

    file.getParentDirectory().createDirectory();
    return root.writeTo (file);
}

bool BP303AudioProcessor::loadSongFromFile (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("BP303Song"))
        return false;

    if (auto* bassBank = xml->getChildByName ("BASSPATTERNS"))
        for (auto* pat : bassBank->getChildWithTagNameIterator ("PAT"))
        {
            const int s = pat->getIntAttribute ("index", -1);
            if (s >= 0 && s < numBassPatterns)
                readBassPatXml (*pat, bassPatterns[(size_t) s]);
        }

    if (auto* drumBank = xml->getChildByName ("DRUMPATTERNS"))
        for (auto* pat : drumBank->getChildWithTagNameIterator ("PAT"))
        {
            const int s = pat->getIntAttribute ("index", -1);
            if (s >= 0 && s < numDrumPatterns)
                readDrumPatXml (*pat, drumPatterns[(size_t) s]);
        }

    if (auto* songXml = xml->getChildByName ("SONG"))
        readSongXml (*songXml);

    songName = file.getFileNameWithoutExtension();

    // The live sequencers hold a copy of the current slots, which the incoming
    // patterns may have just replaced, so refresh them.
    loadBassPatternFrom (bassPatterns[(size_t) currentBassPattern.load()]);
    loadDrumPatternFrom (drumPatterns[(size_t) currentDrumPattern.load()]);
    return true;
}

void BP303AudioProcessor::jumpSongToStep (int index)
{
    if (hostSyncedNow.load())
        return;

    const auto slotSteps = [this] (int slot) { return slotLengthSteps (slot); };
    const double target = song.startBeats (index, slotSteps, sequencer.length.load());
    songOffsetBeats.store (transportPhaseNow.load() - target);
    songJumped.store (true);
}

BP303AudioProcessor::BassPattern BP303AudioProcessor::snapshotBassPattern (int slot) const
{
    slot = juce::jlimit (0, numBassPatterns - 1, slot);

    BassPattern p;
    if (slot == currentBassPattern.load())
        saveBassPatternTo (p);      // the running sequencer holds the live edits
    else
        p = bassPatterns[(size_t) slot];
    return p;
}

BP303AudioProcessor::DrumPattern BP303AudioProcessor::snapshotDrumPattern (int slot) const
{
    slot = juce::jlimit (0, numDrumPatterns - 1, slot);

    DrumPattern p;
    if (slot == currentDrumPattern.load())
        saveDrumPatternTo (p);
    else
        p = drumPatterns[(size_t) slot];
    return p;
}

void BP303AudioProcessor::saveBassPatternTo (BassPattern& p) const
{
    for (int i = 0; i < Sequencer303::maxSteps; ++i)
    {
        p.bass[i].pitch  = Sequencer303::loadPitch (sequencer.steps[i]);
        p.bass[i].gate   = sequencer.steps[i].gate.load();
        p.bass[i].dyn = dyn303::clampDyn (sequencer.steps[i].dyn.load());
        p.bass[i].slide  = sequencer.steps[i].slide.load();
        p.bass[i].hold   = sequencer.steps[i].hold.load();
    }
    p.length = sequencer.length.load();
}

void BP303AudioProcessor::loadBassPatternFrom (const BassPattern& p)
{
    for (int i = 0; i < Sequencer303::maxSteps; ++i)
    {
        Sequencer303::storePitch (sequencer.steps[i], p.bass[i].pitch);
        sequencer.steps[i].gate.store (p.bass[i].gate);
        sequencer.steps[i].dyn.store (dyn303::clampDyn (p.bass[i].dyn));
        sequencer.steps[i].slide.store (p.bass[i].slide);
        sequencer.steps[i].hold.store (juce::jlimit (1, Sequencer303::maxSteps, p.bass[i].hold));
    }
    sequencer.length.store (p.length);
}

void BP303AudioProcessor::saveDrumPatternTo (DrumPattern& p) const
{
    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        p.drumSteps[lane]   = drumSequencer.stepMask[lane].load();
        p.drumAccents[lane] = drumSequencer.accentMask[lane].load();
        p.drumSofts[lane] = drumSequencer.softMask[lane].load();
    }
}

void BP303AudioProcessor::loadDrumPatternFrom (const DrumPattern& p)
{
    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        drumSequencer.stepMask[lane].store (p.drumSteps[lane]);
        drumSequencer.accentMask[lane].store (p.drumAccents[lane]);
        drumSequencer.softMask[lane].store (p.drumSofts[lane]);
    }
    // the slot's masks came off disk, so they get the invariant applied
    drumSequencer.normalise();
}

void BP303AudioProcessor::handleMidiEvent (const juce::MidiMessage& msg)
{
    if (msg.isNoteOn())
    {
        // Classic convention: hard velocity = accent, overlapping notes = slide.
        // A gently played note comes in soft, the same threshold the step grid
        // and the MIDI export use.
        const int  dyn   = dyn303::dynFromVelocity (msg.getVelocity());
        const bool slide = synth.hasHeldNotes();
        synth.noteOn (msg.getNoteNumber(), dyn, slide);
    }
    else if (msg.isNoteOff())
    {
        synth.noteOff (msg.getNoteNumber());
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        synth.allNotesOff();
    }
}

void BP303AudioProcessor::trackHeldMidi (const juce::MidiBuffer& midi)
{
    // Note-offs are the whole point here, which is why HOLD can't ride on
    // recordNotes: that only ever looks at note-ons. Last note wins, so rolling
    // from key to key hands the held note straight over.
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.getChannel() == 10)
            continue;                       // channel 10 is drums

        if (msg.isNoteOn())
            midiHeld = msg.getNoteNumber();
        else if (msg.isNoteOff() && msg.getNoteNumber() == midiHeld)
            midiHeld = -1;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            midiHeld = -1;
    }
}

void BP303AudioProcessor::absorbIntoLatch (int step, int len)
{
    const int combined = latchNote - Sequencer303::baseNote;

    // A note's length is counted forward from its head and doesn't cross the
    // loop, so a note held through the end of the pattern can't stay one run.
    // The run stops at the end and a fresh one of the same note opens at the
    // top — which is what holding through the loop point sounds like anyway.
    if (step == 0 || step <= latchHead)
    {
        latchHead = step;
        auto& head = sequencer.steps[step];
        Sequencer303::storePitch (head, combined);
        head.gate.store (true);
        head.hold.store (1);
        head.slide.store (false);
        return;
    }

    // Destructive, as intended: the held note swallows this step, so whatever
    // was written here is cleared rather than left to reappear if the run is
    // later shortened.
    auto& covered = sequencer.steps[step];
    covered.gate.store (false);
    covered.hold.store (1);
    covered.slide.store (false);

    sequencer.steps[latchHead].hold.store (
        juce::jlimit (1, len - latchHead, step - latchHead + 1));
}

void BP303AudioProcessor::updateHoldLatch (int playingStep)
{
    // Whichever source is holding a note; MIDI wins if both are, which only
    // happens if you reach for the mouse without letting go of the keyboard.
    const int wanted = holdArmed.load() ? (midiHeld >= 0 ? midiHeld : heldKey.load())
                                        : -1;

    if (wanted != latchNote)
    {
        latchNote = wanted;
        latchHead = -1;         // whatever was being written stops where it is
        latchStep = -1;
    }

    // Nothing held, or nothing playing to write along: with no playhead there is
    // no run to lay down. The note still sounds — that part is the monitor voice.
    if (latchNote < 0 || playingStep < 0)
        return;

    const int len = juce::jlimit (1, Sequencer303::maxSteps, sequencer.length.load());

    if (latchHead < 0)
    {
        // First step of a new note: put it down where the playhead is.
        latchHead = juce::jlimit (0, len - 1, playingStep);
        latchStep = latchHead;

        auto& head = sequencer.steps[latchHead];
        Sequencer303::storePitch (head, latchNote - Sequencer303::baseNote);
        head.gate.store (true);
        head.hold.store (1);
        head.slide.store (false);
        return;
    }

    if (playingStep == latchStep)
        return;

    // Walk the steps the playhead has covered since the last block rather than
    // jumping straight to it, so a late block leaves no hole and the wrap at the
    // end of the pattern is just another step along the way.
    for (int step = latchStep, guard = 0; step != playingStep && guard < Sequencer303::maxSteps;
         ++guard)
    {
        step = (step + 1) % len;
        absorbIntoLatch (step, len);
    }

    latchStep = playingStep;
}

void BP303AudioProcessor::recordNotes (juce::MidiBuffer& midi, int numSamples,
                                       double bpm, double basePhaseBeats)
{
    const double sr = sampleRateHz;
    const int len = juce::jlimit (1, Sequencer303::maxSteps, sequencer.length.load());

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (! msg.isNoteOn())
            continue;

        // beat position of this note, quantized to the nearest 16th step
        const int off = juce::jlimit (0, numSamples - 1, metadata.samplePosition);
        const double beats = basePhaseBeats + (double) off / sr * bpm / 60.0;
        const long long step16 = (long long) std::llround (beats * 4.0);
        const int step = (int) (((step16 % len) + len) % len);
        const int dyn = dyn303::dynFromVelocity (msg.getVelocity());

        if (msg.getChannel() == 10)   // drums
        {
            const int voice = gmNoteToVoice (msg.getNoteNumber());
            if (voice >= 0)
            {
                drumSequencer.setDynAt (voice, step, dyn);
            }
        }
        else                          // bass
        {
            auto& s = sequencer.steps[step];
            s.gate.store (true);
            Sequencer303::storePitch (s, msg.getNoteNumber() - Sequencer303::baseNote);
            s.dyn.store (dyn);
        }
    }
}

void BP303AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // The choice parameter stores its index, and the index is the enum's order,
    // so a project saved before a wave was added still opens on the one it had.
    const auto wave = static_cast<Synth303::Wave> (
        juce::jlimit (0, Synth303::numWaves - 1,
                      (int) apvts.getRawParameterValue ("wave")->load()));
    const float pTuning = apvts.getRawParameterValue ("tuning")->load();
    const float pCutoff = apvts.getRawParameterValue ("cutoff")->load();
    const float pRes    = apvts.getRawParameterValue ("resonance")->load();
    const float pEnvMod = apvts.getRawParameterValue ("envmod")->load();
    const float pDecay  = apvts.getRawParameterValue ("decay")->load();
    const float pAccent = apvts.getRawParameterValue ("accent")->load();
    const float pVol    = apvts.getRawParameterValue ("volume")->load();
    const float pVibSpd = apvts.getRawParameterValue ("vibspeed")->load();
    const float pVibDep = apvts.getRawParameterValue ("vibdepth")->load();
    const float pAttack = apvts.getRawParameterValue ("attack")->load();

    // both the sequencer voice and the live-monitor voice share the patch
    synth.setParams (wave, pTuning, pCutoff, pRes, pEnvMod, pDecay, pAccent, pVol, pVibSpd, pVibDep, pAttack);
    monitorSynth.setParams (wave, pTuning, pCutoff, pRes, pEnvMod, pDecay, pAccent, pVol, pVibSpd, pVibDep, pAttack);

    auto* left = buffer.getWritePointer (0);
    const int numSamples = buffer.getNumSamples();

    // Ext = 0, Seq = 1, Song = 2. Seq and Song both run the sequencers; only
    // Song lets the arrangement drive which patterns they play.
    const float playMode = apvts.getRawParameterValue ("playmode")->load();
    const bool seqMode  = playMode >= 0.5f;
    const bool songMode = playMode >= 1.5f;
    if (seqMode != lastSeqMode)
    {
        synth.allNotesOff();
        monitorSynth.allNotesOff();
        sequencer.hardStop();
        lastSeqMode = seqMode;
    }

    double bpm = apvts.getRawParameterValue ("intbpm")->load();
    bool hostPlaying = false, hasPpq = false;
    double ppq = 0.0;

    if (auto* playHead = getPlayHead())
    {
        if (auto info = playHead->getPosition())
        {
            if (auto b = info->getBpm())
                bpm = *b;
            hostPlaying = info->getIsPlaying();
            if (auto p = info->getPpqPosition())
            {
                ppq = *p;
                hasPpq = true;
            }
        }
    }

    // Leaving song mode drops the song's transport, so coming back to it always
    // starts stopped rather than picking up where the RUN button happens to be.
    if (! songMode)
        songPlaying.store (false);

    const bool running = hostPlaying
                      || (songMode ? songPlaying.load()
                                   : apvts.getRawParameterValue ("run")->load() >= 0.5f);

    const bool bassOn  = apvts.getRawParameterValue ("basson")->load() >= 0.5f;
    const bool drumsOn = apvts.getRawParameterValue ("drumson")->load() >= 0.5f;

    // Live step-record: quantize played notes into the pattern (additive). The
    // internal record phase tracks the sequencer's own RUN clock so it stays
    // aligned when there is no host transport.
    const double blockBeats = (double) numSamples / sampleRateHz * bpm / 60.0;
    const double recPhaseStart = recPpq;
    if (running && seqMode)
        recPpq += blockBeats;
    else
        recPpq = 0.0;

    if (seqMode && running && apvts.getRawParameterValue ("rec")->load() >= 0.5f)
        recordNotes (midi, numSamples, bpm,
                     (hostPlaying && hasPpq) ? ppq : recPhaseStart);

    // HOLD reads the same incoming notes, whether or not REC is on: it is a way
    // of playing a pattern in, not a variant of step-record. Tracked before the
    // sequencer runs so a note pressed this block is already down when the write
    // head is stepped, just after.
    trackHeldMidi (midi);

    // One transport phase drives the bass, drums and click. The host ppq is used
    // when available, otherwise the internal RUN clock (recPhaseStart). Both
    // sequencers snap to it, so a line toggled on mid-pattern lands on the shared
    // grid instead of restarting from step 0 out of phase with the others.
    const double transportPhase = (hostPlaying && hasPpq) ? ppq : recPhaseStart;

    // --- song mode: the arrangement picks the patterns ----------------------
    // Song position is derived from the transport phase rather than counted as
    // it goes, so a host loop, a jump or starting mid-project all land on the
    // right step with no state to resync. We ask where the song will be at the
    // *end* of this block: on the block that crosses a step boundary that names
    // the next step's patterns, which the queued-switch code below then lands on
    // the very wrap that ends the step.
    bool songBassMute = false, songDrumMute = false;
    bool songJumpNow = false;

    transportPhaseNow.store (transportPhase);
    hostSyncedNow.store (hostPlaying && hasPpq);

    // A manual row-jump only applies to the internal clock — a host-synced song
    // follows the host's timeline. It survives stopping, so the transport's
    // FF/RW can cue a step to start from; STOP clears it (resetSongPosition).
    if (hostPlaying && hasPpq)
        songOffsetBeats.store (0.0);

    if (songMode)
    {
        // A pattern's length only reaches its slot when the slot is switched
        // away from, so keep the current one current for the length lookup.
        bassPatterns[(size_t) currentBassPattern.load()].length = sequencer.length.load();

        // A row-jump moves the playhead mid-pattern, so its patterns have to
        // land now rather than at the boundary they would otherwise wait for.
        songJumpNow = songJumped.exchange (false);

        const auto slotSteps = [this] (int slot) { return slotLengthSteps (slot); };
        const auto pos = song.locate (transportPhase - songOffsetBeats.load() + blockBeats,
                                      slotSteps, sequencer.length.load());

        songStepPlaying.store (pos.stepIndex);

        if (pos.stepIndex >= 0)
        {
            songBassMute = pos.bassMute;
            songDrumMute = pos.drumMute;

            // Run off the end with looping off: hold the last step, silent. We
            // deliberately don't clear the RUN parameter from the audio thread.
            if (pos.finished)
                songBassMute = songDrumMute = true;

            // A slot still reading `hold` here means no row up to this point has
            // named that line — a *leading* hold, with nothing to carry on from.
            // Leaving it playing would sound whichever pattern happened to be
            // loaded when PLAY was pressed, so the same song would play
            // differently from run to run. Stay silent until a row names a slot;
            // holds after that carry the named pattern on as usual.
            if (pos.bassSlot != SongPlayer::hold)
                requestBassPattern (pos.bassSlot);
            else
                songBassMute = true;

            if (pos.drumSlot != SongPlayer::hold)
                requestDrumPattern (pos.drumSlot);
            else
                songDrumMute = true;

            // Count this step's patterns from the phase the step began at, so a
            // pattern of a different length to the last one starts at its own
            // step 0 rather than entering partway through. Measured back from
            // the transport phase, so any row-jump offset cancels out.
            const double origin = transportPhase + blockBeats - SongPlayer::beatsIntoStep (pos);
            sequencer.phaseOrigin.store (origin);
            drumSequencer.phaseOrigin.store (origin);
        }
    }
    else
    {
        songStepPlaying.store (-1);
        songJumped.store (false);
        sequencer.phaseOrigin.store (0.0);
        drumSequencer.phaseOrigin.store (0.0);
    }

    // Queued pattern switches: bass and drums each land at their own next
    // pattern boundary while that line runs, immediately otherwise.
    const int pendingBass = pendingBassPattern.load();
    if (pendingBass != currentBassPattern.load())
    {
        bool doSwitch = songJumpNow || ! (seqMode && running && bassOn);
        if (! doSwitch)
            doSwitch = sequencer.samplesUntilPatternStart (bpm) <= (double) numSamples;
        if (doSwitch)
        {
            saveBassPatternTo (bassPatterns[(size_t) currentBassPattern.load()]);
            loadBassPatternFrom (bassPatterns[(size_t) pendingBass]);
            currentBassPattern.store (pendingBass);
        }
    }

    const int pendingDrum = pendingDrumPattern.load();
    if (pendingDrum != currentDrumPattern.load())
    {
        bool doSwitch = songJumpNow || ! (seqMode && running && drumsOn);
        if (! doSwitch)
            doSwitch = drumSequencer.samplesUntilPatternStart (bpm, sequencer.length.load())
                           <= (double) numSamples;
        if (doSwitch)
        {
            saveDrumPatternTo (drumPatterns[(size_t) currentDrumPattern.load()]);
            loadDrumPatternFrom (drumPatterns[(size_t) pendingDrum]);
            currentDrumPattern.store (pendingDrum);
        }
    }

    // On-screen keyboard audition: start any note the UI posted on the monitor
    // voice; it is released a fraction of a second later (below).
    if (const int req = pendingAudition.exchange (-1); req >= 0)
    {
        if (auditionActive >= 0)
            monitorSynth.noteOff (auditionActive);
        monitorSynth.noteOn (req, dyn303::Normal, false);
        auditionActive = req;
        auditionSamplesLeft = (int) (0.20 * sampleRateHz);
    }

    // A sustained audition (a key held down in HOLD mode) is a state, not an
    // event, so it is reconciled against what the monitor voice is playing
    // rather than gated on a timer. The amp envelope follows the gate, so a note
    // with no matching note-off simply stays up while its filter envelope falls
    // away — which is what sustaining on a 303 sounds like.
    // A held note from MIDI already sounds — the monitor loop below plays the
    // incoming notes — so only the editor's keyboard needs a voice of its own.
    if (const int want = heldKey.load(); want != sustainHeld)
    {
        // A blip and a held note would fight over the one mono voice, and the
        // blip's note-off would cut the held note short if they shared a number.
        // The blip gives way.
        if (want >= 0 && auditionActive >= 0)
        {
            monitorSynth.noteOff (auditionActive);
            auditionActive = -1;
        }

        // New note on before the old one off, so the voice never sees an empty
        // held-note stack in between and the gate stays up: moving from key to
        // key glides instead of retriggering, the way a slid step does.
        if (want >= 0)
            monitorSynth.noteOn (want, dyn303::Normal, sustainHeld >= 0);
        if (sustainHeld >= 0)
            monitorSynth.noteOff (sustainHeld);

        sustainHeld = want;
    }

    if (seqMode)
    {
        sequencer.process (numSamples, bpm, running && bassOn && ! songBassMute,
                           running, transportPhase,
                           apvts.getRawParameterValue ("shuffle")->load(), seqEvents);

        // Step HOLD's write head after the sequencer has moved, so it works from
        // where the playhead actually is rather than from where it was a block
        // ago. What it writes is picked up on the next block, and on the next
        // time round the loop.
        updateHoldLatch (sequencer.playingStep.load());

        int pos = 0;
        for (const auto& e : seqEvents)
        {
            synth.render (left + pos, e.offset - pos);
            pos = e.offset;
            if (e.noteOn)
                synth.noteOn (e.note, e.dyn, e.slide);
            else
                synth.noteOff (e.note);
        }
        synth.render (left + pos, numSamples - pos);

        // Live-monitor voice: play the notes you press over the sequence so you
        // hear yourself while auditioning / recording. Drums already trigger
        // live via the channel-10 path.
        if ((int) monBuffer.size() < numSamples)
            monBuffer.resize ((size_t) numSamples);
        std::fill_n (monBuffer.begin(), numSamples, 0.0f);
        float* mbuf = monBuffer.data();

        int mpos = 0;
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.getChannel() == 10)
                continue;   // drums, not bass
            const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
            monitorSynth.render (mbuf + mpos, eventTime - mpos);
            mpos = eventTime;
            if (msg.isNoteOn())
                monitorSynth.noteOn (msg.getNoteNumber(),
                                     dyn303::dynFromVelocity (msg.getVelocity()),
                                     monitorSynth.hasHeldNotes());
            else if (msg.isNoteOff())
                monitorSynth.noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                monitorSynth.allNotesOff();
        }
        monitorSynth.render (mbuf + mpos, numSamples - mpos);
        juce::FloatVectorOperations::add (left, mbuf, numSamples);
    }
    else
    {
        int pos = 0;
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
            synth.render (left + pos, eventTime - pos);
            pos = eventTime;
            if (msg.getChannel() != 10)   // channel 10 is reserved for drums
                handleMidiEvent (msg);
        }
        synth.render (left + pos, numSamples - pos);

        // Ext mode has no monitor loop, but the step-grid keyboard can still
        // audition notes through the monitor voice.
        if ((int) monBuffer.size() < numSamples)
            monBuffer.resize ((size_t) numSamples);
        std::fill_n (monBuffer.begin(), numSamples, 0.0f);
        monitorSynth.render (monBuffer.data(), numSamples);
        juce::FloatVectorOperations::add (left, monBuffer.data(), numSamples);
    }

    // Release the audition note once its short gate elapses.
    if (auditionActive >= 0 && (auditionSamplesLeft -= numSamples) <= 0)
    {
        monitorSynth.noteOff (auditionActive);
        auditionActive = -1;
    }

    // Bass line FX chain: filter -> distortion -> delay -> compressor -> chorus
    // -> reverb. Chorus and reverb sit after the compressor so the glue works on
    // the line itself rather than pumping on its own tail.
    //
    // Everything up to the delay is mono — one synth voice, one drive. The delay
    // is where a line can first become stereo, so from there on the chain runs on
    // a channel pair. None of the units after it widens anything by itself, so a
    // MONO delay leaves the two channels identical all the way to the output and
    // the line sounds exactly as it did before STEREO existed.
    //
    // The plugin only ever reports a stereo output (isBusesLayoutSupported), but
    // a mono buffer from a host that ignores that would leave the chain writing
    // off the end of the block, so it gets a scratch channel to discard instead.
    if ((int) spareRight.size() < numSamples)
        spareRight.resize ((size_t) numSamples);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1)
                                              : spareRight.data();

    bassFilter.setParams (apvts.getRawParameterValue ("bflton")->load() >= 0.5f,
                          (int) apvts.getRawParameterValue ("bfltmode")->load(),
                          apvts.getRawParameterValue ("bfltcut")->load(),
                          apvts.getRawParameterValue ("bfltres")->load(),
                          apvts.getRawParameterValue ("bfltenv")->load());
    bassFilter.process (left, numSamples);

    bassDist.setParams (distParams (bassDistIds));
    bassDist.process (left, numSamples);

    // the mono line so far, mirrored into the right channel to enter the delay
    juce::FloatVectorOperations::copy (right, left, numSamples);

    fx.setParams (apvts.getRawParameterValue ("delayon")->load() >= 0.5f,
                  (int) apvts.getRawParameterValue ("delaytype")->load(),
                  (int) apvts.getRawParameterValue ("delaytime")->load() + 1,
                  apvts.getRawParameterValue ("delayfb")->load(),
                  apvts.getRawParameterValue ("delaymix")->load(),
                  bpm);
    fx.process (left, right, numSamples);

    bassComp.setParams (apvts.getRawParameterValue ("bcompon")->load() >= 0.5f,
                        apvts.getRawParameterValue ("bcompthr")->load(),
                        apvts.getRawParameterValue ("bcomprat")->load(),
                        apvts.getRawParameterValue ("bcompmk")->load());
    bassComp.process (left, right, numSamples);

    bassChorus.setParams (apvts.getRawParameterValue ("bchron")->load() >= 0.5f,
                          apvts.getRawParameterValue ("bchrrate")->load(),
                          apvts.getRawParameterValue ("bchrdepth")->load(),
                          apvts.getRawParameterValue ("bchrmix")->load());
    bassChorus.process (left, right, numSamples);

    bassReverb.setParams (apvts.getRawParameterValue ("brevon")->load() >= 0.5f,
                          apvts.getRawParameterValue ("brevsize")->load(),
                          apvts.getRawParameterValue ("brevdamp")->load(),
                          apvts.getRawParameterValue ("brevmix")->load());
    bassReverb.process (left, right, numSamples);

    // ---- Drums: sequencer pattern (Seq mode) plus live MIDI on channel 10
    //             (any mode), so voices/echoes render and can be finger-drummed.
    if (seqMode)
        drumSequencer.process (numSamples, bpm, running && drumsOn && ! songDrumMute,
                               running, transportPhase,
                               apvts.getRawParameterValue ("shuffle")->load(),
                               sequencer.length.load(), drumEvents);
    else
        drumEvents.clear();

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 10)
        {
            const int voice = gmNoteToVoice (msg.getNoteNumber());
            if (voice >= 0)
                drumEvents.push_back ({ juce::jlimit (0, numSamples - 1, metadata.samplePosition),
                                        voice, msg.getVelocity() >= 100 });
        }
    }

    // keep events time-ordered for the gap-render loop below (std::sort is
    // in-place, so no audio-thread allocation)
    std::sort (drumEvents.begin(), drumEvents.end(),
               [] (const DrumEvent& a, const DrumEvent& b) { return a.offset < b.offset; });

    {
        const float laneLevels[] = {
            apvts.getRawParameterValue ("bdlvl")->load(),
            apvts.getRawParameterValue ("sdlvl")->load(),
            apvts.getRawParameterValue ("cplvl")->load(),
            apvts.getRawParameterValue ("chlvl")->load(),
            apvts.getRawParameterValue ("ohlvl")->load(),
        };
        const int kitIdx = (int) apvts.getRawParameterValue ("kit")->load();
        const auto kit = kitIdx == 0 ? DrumMachine::Kit::K606
                       : kitIdx == 2 ? DrumMachine::Kit::K909
                                     : DrumMachine::Kit::K808;
        drums.setParams (
            kit,
            apvts.getRawParameterValue ("bdtune")->load(),
            apvts.getRawParameterValue ("sdtune")->load(),
            apvts.getRawParameterValue ("cptune")->load(),
            apvts.getRawParameterValue ("hattune")->load(),
            apvts.getRawParameterValue ("bddecay")->load(),
            laneLevels,
            apvts.getRawParameterValue ("drumvol")->load(),
            apvts.getRawParameterValue ("sddecay")->load(),
            apvts.getRawParameterValue ("chdecay")->load(),
            apvts.getRawParameterValue ("ohdecay")->load());

        // Render drums into their own buffer so the drum delay applies to the
        // drum sum only, then mix into the master. Voices render mono; the second
        // buffer picks the line up at the delay, as on the bass line.
        if ((int) drumBuffer.size() < numSamples)
            drumBuffer.resize ((size_t) numSamples);
        if ((int) drumBufferR.size() < numSamples)
            drumBufferR.resize ((size_t) numSamples);
        std::fill_n (drumBuffer.begin(), numSamples, 0.0f);
        float* dbuf  = drumBuffer.data();
        float* dbufR = drumBufferR.data();

        int dpos = 0;
        for (const auto& e : drumEvents)
        {
            drums.render (dbuf + dpos, e.offset - dpos);
            dpos = e.offset;
            drums.trigger (e.voice, e.dyn);
        }
        drums.render (dbuf + dpos, numSamples - dpos);

        // Drum line FX chain: filter -> distortion -> delay -> compressor
        // -> chorus -> reverb, mirroring the bass line.
        drumFilter.setParams (apvts.getRawParameterValue ("dflton")->load() >= 0.5f,
                              (int) apvts.getRawParameterValue ("dfltmode")->load(),
                              apvts.getRawParameterValue ("dfltcut")->load(),
                              apvts.getRawParameterValue ("dfltres")->load(),
                              apvts.getRawParameterValue ("dfltenv")->load());
        drumFilter.process (dbuf, numSamples);

        // Drive on the drum bus, ahead of the delay so the echoes stay gritty
        drumDist.setParams (distParams (drumDistIds));
        drumDist.process (dbuf, numSamples);

        juce::FloatVectorOperations::copy (dbufR, dbuf, numSamples);

        drumFx.setParams (apvts.getRawParameterValue ("ddelayon")->load() >= 0.5f,
                          (int) apvts.getRawParameterValue ("ddelaytype")->load(),
                          (int) apvts.getRawParameterValue ("ddelaytime")->load() + 1,
                          apvts.getRawParameterValue ("ddelayfb")->load(),
                          apvts.getRawParameterValue ("ddelaymix")->load(),
                          bpm);
        drumFx.process (dbuf, dbufR, numSamples);

        drumComp.setParams (apvts.getRawParameterValue ("dcompon")->load() >= 0.5f,
                            apvts.getRawParameterValue ("dcompthr")->load(),
                            apvts.getRawParameterValue ("dcomprat")->load(),
                            apvts.getRawParameterValue ("dcompmk")->load());
        drumComp.process (dbuf, dbufR, numSamples);

        drumChorus.setParams (apvts.getRawParameterValue ("dchron")->load() >= 0.5f,
                              apvts.getRawParameterValue ("dchrrate")->load(),
                              apvts.getRawParameterValue ("dchrdepth")->load(),
                              apvts.getRawParameterValue ("dchrmix")->load());
        drumChorus.process (dbuf, dbufR, numSamples);

        drumReverb.setParams (apvts.getRawParameterValue ("drevon")->load() >= 0.5f,
                              apvts.getRawParameterValue ("drevsize")->load(),
                              apvts.getRawParameterValue ("drevdamp")->load(),
                              apvts.getRawParameterValue ("drevmix")->load());
        drumReverb.process (dbuf, dbufR, numSamples);

        juce::FloatVectorOperations::add (left,  dbuf,  numSamples);
        juce::FloatVectorOperations::add (right, dbufR, numSamples);
    }

    // Metronome click, on the same shared transport phase as the sequencers
    metronome.process (left, right, numSamples, transportPhase, bpm,
                       running && apvts.getRawParameterValue ("metro")->load() >= 0.5f);

    // ---- Master stage -------------------------------------------------------
    // Both lines are scaled by the same amount, so the balance set on the drum
    // and bass volumes carries over untouched — the sum just sits low enough
    // that the ceiling below has nothing to do.
    //
    // This used to be a bare tanh across the mix, which is a nonlinearity fed by
    // the *sum*: the gain it applied to the bass moved with the drum waveform
    // riding on top of it, so every kick amplitude-modulated the line and threw
    // non-harmonic sidebands over it. Pulse suffered worst, since a pulse sits at
    // its peak for most of the cycle instead of passing through it and so lived
    // in the compressed part of the curve before the drums even arrived. With
    // the headroom in front and a gain-based limiter behind, the bass is
    // sample-for-sample identical whether or not the drums play.
    juce::FloatVectorOperations::multiply (left,  dsp303::masterHeadroom, numSamples);
    juce::FloatVectorOperations::multiply (right, dsp303::masterHeadroom, numSamples);

    masterLimiter.process (left, right, numSamples);
}

juce::AudioProcessorEditor* BP303AudioProcessor::createEditor()
{
    return new BP303AudioProcessorEditor (*this);
}

void BP303AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("BP303State");
    root.setAttribute ("skin", uiSkin.load());
    root.setAttribute ("uiwidth", editorWidth.load());

    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    // capture live edits into the current slots before serializing the banks
    saveBassPatternTo (bassPatterns[(size_t) currentBassPattern.load()]);
    saveDrumPatternTo (drumPatterns[(size_t) currentDrumPattern.load()]);

    auto* bassBank = root.createNewChildElement ("BASSPATTERNS");
    bassBank->setAttribute ("current", currentBassPattern.load());
    for (int s = 0; s < numBassPatterns; ++s)
    {
        const auto& p = bassPatterns[s];
        if (p.isEmpty() && s != currentBassPattern.load())
            continue;

        auto* pat = bassBank->createNewChildElement ("PAT");
        pat->setAttribute ("index", s);
        writeBassPatXml (*pat, p);
    }

    auto* drumBank = root.createNewChildElement ("DRUMPATTERNS");
    drumBank->setAttribute ("current", currentDrumPattern.load());
    for (int s = 0; s < numDrumPatterns; ++s)
    {
        const auto& p = drumPatterns[s];
        if (p.isEmpty() && s != currentDrumPattern.load())
            continue;

        auto* pat = drumBank->createNewChildElement ("PAT");
        pat->setAttribute ("index", s);
        writeDrumPatXml (*pat, p);
    }

    // --- the song: the arrangement, in order --------------------------------
    writeSongXml (*root.createNewChildElement ("SONG"));

    copyXmlToBinary (root, destData);
}

void BP303AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    if (xml->hasTagName ("BP303State"))
    {
        // A project reopens on the skin it was saved with; absent the attribute
        // (or out of range, e.g. a state written by a build with fewer skins)
        // the global preference loaded in the constructor stands.
        uiSkin.store (juce::jlimit (0, ui303::numSkins - 1,
                                    xml->getIntAttribute ("skin", uiSkin.load())));
        editorWidth.store (xml->getIntAttribute ("uiwidth", 1466));

        if (auto* params = xml->getChildByName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*params));

        for (auto& p : bassPatterns) p = BassPattern {};
        for (auto& p : drumPatterns) p = DrumPattern {};

        auto readBassPat = [] (juce::XmlElement* pat, BassPattern& p) { readBassPatXml (*pat, p); };
        auto readDrumPat = [] (juce::XmlElement* pat, DrumPattern& p) { readDrumPatXml (*pat, p); };

        if (auto* bassBank = xml->getChildByName ("BASSPATTERNS"))
        {
            // current format: independent bass + drum banks
            for (auto* pat : bassBank->getChildWithTagNameIterator ("PAT"))
            {
                const int s = pat->getIntAttribute ("index", -1);
                if (s >= 0 && s < numBassPatterns)
                    readBassPat (pat, bassPatterns[s]);
            }
            const int cb = juce::jlimit (0, numBassPatterns - 1,
                                         bassBank->getIntAttribute ("current", 0));
            currentBassPattern.store (cb);
            pendingBassPattern.store (cb);
            loadBassPatternFrom (bassPatterns[(size_t) cb]);

            int cd = 0;
            if (auto* drumBank = xml->getChildByName ("DRUMPATTERNS"))
            {
                for (auto* pat : drumBank->getChildWithTagNameIterator ("PAT"))
                {
                    const int s = pat->getIntAttribute ("index", -1);
                    if (s >= 0 && s < numDrumPatterns)
                        readDrumPat (pat, drumPatterns[s]);
                }
                cd = juce::jlimit (0, numDrumPatterns - 1,
                                   drumBank->getIntAttribute ("current", 0));
            }
            currentDrumPattern.store (cd);
            pendingDrumPattern.store (cd);
            loadDrumPatternFrom (drumPatterns[(size_t) cd]);
        }
        else if (auto* bank = xml->getChildByName ("PATTERNS"))
        {
            // legacy combined bank: split each slot into the bass + drum banks
            for (auto* pat : bank->getChildWithTagNameIterator ("PAT"))
            {
                const int s = pat->getIntAttribute ("index", -1);
                if (s < 0 || s >= numBassPatterns)   // 27; drops old slots 27..47
                    continue;
                readBassPat (pat, bassPatterns[s]);
                readDrumPat (pat, drumPatterns[s]);
            }
            const int current = juce::jlimit (0, numBassPatterns - 1,
                                              bank->getIntAttribute ("current", 0));
            currentBassPattern.store (current);
            pendingBassPattern.store (current);
            currentDrumPattern.store (current);
            pendingDrumPattern.store (current);
            loadBassPatternFrom (bassPatterns[(size_t) current]);
            loadDrumPatternFrom (drumPatterns[(size_t) current]);
        }

        // The song. Absent in states saved before song mode existed, which load
        // as an empty arrangement.
        if (auto* songXml = xml->getChildByName ("SONG"))
        {
            readSongXml (*songXml);
        }
        else
        {
            song.clear();
            song.setLooping (true);
            songName = {};
        }

        // legacy single-pattern states from the earliest builds
        if (auto* pattern = xml->getChildByName ("PATTERN"))
        {
            sequencer.length.store (pattern->getIntAttribute ("length", 16));
            int i = 0;
            for (auto* step : pattern->getChildWithTagNameIterator ("STEP"))
            {
                if (i >= Sequencer303::maxSteps)
                    break;
                sequencer.steps[i].key.store (step->getIntAttribute ("key"));
                sequencer.steps[i].octave.store (step->getIntAttribute ("octave"));
                sequencer.steps[i].gate.store (step->getBoolAttribute ("gate"));
                sequencer.steps[i].dyn.store (step->getBoolAttribute ("accent")
                                                  ? dyn303::Hard
                                                  : dyn303::Normal);
                sequencer.steps[i].slide.store (step->getBoolAttribute ("slide"));
                sequencer.steps[i].hold.store (1);
                ++i;
            }

            if (auto* drumsXml = xml->getChildByName ("DRUMS"))
            {
                int lane = 0;
                for (auto* laneXml : drumsXml->getChildWithTagNameIterator ("LANE"))
                {
                    if (lane >= DrumSequencer::numLanes)
                        break;
                    drumSequencer.stepMask[lane].store ((uint32_t) laneXml->getIntAttribute ("steps"));
                    drumSequencer.accentMask[lane].store ((uint32_t) laneXml->getIntAttribute ("accents"));
                    drumSequencer.softMask[lane].store (0);
                    ++lane;
                }
                drumSequencer.normalise();
            }

            currentBassPattern.store (0);
            pendingBassPattern.store (0);
            currentDrumPattern.store (0);
            pendingDrumPattern.store (0);
            saveBassPatternTo (bassPatterns[0]);
            saveDrumPatternTo (drumPatterns[0]);
        }
    }
    else if (xml->hasTagName (apvts.state.getType()))
    {
        // Legacy state from earlier builds: parameters only
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BP303AudioProcessor();
}
