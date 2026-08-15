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

**CMake targets** — 36 of them, one per `Tools/*_test.cpp` that is wired up:

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

CI runs only the sixteen JUCE-free targets — `WaveTest`, `DistTest`, `MasterTest`,
`DynFiltTest`, `SpaceTest`, `SongTest`, `RepeatTest`, `AttackTest`,
`DrumDecayTest`, `StereoBusTest`, `UnisonTest`, `PolymeterTest`, `EqTest`,
`GateSplitTest`, `BassMeterTest`, `PadTest` — on both platforms, because a
failure in those is a real portability bug rather than a headless-runner
artefact. The rest are a local macOS run, so they are still on you.

`BP303_Snapshot`, `BP303_FxTabSnapshot`, `BP303_DrumTabSnapshot`,
`BP303_EqTabSnapshot` and `BP303_PadTabSnapshot` render the editor to PNG — the
way to check UI work without opening a host. The EQ one dials a curve in and
renders a second of audio first, because its meters are the only part of the
editor that shows nothing at all until something has played; the pad one holds
the pad off centre, for the same reason. `BP303_PerfBench` measures repaint
cost, and now also what the EQ costs flat, driven, and with its meters running.

## Layout

`Source/` is the plugin. `Tools/` is tests and offline renderers.

The audio path is two independent lines summed at the end of
`PluginProcessor::processBlock`:

- **bass** — `Synth303` → `Pcf` filter → `Distortion` → `Fx303` delay →
  `Compressor` → `Chorus` → `Reverb` → `GraphicEq`
- **drums** — `DrumMachine` through the same chain shape, into its own buffer
- then the metronome, then the master stage

The EQ is last on each line rather than somewhere in the middle, which makes it
a channel EQ: what you dial is what leaves the line, instead of something the
delay and reverb then colour further. It is also what lets its band meters read
what actually reaches the mix.

Both lines are a channel pair from the voice onward. Three things make width, and
nothing else does: the bass voice's UNISON, the drum machine's per-voice pans,
and the ping-pong delay. All three default to off or centred, so an untouched
instance produces two identical channels through every unit — none of the effects
widens on its own.

The drums' DRUMS/BALANCE page sets each voice's position and SPREAD *scales*
those positions rather than replacing them, so SPREAD at 0 still collapses the
kit to the centre whatever BALANCE says. The pan parameters default to
`DrumMachine::voicePan`, which is what keeps SPREAD meaning exactly what it meant
before the page existed.

That default matters more than it looks. Every width control has a test pinning
it: at zero the output must be what it was before the control existed, and for
the bass voice and the two pre-delay effects that means bit-identical, not close.
`Tools/stereo_bus_test.cpp` and `Tools/unison_test.cpp` guard it.

A mono host layout gets the pair folded down at the master stage. It used to get
the left channel with the right discarded, which quietly ate every second
ping-pong echo; `Tools/mono_fold_test.cpp` covers it now.

`Sequencer303` and `DrumSequencer` both snap to one shared transport phase, so a
line toggled on mid-pattern lands on the same grid as the other. `SongPlayer`
derives position from that phase rather than counting, which is what makes host
looping and jumping land correctly.

**One length owns the bar, and it is not a line's own length.** `sequencer.length`
— the LENGTH control — is the bar: the song counts in it, MIDI export renders one
of it, and every drum lane on `followMaster` takes its length from it.

*How many steps a line plays* is a separate value on each line, defaulting to
following the bar: `DrumSequencer::laneLength` with `followMaster`, and
`Sequencer303::patternLength` with `followBar`. Set shorter, a line free-runs
against the bar rather than resetting with it — which is what makes the drift
audible as drift instead of as the kit losing its place.

The bass having both was the awkward part, and the trap is that `length` lives on
`Sequencer303` and so reads like the bass line's own length. It is not. Anything
that means *the bar* reads `length`; anything that means *the steps this line
plays* goes through `lengthOf()`. Writers of steps are the second kind — the HOLD
write head, live recording, the cursor, `shiftBass`, the gate-drag run cap —
because a step past the line's end is one it never plays.

