#pragma once

#include "PluginProcessor.h"

namespace ui303
{
    // Everything visual is palette-driven so skins share all geometry.
    enum KnobStyle { KnobFlat = 0, KnobMetalArc = 1, KnobPlastic = 2, KnobRecessed = 3,
                     KnobArcRing = 4 };

    // Which end of a knob's travel is the extreme one, for the warning tint.
    // It cannot be assumed: RESONANCE and DRIVE get wild at the top, but the
    // CRUSH knobs are a rate and a bit depth, so their clean end is the top and
    // it is the *bottom* that destroys the signal. Most knobs have no extreme
    // end at all and stay the plain accent the whole way round.
    enum HotEnd { HotNone = 0, HotTop = 1, HotBottom = -1 };

    struct Palette
    {
        bool retro = false;
        juce::Colour winBg1, winBg2;        // window background gradient
        juce::Colour panel1, panel2;        // panel fill gradient
        juce::Colour outline, bevelHi;
        juce::Colour title, text;
        juce::Colour orange, red, ledOff, cellOff;
        juce::Colour knobFace1, knobFace2, knobEdge, pointer, tickArc;
        juce::Colour pitchBg, pitchText;    // pitch cells (LCD-style in retro)
        juce::Colour lcdBg, lcdText;        // combo boxes
        int  knobStyle = KnobFlat;
        bool beveledPanels = false;         // raised plastic panel bevel
        juce::Colour kbNatural, kbSharp, kbLabel;   // on-screen keyboard
        juce::Colour buttonFace, buttonText;        // text buttons (COPY, INS, ...)

        // --- shaping. Defaults match the four original hardware-style skins, so
        // only a skin that wants the flat modern look has to set them.
        bool  panelScrews  = false;   // retro corner screws on panel frames
        float panelCorner  = 6.0f;    // panel corner radius
        bool  flatControls = false;   // flat buttons / LEDs / combos, no moulding
        bool  monoDisplay  = false;   // monospaced combo text
        juce::Colour buttonEdge { 0xff1b1b1f };     // button + combo border
    };

    const Palette& palette (int skinIndex);
    const char*    skinName (int skinIndex);

    // The hidden key-colour dial: every skin's accent can be rotated round the
    // colour wheel without touching its chassis. Stops rather than a free dial,
    // so it always lands on a deliberate colour instead of a muddy in-between —
    // twelve of them, an even spread from wherever the skin's own accent sits.
    //
    // The dial has a target (the stop, which is what gets saved) and an animated
    // position that eases toward it, so a change fades rather than snapping.
    // advanceKeyHueFade() moves the position one frame along and says whether it
    // is still travelling; the editor drives it from a short-lived timer.
    inline constexpr int hueStops = 12;
    int  keyHue();                                 // the dial's target stop
    void setKeyHue (int stop, bool snap = false);  // wraps; snap skips the fade
    bool advanceKeyHueFade();                      // one frame; false once settled
    // 0 = Classic, 1 = Retro 90s, 2 = Studio 90s, 3 = Bad Packets, 4 = Neon Slate
    inline constexpr int numSkins = 5;
    inline constexpr int defaultSkin = 4;   // Neon Slate — what a fresh install opens on

    // Where the unlabelled skin picker lives: the BADPACKETS legend in the
    // top-left of the header, in the editor's native coordinates. Right-clicking
    // inside it opens the menu. Exposed so a test can check nothing is laid out
    // on top of it — a control there would swallow the click.
    juce::Rectangle<int> skinMenuHotspot();

    // How far a soft cell is lit from the empty colour toward the full one, in
    // both grids. How lit a cell is — not what colour it is — is what carries
    // "how hard": that is the convention every hardware step sequencer uses, and
    // unlike a hue shift it survives an eye that can't separate two hues. It also
    // stays right on the light skins, where "off" is a pale grey rather than
    // black, so a soft cell there sits between the grey and the full colour
    // instead of literally darker. Halfway is deliberate — the furthest a third
    // state can sit from both a full cell and an empty one at once.
    inline constexpr float softCellStrength = 0.5f;

    // ...and the accent is lifted clear of a normal hit, so the three stay in
    // order even on a skin whose two lit colours are nearly the same (the neon
    // one's two greens), where the hue alone would carry almost nothing.
    inline constexpr float accentCellLift = 0.18f;

    // Draws a titled, framed control panel in the current skin. Shared by the
    // editor and the tabbed FxSection so every panel looks identical.
    void drawPanel (juce::Graphics&, juce::Rectangle<int>,
                    const juce::String& title, const Palette&);
}

class Look303 : public juce::LookAndFeel_V4
{
public:
    Look303() { setSkin (0); }

    void setSkin (int skinIndex);

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

private:
    int skin = 0;
};

// Physical-style segmented switch bound to an AudioParameterChoice — replaces
// a dropdown with a row of hardware selector buttons (e.g. SAW / SQUARE).
class SegmentedSwitch : public juce::Component
{
public:
    SegmentedSwitch (BP303AudioProcessor& proc,
                     juce::AudioProcessorValueTreeState& state,
                     const juce::String& paramID);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    BP303AudioProcessor& proc;
    juce::AudioParameterChoice* param = nullptr;
    juce::StringArray labels;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 0;
};

