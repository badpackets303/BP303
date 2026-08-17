cmake_minimum_required(VERSION 3.22)
project(BP303 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Both Mac architectures, so a released standalone runs on Intel as well as
# Apple Silicon. Roughly doubles the build, so override it while developing:
# -DCMAKE_OSX_ARCHITECTURES=arm64. project() has already put an empty entry in
# the cache by this point, so a plain set() here would be ignored — only fill it
# in when nothing was actually asked for.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures to build" FORCE)
endif()

# Static CRT, so the Windows standalone is one self-contained .exe. Someone
# unzipping a demo should not first have to go and find a Visual C++
# redistributable, which is what the default dynamic runtime would ask of them.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BP303
    COMPANY_NAME              "JC Audio"
    BUNDLE_ID                 com.jcaudio.bp303
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    PLUGIN_MANUFACTURER_CODE  Jcau
    PLUGIN_CODE               Bp33
    FORMATS                   AU Standalone
    PRODUCT_NAME              "BP303"
    COPY_PLUGIN_AFTER_BUILD   TRUE)

target_sources(BP303 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(BP303 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Native host-window centring (Objective-C++). Only the plugin gets the real
# implementation; the console tools fall back to the header's inline no-op.
if(APPLE)
    enable_language(OBJCXX)
    target_sources(BP303 PRIVATE Source/HostWindowCentre.mm)
    target_compile_definitions(BP303 PRIVATE BP303_HAS_NATIVE_WINDOW=1)
    target_link_libraries(BP303 PRIVATE "-framework AppKit")
endif()

target_link_libraries(BP303
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless snapshot tool for visually checking the editor during development
juce_add_console_app(BP303_Snapshot PRODUCT_NAME "BP303_Snapshot")
target_sources(BP303_Snapshot PRIVATE
    Tools/editor_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_Snapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_Snapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders one snapshot per FX tab, for checking every BASS FX / DRUM FX page
juce_add_console_app(BP303_FxTabSnapshot PRODUCT_NAME "BP303_FxTabSnapshot")
target_sources(BP303_FxTabSnapshot PRIVATE
    Tools/fxtab_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_FxTabSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_FxTabSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders one snapshot per DRUMS tab, for checking MIX / TUNE / DECAY
juce_add_console_app(BP303_DrumTabSnapshot PRODUCT_NAME "BP303_DrumTabSnapshot")
target_sources(BP303_DrumTabSnapshot PRIVATE
    Tools/drumtab_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_DrumTabSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_DrumTabSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders one snapshot per EQ tab, with a curve dialled in and the band meters
# live — the only way to see the meters move without opening a host
juce_add_console_app(BP303_EqTabSnapshot PRODUCT_NAME "BP303_EqTabSnapshot")
target_sources(BP303_EqTabSnapshot PRIVATE
    Tools/eqtab_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_EqTabSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_EqTabSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders the LFO row running, one snapshot per shape plus the knob rings — at
# rest the row draws a greyed wave with no dot and no rings
juce_add_console_app(BP303_LfoSnapshot PRODUCT_NAME "BP303_LfoSnapshot")
target_sources(BP303_LfoSnapshot PRIVATE
    Tools/lfo_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_LfoSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # The rings come off the scope's timer, so the ring snapshot has to let the
    # message loop run for a tick. Same dev-aid-only exception the pad's makes.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="BP303")
target_link_libraries(BP303_LfoSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders one snapshot per XY pad mode with the pad held off centre — at rest
# the pad draws a handle in an empty square and none of the states worth seeing
juce_add_console_app(BP303_PadTabSnapshot PRODUCT_NAME "BP303_PadTabSnapshot")
target_sources(BP303_PadTabSnapshot PRIVATE
    Tools/padtab_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_PadTabSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # The pad's sparks come off its own timer, so the drag snapshot has to let
    # the message loop run between the moves. Only this dev aid needs it — the
    # plugin never runs a modal loop.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="BP303")
target_link_libraries(BP303_PadTabSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders every step of the help tour — the only way to proofread it, and to see
# a step whose text has outgrown its callout or landed on top of what it points at
juce_add_console_app(BP303_HelpSnapshot PRODUCT_NAME "BP303_HelpSnapshot")
target_sources(BP303_HelpSnapshot PRIVATE
    Tools/help_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_HelpSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_HelpSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders one knob across its whole travel, per skin, for judging anything that
# varies along the sweep — the editor snapshots only ever show one position
juce_add_console_app(BP303_KnobSnapshot PRODUCT_NAME "BP303_KnobSnapshot")
target_sources(BP303_KnobSnapshot PRIVATE
    Tools/knob_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_KnobSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_KnobSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless reproduction of an audio-device / sample-rate change with a live editor
juce_add_console_app(BP303_DeviceTest PRODUCT_NAME "BP303_DeviceTest")
target_sources(BP303_DeviceTest PRIVATE
    Tools/device_change_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_DeviceTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="BP303")
target_link_libraries(BP303_DeviceTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test for each grid's playing-step highlight following its own line
juce_add_console_app(BP303_PlayheadTest PRODUCT_NAME "BP303_PlayheadTest")
target_sources(BP303_PlayheadTest PRIVATE
    Tools/playhead_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_PlayheadTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_PlayheadTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test for the live step-recording path
juce_add_console_app(BP303_RecordTest PRODUCT_NAME "BP303_RecordTest")
target_sources(BP303_RecordTest PRIVATE
    Tools/record_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_RecordTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_RecordTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the per-line compressor + PCF filter
juce_add_console_app(BP303_DynFiltTest PRODUCT_NAME "BP303_DynFiltTest")
target_sources(BP303_DynFiltTest PRIVATE
    Tools/dynfilt_test.cpp)
target_link_libraries(BP303_DynFiltTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test that the stereo overloads on the pre-delay units cost
# nothing: identical channels in must give the mono result out, and the two
# channels must not share state
juce_add_console_app(BP303_StereoBusTest PRODUCT_NAME "BP303_StereoBusTest")
target_sources(BP303_StereoBusTest PRIVATE
    Tools/stereo_bus_test.cpp)
target_link_libraries(BP303_StereoBusTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for UNISON on the bass voice: one voice is still the
# plain 303, VOICES holds its level, and SPREAD widens without cancelling in mono
juce_add_console_app(BP303_UnisonTest PRODUCT_NAME "BP303_UnisonTest")
target_sources(BP303_UnisonTest PRIVATE
    Tools/unison_test.cpp)
target_link_libraries(BP303_UnisonTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the ten-band graphic EQ: flat is bit-identical
# (on, off, and after a band has been moved and put back), each fader hits its
# own centre, the pair never widens, and the meters follow the signal
juce_add_console_app(BP303_EqTest PRODUCT_NAME "BP303_EqTest")
target_sources(BP303_EqTest PRIVATE
    Tools/eq_test.cpp)
target_link_libraries(BP303_EqTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the bass line running its own cycle: LENGTH still
# means the bar, a short line drifts against it, and a fitted one divides it
juce_add_console_app(BP303_BassMeterTest PRODUCT_NAME "BP303_BassMeterTest")
target_sources(BP303_BassMeterTest PRIVATE
    Tools/bass_meter_test.cpp)
target_link_libraries(BP303_BassMeterTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for splitting a bass gate: an unsplit pattern is
# untouched, and a split one retriggers inside its own step
juce_add_console_app(BP303_GateSplitTest PRODUCT_NAME "BP303_GateSplitTest")
target_sources(BP303_GateSplitTest PRIVATE
    Tools/gate_split_test.cpp)
target_link_libraries(BP303_GateSplitTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the XY pad's mapping: an untouched pad is inert,
# a mode reaches only its own destinations, and every one of them is in a unit
# the mode engages
juce_add_console_app(BP303_PadTest PRODUCT_NAME "BP303_PadTest")
target_sources(BP303_PadTest PRIVATE
    Tools/pad_test.cpp)
target_link_libraries(BP303_PadTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the LFO core: its shapes, its derived phase, and
# the identity that keeps an unrouted LFO from re-voicing anything
juce_add_console_app(BP303_LfoTest PRODUCT_NAME "BP303_LfoTest")
target_sources(BP303_LfoTest PRIVATE
    Tools/lfo_test.cpp)
target_link_libraries(BP303_LfoTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless test that LFO 1 is wired through to the audio, and that processBlock's
# chunked path stays unreachable — bit-identically — until something is routed
juce_add_console_app(BP303_LfoWireTest PRODUCT_NAME "BP303_LfoWireTest")
target_sources(BP303_LfoWireTest PRIVATE
    Tools/lfo_wire_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_LfoWireTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_LfoWireTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test that drawing into the LFO scope stores the level it looks like,
# and that a step drawn up moves its knob up (with a positive depth) — one
# negation in the whole screen-y-to-knob path, and this pins it
juce_add_console_app(BP303_LfoDrawTest PRODUCT_NAME "BP303_LfoDrawTest")
target_sources(BP303_LfoDrawTest PRIVATE
    Tools/lfo_draw_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_LfoDrawTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_LfoDrawTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless, JUCE-free measurement of what modulating cutoff at block rate costs:
# how big each riser of the staircase is in cents, and what per-sample
# modulation costs the voice. Decides where an LFO's update rate has to live
juce_add_console_app(BP303_LfoRateTest PRODUCT_NAME "BP303_LfoRateTest")
target_sources(BP303_LfoRateTest PRIVATE
    Tools/lfo_rate_test.cpp)
target_link_libraries(BP303_LfoRateTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for per-lane drum pattern length: a following lane is
# unchanged, a short lane free-runs, and the master still owns the bar
juce_add_console_app(BP303_PolymeterTest PRODUCT_NAME "BP303_PolymeterTest")
target_sources(BP303_PolymeterTest PRIVATE
    Tools/polymeter_test.cpp)
target_link_libraries(BP303_PolymeterTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test that the master stage mixes rather than shapes:
# the drums must not modulate the bass on their way through it
juce_add_console_app(BP303_MasterTest PRODUCT_NAME "BP303_MasterTest")
target_sources(BP303_MasterTest PRIVATE
    Tools/master_test.cpp)
target_link_libraries(BP303_MasterTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the oscillator's waveforms
juce_add_console_app(BP303_WaveTest PRODUCT_NAME "BP303_WaveTest")
target_sources(BP303_WaveTest PRIVATE
    Tools/wave_test.cpp)
target_link_libraries(BP303_WaveTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless test for dragging the whole song out as one MIDI region
juce_add_console_app(BP303_SongMidiTest PRODUCT_NAME "BP303_SongMidiTest")
target_sources(BP303_SongMidiTest PRIVATE
    Tools/song_midi_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_SongMidiTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_SongMidiTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the per-voice snare and hat decay multipliers
juce_add_console_app(BP303_DrumDecayTest PRODUCT_NAME "BP303_DrumDecayTest")
target_sources(BP303_DrumDecayTest PRIVATE
    Tools/drumdecay_test.cpp)
target_link_libraries(BP303_DrumDecayTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the filter envelope's ATTACK stage
juce_add_console_app(BP303_AttackTest PRODUCT_NAME "BP303_AttackTest")
target_sources(BP303_AttackTest PRIVATE
    Tools/attack_test.cpp)
target_link_libraries(BP303_AttackTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for a pattern landing back on the note it is playing
juce_add_console_app(BP303_RepeatTest PRODUCT_NAME "BP303_RepeatTest")
target_sources(BP303_RepeatTest PRIVATE
    Tools/repeat_test.cpp)
target_link_libraries(BP303_RepeatTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the selectable per-line distortion
juce_add_console_app(BP303_DistTest PRODUCT_NAME "BP303_DistTest")
target_sources(BP303_DistTest PRIVATE
    Tools/dist_test.cpp)
target_link_libraries(BP303_DistTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless test that a mono host buffer gets the fold-down of the pair rather
# than just its left channel
juce_add_console_app(BP303_MonoFoldTest PRODUCT_NAME "BP303_MonoFoldTest")
target_sources(BP303_MonoFoldTest PRIVATE
    Tools/mono_fold_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_MonoFoldTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_MonoFoldTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test that the distortion type + controls reach the audio
juce_add_console_app(BP303_DistWireTest PRODUCT_NAME "BP303_DistWireTest")
target_sources(BP303_DistWireTest PRIVATE
    Tools/dist_wire_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_DistWireTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_DistWireTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test that every FX control reaches the audio when its unit is
# ACTIVE, and reaches nothing when it isn't — what a host's automation drives
juce_add_console_app(BP303_AutomationTest PRODUCT_NAME "BP303_AutomationTest")
target_sources(BP303_AutomationTest PRIVATE
    Tools/automation_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_AutomationTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_AutomationTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test for the three-way step dynamics on the bass ACC row
juce_add_console_app(BP303_DynTest PRODUCT_NAME "BP303_DynTest")
target_sources(BP303_DynTest PRIVATE
    Tools/dyn_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_DynTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_DynTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test that the delay TYPE reaches both output channels
juce_add_console_app(BP303_StereoDelayTest PRODUCT_NAME "BP303_StereoDelayTest")
target_sources(BP303_StereoDelayTest PRIVATE
    Tools/stereo_delay_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_StereoDelayTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_StereoDelayTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the per-line chorus + reverb
juce_add_console_app(BP303_SpaceTest PRODUCT_NAME "BP303_SpaceTest")
target_sources(BP303_SpaceTest PRIVATE
    Tools/space_test.cpp)
target_link_libraries(BP303_SpaceTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless test for the independent bass/drum pattern banks + state round-trip
juce_add_console_app(BP303_PatternTest PRODUCT_NAME "BP303_PatternTest")
target_sources(BP303_PatternTest PRIVATE
    Tools/pattern_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_PatternTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_PatternTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless, JUCE-free test for the song chain (pattern arrangement)
juce_add_console_app(BP303_SongTest PRODUCT_NAME "BP303_SongTest")
target_sources(BP303_SongTest PRIVATE
    Tools/song_test.cpp)
target_link_libraries(BP303_SongTest PRIVATE
    juce::juce_recommended_config_flags)

# Headless test for song mode driving the real processor
juce_add_console_app(BP303_SongPlayTest PRODUCT_NAME "BP303_SongPlayTest")
target_sources(BP303_SongPlayTest PRIVATE
    Tools/song_play_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_SongPlayTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_SongPlayTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test for dragging a pattern key out of the plugin as a MIDI file
juce_add_console_app(BP303_MidiDragTest PRODUCT_NAME "BP303_MidiDragTest")
target_sources(BP303_MidiDragTest PRIVATE
    Tools/midi_drag_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_MidiDragTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_MidiDragTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test that the drum lane handle's three gestures stay distinct:
# drag sets length, double-click toggles FIT, right-click returns to the master
juce_add_console_app(BP303_FitGestureTest PRODUCT_NAME "BP303_FitGestureTest")
target_sources(BP303_FitGestureTest PRIVATE
    Tools/fit_gesture_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_FitGestureTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_FitGestureTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test for the HOLD button and the held notes it writes
juce_add_console_app(BP303_HoldTest PRODUCT_NAME "BP303_HoldTest")
target_sources(BP303_HoldTest PRIVATE
    Tools/hold_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_HoldTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_HoldTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test for the start of a pattern-key drag through the real editor
juce_add_console_app(BP303_DragStartTest PRODUCT_NAME "BP303_DragStartTest")
target_sources(BP303_DragStartTest PRIVATE
    Tools/drag_start_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_DragStartTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_DragStartTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Headless test for dragging a pattern key into the song list
juce_add_console_app(BP303_SongDragTest PRODUCT_NAME "BP303_SongDragTest")
target_sources(BP303_SongDragTest PRIVATE
    Tools/song_drag_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_SongDragTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_SongDragTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)



# Headless test for the skin palettes and the unlabelled skin picker
juce_add_console_app(BP303_SkinTest PRODUCT_NAME "BP303_SkinTest")
target_sources(BP303_SkinTest PRIVATE
    Tools/skin_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_SkinTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_SkinTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# CPU benchmark: processBlock cost and editor repaint cost, separately
juce_add_console_app(BP303_PerfBench PRODUCT_NAME "BP303_PerfBench")
target_sources(BP303_PerfBench PRIVATE
    Tools/perf_bench.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_PerfBench PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_PerfBench PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# The grids' playhead repaints only the cells that changed — check that holds
juce_add_console_app(BP303_RepaintTest PRODUCT_NAME "BP303_RepaintTest")
target_sources(BP303_RepaintTest PRIVATE
    Tools/repaint_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_RepaintTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_RepaintTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Each FX tab's on-lamp follows its own effect, not a neighbour's
juce_add_console_app(BP303_FxTabTest PRODUCT_NAME "BP303_FxTabTest")
target_sources(BP303_FxTabTest PRIVATE
    Tools/fxtab_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(BP303_FxTabTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BP303")
target_link_libraries(BP303_FxTabTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)


# Headless, JUCE-free test for the hi-hat voicing
juce_add_console_app(BP303_HatTest PRODUCT_NAME "BP303_HatTest")
target_sources(BP303_HatTest PRIVATE
    Tools/hat_test.cpp)
target_link_libraries(BP303_HatTest PRIVATE
    juce::juce_recommended_config_flags)