`DrumSequencer` therefore keeps one never-wrapping `absStep` and derives every
lane's position as a modulo of it. Shuffle parity comes off `absStep` too: swing
belongs to the grid, so a seven-step lane has to swing with everything else
rather than flipping its own idea of the off-beats each time it wraps. For an
even master length the two are identical.

**Length and FIT are the two halves of one question, with opposite answers.**
`laneLength` alone gives *polymeter* — the lane keeps the sixteenth pulse, runs a
shorter bar, and drifts. `DrumSequencer::laneFit` gives *polyrhythm* — the lane's
steps divide one master bar evenly, so it keeps the bar and changes the pulse.
Three fitted steps are three evenly spaced hits a bar; twelve are eighth-note
triplets. The cap of sixteen steps is what stops sixteenth triplets (24 a bar).

Every lane therefore runs on its own clock, and everything that used to read
`absStep`/`posInStep` goes through `DrumSequencer::laneClock` instead. The grid
case returns the master's own step and position *untouched* rather than deriving
them back out of a continuous position — the round trip through a divide and a
multiply is exact in arithmetic and not in float, and it is the path every
default pattern takes. Same reasoning as the flat EQ below: the cheap way to
guarantee nothing moved is not to compute it. `Tools/polymeter_test.cpp` pins the
identity case both ways round.

A fitted lane has no sixteenth grid to belong to, so it swings its *own* steps by
the same fraction of its own step, and its ratchets subdivide its own step. Both
fall out of taking the parity and the span off `LanePos` rather than off `sps`.

**Both lines split a step the same way, and both re-clock the same way.** The
bass gets the drum lanes' ratchets as `Sequencer303::Step::ratchet` — alt-click
on the GATE row — and it composes with hold and slide the way you would want: the
repeats happen in the head step, and only the *last* one ties forward, so a slid
split glides out of its final note instead of being one long note with retriggers
buried in it. `Tools/gate_split_test.cpp` pins that apart.

`patternFit` is `laneFit` for the bass, and its span is the bar rather than a
lane's. Because `Sequencer303` already keeps its own `posInStep` and advances a
`stepIdx`, fitting it needed no continuous-position rewrite the way the drums
did: the step's *duration* becomes `sps * barLen / len` and the host-sync
derivation divides by the same ratio, which is what keeps it locked across a loop
or a jump rather than merely running at the right rate. Unfitted, `span` is `sps`
literally — the same not-computing-it trick as `laneClock` and the flat EQ, and
`Tools/bass_meter_test.cpp` pins the default bit-identical, straight and shuffled.

The one thing that stayed on the line rather than moving to the bar is
`samplesUntilPatternStart`: a queued *bass* pattern lands when the bass line
comes round. With the line following the bar — every pattern that predates this —
those are the same instant.

**Ratchets subdivide a step, they don't change the cycle.** `ratchetMask` holds
two bits per step, so a lane is still one integer that copies and serialises like
the others. The repeats fill what is left of the step *after* the swing has moved
its start, which is what keeps a ratchet on a shuffled sixteenth inside its own
slot. Three of them in a sixteenth are 48ths, not triplets — triplets are FIT's
job, and the two compose rather than overlapping.

The drum grid is now three gestures on one cell: left toggles the hit,
right/shift walks the level, alt walks the ratchet count. Each has a paint mode
so a drag stamps it across the lane. The lane's end handle carries the other two:
drag it for length, double-click to fit it to the bar, right-click to put the
lane back on the master — clock included, since a lane fitted in as many steps as
the bar has is the identity case and would only leave the handle coloured for a
mode it isn't meaningfully in. `Tools/fit_gesture_test.cpp` pins the three apart,
because a drag that also turned FIT on would silently swap polymeter for
polyrhythm on a pattern someone thought they were only resizing.

**The grid draws slots, not time.** A fitted lane's cells still occupy one column
each, because the grid is a sixteen-cell editor whatever clock the lane runs on —
so its real step boundaries go in as ticks across the row. Without them "three
lit cells" reads as three sixteenths rather than as the bar cut in three. Drawing
the cells stretched instead would be more honest about timing and would leave the
length handle sitting mid-cell with nothing to point at, which is the trade that
was taken.