// 16-column editor for the 303 pattern: gate, pitch (drag), accent, slide,
// with a playing-position LED row.
class StepGrid : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid (BP303AudioProcessor& p) : proc (p) { startTimerHz (25); }

    // A held key sounds until the editor releases it, so an editor torn down
    // mid-note — a host closing the window, a device change — must not leave
    // one hanging. HOLD itself stays armed: MIDI still plays into it.
    ~StepGrid() override { proc.releaseHeldKey(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // The one cell a moving playhead changes. Public so BP303_PerfTest can check
    // that repainting only this really does leave the grid correct.
    juce::Rectangle<int> ledCellBounds (int col) const;

    // The grabbable bar at the line's end, on the LED row. Same control the drum
    // lanes carry and the same three gestures: drag for length, double-click to
    // fit across the bar, right-click back to following it. It sets how many
    // steps the *bass line* runs — LENGTH still owns the bar.
    juce::Rectangle<int> patternEndHandle (int len) const;

    // HOLD: a latching live-write mode. While it is armed, a note held down —
    // here or over MIDI — is written at the playhead and sustained over every
    // step the sequencer reaches until it is released, so a pattern gets filled
    // by playing it in rather than drawn a cell at a time. Destructive by
    // design. The mode and the write head live on the processor, since MIDI
    // arrives on the audio thread and keeps working with no editor open; this
    // is just the on-screen keyboard's way in.
    void setHoldLatch (bool shouldArm);
    bool isHoldArmed() const { return proc.holdArmed.load(); }

private:
    void timerCallback() override;
    int columnAt (int xPos) const;

    // Everything paint() reads, so the timer can tell a frame with something to
    // redraw from one without. `shown` is written by paint() itself, which keeps
    // it true for repaints from anywhere — a mouse edit, a skin change, the host.
    struct View
    {
        int playing = -2, length = -1, cursor = -1, kbLow = 0;
        bool fit = false;              // the line fitted across the bar
        juce::uint64 pattern = 0;      // the sixteen steps, hashed

        bool operator== (const View& o) const
        {
            return playing == o.playing && sameApartFromPlayhead (o);
        }

        // The playhead is only the LED row; everything else is the grid proper.
        bool sameApartFromPlayhead (const View& o) const
        {
            return length == o.length && fit == o.fit && cursor == o.cursor
                && kbLow == o.kbLow && pattern == o.pattern;
        }
    };

    View liveView() const;
    void repaintLed (int col);

    // Set while the pattern-end handle is being dragged, so a drag that wanders
    // off the LED row keeps moving the end rather than starting to edit steps.
    bool draggingLength = false;

    View shown;

    // On-screen keyboard ("write mode"): clicking a key writes its note into the
    // cursor step and advances the cursor, TB-303 style. The gutter holds octave
    // shift and a REST pad (advance leaving the step silent).
    int  keyAt (juce::Point<int> pos) const;   // combined semitone, or noKey
    void stampCursor (int combined);           // set pitch + gate, then advance
    void writeCursor (int combined);           // as stampCursor, and blip the note
    void advanceCursor();
    static int whiteSemis (int whiteIndex);    // white-key index -> semitone in span

    // HOLD mode, editor side only: `latchKeyDown` means a key on this keyboard is
    // being held, so a drag onto another key knows to hand the note over.
    void startLatchedNote (int combined);
    void endLatchedNote();

    bool latchKeyDown = false;
    int  latchNote = 0;

    BP303AudioProcessor& proc;
    int dragCol = -1, dragStartPitch = 0, dragStartY = 0;
    int gateDragCol = -1;      // step whose gate/length a GATE-row drag is editing
    bool gateDragMoved = false; // true once a drag extended the note (vs. a plain click)
    int cursor = 0;      // step the keyboard writes to
    int kbLow = -12;     // combined semitone of the keyboard's leftmost (low) C

    static constexpr int labelW = 44;
    static constexpr int handleW = 5;
    static constexpr int ledH = 14, gateH = 20, pitchH = 22, flagH = 15;
    static constexpr int kbTop = ledH + gateH + pitchH + flagH + flagH;
    static constexpr int whiteKeys = 15;   // 2 octaves + top C
    static constexpr int noKey = -1000;
};

// 5-lane × 16-step drum grid. Click toggles a hit, shift/right-click toggles accent.
class DrumGrid : public juce::Component, private juce::Timer
{
public:
    explicit DrumGrid (BP303AudioProcessor& p) : proc (p) { startTimerHz (25); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // The one cell a moving playhead changes, for one lane. Each lane carries its
    // own marker, since lanes of different lengths are in different places.
    juce::Rectangle<int> playheadCellBounds (int lane, int col) const;

    // The grabbable bar at a lane's end.
    juce::Rectangle<int> laneEndHandle (int lane, int len) const;

private:
    void timerCallback() override;

    // Click-drag painting: the pressed cell picks the operation, then a drag
    // applies it to every step it crosses within the starting lane.
    // SetLevel carries the dynamics the pressed cell cycled to, so the drag
    // stamps that same level onto everything it crosses. DragLength is the odd
    // one out — it moves the lane's end rather than touching any step.
    enum class Paint { None, SetHit, ClearHit, SetLevel, SetRatchet, DragLength };
    void applyPaint (int lane, int col);

    static constexpr int handleW = 5;

    // As in StepGrid: what paint() reads, so the timer can skip a frame with
    // nothing to show. The playhead marker only sits on the top lane.
    struct View
    {
        // one marker and one end per lane now, so both are arrays; length still
        // carries the master, since a following lane's end moves with it
        int playing[DrumSequencer::numLanes] = { -2, -2, -2, -2, -2 };
        int laneLen[DrumSequencer::numLanes] = { -1, -1, -1, -1, -1 };
        bool fit[DrumSequencer::numLanes] = {};
        int length = -1;
        juce::uint64 pattern = 0;

        bool operator== (const View& o) const
        {
            return samePlayheads (o) && sameApartFromPlayhead (o);
        }
        bool samePlayheads (const View& o) const
        {
            for (int i = 0; i < DrumSequencer::numLanes; ++i)
                if (playing[i] != o.playing[i])
                    return false;
            return true;
        }
        bool sameApartFromPlayhead (const View& o) const
        {
            for (int i = 0; i < DrumSequencer::numLanes; ++i)
                if (laneLen[i] != o.laneLen[i] || fit[i] != o.fit[i])
                    return false;
            return length == o.length && pattern == o.pattern;
        }
    };

    View liveView() const;
    void repaintPlayhead (int lane, int col);

    View shown;

    BP303AudioProcessor& proc;
    Paint paintMode = Paint::None;
    int paintLane = -1;
    int paintDyn = dyn303::Normal;   // for Paint::SetLevel
    int paintRatchet = 1;            // for Paint::SetRatchet
    static constexpr int labelW = 44;
};

// The graphic EQ for one line, drawn as its response curve over a live
// spectrum: the ten band followers fill in behind as octave-wide columns, the
// composite response of the ten filters runs across the top, and each band is a
// node on that curve you drag.
//
// The curve is the honest picture of a graphic EQ in a way ten faders are not.
// Octave bands overlap, so two neighbouring faders at +6 do not make two +6 dB
// bumps — they make one +10 dB shelf, and the fader bank never shows you that.
// The curve is built from `GraphicEq::responseDb`, which is the audio path's own
// coefficients, so the line cannot drift from what is being heard.
//
// The meters share the curve's frequency axis rather than sitting beside it.
// Each column spans exactly the octave its band covers, which is the one honest
// way to draw ten bins: no interpolation pretending to be resolution nobody
// measured.
//
// Still one component rather than thirty children. Ten nodes, ten meters and a
// grid, each with its own paint and repaint region at 25 Hz, is exactly the
// shape of thing that made repaints the expensive half of this plugin.
class EqBands : public juce::Component, private juce::Timer
{
public:
    // line 0 is the bass, 1 the drums — the same numbering the processor's
    // meter accessor uses.
    EqBands (BP303AudioProcessor& p, int lineIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Every band back to 0 dB — the panel's FLAT button. One gesture per band
    // rather than one for the lot, since a host records them per parameter.
    void flatten();

    // "500Hz +12.0 dB" for whichever node is being dragged or pointed at, and
    // empty when neither. The readout lives in the section header rather than
    // in the plot, where at 54px tall it would be sitting on the curve.
    std::function<void (const juce::String&)> onReadout;

    // The plot, and the vertical strip one band's meter occupies in it. Public
    // so BP303_PerfBench can measure the repaint that actually runs at 25 Hz
    // (the strip) against the one that only runs while dragging (the plot).
    juce::Rectangle<int> plotArea() const;
    juce::Rectangle<int> bandStrip (int b) const;

private:
    void timerCallback() override;

    float freqToX (float hz) const;
    float gainToY (float db) const;
    float yToGain (float y) const;
    int   nearestBand (int x) const;
    void  writeBand (int band, const juce::MouseEvent&);
    void  setReadout (int band);

    // The curve only changes when a gain does, so it is built into a Path and
    // kept. Rebuilding it on every meter tick would be ~375 response
    // evaluations a frame for a line that did not move.
    void ensureCurve();
    juce::Path curve;
    float curveBuiltFrom[GraphicEq::numBands] = {};
    bool  curveValid = false;

    BP303AudioProcessor& proc;
    int line = 0;

    std::atomic<float>* gain[GraphicEq::numBands] = {};
    std::unique_ptr<juce::ParameterAttachment> att[GraphicEq::numBands];

    int dragBand = -1, hoverBand = -1;
    float dragStartGain = 0.0f;
    int   dragStartY = 0;

    // What each meter last drew, quantised to the pixel row it lands on: a
    // level that hasn't moved far enough to look different doesn't get to cost
    // a repaint.
    int shownMeter[GraphicEq::numBands] = {};

    // ±14 rather than ±12 so a node parked at the top of its travel still has
    // room to be drawn as a circle instead of a clipped half one.
    static constexpr float plotRangeDb = 14.0f;
    static constexpr float loHz = 22.0f, hiHz = 20000.0f;
    static constexpr int labelH = 11, axisW = 22, nodeR = 4;
};

// The performance XY pad: one gesture across several parameters at once, in the
// space the EQ was using for width it did not need.
//
// The pad writes `padx`/`pady`/`padon` and nothing else. What those reach is
// macropad's business — see MacroPad.h for why the pad offsets the knobs rather
// than writing them, and why touching it engages the mode's units.
//
// Two ways of letting go, because the pad has two jobs. Momentary is the Kaoss
// behaviour and is what a pad is for live: the axes spring back to centre on
// release and the patch is exactly what it was. LATCH parks the gesture where it
// was left, which is what you want when the pad is a sound-design control rather
// than a performance one. The spring-back is a complete gesture of its own so a
// host in write-automation records the return as well as the throw.
class XyPad : public juce::Component, private juce::Timer
{
public:
    explicit XyPad (BP303AudioProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // The pad square itself. The rest of the component is the axis readout
    // column beside it, which is not a target.
    juce::Rectangle<int> padArea() const;

    // Fired whenever the axes or the hold state have moved, and on every tick
    // while the pad is held. The editor uses it to keep the modulation arc on
    // each destination knob following the gesture.
    std::function<void()> onPadMoved;

private:
    void timerCallback() override;
    void writeAxes (const juce::MouseEvent&);
    void setHeld (bool);

    // A puff of sparks off the handle while the gesture is moving. Driven from
    // how far the handle travelled since the last tick rather than from the
    // mouse, so a pad swept by automation throws them too — the same reason the
    // knob arcs come off the timer.
    //
    // Only *moving* spawns them. A pad held still, or latched and left, settles
    // to nothing in about half a second and then costs no repaints at all, which
    // is what keeps BP303_PerfBench's stopped-and-untouched line at zero.
    struct Spark
    {
        float x = 0.0f, y = 0.0f;     // component coordinates
        float vx = 0.0f, vy = 0.0f;   // pixels a tick
        float life = 0.0f;            // 1 down to 0; <= 0 is a free slot
        float size = 1.0f;
    };

    void spawnSparks (juce::Point<float> at, float speed);
    bool advanceSparks();             // true while any are still alive

    // Fixed, and reused in place: a burst is capped at what can be drawn inside
    // a 110px square without becoming a smear, and nothing allocates on a timer.
    static constexpr int maxSparks = 56;
    Spark sparks[maxSparks];
    juce::Random sparkRng;

    BP303AudioProcessor& proc;

    std::atomic<float>* xVal = nullptr;
    std::atomic<float>* yVal = nullptr;
    std::atomic<float>* onVal = nullptr;
    std::unique_ptr<juce::ParameterAttachment> xAtt, yAtt, onAtt, modeAtt;

    bool dragging = false;

    // What the handle last drew, in pixels. Automation moving the pad has to
    // redraw it, but at 25 Hz a value that lands on the same pixel does not get
    // to cost a repaint — the same rule the EQ's meters follow.
    int shownX = -1, shownY = -1;

    // The raw values behind onPadMoved. Separate from the pixels above because a
    // sub-pixel move still changes what the destination knobs should be showing.
    float lastX = 0.0f, lastY = 0.0f;
    bool  lastHeld = false;

    static constexpr int margin = 6;
};

// Per-line pattern selector: an A/B/C bank row over a 3x3 key grid. Clicking a
// key queues that line's slot; while the line runs the switch lands on the next
// pattern boundary (the key blinks until it takes over). One instance drives the
// bass bank, another the drum bank (chosen by Role).
//
// A key is also a drag source, with two destinations: dropped on the SONG list
// it arranges that pattern, and dragged out of the plugin window it becomes a
// .mid file for the host (see the editor's shouldDropFilesWhenDraggedExternally).
// The LFO's shape with a dot riding it, drawn from the same `lfo::Lfo` the audio
// thread reads and the phase that thread published — so it cannot show a wave
// the DSP isn't producing, the way `EqBands` takes its curve from the audio
// path's own coefficients rather than redrawing them.
//
// It earns its place by answering the two questions the controls can't: whether
// the LFO is moving at all, and where in its cycle it is. Both were guesswork
// while this thing lived only in the host's automation list.
class LfoScope : public juce::Component, private juce::Timer
{
public:
    explicit LfoScope (BP303AudioProcessor& p);
    void paint (juce::Graphics&) override;

    // Fired on the ticks this decides are worth drawing, so the rings round the
    // modulated knobs come off the same clock and the same early-out. The pad's
    // timer only ticks the knobs while the pad is moving, so without this an
    // LFO's rings would sit still while the sound moved.
    std::function<void()> onLfoMoved;

    // With DRAW selected the scope *is* the shape editor: dragging across it
    // paints step levels, the same gesture DrumGrid uses on the pattern lanes.
    // Editing the picture you are already watching beats a separate panel that
    // would have to be squeezed in beside it, and the phase dot means you are
    // drawing with the playhead visible.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // Which step a point is over, and what level its height means. Shared by
    // the mouse handlers and the drawing so a step cannot be painted somewhere
    // other than where it is shown.
    int   stepAt (juce::Point<float> p) const;
    float levelAt (juce::Point<float> p) const;
    juce::Rectangle<float> plotArea() const;
    void  paintStep (juce::Point<float> p);

    // The loop end marker: where it sits, and setting it from a drag. The end
    // of the drawn loop is one grabbable bar the same way a drum lane's length
    // is — near it a drag moves the loop point, away from it a drag paints
    // steps, which is why the two need telling apart on mouseDown.
    int   drawLen() const;
    float endMarkerX() const;
    void  setLenFrom (juce::Point<float> p);
    bool  draggingEnd = false;

    BP303AudioProcessor& proc;

    // Only a moved dot or a changed shape costs a repaint. An LFO switched off
    // is a still picture, and a still picture should not cost 25 frames a second
    // in a plugin whose CPU has always been in the editor.
    double lastPhase = -1.0;
    int    lastShape = -1;
    float  lastDepth = 0.0f;
    bool   lastOn = false;
    int    lastLen = -1;    // so a host-automated loop length repaints the marker

    static constexpr int plotInset = 4;
};

class PatternKeys : public juce::Component, private juce::Timer
{
public:
    enum class Role { Bass, Drum };
    PatternKeys (BP303AudioProcessor& p, Role r);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    int current() const;
    int pending() const;
    void request (int idx);

    BP303AudioProcessor& proc;
    Role role;
    int viewBank = 0;
    bool blinkOn = false;

    // The blink is the only thing here that animates, and it only runs while a
    // switch is queued. With nothing pending and the selection unmoved, the
    // keypad would redraw the same pixels, so the timer stops repainting.
    bool wasBlinking = false;
    int  shownCurrent = -1, shownBank = -1;

    // A press queues the pattern on release, so that dragging a key into the
    // song list copies it there without also switching the live pattern.
    int  pressedSlot = -1;
    bool dragStarted = false;

    static constexpr int numBanks = 3, keysPerBank = 9, cols = 3, rows = 3;
    static constexpr int bankRowH = 20, gap = 4;
};

// A guided tour of the interface. Covers the editor, dims everything except the
// part being explained, and steps through with NEXT / BACK. Purely a teaching
// layer — it never touches the plugin's state.
class HelpOverlay : public juce::Component
{
public:
    struct Step
    {
        juce::Rectangle<int> target;   // empty = no highlight, callout centred
        juce::String title, body;
    };

    explicit HelpOverlay (BP303AudioProcessor& p);

    void setSteps (std::vector<Step> newSteps);
    void start();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void showStep (int index);
    void layoutCallout();

    // The callout grows to fit its text, so a long step can't push its body
    // under the buttons. paint() and layoutCallout() both take their rectangles
    // from calloutParts(), so the two can never disagree about where the text
    // ends and the buttons begin.
    juce::AttributedString bodyText (int index) const;
    int bodyHeight (int index) const;
    juce::Rectangle<int> calloutBounds() const;
    void calloutParts (juce::Rectangle<int>& title,
                       juce::Rectangle<int>& body,
                       juce::Rectangle<int>& buttons) const;

    BP303AudioProcessor& proc;
    std::vector<Step> steps;
    int current = 0;

    juce::TextButton backButton { "BACK" }, nextButton { "NEXT" }, closeButton { "CLOSE" };

    static constexpr int calloutW = 360, pad = 14;
    static constexpr int innerPad = 14, titleH = 20, buttonH = 22, buttonGap = 12;
};

// Transport for the song: rewind / play / stop / fast-forward. Play and stop
// drive the RUN clock; the cue buttons step the playhead through the
// arrangement, and work while stopped so you can start from a chosen row.
class SongTransport : public juce::Component, private juce::Timer
{
public:
    explicit SongTransport (BP303AudioProcessor& p) : proc (p) { startTimerHz (10); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onCue;   // editor follows the cued row in the list

private:
    void timerCallback() override;
    enum Button { Rewind = 0, Play, Stop, Forward, numButtons };
    juce::Rectangle<int> buttonArea (int index) const;
    bool isRunning() const;

    BP303AudioProcessor& proc;
    bool lastRunning = false;
};

// A combo box that rescans the song library just before it drops down, so newly
// saved songs appear without the editor having to watch the folder.
class LibraryBox : public juce::ComboBox
{
public:
    std::function<void()> onOpen;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onOpen)
            onOpen();
        juce::ComboBox::mouseDown (e);
    }
};

// The song: a scrolling list of arrangement rows, one per song step, showing
// the bass and drum pattern each step plays, how many times it repeats, and
// whether either line is dropped. Clicking the number column drops the playhead
// on that row; selecting a row arms it, so the pattern keypads assign into it
// instead of switching patterns.
class SongList : public juce::Component,
                 public juce::DragAndDropTarget,
                 private juce::Timer
{
public:
    explicit SongList (BP303AudioProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Dragging a pattern key in from either keypad: dropping on the body of a
    // row assigns into that row's column, dropping on the thin band at the top
    // of a row (or past the last row) inserts a new row there.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    static juce::String dragDescription (bool bass, int slot);
    static bool parseDrag (const juce::var&, bool& bass, int& slot);

    int  getSelectedRow() const { return selected; }
    void followPlayhead();      // scroll the cued/playing row into view

    // toolbar operations
    void insertRow();
    void deleteRow();
    void duplicateRow();
    void moveRow (int direction);
    void clearSong();

    std::function<void()> onSongChanged;   // editor refreshes the summary text

    static juce::String slotName (int slot);   // 0 -> "A1", 26 -> "C9", hold -> "-"

private:
    void timerCallback() override;
    void changed();

    // What paint() reads, so the timer can skip a frame with nothing to show.
    // Only the visible rows are hashed — a change below the fold isn't drawn.
    struct View
    {
        int playing = -2, count = -1, scrollTop = -1, selected = -2, dropRow = -2;
        bool dropInsert = false;
        juce::uint64 rows = 0;

        bool operator== (const View& o) const
        {
            return playing == o.playing && count == o.count && scrollTop == o.scrollTop
                && selected == o.selected && dropRow == o.dropRow
                && dropInsert == o.dropInsert && rows == o.rows;
        }
    };

    View liveView() const;
    View shown;

    enum class Col { None, Index, Bass, Drum, Reps, BassMute, DrumMute };
    static constexpr int numCols = 6;
    static void columnRects (juce::Rectangle<int> row, juce::Rectangle<int>* out);
    Col  columnAt (int x) const;
    int  rowAt (int y) const;             // song step under y, or -1
    int  visibleRows() const;
    void scrollTo (int row);
    void select (int row);

    BP303AudioProcessor& proc;
    int selected = -1;
    int scrollTop = 0;
    int lastPlaying = -1;
    Col dragCol = Col::None;
    int dragRow = -1, dragStartY = 0, dragStartValue = 0;

    // incoming pattern drag
    void updateDropTarget (const SourceDetails&);
    int  dropRow = -1;          // row to assign into, or to insert before
    bool dropInsert = false;
    bool dropIsBass = true;
    int  dragScroll = 0;        // edge auto-scroll while a drag hovers

    static constexpr int headerH = 15, rowH = 21, insertBand = 6;
};

// One effect's controls, laid out by a caller-supplied function so a page lays
// itself out in its own local coordinates regardless of where the tabbed panel
// sits or how wide it is.
struct FxPage : juce::Component
{
    std::function<void (juce::Rectangle<int>)> layoutFn;
    void resized() override { if (layoutFn) layoutFn (getLocalBounds()); }
};

// A titled, framed FX panel with a row of tabs at the top and one page visible
// at a time. Skin-driven so it matches the surrounding hardware look. Pages are
// owned by the editor and registered with addPage() in tab order.
class FxSection : public juce::Component
{
public:
    // `enableIds` names each tab's ACTIVE parameter, in tab order, so the bar can
    // show which effects are switched on without the tab having to be opened.
    FxSection (BP303AudioProcessor& p, juce::String title, juce::StringArray tabNames,
               juce::StringArray enableIds);

    void addPage (juce::Component& page);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Rectangle<int> contentArea() const;

    // Whether that tab's effect is switched on. Public so BP303_FxTabSnapshot
    // can render the bar in a known state.
    bool tabEnabled (int i) const;

    // Which panel this is. The DRUMS panel is an FxSection too, so the tools
    // that walk the tree for "the FX sections" need a way to say which they mean.
    const juce::String& sectionTitle() const { return title; }

    // Which tab is showing, and a way to hear about it changing. The EQ panel
    // keeps its ACTIVE and FLAT in the header rather than on the pages, so it
    // has to swap them over when the tab does.
    int currentTab() const { return current; }
    std::function<void (int)> onTabChanged;

    // Move the bar from outside. The pad's tab *is* a parameter, so automation
    // or a preset load has to be able to put the bar where the parameter says.
    void setTab (int i) { showTab (i); }

    // The part of the tab bar the tabs themselves don't use. With two tabs
    // capped at maxTabW on a wide panel that is most of the bar, and it is the
    // natural home for controls belonging to the panel rather than to a page.
    juce::Rectangle<int> tabBarFreeArea() const;

private:
    juce::Rectangle<int> tabBarArea() const;
    // Shared by paint and hit-testing so the two can never drift apart.
    float tabSegmentWidth() const;
    void showTab (int i);

    BP303AudioProcessor& proc;
    juce::String title;
    juce::StringArray tabs;
    juce::StringArray enables;
    std::vector<juce::Component*> pages;
    int current = 0;

    // An ACTIVE toggle can move from the page, from automation or from a preset
    // load, and any of those has to redraw the bar. Listening beats polling.
    std::vector<std::unique_ptr<juce::ParameterAttachment>> enableWatchers;

    static constexpr int titleH = 18, tabH = 18;
    // Tabs divide the bar evenly, but only up to a cap. Without one the DRUMS
    // panel — three tabs across the full width of the window — would give each a
    // 480px slab with a word lost in the middle, sitting next to FX panels whose
    // tabs are a third of that. The cap keeps every tab bar reading at one scale.
    static constexpr int maxTabW = 150;
};

// Everything the editor draws lives inside one fixed-size component that the
// editor scales to whatever size the host gives it. That keeps the whole layout
// in a single native coordinate space — every panel, grid and knob position
// stays in pixels — while still letting the window be resized to fit a smaller
// screen. Only the scale factor changes; nothing has to be laid out twice.
class ScaledContent : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> onPaint;
    std::function<void()> onResized;
    std::function<void (const juce::MouseEvent&)> onMouseDown;
    std::function<void (const juce::MouseEvent&)> onMouseDrag;
    std::function<void (const juce::MouseEvent&)> onMouseUp;

    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    void resized() override                 { if (onResized) onResized(); }
    void mouseDown (const juce::MouseEvent& e) override { if (onMouseDown) onMouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (onMouseDrag) onMouseDrag (e); }
    void mouseUp   (const juce::MouseEvent& e) override { if (onMouseUp)   onMouseUp (e); }
};

// The window background and the panel frames: everything behind the controls
// that only changes when the skin does. Kept in its own component and cached to
// an image, because otherwise every small repaint — a playhead LED moving — re-runs
// all eleven panel gradients, shadows and screws under a clip that throws them
// away. JUCE renders the cache at the display's real pixel scale, so it stays
// sharp on Retina. Transparent to the mouse, so the header's skin hotspot still
// sees its right-click.
class Chrome : public juce::Component
{
public:
    Chrome()
    {
        setBufferedToImage (true);
        setInterceptsMouseClicks (false, false);
    }

    std::function<void (juce::Graphics&)> onPaint;

    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
};

class BP303AudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::DragAndDropContainer,
                                  private juce::Timer
{
public:
    explicit BP303AudioProcessorEditor (BP303AudioProcessor&);
    ~BP303AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The design size the layout is written in. Public because the pixel-level
    // tools — repaint_test, skin_test — have to render at scale 1 to compare a
    // clipped redraw against a full one, and they used to get it from the resize
    // *maximum*. That is no longer the same number: the maximum is now whatever
    // fits the user's display, since a window taller than the screen hides its
    // own resize corner and cannot be shrunk back.
    static juce::Point<int> nativeSize();

    // Dragging a pattern key clear of the plugin window turns the drag into a
    // .mid file for the host, so the same gesture arranges into the SONG list or
    // drops a region on a track depending on where it ends up.
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                               juce::StringArray& files,
                                               bool& canMoveFiles) override;

    // JUCE offers the external drag once per gesture and never again, so a
    // refusal while the pointer is still inside the window ends the export for
    // good. These watch the drag and hand the file over when it does leave.
    void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded   (const juce::DragAndDropTarget::SourceDetails&) override;

    // Where the pointer is, in screen coordinates. Only the external-drag guard
    // below uses it; it is a member so a test can place the pointer, since the
    // guard's whole job is to behave differently inside and outside the editor.
    std::function<juce::Point<int>()> pointerPosition { [] { return juce::Desktop::getMousePosition(); } };

    // What the song drag hands over. The choice has to be armed before the drag
    // starts: the host asks for the file mid-gesture, so there is no moment at
    // which a dialog could be shown. Hence a mode on the button rather than a
    // prompt on the drop. Public alongside the drag itself, so a test can arm a
    // mode without going through the menu.
    enum SongMidiMode { splitTracks = 0, bassOnly, drumsOnly };
    void setSongMidiMode (int mode);

private:
    void timerCallback() override;
    void updateSongInfo();       // step / bar summary under the song list

    // The layout, in the native coordinate space of `content`. The editor's own
    // paint()/resized() only position and scale that component.
    void paintContent (juce::Graphics&);
    void layoutContent();

    // Skin picking. Deliberately unlabelled: right-click the BADPACKETS logo in
    // the top-left of the header to get the menu. `applySkin` re-reads the
    // processor's current skin into the look & feel and redraws.
    void showSkinMenu();
    void applySkin();

    // --- the key-colour easter egg (see ui303::setKeyHue) -------------------
    // Press the BADPACKETS legend and drag sideways, or shift+arrows where the
    // host lets arrow keys through. The change fades in: setKeyHueTarget moves
    // the dial's target and starts a short timer that eases the palette there,
    // repainting as it goes, with a full applySkin() once it lands.
    bool keyPressed (const juce::KeyPress&) override;
    void setKeyHueTarget (int stop);

    struct FadeTimer : juce::Timer
    {
        std::function<void()> onTick;
        void timerCallback() override { if (onTick) onTick(); }
    };
    FadeTimer hueFade;

    static constexpr int hueDragPixelsPerStop = 26;
    int  hueDragStartX = 0, hueDragStartStop = 0;
    bool hueDragging = false;

    ScaledContent content;
    Chrome        chrome;   // added to `content` first, so it sits behind everything

    // Native design size. The window keeps this aspect ratio and scales.
    //
    // The height grew by the 44px the drums row took to fit the XY pad. Every
    // row above the lower region is removed from the top, so growing both by the
    // same amount leaves the grids, keypads and SONG column exactly where they
    // were — the aspect ratio is the only thing that moved, and a project saved
    // before the pad reopens at its old width and a slightly taller window.
    // The height grew again, by the LFO row plus its gap. Rows above the lower
    // region are still removed from the top, so the grids, keypads and SONG
    // column stay exactly where they were and a project saved before the LFO
    // reopens at its old width and a taller window — the same trade the pad's
    // 44px took.
    static constexpr int nativeWidth = 1466, nativeHeight = 892 + 90 + 6;

    // The drums/pad/EQ row. Read by both layoutContent and the chrome, which is
    // why it is a constant rather than a literal in each.
    static constexpr int drumRowH = 164;

    // --- the vertical rules ---------------------------------------------------
    // The lower region's two right-hand columns are what the rest of the window
    // lines up on. `keysLeft` is the left edge of the keypad column, and it is
    // also where DRUM FX and the EQ start; one gap to its left is the right edge
    // of the pattern grids, and that is where BASS FX and the PAD end.
    //
    // Every panel on those rules is *derived* from these three numbers rather
    // than written out as a width, so widening the SONG column moves the six
    // panels above it instead of quietly breaking the alignment.
    static constexpr int panelGap = 6;
    static constexpr int songColumnW = 280, keysColumnW = 204;
    static constexpr int contentW = nativeWidth - 16;    // the 8px margin, both sides
    static constexpr int keysLeft = contentW - songColumnW - panelGap - keysColumnW;

    // Row 2: DRUM FX sits on the rule, BASS FX matches it, PERFORMANCE takes the
    // rest — which is 128px more than it had when the two FX panels split the
    // row evenly between them.
    static constexpr int fxSectionW   = contentW - keysLeft;
    static constexpr int perfSectionW = keysLeft - 2 * panelGap - fxSectionW;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::NoTextBox };
        juce::Label label;
        std::unique_ptr<SliderAtt> att;

        // Kept so a knob the XY pad reaches can be asked for its own parameter
        // without the pad's table having to name the id a second time.
        juce::String paramId;

        void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramId, const juce::String& text,
                   int hotEnd = ui303::HotNone, bool bipolar = false);
        void setBounds (juce::Rectangle<int> r);
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAtt> att;

        void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramId, const juce::String& text);
        void setBounds (juce::Rectangle<int> r);
    };

    struct Switch
    {
        std::unique_ptr<SegmentedSwitch> sw;
        juce::Label label;

        void init (juce::Component& parent, BP303AudioProcessor& proc,
                   juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramId, const juce::String& text);
        void setBounds (juce::Rectangle<int> r);
    };

    BP303AudioProcessor& proc;
    Look303 look;

    // synth row
    Switch wave;
    // VIB SPEED and VIB DEPTH are no longer on the panel — unison took their
    // slots. The two parameters are still registered and still sound, so a
    // project that used vibrato keeps it and no host's automation lane shifts
    // index; they are simply reachable from the host rather than from here.
    Knob tuning, cutoff, resonance, envmod, attack, decay, accent, volume;
    Knob uniVoices, uniDetune, uniSpread;

    // performance
    Switch playMode;
    juce::ToggleButton runButton { "RUN" };
    juce::ToggleButton recButton { "REC" };
    juce::ToggleButton metroButton { "CLICK" };
    std::unique_ptr<ButtonAtt> runAtt, recAtt, metroAtt;
    Knob intBpm, shuffle;
    juce::Slider lengthSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label lengthLabel;

    // --- bass fx (tabbed) ---
    FxSection bassFx { proc, "BASS FX",
                       juce::StringArray { "DIST", "DELAY", "FILTER", "COMP",
                                           "CHORUS", "REVERB" },
                       juce::StringArray { "diston", "delayon", "bflton", "bcompon",
                                           "bchron", "brevon" } };
    FxPage bassDistPage, bassDelayPage, bassFilterPage, bassCompPage,
           bassChorusPage, bassReverbPage;

    // The DIST tab is itself a small stack: TYPE picks which knob group shows,
    // so each shaper gets controls that mean something rather than a pair of
    // generic ones. SOFT and FUZZ share the original DRIVE/COLOR pair.
    juce::ToggleButton distOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> distOnAtt;
    Choice bassDistType;
    Knob bassDistLows;
    FxPage bassDriveGroup, bassCrushGroup, bassFoldGroup, bassRectGroup;
    Knob distDrive, distColor;
    Knob bassCrushBits, bassCrushRate;
    Knob bassFoldAmt, bassFoldSym;
    Knob bassRectAmt, bassRectTone;
    // TYPE picks MONO (one feedback line) or STEREO (ping-pong). Unlike DIST it
    // needs no per-type controls: TIME, FEEDBACK and MIX mean the same thing for
    // both, so the one selector is the whole difference between them.
    juce::ToggleButton delayOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> delayOnAtt;
    Choice delayType, delayTime;
    Knob delayFb, delayMix;

    juce::ToggleButton bassFltOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> bassFltOnAtt;
    Switch bassFltMode;
    Knob bassFltCut, bassFltRes, bassFltEnv;

    juce::ToggleButton bassCompOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> bassCompOnAtt;
    Knob bassCompThr, bassCompRat, bassCompMk;

    juce::ToggleButton bassChorusOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> bassChorusOnAtt;
    Knob bassChorusRate, bassChorusDepth, bassChorusMix;

    juce::ToggleButton bassReverbOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> bassReverbOnAtt;
    Knob bassReverbSize, bassReverbDamp, bassReverbMix;

    // drums
    // KIT stays outside the tabs — it applies to every page — so it is a child
    // of the editor drawn over the section's content area, not of a page. It is
    // a 3-segment switch, so it gets more width than a knob column.
    static constexpr int drumKitColumnW = 96;
    Switch kit;
    Knob bdTune, sdTune, cpTune, hatTune, bdDecay, drumVol;
    Knob sdDecay, chDecay, ohDecay, drumSpread;
    Knob panKnobs[5];
    Knob laneKnobs[5];

    // Fourteen drum controls will not read at 90px a column across one strip, so
    // the panel borrows the FX sections' tab bar. Tabbed by function rather than
    // by voice: setting a balance means comparing levels across voices, and
    // per-voice pages would turn that into five tab switches. No enable ids —
    // a drum page has nothing to switch on — which FxSection handles by simply
    // not drawing any lamps.
    FxSection drums { proc, "DRUMS",
                      juce::StringArray { "MIX", "TUNE", "DECAY", "BALANCE" },
                      juce::StringArray {} };
    FxPage drumMixPage, drumTunePage, drumDecayPage, drumBalancePage;

    // The drum pages lay their knobs out at a fixed column width and stop,
    // rather than dividing whatever the section is given between them. That is
    // what frees the right of the row for the EQ, and it is what stops the
    // four-knob pages putting their knobs somewhere different from the six-knob
    // ones — a proportional split meant the same knob moved when you changed
    // tab.
    //
    // The width is spacing, not size: a knob on these pages is 53px across —
    // the 66px page height less its 13px label — whatever column it is given,
    // so this number only decides how much air sits between them and how much
    // room the longest label ("DRUM VOL") has.
    //
    // It went 92 -> 96 when the EQ moved onto the keypad rule: that freed 24px
    // on this row, and spending it on the six columns keeps DRUMS sized to
    // exactly what its widest page needs rather than leaving a gap at its right.
    static constexpr int drumKnobColumnW = 96;

    // ...and this is the height that decides the size, which is why it is pinned
    // rather than taken from the page: the row is 44px taller than the knobs
    // were drawn for, and letting them take it would make the drum controls the
    // largest knobs in the window.
    static constexpr int drumKnobRowH = 66;

    // --- performance XY pad --------------------------------------------------
    // Its tabs pick a mode rather than a page, so the section has four tabs and
    // one page — the same pad, redrawn with different axis labels. No enable
    // ids: a mode's units are engaged by the gesture rather than by a switch,
    // so there is nothing for a lamp to report that the pad itself doesn't
    // already show.
    //
    // Tab order is `macropad::Mode` order, which `padmode` stores by index.
    // Sized to the pad square plus the readout column, and no wider: the square
    // is capped by the row height, so every pixel past that is width the EQ
    // could have had instead.
    static constexpr int padSectionW = 264;

    // DRUMS, the PAD and the two gaps have to land exactly on the keypad rule,
    // or the EQ starts somewhere other than where DRUM FX does and the whole
    // point of the alignment is lost. Checked here rather than discovered in a
    // snapshot, since the three widths are set in three different places.
    static_assert (12 + drumKitColumnW + 6 * drumKnobColumnW
                       + panelGap + padSectionW + panelGap == keysLeft,
                   "the drums row no longer lands on the keypad column's left edge");
    FxSection padSection { proc, "PAD",
                           juce::StringArray { "ACID", "GRIT", "SPACE", "KIT" },
                           juce::StringArray {} };
    XyPad xyPad { proc };

    // Every knob the pad can reach, so the arc it draws round each one can be
    // kept in step with the gesture. Built once in the constructor: the pad's
    // destinations are fixed by MacroPad.h, and a knob that is off-screen behind
    // an FX tab simply never paints.
    struct PadKnob { Knob* knob; macropad::Dest dest; };
    std::vector<PadKnob> padKnobs;

    // Recomputes each destination knob's offset and repaints the ones that moved.
    // Both modulators feed the one `padOffset` property a knob draws from: two
    // indicator systems on one knob would need every skin to know about both,
    // and the whole point of putting the offset into `drawRotarySlider` was that
    // no skin has to know about any of it.
    void updatePadKnobs();

    // --- LFO 1 ---------------------------------------------------------------
    // A row of its own rather than a tab, because a tab you have to open is the
    // wrong home for the one control that tells you whether a patch is moving.
    // Sized for the LFOs that follow: LFO 1 takes the left, and the scope fills
    // the rest until there is a second one to put there.
    // A plain titled panel rather than an FxSection: a tab bar with one tab on
    // it costs 18px of the row's height and offers nothing to click. When LFO 2
    // and 3 arrive they go side by side across this row, not behind tabs — a
    // source you have to reveal is no use as a drag source.
    static constexpr int lfoRowH = 90;

    juce::ToggleButton lfoOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> lfoOnAtt;
    juce::ToggleButton lfoSync { "SYNC" };
    std::unique_ptr<ButtonAtt> lfoSyncAtt;
    juce::ToggleButton lfoSmooth { "SMOOTH" };
    std::unique_ptr<ButtonAtt> lfoSmoothAtt;
    // Watches the shape choice so SMOOTH greys and the scope's step slots
    // appear when DRAW is picked, whether that came from the switch, a preset
    // load or a host automation lane.
    std::unique_ptr<juce::ParameterAttachment> lfoShapeAtt;
    Switch lfoShape;
    Choice lfoDiv, lfoDest;
    Knob   lfoRate, lfoAmt;
    LfoScope lfoScope { proc };

    // LATCH sits in the pad's readout column rather than the tab bar: four tabs
    // across 300px leave the bar with nothing spare, unlike the EQ's two.
    juce::ToggleButton padLatch { "LATCH" };
    std::unique_ptr<ButtonAtt> padLatchAtt;

    // Keeps the tab on whatever `padmode` says, so automation or a preset load
    // moves it rather than leaving the bar pointing at a mode the pad is no
    // longer in.
    std::unique_ptr<juce::ParameterAttachment> padModeAtt;

    // --- graphic EQ (tabbed BASS / DRUMS) -----------------------------------
    // Its own section beside DRUMS rather than a fifth drum page. Half of it
    // edits the bass, and a bass fader living behind a panel titled DRUMS would
    // be a lie about what it does — so the two sit side by side on the row and
    // each says what it is.
    FxSection eqSection { proc, "EQ",
                          juce::StringArray { "BASS", "DRUMS" },
                          juce::StringArray { "beqon", "deqon" } };
    FxPage bassEqPage, drumEqPage;
    EqBands bassEqBands { proc, 0 }, drumEqBands { proc, 1 };

    // ACTIVE, FLAT and the readout live in the section's header, not on the
    // pages: at 54px the plot has no room to spare, and all three belong to
    // whichever line is showing rather than to the curve itself. The two
    // toggles share one rectangle and swap with the tab, since each is attached
    // to its own line's parameter.
    juce::ToggleButton bassEqOn { "ACTIVE" }, drumEqOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> bassEqOnAtt, drumEqOnAtt;
    juce::TextButton eqFlat { "FLAT" };
    juce::Label eqReadout;

    EqBands& shownEqBands() { return eqSection.currentTab() == 0 ? bassEqBands
                                                                : drumEqBands; }

    // --- drum fx (tabbed) ---
    FxSection drumFx { proc, "DRUM FX",
                       juce::StringArray { "DIST", "DELAY", "FILTER", "COMP",
                                           "CHORUS", "REVERB" },
                       juce::StringArray { "ddiston", "ddelayon", "dflton", "dcompon",
                                           "dchron", "drevon" } };
    FxPage drumDistPage, drumDelayPage, drumFilterPage, drumCompPage,
           drumChorusPage, drumReverbPage;

    juce::ToggleButton drumDistOn { "ACTIVE" };
    std::unique_ptr<ButtonAtt> drumDistOnAtt;
    Choice drumDistType;
    Knob drumDistLows;
    FxPage drumDriveGroup, drumCrushGroup, drumFoldGroup, drumRectGroup;
    Knob drumDrive, drumDistColor;
    Knob drumCrushBits, drumCrushRate;
    Knob drumFoldAmt, drumFoldSym;
    Knob drumRectAmt, drumRectTone;

    juce::ToggleButton drumDelayOn { "ON" };
    std::unique_ptr<ButtonAtt> drumDelayOnAtt;
    Choice drumDelayType, drumDelayTime;
    Knob drumDelayFb, drumDelayMix;

    juce::ToggleButton drumFltOn { "ON" };
    std::unique_ptr<ButtonAtt> drumFltOnAtt;
    Switch drumFltMode;
    Knob drumFltCut, drumFltRes, drumFltEnv;

    juce::ToggleButton drumCompOn { "ON" };
    std::unique_ptr<ButtonAtt> drumCompOnAtt;
    Knob drumCompThr, drumCompRat, drumCompMk;

    juce::ToggleButton drumChorusOn { "ON" };
    std::unique_ptr<ButtonAtt> drumChorusOnAtt;
    Knob drumChorusRate, drumChorusDepth, drumChorusMix;

    juce::ToggleButton drumReverbOn { "ON" };
    std::unique_ptr<ButtonAtt> drumReverbOnAtt;
    Knob drumReverbSize, drumReverbDamp, drumReverbMix;

    // per-sequence enables
    juce::ToggleButton bassOnButton { "ON" };
    juce::ToggleButton drumsOnButton { "ON" };
    std::unique_ptr<ButtonAtt> bassOnAtt, drumsOnAtt;

    // pattern management
    juce::TextButton bassCopy { "COPY" }, bassPaste { "PASTE" }, bassClear { "CLEAR" },
                     bassTransDown { "TR -" }, bassTransUp { "TR +" },
                     bassShiftL { "SH <" }, bassShiftR { "SH >" },
                     bassHold { "HOLD" };
    juce::TextButton drumCopy { "COPY" }, drumPaste { "PASTE" }, drumClear { "CLEAR" },
                     drumShiftL { "SH <" }, drumShiftR { "SH >" };

    // Shows the knob group belonging to the selected distortion type. Driven by
    // the TYPE combo, which the host can move too, so automating the type
    // repaints the tab.
    void updateDistGroups();

    void transposeBass (int semitones);
    void shiftBass (int direction);
    void shiftDrums (int direction);

    // Drag-to-host MIDI export: writes one pattern slot to a temp .mid file and
    // returns it, or a non-existent File if it couldn't be written.
    // Distinct from SongList's "BP303PAT:" keys, so the two drags never collide.
    static constexpr const char* songDragDescription = "BP303SONG";

    int songMidiMode = splitTracks;
    void showSongMidiMenu();

    // Shared by JUCE's own external-drag offer and by the watcher that covers for
    // it, so the two can never disagree about what a drag hands over.
    bool buildDragFiles (const juce::var& description,
                         juce::StringArray& files, bool& canMoveFiles);
    void checkDragLeftWindow();

    // A drag is running and has not yet been handed to the host as a file. The
    // editor's own Timer is spoken for, so the watcher carries its own.
    struct DragWatcher : juce::Timer
    {
        std::function<void()> onTick;
        void timerCallback() override { if (onTick) onTick(); }
    };
    DragWatcher dragWatcher;
    juce::var draggingDescription;
    bool externalDragFired = false;

    juce::File writeSlotMidiFile (bool bass, int slot);
    juce::File writeSongMidiFile();
    juce::MidiMessageSequence bassSlotSequence (int slot, int& lengthSteps) const;
    juce::MidiMessageSequence drumSlotSequence (int slot, int lengthSteps) const;

    // song / arrangement
    SongTransport songTransport { proc };
    juce::ToggleButton songLoopButton { "LOOP" };
    juce::TextButton songIns { "INS" }, songDel { "DEL" }, songDup { "DUP" },
                     songUp { "UP" }, songDown { "DN" }, songClear { "CLR" };
    juce::Label songInfo;

    // song library (files under ~/Music/BP303/Songs)
    LibraryBox songLibrary;
    juce::TextButton songSave { "SAVE" };

    // Drag the whole arrangement out as one MIDI region. A TextButton cannot
    // start a drag on its own, so this is a button that also reports a drag —
    // pressed like a button, dragged like a pattern key.
    class SongDragButton : public juce::TextButton
    {
    public:
        SongDragButton() : juce::TextButton ("MIDI") {}
        std::function<void()> onDragOut;
        void mouseDrag (const juce::MouseEvent& e) override
        {
            // A few pixels of slop, so a slightly unsteady click is still a click.
            if (! dragging && e.getDistanceFromDragStart() > 4 && onDragOut)
            {
                dragging = true;
                onDragOut();
            }
            juce::TextButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            // Always passed on: Button tracks its own pressed state here, and
            // swallowing it leaves the button stuck looking held down after a
            // drag. The click is what opens the mode menu, so the flag has to
            // outlive the base call — a drag that ends over the button would
            // otherwise pop a menu on top of the region the host just took.
            endingDrag = dragging;
            dragging = false;
            juce::TextButton::mouseUp (e);
            endingDrag = false;
        }

        // True only for the duration of the mouseUp that ends a drag.
        bool isEndingDrag() const { return endingDrag; }

    private:
        bool dragging = false, endingDrag = false;
    };
    SongDragButton songDragMidi;
    juce::Array<juce::File> libraryFiles;
    std::unique_ptr<juce::FileChooser> chooser;

    void refreshLibrary();

    juce::TextButton helpButton { "HELP" };
    HelpOverlay help { proc };
    void startHelp();

    PatternKeys bassKeys { proc, PatternKeys::Role::Bass };
    PatternKeys drumKeys { proc, PatternKeys::Role::Drum };
    StepGrid stepGrid { proc };
    DrumGrid drumGrid { proc };
    SongList songList { proc };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BP303AudioProcessorEditor)
};
