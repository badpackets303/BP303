# BP303

A TB-303 style bass voice and a five-voice drum machine in one instrument, built
with JUCE. Developed and tested as an AU in **Logic Pro** on macOS; also shipped
as a standalone app for macOS and Windows.

## Build

JUCE 8.0.8 is fetched by CMake at configure time — don't install or vendor it.
The first configure clones it, so `build/` ends up around 1 GB. It is gitignored.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --target BP303_AU -j8
```

The AU installs itself to `~/Library/Audio/Plug-Ins/Components/` as a post-build
step, so a successful build is already loadable in Logic. `BP303_Standalone`
builds an app instead.

Mac builds are universal (arm64 + x86_64) by default, which roughly doubles the
link. Pass `-DCMAKE_OSX_ARCHITECTURES=arm64` when iterating.

**Windows is built only by CI** (`.github/workflows/build.yml`) — there is no
Windows machine in the loop, so anything that has to work there has to be
reasoned about rather than tried. The rules that keeps: no `M_PI` (MSVC hides it
behind `_USE_MATH_DEFINES` — spell pi out, as the DSP headers do), no POSIX
headers, no hardcoded font names or paths, and anything Objective-C++ stays
behind `BP303_HAS_NATIVE_WINDOW` with an inline no-op fallback the way
`HostWindowCentre.h` does it. JUCE drops the AU format by itself off macOS, so
`FORMATS AU Standalone` is already correct for both.

## Tests

Two kinds, both headless. Run both before calling anything done.

**CMake targets** — 24 of them, one per `Tools/*_test.cpp` that is wired up:

```bash
cmake --build build -j8 && for t in build/BP303_*Test_artefacts/Release/BP303_*Test; do "$t" >/dev/null || echo "FAIL $t"; done
```

**Standalone tools** — six older ones are *not* in CMakeLists and only build
directly: `drum`, `fuzz`, `fx`, `metro`, `render`, `seq`. Each carries its own
build line in its header comment:

```bash
clang++ -std=c++17 -O2 Tools/drum_test.cpp -o /tmp/drum_test && /tmp/drum_test
```

Tests print a trailing `OK` / `ALL PASS` line and set the exit code. Some also
write PNG or WAV output into the working directory for eyeballing; that output is
gitignored.

CI runs only the nine JUCE-free targets — `WaveTest`, `DistTest`, `MasterTest`,
`DynFiltTest`, `SpaceTest`, `SongTest`, `RepeatTest`, `AttackTest`,
`DrumDecayTest` — on both platforms, because a failure in those is a real
portability bug rather than a headless-runner artefact. The rest are a local
macOS run, so they are still on you.

`BP303_Snapshot`, `BP303_FxTabSnapshot` and `BP303_DrumTabSnapshot` render the
editor to PNG — the way to check UI work without opening a host.
`BP303_PerfBench` measures repaint cost.

## Layout

`Source/` is the plugin. `Tools/` is tests and offline renderers.

The audio path is two independent lines summed at the end of
`PluginProcessor::processBlock`:

- **bass** — `Synth303` → `Pcf` filter → `Distortion` → `Fx303` delay →
  `Compressor` → `Chorus` → `Reverb`
- **drums** — `DrumMachine` through the same chain shape, into its own buffer
- then the metronome, then the master stage

Mono up to the delay, which is where a line can first become stereo; everything
after runs on a channel pair.

`Sequencer303` and `DrumSequencer` both snap to one shared transport phase, so a
line toggled on mid-pattern lands on the same grid as the other. `SongPlayer`
derives position from that phase rather than counting, which is what makes host
looping and jumping land correctly.

## Constraints that are easy to break

**The master stage must stay a mix, not a waveshaper.** It was a bare `tanh`
across the summed output, which made the gain applied to the bass move with the
drum waveform — every kick amplitude-modulated the line and threw non-harmonic
sidebands over it. Pulse suffered worst. It is now a headroom trim plus a
gain-based limiter that is *exactly* unity below threshold. `Tools/master_test.cpp`
guards this: at stock levels the bass must come out of the mix bit-identical
whether or not the drums play. Do not put a nonlinearity across the sum.

**Keep DSP and sequencer code JUCE-free.** `Synth303`, `DrumMachine`,
`Sequencer303`, `SongPlayer`, `DspUtil` and friends deliberately avoid JUCE so
they can be compiled standalone by the `Tools/` programs. Reaching for a JUCE
type in these files costs the headless tests.

**CPU cost is in the editor, not the DSP.** The DSP is around 1% of a core; editor
repaints have been the expensive part. Measure with `BP303_PerfBench` before
optimising anything, and don't go hunting for DSP savings that aren't there.

**Parameter and enum order is save-file compatibility.** A choice parameter stores
its index, so `Synth303::Wave` and the other enums must only grow at the end —
reordering changes what every saved project plays.

**Logic quirk, not a bug:** changing the audio device makes Logic's engine reset
and close open plugin windows. `Tools/device_change_test.cpp` covers the
re-prepare path.

## Style

Comments in this codebase explain *why*, not what — the trade-off taken, the quirk
being reproduced, the failure being avoided. Match that; it's most of the value in
the existing ones. Match the surrounding naming and spacing rather than importing
a different house style.

## Licence

AGPLv3, because BP303 links JUCE and JUCE's open-source option is AGPLv3. Keep new
files compatible with that, and don't add a dependency under a conflicting
licence.