**Two vertical rules organise the window, and both come from the lower region.**
`keysLeft` is the left edge of the pattern-keypad column, and one `panelGap` to
its left is the right edge of the pattern grids. DRUM FX and the EQ start on the
first; BASS FX and the PAD end on the second. Nothing that lines up with those is
written out as a width — `fxSectionW` and `perfSectionW` are derived from
`songColumnW` and `keysColumnW`, so widening the SONG column moves the six panels
above it instead of quietly breaking the alignment.

The drums row is the one that can't be derived, because DRUMS is sized by its
knob columns and the PAD by its square. A `static_assert` beside `padSectionW`
holds the three widths to the rule instead, which is why `drumKnobColumnW` is 96
rather than 92: the EQ moving onto the rule freed 24px on that row, and six
columns is exactly where it goes. Discovering that in a snapshot rather than at
compile time is the failure mode being avoided — the three widths live in three
different places.

The drums row carries three sections side by side: DRUMS on the left, sized to
exactly the width its widest page needs — its own margins, the KIT column, then
six `drumKnobColumnW` columns — then the PAD at a fixed `padSectionW`, and the EQ
taking the rest. The drum pages lay their knobs out at that fixed column width
rather than dividing up whatever they are given, which is both what frees the
space and what stops the four-knob pages putting their knobs somewhere different
from the six-knob ones. `drumKnobColumnW` is only ever buying spacing and label
room, and moving it trades directly against how wide the EQ's bands get.

A knob on those pages takes its size from the *page height*, not its column —
which is why the row growing to `drumRowH` for the pad needed `drumKnobRowH`.
Left alone the drum knobs would have grown with the row and become the largest
knobs in the window; they keep the height they had and sit centred in a taller
page, and KIT is pinned to the same strip for the same reason.

**The pad and the EQ are the two ends of one width.** `padSectionW` is the pad
square plus its readout column and no wider, because the square is capped by the
row height — every pixel past that is width the EQ could have had. The EQ gave up
294 of them and lost nothing: at 784 it was drawing ten octave bands across most
of the window through a 67px-tall plot, which is the wrong aspect ratio in both
directions. At 490 a band is still 45px, and the taller row took the curve from
2.4 px/dB to 4.

## Constraints that are easy to break

**The master stage must stay a mix, not a waveshaper.** It was a bare `tanh`
across the summed output, which made the gain applied to the bass move with the
drum waveform — every kick amplitude-modulated the line and threw non-harmonic
sidebands over it. Pulse suffered worst. It is now a headroom trim plus a
gain-based limiter that is *exactly* unity below threshold. `Tools/master_test.cpp`
guards this: at stock levels the bass must come out of the mix bit-identical
whether or not the drums play. Do not put a nonlinearity across the sum.

**A flat EQ has to be a branch, not a coefficient.** A peaking biquad at 0 dB has
`b == a`, so it is transparent in exact arithmetic and *not* in float — it still
rounds every sample. Ten of them a line would therefore quietly re-voice every
project that ever loads, which is the one thing the width and tone defaults in
this instrument are all pinned against. `GraphicEq` skips the samples entirely
when every band is zero, and clears each band's filter state at the moment it
lands on zero, since that is the one moment when clearing it is silent. Don't
"simplify" that into always running the filters. `Tools/eq_test.cpp` guards it
on, off, and after a band has been moved and put back.

The band meters are a separate tap — a bandpass pair and a follower per band,
reading the post-EQ signal and touching nothing. They only run while an editor
is open (`addEqMeterClient`), because they cost about as much as the EQ itself.
The bandpass is *two* biquads in series and not one: a single one's 6 dB/octave
skirts put the neighbouring bar within a few dB of the driven one, and ten
meters that rise and fall together are not a spectrum.

