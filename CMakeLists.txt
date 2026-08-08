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
