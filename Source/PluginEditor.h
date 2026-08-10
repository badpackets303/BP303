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

    // The one cell a moving playhead changes. Public so BP303_PerfTest can check
    // that repainting only this really does leave the grid correct.
    juce::Rectangle<int> ledCellBounds (int col) const;

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
        juce::uint64 pattern = 0;      // the sixteen steps, hashed

        bool operator== (const View& o) const
        {
            return playing == o.playing && sameApartFromPlayhead (o);
        }

        // The playhead is only the LED row; everything else is the grid proper.
        bool sameApartFromPlayhead (const View& o) const
        {
            return length == o.length && cursor == o.cursor
                && kbLow == o.kbLow && pattern == o.pattern;
        }
    };

    View liveView() const;
    void repaintLed (int col);

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

    // The one cell a moving playhead changes: the marker sits on the top lane.
    juce::Rectangle<int> playheadCellBounds (int col) const;

private:
    void timerCallback() override;

    // Click-drag painting: the pressed cell picks the operation, then a drag
    // applies it to every step it crosses within the starting lane.
    // SetLevel carries the dynamics the pressed cell cycled to, so the drag
    // stamps that same level onto everything it crosses.
    enum class Paint { None, SetHit, ClearHit, SetLevel };
    void applyPaint (int lane, int col);

    // As in StepGrid: what paint() reads, so the timer can skip a frame with
    // nothing to show. The playhead marker only sits on the top lane.
    struct View
    {
        int playing = -2, length = -1;
        juce::uint64 pattern = 0;

        bool operator== (const View& o) const
        {
            return playing == o.playing && sameApartFromPlayhead (o);
        }
        bool sameApartFromPlayhead (const View& o) const
        {
            return length == o.length && pattern == o.pattern;
        }
    };

    View liveView() const;
    void repaintPlayhead (int col);

    View shown;

    BP303AudioProcessor& proc;
    Paint paintMode = Paint::None;
    int paintLane = -1;
    int paintDyn = dyn303::Normal;   // for Paint::SetLevel
    static constexpr int labelW = 44;
};

// Per-line pattern selector: an A/B/C bank row over a 3x3 key grid. Clicking a
// key queues that line's slot; while the line runs the switch lands on the next
// pattern boundary (the key blinks until it takes over). One instance drives the
// bass bank, another the drum bank (chosen by Role).
//
// A key is also a drag source, with two destinations: dropped on the SONG list
// it arranges that pattern, and dragged out of the plugin window it becomes a
// .mid file for the host (see the editor's shouldDropFilesWhenDraggedExternally).
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

private:
    juce::Rectangle<int> tabBarArea() const;
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

    // Dragging a pattern key clear of the plugin window turns the drag into a
    // .mid file for the host, so the same gesture arranges into the SONG list or
    // drops a region on a track depending on where it ends up.
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                               juce::StringArray& files,
                                               bool& canMoveFiles) override;

    // Where the pointer is, in screen coordinates. Only the external-drag guard
    // below uses it; it is a member so a test can place the pointer, since the
    // guard's whole job is to behave differently inside and outside the editor.
    std::function<juce::Point<int>()> pointerPosition { [] { return juce::Desktop::getMousePosition(); } };

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
    static constexpr int nativeWidth = 1466, nativeHeight = 848;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::NoTextBox };
        juce::Label label;
        std::unique_ptr<SliderAtt> att;

        void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramId, const juce::String& text,
                   int hotEnd = ui303::HotNone);
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
    Knob tuning, cutoff, resonance, envmod, attack, decay, accent, volume, vibSpeed, vibDepth;

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
    Switch kit;
    Knob bdTune, sdTune, cpTune, hatTune, bdDecay, drumVol;
    Knob laneKnobs[5];

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
    juce::File writeSlotMidiFile (bool bass, int slot);

    // song / arrangement
    SongTransport songTransport { proc };
    juce::ToggleButton songLoopButton { "LOOP" };
    juce::TextButton songIns { "INS" }, songDel { "DEL" }, songDup { "DUP" },
                     songUp { "UP" }, songDown { "DN" }, songClear { "CLR" };
    juce::Label songInfo;

    // song library (files under ~/Music/BP303/Songs)
    LibraryBox songLibrary;
    juce::TextButton songSave { "SAVE" };
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