**The EQ panel is a response curve, not a fader bank.** `EqBands` draws the
composite response over the ten meters, which fill in behind as octave-wide
columns on the same log-frequency axis, and each band is a node on the curve.
The curve comes from `GraphicEq::responseDb` — the audio path's own coefficients,
not a redrawing of them — so the line cannot promise a shape the filters aren't
producing; `Tools/eq_test.cpp` holds it against a measured sweep to 0.1 dB. This
is also why the curve is worth the trouble over ten faders: octave bands overlap,
so two neighbours at +6 make one +10 dB shelf rather than two +6 bumps, and a
fader bank never shows you that.

Two things about the drawing that measurement decided rather than taste. The
curve is stroked and not filled, because the meters already own the fill and two
washes of one colour cannot be told apart. And a meter tick repaints the whole
plot once when more than three bands moved, rather than one strip each: a strip
re-strokes the entire response path under a narrow clip, so ten of them cost
1.08 ms against 0.32 ms for the plot in one go. `BP303_PerfBench` reports both.

**Keep DSP and sequencer code JUCE-free.** `Synth303`, `DrumMachine`,
`Sequencer303`, `SongPlayer`, `DspUtil`, `MacroPad` and friends deliberately
avoid JUCE so they can be compiled standalone by the `Tools/` programs. Reaching
for a JUCE type in these files costs the headless tests.

**CPU cost is in the editor, not the DSP.** The DSP is around 1% of a core; editor
repaints have been the expensive part. Measure with `BP303_PerfBench` before
optimising anything, and don't go hunting for DSP savings that aren't there.

**Every FX unit is off by default, and a bypassed unit ignores its controls.**
`bflton`, `diston`, `delayon`, `bcompon`, `bchron`, `brevon`, `beqon` and their
drum counterparts all default to false, and each unit's `process` returns
immediately when off — so automating BASS FILTER CUTOFF from a host does nothing
at all until BASS FILTER ACTIVE is switched on (it is a parameter too, so the
lane can do it). This is the single most confusing thing about the plugin from a
host's automation list, and it is worse because the 303's own filter is a
*different* parameter — "Cut Off" and "Resonance", always live, no switch — sitting
next to "Bass Filter Cutoff" and "Bass Filter Res", which are the extra per-line
PCF on the FX row. `Tools/automation_test.cpp` pins both halves for every unit:
inert with ACTIVE off, live with it on.

**The XY pad offsets the knobs, and borrows the units.** `MacroPad.h` is the pad:
four named modes, each hardwiring what its two axes reach. The Kaoss model rather
than Alchemy's Transform Pad — a snapshot morph would need a capture UI and would
mean nothing for the discrete controls this plugin is full of.

Two rules hold it together, and both answer something above. It *offsets* the
parameters rather than writing them, so the knobs stay where the user left them
and the host gets two automation lanes instead of a dozen fighting ones. And
holding the pad engages the mode's units *without writing their ACTIVE switches*,
which is the trap in the paragraph above turned into the feature — a pad driving
REVERB MIX would otherwise be silent until the user went hunting for a switch.
The switch reads the same after the gesture as before it.

Depths are in normalised units — a fraction of the knob's own travel — so a depth
means the same thing on a linear mix control and on a skewed frequency. That
means a round trip through the parameter's skew, which is *not* exact in float,
and the pad sits across the 303's own cutoff. So an unheld pad must not take that
path at all: `apply` hands the base value straight back without computing
anything. Same trick as the flat-EQ branch and `DrumSequencer::laneClock` — the
cheap way to guarantee nothing moved is not to compute it. `Tools/pad_test.cpp`
pins that identity for every mode at every pad position, and `automation_test`
pins the other end: the axes reach the audio when held and change exactly nothing
when not.

ACID is the only mode needing no unit, because the 303's own filter is always
live — which is also what makes it the one that does something on an untouched
instance. Every other mode's destinations have to be covered by a unit that mode
engages, and `pad_test` checks that against the table rather than trusting it.

Engagement is its own parameter (`padon`) rather than "the axes are not both
zero", so a finger resting at dead centre still counts and a drag through the
middle doesn't drop the units for a block. LATCH only decides what the mouse does
with `padon` on release, so it never reaches the audio thread at all.

