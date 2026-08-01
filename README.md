# BP303

A TB-303 style bass synthesiser and drum machine in one Audio Unit, built with
[JUCE](https://juce.com). Monophonic acid bass line with the classic accent and
slide behaviour, a five-voice drum machine, a step sequencer for each, and a song
arranger to chain patterns together.

Developed and tested as an AU in Logic Pro on macOS.

## What's in it

**Bass voice** — PolyBLEP saw / square / pulse oscillator into a zero-delay-feedback
4-pole ladder filter, with the 303's cutoff, resonance, env mod, decay and accent
controls. Accent, slide and the fixed snappy accent decay all behave the way the
original does. Vibrato on top, retriggered per note.

**Drum machine** — five voices (kick, snare, clap/rim, closed hat, open hat) in
three synthesised kits: 606, 808 and 909. Per-voice level and tuning, plus kick decay.

**Sequencers** — 16 steps each for bass and drums, three-way step dynamics
(soft / normal / accent), slide and hold per bass step, shuffle, and 27 pattern
slots per line. Live step-record and a HOLD mode for playing patterns in from a
keyboard. Both lines share one transport phase, so a line toggled on mid-pattern
lands on the shared grid.

**Song mode** — an arrangement that names a bass pattern and a drum pattern per
row, with per-line mutes. Song position is derived from the host transport rather
than counted, so looping, jumping and starting mid-project all land correctly.

**Per-line FX** — bass and drums each get their own chain: envelope-followed
filter, distortion (soft / fuzz / fold / rectify / crush), tempo-synced delay,
compressor, chorus and reverb.

**Also** — five UI skins with a hue control, a metronome, MIDI drag-out of the
current pattern, and live drum triggering from GM notes on MIDI channel 10.

## Building

Requires CMake 3.22+, a C++17 compiler, and macOS 11.0 or later. JUCE 8.0.8 is
fetched automatically by CMake — you don't need to install it.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --target BP303_AU -j8
```

The AU is copied to `~/Library/Audio/Plug-Ins/Components/` after the build.
`BP303_Standalone` builds a standalone app instead.

The first configure clones JUCE, so it takes a few minutes and the `build/`
directory ends up around 1 GB.

## Tests

The DSP and sequencer code is deliberately JUCE-free where it can be, so most of
it is testable headlessly. Each `Tools/*_test.cpp` is a self-contained program;
they're also wired up as CMake targets.

```bash
cmake --build build -j8
```

```bash
for t in build/BP303_*Test_artefacts/Release/BP303_*Test; do "$t" >/dev/null || echo "FAIL $t"; done
```

Some tools write PNG or WAV output into the working directory for eyeballing —
`BP303_Snapshot` renders the editor, `BP303_PerfBench` measures repaint cost.
Those outputs are gitignored.

## Licence

BP303 is released under the **GNU Affero General Public License v3.0**. See
[LICENSE](LICENSE).

This is not an arbitrary choice: BP303 links the JUCE framework, whose modules are
dual-licensed under the AGPLv3 and a commercial licence. Building on the
open-source option means this project inherits the AGPLv3. If you want to ship a
closed-source derivative you will need a commercial JUCE licence from
[juce.com](https://juce.com/get-juce/) — and my permission for this code.

JUCE itself is not included in this repository; CMake fetches it at configure time
under its own licence.

TB-303, TR-606, TR-808 and TR-909 are trademarks of Roland Corporation, which has
no association with this project. The kit names describe what the voices are
modelled on.