**A knob the pad is pushing turns, and leaves a mark where it was.** The pointer,
the LED collar, the value ring and the cap's highlight all read the *modulated*
position, because a control being played should look like it is being played.
`drawRotarySlider` takes the offset at the top and adds it to `sliderPos` before
anything derives an angle from it, so every skin follows without knowing the pad
exists — the alternative was five per-skin branches each drawing their own idea
of it.

The setting underneath still has to survive, or the knob merely looks like it was
turned and appears to jump back on release. So the position the user left it at
is kept as a dim tick in the margin, with a thin arc from there round to where
the pad has taken it. Both live in the 3px `drawRotarySlider` reduces its bounds
by, outside the notch collar and outside `KnobArcRing`'s value ring, which is
what lets them be drawn once rather than once per skin.

`updatePadKnobs` asks the same `macropad::Pad` the audio thread reads, through
the same `apply`, so the rotation is the offset actually reaching the DSP
including its clamping: a knob already at the top of its travel does not move,
because the pad is genuinely doing nothing to it.

The arcs are driven from `XyPad`'s own 25 Hz timer rather than from the mouse, so
a pad moved by automation moves them too. While the pad is held they refresh
every tick even when it hasn't moved, because the user can turn a knob under a
parked gesture and the offset clamps against wherever that knob now sits — it is
not a function of the pad alone. Unchanged offsets don't repaint, so a still pad
costs twelve comparisons a frame and `BP303_PerfBench` still reports 0.0% stopped
and untouched. The constructor calls it once as well: an editor reopening onto a
latched gesture that waited 40 ms for its knobs would look unwired.

The pad throws sparks off the handle from the same tick, sized by how far the
handle moved *on screen* since the last one — so an automation ramp throws the
same burst a drag does, and a sub-pixel crawl throws none. Only movement spawns
them, which is what keeps a latched pad left alone from animating forever: the
last spark fades about half a second after the gesture stops and the repaints
stop with it. They are a fixed array reused in place, since nothing should be
allocating on a 25 Hz timer.

`BP303_PadTabSnapshot` renders the four modes posed, and then drives a real drag
with the message loop running between the moves — the sparks come off the timer,
so they cannot be posed, and that snapshot is the only way to see them without a
host. It is the one target built with `JUCE_MODAL_LOOPS_PERMITTED`.

**The help tour is UI too, and `BP303_HelpSnapshot` is how you check it.** It
walks every step to PNG. The failure it catches is not a compile error: the
callout grows to fit its body but is capped at the window height, so a step
written a paragraph too long silently loses its last lines behind the NEXT
button. It also picks its own position — below its target, above it, or centred
if neither fits — so a step can end up covering the control it describes. Run it
after touching `startHelp`.

**Parameter and enum order is save-file compatibility.** A choice parameter stores
its index, so `Synth303::Wave` and the other enums must only grow at the end —
reordering changes what every saved project plays.

**A hi-hat's character is in the filter, not the oscillators.** The 808 and 606
hats are six square oscillators, and whether that reads as a "tsss" or as pitched
metal depends on getting the fundamentals out of the way — the sound is the
thicket of upper harmonics, not the oscillators themselves. Twelve dB an octave
cannot do it, and retuning doesn't help: drop the bank an octave and its
harmonics land in the same place. Four poles is what turns the chord into a
sheen, and the makeup gains at the call site are measured, not chosen, so a
pattern mixed against the old voicing still balances. The 909 hat is noise, was
never the problem, and must stay untouched — including drawing `noise()` the same
number of times per sample, since another draw shifts its whole random stream.
`Tools/hat_test.cpp` guards all of it.

**The external drag is offered once per gesture.** JUCE's `checkForExternalDrag`
sets its `hasCheckedForExternalDrag` flag *before* calling
`shouldDropFilesWhenDraggedExternally`, and never clears it. So returning false
there does not mean "ask again when I've really left the window" — it means the
drag can never become a file, and it dies as a bounce-back to the key it came
from. Inside a host, the offer arrives early and often: a point over no child
component reads as open desktop, which most of the plugin's interior does. That
is why the export is driven from `dragOperationStarted` and a polling watcher
instead. Don't put an early return back in that method.

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
