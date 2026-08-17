#include "PluginEditor.h"

#include "MidiExport.h"

//==============================================================================
// Palettes

namespace ui303
{
    // The key-colour dial. The stop is the target — what the gesture sets and
    // what gets saved — and the turn is where the fade has animated to, so the
    // palette can sit anywhere between two stops while a change is in flight.
    // Both are process-wide, like the tinted-palette cache below: the dial is an
    // app preference (every window shows the same look), not project state.
    // Message thread only, like everything else about how the editor looks.
    static int   keyHueStop = 0;
    static float keyHueTurn = 0.0f;   // in turns round the colour wheel

    const Palette& palette (int skinIndex)
    {
        using C = juce::Colour;

        static const Palette classic = {
            false,
            C (0xffd8d4cb), C (0xffd8d4cb),      // window
            C (0xffcfccc3), C (0xffcfccc3),      // panel
            C (0xff9a978e), C (0x00ffffff),      // outline, bevel
            C (0xbf3a3a36), C (0xff3a3a36),      // title, text
            C (0xffe0881f), C (0xffd04a30), C (0xff5a2020), C (0xffc2bfb6),
            C (0xffefede8), C (0xffefede8), C (0xff86847d), C (0xff3a3a36), C (0xff9a978e),
            C (0xffe9e7e1), C (0xff3a3a36),      // pitch cells
            C (0xffe9e7e1), C (0xff3a3a36),      // combos
            KnobFlat, false,
            C (0xffd8d4c8), C (0xff1b1815), C (0xff5a564d),   // keyboard
            C (0xffe9e7e1), C (0xff3a3a36),                   // text buttons
        };

        static const Palette retro = {
            true,
            C (0xff3a3a41), C (0xff202024),      // window
            C (0xff4c4c55), C (0xff393940),      // panel
            C (0xff1b1b1f), C (0x2effffff),      // outline, bevel
            C (0xffe6b054), C (0xffd8d8dc),      // title, text
            C (0xffffa028), C (0xffe0402a), C (0xff3c1410), C (0xff2b2b31),
            C (0xff74747e), C (0xff313136), C (0xff141416), C (0xffffc46a), C (0xff5c5c64),
            C (0xff0e120a), C (0xff8ce88c),      // pitch cells (LCD)
            C (0xff0e120a), C (0xff8ce88c),      // combos (LCD)
            KnobMetalArc, true,
            C (0xffd8d4c8), C (0xff1b1815), C (0xff5a564d),   // keyboard
            C (0xff50505a), C (0xffd8d8dc),                   // text buttons
            true,                                             // corner screws
        };

        // Cream-plastic hardware look modelled on the reference unit: warm
        // beige body, machined-aluminium knobs, olive/tan LCD displays.
        static const Palette studio = {
            false,
            C (0xffd3cfc4), C (0xffc6c2b6),      // window (subtle vertical gradient)
            C (0xffcecabf), C (0xffc5c1b5),      // panel
            C (0xff86837b), C (0x88ffffff),      // outline, bevel highlight
            C (0xff585650), C (0xff33322e),      // title, text
            C (0xffef8a1c), C (0xffc0472e), C (0xff6a2a22), C (0xffbfbbb0),
            C (0xfff2f1ec), C (0xffb8b6ae), C (0xff5d5b55), C (0xff2c2b28), C (0xff9a978e),
            C (0xffe6e3da), C (0xff33322e),      // pitch cells
            C (0xffb9ba94), C (0xff2c2e1c),      // combos (olive LCD)
            KnobPlastic, true,
            C (0xffd8d4c8), C (0xff1b1815), C (0xff5a564d),   // keyboard
            C (0xff50505a), C (0xff3a3a36),                   // text buttons
        };

        // "Bad Packets": warm near-black charcoal plastic with amber accents and
        // recessed knobs — matches the Claude Design skin.
        static const Palette bad = {
            true,
            C (0xff232120), C (0xff141210),      // window (warm black gradient)
            C (0xff211f1c), C (0xff191715),      // panel
            C (0xff2f2c28), C (0x18ffffff),      // outline, bevel highlight
            C (0xffe8912a), C (0xff8f8a83),      // title (amber), label text
            C (0xffef9327), C (0xffe0662e), C (0xff4a1a12), C (0xff262320),
            C (0xff34302c), C (0xff211e1b), C (0xff0d0c0b), C (0xfff2a233), C (0xff3a3632),
            C (0xffd8d4c8), C (0xff23201c),      // pitch cells (cream)
            C (0xff17150f), C (0xffe8912a),      // combos (dark, amber text)
            KnobRecessed, true,
            // aged-brass naturals against near-black sharps, so the keyboard reads
            // as part of the dark chassis instead of a white strip across it
            C (0xffeae1cd), C (0xff17140f), C (0xff2b251c),   // keyboard (ivory)
            // dark warm charcoal so the near-white legends read clearly
            C (0xff2b2724), C (0xfff0ece4),                   // text buttons
            true,                                             // corner screws
        };

        // "Neon Slate": cool slate-blue chassis, flat soft-cornered panels and a neon-green
        // accent, with value shown as a ring around each knob rather than a
        // collar of notches. Screen-native rather than moulded plastic.
        static const Palette neon = {
            true,
            C (0xff0f1219), C (0xff090b10),      // window (slate gradient)
            C (0xff161b26), C (0xff10141d),      // panel
            C (0xff262c3b), C (0x0dffffff),      // outline, bevel highlight
            C (0xff7a86a6), C (0xffaeb9d2),      // panel titles, label text
            // orange = every lit accent (LEDs, tabs, ACC/SLIDE); red = the GATE
            // row, a shade deeper so held-note bars still read apart from it
            C (0xff39ff6a), C (0xff2ae55c), C (0xff232a3b), C (0xff171c28),
            C (0xff333a4d), C (0xff0e1118), C (0xff10141d), C (0xff39ff6a), C (0xff222939),
            C (0xff141926), C (0xffd9e1f2),      // pitch cells
            C (0xff1c2230), C (0xffe2e8f6),      // combos
            KnobArcRing, false,
            C (0xffc9d2e4), C (0xff0b0e15), C (0xff525d7b),   // keyboard
            C (0xff1c2230), C (0xffaeb9d2),                   // text buttons
            false, 11.0f, true, true, C (0xff2e3547),         // flat, mono, soft corners
        };

        static const Palette* const base[numSkins] = { &classic, &retro, &studio, &bad, &neon };
        const int idx = juce::jlimit (0, numSkins - 1, skinIndex);

        if (keyHueTurn == 0.0f)
            return *base[idx];

        // The tinted copies are rebuilt only when the fade actually moves, so a
        // recoloured skin costs a paint exactly what an untinted one does. Only
        // the message thread ever gets here — everything that reads a palette is
        // painting or laying out — so a plain static needs no guarding.
        static Palette tinted[numSkins];
        static float builtFor = -1.0f;

        if (builtFor != keyHueTurn)
        {
            for (int i = 0; i < numSkins; ++i)
            {
                tinted[i] = *base[i];

                // Only the colours that carry the skin's key: the lit cells and
                // LEDs, the knob pointer and its value ring, the titles and the
                // little displays. The chassis stays the skin's own, so a
                // recoloured Bad Packets is still Bad Packets. Rotating hue
                // keeps each one's saturation and brightness, which is what lets
                // one dial work for five very different skins — and it does
                // nothing at all to the greys, so the skins whose titles are
                // grey simply don't move.
                for (auto* c : { &tinted[i].orange, &tinted[i].red, &tinted[i].pointer,
                                 &tinted[i].tickArc, &tinted[i].ledOff, &tinted[i].title,
                                 &tinted[i].lcdText, &tinted[i].lcdBg })
                    *c = c->withRotatedHue (keyHueTurn);
            }
            builtFor = keyHueTurn;
        }

        return tinted[idx];
    }

    int  keyHue() { return keyHueStop; }

    void setKeyHue (int stop, bool snap)
    {
        keyHueStop = ((stop % hueStops) + hueStops) % hueStops;   // wraps both ways
        if (snap)
            keyHueTurn = (float) keyHueStop / (float) hueStops;
    }

    bool advanceKeyHueFade()
    {
        const float target = (float) keyHueStop / (float) hueStops;

        // Ease out along the short way round the wheel, so green-to-pink sweeps
        // through blue rather than trudging back through orange.
        float diff = target - keyHueTurn;
        diff -= std::round (diff);

        if (std::abs (diff) < 0.0015f)
        {
            keyHueTurn = target;   // land exactly, so stop 0 is the true palette
            return false;
        }

        keyHueTurn += diff * 0.28f;
        keyHueTurn -= std::floor (keyHueTurn);
        return true;
    }

    juce::Rectangle<int> skinMenuHotspot()
    {
        // The "BADPACKETS  BP-303" legend in the top-left of the header panel:
        // the strip above the synth row and left of the TUNING knob's column.
        // BP303_SkinTest checks nothing has been laid out into it.
        return { 8, 8, 150, 16 };
    }

    const char* skinName (int skinIndex)
    {
        switch (skinIndex)
        {
            case 1:  return "Retro 90s";
            case 2:  return "Studio 90s";
            case 3:  return "Bad Packets";
            case 4:  return "Neon Slate";
            default: return "Classic";
        }
    }

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> r,
                    const juce::String& title, const Palette& p)
    {
        auto rf = r.toFloat();
        if (! p.retro && p.beveledPanels)
        {
            // studio: raised cream-plastic panel with a soft bevel
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillRoundedRectangle (rf.translated (0.0f, 1.6f), 7.0f);
            g.setGradientFill (juce::ColourGradient (p.panel1, 0.0f, rf.getY(),
                                                     p.panel2, 0.0f, rf.getBottom(), false));
            g.fillRoundedRectangle (rf, 7.0f);
            g.setColour (p.bevelHi);   // top highlight
            g.drawLine (rf.getX() + 6.0f, rf.getY() + 1.3f,
                        rf.getRight() - 6.0f, rf.getY() + 1.3f, 1.3f);
            g.setColour (juce::Colours::black.withAlpha (0.10f));  // bottom shade
            g.drawLine (rf.getX() + 6.0f, rf.getBottom() - 1.0f,
                        rf.getRight() - 6.0f, rf.getBottom() - 1.0f, 1.0f);
            g.setColour (p.outline);
            g.drawRoundedRectangle (rf, 7.0f, 1.2f);

            g.setColour (p.title);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (title, r.reduced (10, 4), juce::Justification::topLeft);
            return;
        }
        if (p.retro)
        {
            const float rad = p.panelCorner;
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (rf.translated (0.0f, 2.0f), rad);
            g.setGradientFill (juce::ColourGradient (p.panel1, 0.0f, rf.getY(),
                                                     p.panel2, 0.0f, rf.getBottom(), false));
            g.fillRoundedRectangle (rf, rad);
            g.setColour (p.outline);
            g.drawRoundedRectangle (rf, rad, 1.2f);
            g.setColour (p.bevelHi);
            g.drawLine (rf.getX() + 5.0f, rf.getY() + 1.2f,
                        rf.getRight() - 5.0f, rf.getY() + 1.2f, 1.2f);

            auto screw = [&] (float cx, float cy) {
                juce::Rectangle<float> s (cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
                g.setGradientFill (juce::ColourGradient (
                    juce::Colour (0xff9a9aa2), s.getX(), s.getY(),
                    juce::Colour (0xff3a3a40), s.getRight(), s.getBottom(), true));
                g.fillEllipse (s);
                g.setColour (juce::Colour (0xff1a1a1e));
                g.drawEllipse (s, 0.8f);
                g.drawLine (cx - 2.2f, cy - 2.2f, cx + 2.2f, cy + 2.2f, 1.0f);
            };
            if (p.panelScrews)
            {
                screw (rf.getX() + 10.0f, rf.getY() + 10.0f);
                screw (rf.getRight() - 10.0f, rf.getY() + 10.0f);
                screw (rf.getX() + 10.0f, rf.getBottom() - 10.0f);
                screw (rf.getRight() - 10.0f, rf.getBottom() - 10.0f);
            }
        }
        else
        {
            g.setColour (p.panel1);
            g.fillRoundedRectangle (rf, p.panelCorner);
            g.setColour (p.outline);
            g.drawRoundedRectangle (rf, p.panelCorner, 1.2f);
        }
        g.setColour (p.title);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, r.reduced (10, 4).withTrimmedLeft (p.panelScrews ? 14 : 0),
                    juce::Justification::topLeft);
    }
}

//==============================================================================
// Look & feel

void Look303::setSkin (int skinIndex)
{
    skin = juce::jlimit (0, ui303::numSkins - 1, skinIndex);
    const auto& p = ui303::palette (skin);
    const bool retro = p.retro;

    setColour (juce::ComboBox::backgroundColourId, p.lcdBg);
    setColour (juce::ComboBox::textColourId, p.lcdText);
    setColour (juce::ComboBox::outlineColourId, p.outline);
    setColour (juce::ComboBox::arrowColourId, p.lcdText);
    setColour (juce::PopupMenu::backgroundColourId,
               retro ? juce::Colour (0xff181c14) : juce::Colour (0xffe9e7e1));
    setColour (juce::PopupMenu::textColourId, retro ? p.lcdText : p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.orange);
    setColour (juce::PopupMenu::highlightedTextColourId,
               retro ? juce::Colours::black : p.text);
    setColour (juce::Label::textColourId, p.text);
    setColour (juce::Slider::textBoxTextColourId, retro ? p.lcdText : p.text);
    setColour (juce::Slider::textBoxBackgroundColourId,
               retro ? p.lcdBg : juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, p.outline);
    setColour (juce::TextButton::buttonColourId, p.buttonFace);
    setColour (juce::TextButton::textColourOffId, p.buttonText);
    // A latching text button reads lit in the accent, like an active FX tab, with
    // dark text for contrast against it. Skin 0 draws with these; the skinned
    // path below picks the same colours up itself.
    setColour (juce::TextButton::buttonOnColourId, p.orange);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff23201c));
}

namespace
{
    // How far into the extreme end of the travel a position sits, 0 anywhere
    // else. Which end that is comes from the knob itself — see ui303::HotEnd.
    float hotAmount (float t, int hot)
    {
        constexpr float zone = 0.28f;   // the last quarter or so of the travel
        if (hot > 0) return t <= 1.0f - zone ? 0.0f : (t - (1.0f - zone)) / zone;
        if (hot < 0) return t >= zone ? 0.0f : (zone - t) / zone;
        return 0.0f;
    }

    // The skin's accent, taken apart once per knob rather than per tick.
    // juce::Colour stores RGB and converts on every getHue/withMultiplied* call,
    // so building the tint by chaining those cost five round trips per colour —
    // times eleven ticks, times every knob, times every frame.
    struct Accent { float h, s, v; };

    // The accent, coloured by where along the travel it sits.
    //
    // Derived from the palette's own accent rather than named outright, so it
    // rides the key-hue dial instead of fighting it: rotate the dial and the
    // ramp rotates with it. Saturation and brightness carry the ordinary
    // travel, which keeps each skin's accent recognisably its own colour for
    // most of the sweep.
    juce::Colour valueTint (const Accent& a, float t, int hot)
    {
        float h = a.h;
        float s = a.s * (0.70f + 0.30f * t);
        float v = a.v * (0.78f + 0.22f * t);

        // Only the knobs that actually have an extreme end get the warning, and
        // it is a rotation of the accent toward red rather than a blend toward
        // Palette::red — that field is each skin's *second* accent, not a
        // warning colour, and on Neon Slate it sits one degree from the first,
        // so blending toward it there does nothing at all. An absolute red
        // would read, but it would be the one colour on the panel the key-hue
        // dial could not move.
        if (const float amount = hotAmount (t, hot); amount > 0.0f)
        {
            const float target = h > 0.5f ? 1.0f : 0.0f;   // red, the short way
            const float rot = juce::jmin (std::abs (target - h), 0.16f) * amount;
            h += target > h ? rot : -rot;
            if (h < 0.0f)  h += 1.0f;
            if (h >= 1.0f) h -= 1.0f;
            s *= 1.0f + 0.22f * amount;
            v *= 1.0f + 0.12f * amount;
        }

        return juce::Colour::fromHSV (h, juce::jmin (1.0f, s), juce::jmin (1.0f, v), 1.0f);
    }

    // A knob's cap catches the light differently as it turns. Physically a
    // smooth dome would not, but these caps read as turned or brushed metal and
    // the grain goes round with them. Only a fraction of the rotation is used:
    // at full travel the highlight would swing right under the pointer and the
    // knob would look like it was spinning rather than being turned.
    constexpr float highlightFollow = 0.35f;
}

void Look303::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                float sliderPos, float startAngle, float endAngle,
                                juce::Slider& slider)
{
    const auto& p = ui303::palette (skin);
    const int hot = (int) slider.getProperties().getWithDefault ("hotEnd", 0);
    const Accent accent { p.orange.getHue(), p.orange.getSaturation(),
                          p.orange.getBrightness() };

    // --- where the XY pad has taken this knob --------------------------------
    // The knob turns to what is actually being heard: the pointer, the LED
    // collar, the value ring and the cap's highlight all read the modulated
    // position, because a control being played should look like it is being
    // played. Taken here, before anything derives an angle from it, so every
    // skin follows without knowing the pad exists.
    //
    // The pad only offsets the parameter, so the setting underneath survives —
    // it is kept visible as a dim tick out in the margin at the position the
    // user left it, with a thin arc from there to where the pad has pushed it.
    // Without that the knob would look like it had been turned, and on release
    // it would appear to jump back on its own.
    const float padOffset = (float) slider.getProperties().getWithDefault ("padOffset", 0.0f);
    const float basePos   = sliderPos;
    const bool  padMoved  = std::abs (padOffset) > 1.0e-3f;

    if (padMoved)
        sliderPos = juce::jlimit (0.0f, 1.0f, sliderPos + padOffset);

    // Where the value readout grows from. A bipolar control — a pan — reads out
    // from its centre detent rather than from the bottom of its travel: filling
    // from the left edge would show a pan sitting dead centre as a ring half
    // full of something, which is the one thing it is not.
    const bool  bipolar   = (bool) slider.getProperties().getWithDefault ("bipolar", false);
    const float originPos = bipolar ? 0.5f : 0.0f;
    const float originAngle = startAngle + originPos * (endAngle - startAngle);
    // distance from the origin, 0..1 — what the colour ramp follows, so hard
    // left and hard right read as equally far out rather than one reading as low
    const float valuePos = bipolar ? std::abs (sliderPos - 0.5f) * 2.0f : sliderPos;
    // how far the cap's lighting has drifted round with the knob
    const float lightAngle = (startAngle + sliderPos * (endAngle - startAngle))
                                 * highlightFollow;
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h)
                      .reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float body = radius * 0.74f;
    juce::Rectangle<float> bodyRect (centre.x - body, centre.y - body, body * 2, body * 2);

    // The marker for where the user left it, and the run from there to where the
    // pad has taken it. Both sit in the 3px the bounds were reduced by, outside
    // the notch collar and outside KnobArcRing's value ring, so they are drawn
    // once here instead of once inside each per-skin branch below.
    if (padMoved)
    {
        const float baseAngle = startAngle + basePos * (endAngle - startAngle);
        const float ringR     = radius + 1.5f;

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                           juce::jmin (baseAngle, angle), juce::jmax (baseAngle, angle), true);
        g.setColour (p.orange.withAlpha (0.55f));
        g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::butt));

        const float bs = std::sin (baseAngle), bc = std::cos (baseAngle);
        g.setColour (p.text.withAlpha (0.8f));
        g.drawLine (centre.x + bs * (radius - 0.5f), centre.y - bc * (radius - 0.5f),
                    centre.x + bs * (radius + 2.8f), centre.y - bc * (radius + 2.8f), 1.8f);
    }

    // Where the cap's highlight sits, swung part of the way round with the knob.
    auto litFrom = [&] (float ox, float oy)
    {
        const float s = std::sin (lightAngle), c = std::cos (lightAngle);
        return juce::Point<float> (centre.x + ox * c - oy * s,
                                   centre.y + ox * s + oy * c);
    };

    // Level notches around the knob: lit amber up to the current value, dim
    // above it — a small LED-collar level indicator on every skin. Drawn on
    // top of the knob body so recessed wells / collars don't cover them.
    auto drawNotches = [&]
    {
        const int nTicks = 11;
        const float ro = radius - 0.5f;
        const float ri = radius - juce::jmax (2.5f, radius * 0.17f);
        for (int i = 0; i < nTicks; ++i)
        {
            const float tt = (float) i / (float) (nTicks - 1);
            const float a  = startAngle + tt * (endAngle - startAngle);
            // Bipolar lights the span between the centre and the pointer, either
            // side of it; unipolar lights everything up to the pointer.
            //
            // The pointer end is rounded outward by half a tick. Eleven ticks is
            // only five a side once the collar reads from the centre, and without
            // this a pan of -0.15 lit exactly the one tick a centred pan does —
            // the two knobs were indistinguishable except by pointer angle. Half
            // a tick of slack means any pan worth calling one shows as off
            // centre, and dead centre still shows as the single centre tick.
            const float slack = 0.5f / (float) (nTicks - 1);
            const bool  lit = bipolar
                ? (tt >= std::min (originPos, sliderPos - slack) - 1.0e-3f
                   && tt <= std::max (originPos, sliderPos + slack) + 1.0e-3f)
                : a <= angle + 1.0e-3f;
            const float s = std::sin (a), c = std::cos (a);
            // Each tick is tinted for its own place on the dial, not for where
            // the knob currently sits, so the collar is a ramp that turning
            // uncovers rather than a block of colour that changes wholesale.
            //
            // Ticks in the extreme zone lengthen and thicken as well as shift
            // colour. On a mark this small the hue change alone is almost
            // invisible, and it would carry nothing at all to someone who reads
            // the two hues as the same — the size change does the work.
            const float tint  = bipolar ? std::abs (tt - 0.5f) * 2.0f : tt;
            const float warn  = lit ? hotAmount (tt, hot) : 0.0f;
            const float inner = ri - warn * (ro - ri) * 0.55f;
            g.setColour (lit ? valueTint (accent, tint, hot) : p.tickArc.withAlpha (0.7f));
            g.drawLine (centre.x + s * inner, centre.y - c * inner,
                        centre.x + s * ro, centre.y - c * ro,
                        lit ? 1.7f + warn * 1.4f : 1.2f);
        }
    };

    if (p.knobStyle == ui303::KnobArcRing)
    {
        // --- flat cap inside a value ring: the ring replaces the notch collar ---
        const float thick = juce::jmax (2.5f, radius * 0.17f);
        const float ringR = radius - thick * 0.5f;
        const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::butt);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (p.tickArc);
        g.strokePath (track, stroke);

        // The lit span runs from the origin to the pointer, which for a bipolar
        // control means it can run anticlockwise. Ordered here so the arcs below
        // are always drawn low angle to high.
        const float lo = juce::jmin (originAngle, angle);
        const float hi = juce::jmax (originAngle, angle);

        if (hi > lo + 1.0e-3f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, lo, hi, true);
            g.setColour (valueTint (accent, valuePos, hot).withAlpha (0.28f));
            g.strokePath (value, juce::PathStrokeType (thick + 3.0f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::butt));

            // A ColourGradient is linear or radial and never angular, so the
            // ramp round the ring is laid down as a short run of arcs. They
            // overlap by a hair; drawn exactly end to end the joins read as
            // notches in the ring.
            //
            // The ramp always runs outward from the origin, so on a bipolar
            // control it is mirrored when the pointer sits left of centre.
            constexpr int segments = 12;
            const float span = hi - lo;
            const bool  reversed = angle < originAngle;
            for (int i = 0; i < segments; ++i)
            {
                const float a0 = lo + span * (float) i / (float) segments;
                const float a1 = juce::jmin (hi, lo + span * (float) (i + 1)
                                                     / (float) segments + 0.015f);
                juce::Path seg;
                seg.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, a0, a1, true);

                const float along = ((float) i + 0.5f) / (float) segments;
                g.setColour (valueTint (accent,
                                        valuePos * (reversed ? 1.0f - along : along),
                                        hot));
                g.strokePath (seg, stroke);
            }
        }

        const float capR = juce::jmax (4.0f, radius - thick - 2.5f);
        juce::Rectangle<float> capRect (centre.x - capR, centre.y - capR, capR * 2, capR * 2);
        const auto capLit = litFrom (-capR * 0.32f, -capR * 0.44f);
        juce::ColourGradient cap (p.knobFace1, capLit.x, capLit.y,
                                  p.knobFace2,
                                  capLit.x + capR * 1.5f, capLit.y + capR * 1.5f, true);
        g.setGradientFill (cap);
        g.fillEllipse (capRect);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawEllipse (capRect, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawEllipse (capRect.reduced (0.9f).translated (0.0f, 0.6f), 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.6f, -capR + 3.0f, 3.2f, capR * 0.58f, 1.6f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                    .translated (centre.x, centre.y));
        g.setColour (p.pointer);
        g.fillPath (pointer);
        return;   // the ring is the level readout, so no notch collar
    }

    if (p.knobStyle == ui303::KnobFlat)
    {
        g.setColour (p.knobFace1);
        g.fillEllipse (bodyRect);
        g.setColour (p.knobEdge);
        g.drawEllipse (bodyRect, 1.6f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.8f, -body + 2.0f, 3.6f, body * 0.62f, 1.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                    .translated (centre.x, centre.y));
        g.setColour (p.pointer);
        g.fillPath (pointer);
        drawNotches();
        return;
    }

    if (p.knobStyle == ui303::KnobPlastic)
    {
        // --- machined-aluminium hardware knob ---
        const float collarR = radius * 0.94f;
        const float capR    = radius * 0.70f;
        juce::Rectangle<float> collarRect (centre.x - collarR, centre.y - collarR,
                                           collarR * 2, collarR * 2);
        juce::Rectangle<float> capRect (centre.x - capR, centre.y - capR, capR * 2, capR * 2);

        // drop shadow
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillEllipse (collarRect.translated (0.0f, radius * 0.12f));

        // machined collar / skirt: diagonal metal gradient
        juce::ColourGradient collar (juce::Colour (0xffa8a69d),
                                     collarRect.getX(), collarRect.getY(),
                                     juce::Colour (0xff504e49),
                                     collarRect.getRight(), collarRect.getBottom(), false);
        g.setGradientFill (collar);
        g.fillEllipse (collarRect);
        g.setColour (juce::Colour (0xff3f3d39));
        g.drawEllipse (collarRect, 1.0f);

        // domed cap: radial gradient, highlight upper-left and drifting with the knob
        const auto capLit = litFrom (-capR * 0.32f, -capR * 0.38f);
        juce::ColourGradient cap (p.knobFace1, capLit.x, capLit.y,
                                  p.knobFace2, capLit.x + capR, capLit.y, true);
        cap.addColour (0.72, p.knobFace1.interpolatedWith (p.knobFace2, 0.5f));
        g.setGradientFill (cap);
        g.fillEllipse (capRect);
        g.setColour (p.knobEdge);
        g.drawEllipse (capRect, 1.1f);

        // concentric turned-metal highlight ring
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawEllipse (capRect.reduced (capR * 0.16f), 1.0f);

        // specular highlight arc, upper-left, travelling with the cap
        juce::Path spec;
        spec.addCentredArc (centre.x, centre.y, capR * 0.80f, capR * 0.80f,
                            0.0f, -2.5f + lightAngle, -1.0f + lightAngle, true);
        g.setColour (juce::Colours::white.withAlpha (0.42f));
        g.strokePath (spec, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // engraved pointer: dark notch with a light edge for depth
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -capR + 3.0f, 3.0f, capR * 0.66f, 1.4f);
        auto rot = juce::AffineTransform::rotation (angle).translated (centre.x, centre.y);
        pointer.applyTransform (rot);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillPath (pointer, juce::AffineTransform::translation (0.8f, 0.8f));
        g.setColour (p.pointer);
        g.fillPath (pointer);
        drawNotches();
        return;
    }

    if (p.knobStyle == ui303::KnobRecessed)
    {
        // --- dark plastic knob recessed into a concave well, amber pointer ---
        const float wellR = radius;
        const float capR  = radius * 0.74f;
        juce::Rectangle<float> wellRect (centre.x - wellR, centre.y - wellR, wellR * 2, wellR * 2);
        juce::Rectangle<float> capRect (centre.x - capR, centre.y - capR, capR * 2, capR * 2);

        // concave well: dark at the top rim (in shadow), lighter lip at bottom
        juce::ColourGradient well (juce::Colour (0xff100f0d), 0.0f, wellRect.getY(),
                                   juce::Colour (0xff35322d), 0.0f, wellRect.getBottom(), false);
        g.setGradientFill (well);
        g.fillEllipse (wellRect);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (wellRect.reduced (0.6f), 1.2f);

        // knob cap: charcoal dome, slightly lit from upper-left
        const auto capLit = litFrom (-capR * 0.3f, -capR * 0.4f);
        juce::ColourGradient cap (p.knobFace1, capLit.x, capLit.y,
                                  p.knobFace2, capLit.x + capR, capLit.y, true);
        g.setGradientFill (cap);
        g.fillEllipse (capRect);
        g.setColour (p.knobEdge);
        g.drawEllipse (capRect, 1.2f);
        // top highlight sliver
        juce::Path capSpec;
        capSpec.addCentredArc (centre.x, centre.y, capR * 0.86f, capR * 0.86f,
                               0.0f, -2.4f + lightAngle, -0.9f + lightAngle, true);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.strokePath (capSpec, juce::PathStrokeType (1.6f));

        // amber pointer with a soft glow
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.7f, -capR + 3.0f, 3.4f, capR * 0.66f, 1.7f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                    .translated (centre.x, centre.y));
        g.setColour (p.orange.withAlpha (0.35f));
        g.strokePath (pointer, juce::PathStrokeType (3.0f));
        g.setColour (p.pointer);
        g.fillPath (pointer);
        drawNotches();
        return;
    }

    // --- retro: metallic knob (level shown by the shared notches) ---
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (bodyRect.translated (0.0f, 1.8f).expanded (0.4f));

    const auto faceLit = litFrom (-body * 0.5f, -body * 0.6f);
    juce::ColourGradient face (p.knobFace1, faceLit.x, faceLit.y,
                               p.knobFace2,
                               faceLit.x + body * 1.1f, faceLit.y + body * 1.5f, true);
    g.setGradientFill (face);
    g.fillEllipse (bodyRect);
    g.setColour (p.knobEdge);
    g.drawEllipse (bodyRect, 1.8f);

    // specular highlight, upper-left, travelling with the face
    juce::Path spec;
    spec.addCentredArc (centre.x, centre.y, body - 2.5f, body - 2.5f,
                        0.0f, -2.4f + lightAngle, -1.1f + lightAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.strokePath (spec, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.6f, -body + 3.0f, 3.2f, body * 0.55f, 1.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                .translated (centre.x, centre.y));
    g.setColour (p.pointer);
    g.fillPath (pointer);
    drawNotches();
}

void Look303::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                bool highlighted, bool)
{
    const auto& p = ui303::palette (skin);
    auto r = b.getLocalBounds();
    auto led = r.removeFromLeft (r.getHeight()).toFloat().reduced (4.0f);

    if (p.flatControls)
    {
        // round indicator lamp with a soft halo, no moulded bezel
        if (b.getToggleState())
        {
            g.setColour (p.orange.withAlpha (0.30f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (b.getToggleState() ? p.orange : p.ledOff);
        g.fillEllipse (led);
        g.setColour (p.buttonEdge.withAlpha (highlighted ? 0.9f : 0.6f));
        g.drawEllipse (led, 1.0f);
    }
    else
    {
        if (p.retro && b.getToggleState())
        {
            g.setColour (p.orange.withAlpha (0.35f));
            g.fillRoundedRectangle (led.expanded (3.0f), 5.0f);
        }
        g.setColour (b.getToggleState() ? p.orange : p.ledOff);
        g.fillRoundedRectangle (led, 3.0f);
        g.setColour ((p.retro ? juce::Colours::black : p.text)
                         .withAlpha (highlighted ? 0.9f : 0.6f));
        g.drawRoundedRectangle (led, 3.0f, 1.2f);
    }

    g.setColour (p.text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (2), juce::Justification::centredLeft);
}

void Look303::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                    const juce::Colour& backgroundColour,
                                    bool highlighted, bool down)
{
    if (skin == 0)
    {
        LookAndFeel_V4::drawButtonBackground (g, b, backgroundColour, highlighted, down);
        return;
    }

    const auto& pal = ui303::palette (skin);
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    // This path draws from the palette rather than the colour it is handed, so a
    // latched button has to be lit here too or it would look identical to an
    // unlatched one on every skin but the first.
    auto base = b.getToggleState() ? pal.orange : pal.buttonFace;
    if (highlighted)
        base = base.brighter (0.12f);

    if (pal.flatControls)
    {
        g.setColour (down ? base.brighter (0.22f) : base);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (pal.buttonEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        return;
    }

    juce::ColourGradient grad (down ? base.darker (0.3f) : base.brighter (0.18f),
                               0.0f, r.getY(),
                               down ? base.brighter (0.05f) : base.darker (0.28f),
                               0.0f, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (pal.buttonEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawLine (r.getX() + 3.0f, r.getY() + 1.2f, r.getRight() - 3.0f, r.getY() + 1.2f, 1.0f);
    }
}

void Look303::drawComboBox (juce::Graphics& g, int width, int height, bool,
                            int, int, int, int, juce::ComboBox&)
{
    const auto& p = ui303::palette (skin);
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);

    if (p.flatControls)
    {
        g.setColour (p.lcdBg);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (p.buttonEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
    else if (! p.retro)
    {
        g.setColour (p.lcdBg);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (p.outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    else
    {
        // inset LCD window
        g.setColour (p.lcdBg);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 3.0f, 1.4f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawLine (r.getX() + 2.0f, r.getBottom() - 0.8f,
                    r.getRight() - 2.0f, r.getBottom() - 0.8f, 1.0f);
    }

    juce::Path tri;
    const float ax = r.getRight() - 14.0f, ay = r.getCentreY() - 2.0f;
    tri.addTriangle (ax, ay, ax + 8.0f, ay, ax + 4.0f, ay + 5.0f);
    g.setColour (p.lcdText.withAlpha (0.9f));
    g.fillPath (tri);
}

juce::Font Look303::getComboBoxFont (juce::ComboBox&)
{
    if (skin == 1 || ui303::palette (skin).monoDisplay)
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              13.0f, juce::Font::bold));
    return juce::Font (juce::FontOptions (14.0f));
}

//==============================================================================
// SegmentedSwitch

SegmentedSwitch::SegmentedSwitch (BP303AudioProcessor& p,
                                  juce::AudioProcessorValueTreeState& state,
                                  const juce::String& paramID)
    : proc (p)
{
    param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID));
    if (param != nullptr)
    {
        for (const auto& c : param->choices)
            labels.add (c);
        attachment = std::make_unique<juce::ParameterAttachment> (
            *param, [this] (float v) { index = (int) std::round (v); repaint(); });
        attachment->sendInitialUpdate();
    }
}

void SegmentedSwitch::paint (juce::Graphics& g)
{
    const auto& p = ui303::palette (proc.uiSkin.load());
    const int n = labels.size();
    if (n < 1)
        return;

    auto rf = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = 4.0f;

    // inset track
    g.setColour (p.cellOff);
    g.fillRoundedRectangle (rf, rad);

    const float segW = rf.getWidth() / (float) n;
    g.setFont (juce::FontOptions (juce::jlimit (8.0f, 11.0f, segW * 0.42f),
                                  juce::Font::bold));

    for (int i = 0; i < n; ++i)
    {
        auto seg = rf.withX (rf.getX() + (float) i * segW).withWidth (segW);
        const bool active = i == index;

        if (active)
        {
            if (p.retro)
            {
                g.setColour (p.orange.withAlpha (0.3f));
                g.fillRoundedRectangle (seg.reduced (0.5f), rad);
            }
            g.setColour (p.orange);
            g.fillRoundedRectangle (seg.reduced (1.5f), rad - 1.0f);
        }
        else if (i > 0)
        {
            g.setColour (p.outline.withAlpha (0.6f));
            g.drawVerticalLine ((int) (rf.getX() + (float) i * segW),
                                rf.getY() + 2.0f, rf.getBottom() - 2.0f);
        }

        g.setColour (active ? juce::Colour (0xff23201c) : p.text.withAlpha (0.85f));
        g.drawText (labels[i], seg, juce::Justification::centred);
    }

    g.setColour (p.outline);
    g.drawRoundedRectangle (rf, rad, 1.0f);
}

void SegmentedSwitch::mouseDown (const juce::MouseEvent& e)
{
    const int n = labels.size();
    if (n < 1 || attachment == nullptr)
        return;
    const int seg = juce::jlimit (0, n - 1, e.x * n / juce::jmax (1, getWidth()));
    attachment->setValueAsCompleteGesture ((float) seg);
}

//==============================================================================
// FxSection

FxSection::FxSection (BP303AudioProcessor& p, juce::String titleIn,
                      juce::StringArray tabNames, juce::StringArray enableIds)
    : proc (p), title (std::move (titleIn)), tabs (std::move (tabNames)),
      enables (std::move (enableIds))
{
    jassert (enables.isEmpty() || enables.size() == tabs.size());

    for (const auto& id : enables)
        if (auto* param = proc.apvts.getParameter (id))
            enableWatchers.push_back (std::make_unique<juce::ParameterAttachment> (
                *param, [this] (float) { repaint (tabBarArea()); }));
}

bool FxSection::tabEnabled (int i) const
{
    if (! juce::isPositiveAndBelow (i, enables.size()))
        return false;

    const auto* value = proc.apvts.getRawParameterValue (enables[i]);
    return value != nullptr && value->load() >= 0.5f;
}

void FxSection::addPage (juce::Component& page)
{
    pages.push_back (&page);
    addAndMakeVisible (page);
    page.setVisible ((int) pages.size() - 1 == current);
}

juce::Rectangle<int> FxSection::tabBarArea() const
{
    return getLocalBounds().reduced (8, 0).withTrimmedTop (titleH).withHeight (tabH);
}

float FxSection::tabSegmentWidth() const
{
    const int n = juce::jmax (1, tabs.size());
    return juce::jmin ((float) maxTabW,
                       (float) tabBarArea().getWidth() / (float) n);
}

juce::Rectangle<int> FxSection::contentArea() const
{
    return getLocalBounds().withTrimmedTop (titleH + tabH + 2).withTrimmedBottom (4);
}

juce::Rectangle<int> FxSection::tabBarFreeArea() const
{
    const int used = juce::roundToInt (tabSegmentWidth() * (float) tabs.size());
    return tabBarArea().withTrimmedLeft (used);
}

void FxSection::showTab (int i)
{
    // Tabs and pages are one-to-one on every FX panel, but not on the pad, whose
    // tabs pick a mode and whose content is the same pad either way. Clamp
    // against the tabs — they are what the bar draws and what onTabChanged
    // reports — and let the page index saturate, so one page stays showing
    // whichever tab is chosen.
    current = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), i);
    const int shown = juce::jmin (current, (int) pages.size() - 1);
    for (int k = 0; k < (int) pages.size(); ++k)
        pages[(size_t) k]->setVisible (k == shown);
    repaint();

    if (onTabChanged)
        onTabChanged (current);
}

void FxSection::resized()
{
    for (auto* page : pages)
        page->setBounds (contentArea());
}

void FxSection::paint (juce::Graphics& g)
{
    const auto& p = ui303::palette (proc.uiSkin.load());
    ui303::drawPanel (g, getLocalBounds(), title, p);

    const int n = tabs.size();
    if (n < 1)
        return;

    auto bar = tabBarArea().toFloat();
    const float segW = tabSegmentWidth();
    g.setFont (juce::FontOptions (juce::jlimit (8.0f, 10.5f, segW * 0.30f),
                                  juce::Font::bold));

    for (int i = 0; i < n; ++i)
    {
        auto seg = bar.withX (bar.getX() + (float) i * segW).withWidth (segW).reduced (1.5f, 0.5f);
        const bool active = i == current;
        const bool lit    = tabEnabled (i);   // the effect itself is switched on

        if (active)
        {
            if (p.retro)
            {
                g.setColour (p.orange.withAlpha (0.30f));
                g.fillRoundedRectangle (seg.expanded (1.0f), 4.0f);
            }
            g.setColour (p.orange);
            g.fillRoundedRectangle (seg, 3.5f);
        }
        else
        {
            g.setColour (p.cellOff);
            g.fillRoundedRectangle (seg, 3.5f);

            // A switched-on effect on a tab you aren't looking at: wash the tab
            // in the accent so the whole bar can be read at a glance.
            if (lit)
            {
                g.setColour (p.orange.withAlpha (0.20f));
                g.fillRoundedRectangle (seg, 3.5f);
            }
        }
        g.setColour (lit && ! active ? p.orange.withAlpha (0.7f) : p.outline.withAlpha (0.7f));
        g.drawRoundedRectangle (seg, 3.5f, 0.8f);

        // The lamp is the definitive readout, and means the same thing on every
        // tab: present = on. Only its colour changes, for contrast against the
        // selected tab's accent fill.
        if (lit)
        {
            const float d = juce::jmin (4.5f, seg.getHeight() * 0.32f);
            auto lamp = juce::Rectangle<float> (d, d)
                            .withCentre ({ seg.getX() + 3.5f + d * 0.5f, seg.getCentreY() });

            if (active)
            {
                g.setColour (juce::Colour (0xff23201c));
            }
            else
            {
                if (p.retro)
                {
                    g.setColour (p.orange.withAlpha (0.45f));
                    g.fillEllipse (lamp.expanded (1.8f));
                }
                g.setColour (p.orange);
            }
            g.fillEllipse (lamp);
        }

        g.setColour (active ? juce::Colour (0xff23201c)
                            : p.text.withAlpha (lit ? 1.0f : 0.85f));
        g.drawText (tabs[i], seg, juce::Justification::centred);
    }
}

void FxSection::mouseDown (const juce::MouseEvent& e)
{
    auto bar = tabBarArea();
    if (! bar.contains (e.getPosition()))
        return;
    const int n = juce::jmax (1, tabs.size());
    const int i = (int) ((float) (e.x - bar.getX()) / tabSegmentWidth());
    if (juce::isPositiveAndBelow (i, n))
        showTab (i);
}

//==============================================================================
// StepGrid

static juce::String pitchText (int key, int octave)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    const int combined = key + 12 * octave;
    const int idx = ((combined % 12) + 12) % 12;
    const int oct = 2 + (int) std::floor ((double) combined / 12.0);
    return juce::String (names[idx]) + juce::String (oct);
}

int StepGrid::columnAt (int xPos) const
{
    const int cellW = (getWidth() - labelW) / 16;
    if (xPos < labelW || cellW <= 0)
        return -1;
    return juce::jlimit (0, 15, (xPos - labelW) / cellW);
}

StepGrid::View StepGrid::liveView() const
{
    View v;
    v.playing = proc.sequencer.playingStep.load();
    v.length  = proc.sequencer.lengthOf (proc.sequencer.length.load());
    v.fit     = proc.sequencer.patternFit.load();
    v.cursor  = cursor;
    v.kbLow   = kbLow;

    // FNV-1a over the fields the grid draws. Sixteen steps of atomic loads is
    // nothing next to the repaint this decides whether to skip.
    juce::uint64 h = 1469598103934665603ull;
    const auto mix = [&h] (juce::uint64 value) { h = (h ^ value) * 1099511628211ull; };

    for (const auto& s : proc.sequencer.steps)
    {
        mix ((juce::uint64) s.key.load());
        mix ((juce::uint64) (s.octave.load() + 8));
        mix ((juce::uint64) s.hold.load());
        mix ((juce::uint64) s.ratchet.load());
        mix ((juce::uint64) ((s.gate.load()  ? 1 : 0)
                           | (s.slide.load() ? 4 : 0)));
        mix ((juce::uint64) (s.dyn.load() + 2));
    }
    v.pattern = h;
    return v;
}

juce::Rectangle<int> StepGrid::ledCellBounds (int col) const
{
    if (col < 0 || col > 15)
        return {};

    const int cellW = (getWidth() - labelW) / 16;
    return { labelW + col * cellW, 0, cellW, ledH };
}

// The bar at the pattern's end. It lives on the LED row rather than spanning the
// grid because the four rows below it are all editable cells — a full-height
// handle would sit on top of a gate, a pitch, an accent and a slide, and steal a
// click from whichever the pointer was actually aiming at.
juce::Rectangle<int> StepGrid::patternEndHandle (int len) const
{
    const int cellW = (getWidth() - labelW) / 16;
    const int x = juce::jlimit (labelW, getWidth() - handleW,
                                labelW + len * cellW - handleW / 2);
    return { x, 0, handleW, ledH };
}

void StepGrid::repaintLed (int col)
{
    if (const auto r = ledCellBounds (col); ! r.isEmpty())
        repaint (r);
}

void StepGrid::timerCallback()
{
    const auto now = liveView();
    if (now == shown)
        return;   // nothing on screen has moved; a repaint would redraw the same pixels

    if (now.sameApartFromPlayhead (shown))
    {
        // The step LED moved and nothing else did, so redraw the two cells that
        // changed rather than the whole grid and its keyboard.
        repaintLed (shown.playing);
        repaintLed (now.playing);
        return;
    }

    repaint();
}

void StepGrid::paint (juce::Graphics& g)
{
    // Recorded here rather than in the timer, so a repaint from anywhere else
    // leaves `shown` describing what is actually on screen.
    shown = liveView();

    const auto& p = ui303::palette (proc.uiSkin.load());
    const int cellW = (getWidth() - labelW) / 16;
    const int playing = proc.sequencer.playingStep.load();
    const int barLen = proc.sequencer.length.load();
    const int len = proc.sequencer.lengthOf (barLen);
    const bool fit = proc.sequencer.patternFit.load();

    // Which columns a held note (hold > 1) owns, computed the same greedy way the
    // sequencer plays them: a gated head covers the next hold-1 columns, and a
    // covered column never starts its own run.
    bool covered[16] = {};
    int  runLen[16]  = {};
    for (int c = 0, cover = 0; c < 16; ++c)
    {
        if (cover > 0)          { covered[c] = true; --cover; }
        else if (proc.sequencer.steps[c].gate.load())
        {
            const int h = juce::jlimit (1, 16 - c, proc.sequencer.steps[c].hold.load());
            runLen[c] = h;
            cover = h - 1;
        }
    }

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));

    struct RowInfo { const char* name; int y, h; };
    const RowInfo rows[] = {
        { "",      0,                             ledH },
        { "GATE",  ledH,                          gateH },
        { "PITCH", ledH + gateH,                  pitchH },
        { "ACC",   ledH + gateH + pitchH,         flagH },
        { "SLIDE", ledH + gateH + pitchH + flagH, flagH },
    };

    // A playhead move only invalidates one cell, so skip anything the clip has
    // already thrown away rather than issuing drawing calls that draw nothing.
    const auto clip = g.getClipBounds();

    if (clip.getX() < labelW)
        for (const auto& row : rows)
        {
            g.setColour (p.text);
            g.drawText (row.name, 0, row.y, labelW - 6, row.h, juce::Justification::centredRight);
        }

    for (int col = 0; col < 16; ++col)
    {
        const int cx = labelW + col * cellW;
        if (! clip.intersects (juce::Rectangle<int> (cx, 0, cellW, kbTop)))
            continue;

        auto& step = proc.sequencer.steps[col];
        const bool inPattern = col < len;
        const float dim = inPattern ? 1.0f : 0.35f;

        // write cursor: tint the whole step column so it's clear which step the
        // keyboard is aimed at
        if (col == cursor)
        {
            g.setColour (p.orange.withAlpha (0.14f));
            g.fillRect (cx, ledH, cellW, kbTop - ledH);
            g.setColour (p.orange.withAlpha (0.7f));
            g.drawRect (cx, ledH, cellW, kbTop - ledH, 1);
        }

        // playing LED
        {
            const float d = juce::jmin ((float) cellW * 0.4f, (float) ledH - 5.0f);
            auto led = juce::Rectangle<float> (d, d)
                           .withCentre ({ (float) cx + cellW * 0.5f, ledH * 0.5f });
            const bool lit = col == playing;
            if (lit && p.retro)
            {
                g.setColour (p.orange.withAlpha (0.4f * dim));
                g.fillEllipse (led.expanded (2.5f));
            }
            g.setColour ((lit ? p.orange : p.ledOff).withAlpha (dim));
            g.fillEllipse (led);
        }

        auto cell = [&] (int rowY, int rowH) {
            return juce::Rectangle<float> ((float) cx, (float) rowY,
                                           (float) cellW, (float) rowH).reduced (2.0f);
        };

        // strength is how lit the cell is: 0 leaves it dark, 1 is the full colour.
        // ACC uses a value in between for a soft step, so the row reads as one
        // dynamics axis - same hue throughout, only the strength moves.
        auto drawCell = [&] (juce::Rectangle<float> r, float strength, juce::Colour onColour)
        {
            if (strength > 0.0f && p.retro)
            {
                g.setColour (onColour.withAlpha (0.35f * strength * dim));
                g.fillRoundedRectangle (r.expanded (1.5f), 4.0f);
            }
            g.setColour (p.cellOff.interpolatedWith (onColour, strength).withAlpha (dim));
            g.fillRoundedRectangle (r, 3.0f);
            if (p.retro)
            {
                g.setColour (juce::Colours::black.withAlpha (0.4f * dim));
                g.drawRoundedRectangle (r, 3.0f, 0.8f);
            }
        };

        auto drawToggleCell = [&] (juce::Rectangle<float> r, bool on, juce::Colour onColour)
        {
            drawCell (r, on ? 1.0f : 0.0f, onColour);
        };

        // GATE row: single-step notes are a rounded cell; a held note (hold > 1)
        // draws as one continuous bar spanning the steps it owns.
        const bool gateOn = step.gate.load();
        if (covered[col] || (gateOn && runLen[col] > 1))
        {
            auto bar = juce::Rectangle<float> ((float) cx, (float) rows[1].y,
                                               (float) cellW, (float) rows[1].h).reduced (0.0f, 2.0f);
            if (p.retro)
            {
                g.setColour (p.red.withAlpha (0.35f * dim));
                g.fillRect (bar.expanded (0.0f, 1.5f));
            }
            g.setColour (p.red.withAlpha (dim));
            g.fillRect (bar);
        }
        else
        {
            drawToggleCell (cell (rows[1].y, rows[1].h), gateOn, p.red);
        }

        // A split gate is sliced into the number of times it fires, cut out of
        // the lit cell rather than drawn over it — the same mark the drum grid
        // uses for the same thing, because they are the same thing.
        if (const int count = proc.sequencer.ratchetAt (col); count > 1)
        {
            auto r = cell (rows[1].y, rows[1].h);
            g.setColour (p.cellOff.withAlpha (0.9f * dim));
            for (int cut = 1; cut < count; ++cut)
                g.fillRect (r.getX() + r.getWidth() * (float) cut / (float) count - 0.8f,
                            r.getY(), 1.6f, r.getHeight());
        }

        // pitch cell
        auto pr = cell (rows[2].y, rows[2].h);
        g.setColour (p.pitchBg.withAlpha (dim));
        g.fillRoundedRectangle (pr, 3.0f);
        g.setColour ((p.retro ? juce::Colours::black : p.outline).withAlpha (dim));
        g.drawRoundedRectangle (pr, 3.0f, 1.0f);
        g.setColour (p.pitchText.withAlpha (dim));
        g.setFont (p.retro
                       ? juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                            11.0f, juce::Font::bold)
                       : juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (pitchText (step.key.load(), step.octave.load()), pr,
                    juce::Justification::centred);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));

        const int dyn = dyn303::clampDyn (step.dyn.load());
        drawCell (cell (rows[3].y, rows[3].h),
                  dyn > 0 ? 1.0f : dyn < 0 ? ui303::softCellStrength : 0.0f, p.orange);
        drawToggleCell (cell (rows[4].y, rows[4].h), step.slide.load(), p.orange);

        if (col % 4 == 0)
        {
            g.setColour (p.text.withAlpha (0.25f));
            g.drawVerticalLine (cx, 0.0f, (float) kbTop);
        }
    }

    // A fitted line's steps don't sit on the columns they are drawn in, so its
    // real step boundaries go in as ticks down the grid — the same mark a fitted
    // drum lane carries, for the same reason.
    if (fit)
    {
        const double stepW = (double) cellW * (double) barLen / (double) len;
        g.setColour (p.text.withAlpha (0.4f));
        for (int i = 1; i < len; ++i)
            g.fillRect ((float) labelW + (float) (i * stepW) - 0.5f,
                        (float) ledH, 1.0f, (float) (kbTop - ledH));
    }

    // The line's end. Drawn whatever the length, not only on a shortened line: a
    // control you cannot see until after you have used it teaches nobody it
    // exists, and full length is exactly where you reach to shorten it. The
    // title colour once fitted, as the drum lanes' handles are, since then it is
    // setting how many steps divide the bar rather than where the line stops.
    if (const auto handle = patternEndHandle (len); clip.intersects (handle))
    {
        g.setColour (fit ? p.title.withAlpha (0.9f)
                         : p.orange.withAlpha (len < barLen ? 0.85f : 0.3f));
        g.fillRect (handle);
    }

    // --- on-screen keyboard ---
    const int kbH = getHeight() - kbTop;
    if (kbH < 12 || ! clip.intersects (juce::Rectangle<int> (0, kbTop, getWidth(), kbH)))
        return;

    const juce::Colour whiteKey = p.kbNatural, blackKey = p.kbSharp;
    const int cursorPitch = proc.sequencer.steps[cursor].gate.load()
                                ? Sequencer303::loadPitch (proc.sequencer.steps[cursor])
                                : noKey;
    const float whiteW = (float) (getWidth() - labelW) / (float) whiteKeys;
    const float blackW = whiteW * 0.62f;
    const float blackH = (float) kbH * 0.6f;
    const float lw = (float) labelW;
    const float kbHf = (float) kbH;

    // gutter: octave shift (top) + REST pad (bottom)
    {
        auto gutter = juce::Rectangle<float> (0.0f, (float) kbTop, lw, kbHf);
        auto top = gutter.removeFromTop (kbHf * 0.5f);
        auto oL = top.removeFromLeft (lw * 0.5f).reduced (2.0f);
        auto oR = top.reduced (2.0f);
        auto rest = gutter.reduced (2.0f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        for (auto pad : { std::pair { oL, juce::String (juce::CharPointer_UTF8 ("◀")) },
                          std::pair { oR, juce::String (juce::CharPointer_UTF8 ("▶")) },
                          std::pair { rest, juce::String ("REST") } })
        {
            g.setColour (p.cellOff);
            g.fillRoundedRectangle (pad.first, 3.0f);
            g.setColour (p.outline);
            g.drawRoundedRectangle (pad.first, 3.0f, 0.8f);
            g.setColour (p.text);
            g.setFont (juce::FontOptions (pad.second == "REST" ? 9.0f : 11.0f, juce::Font::bold));
            g.drawText (pad.second, pad.first, juce::Justification::centred);
        }
    }

    // white keys
    for (int wi = 0; wi < whiteKeys; ++wi)
    {
        const int combined = kbLow + whiteSemis (wi);
        auto r = juce::Rectangle<float> (lw + (float) wi * whiteW, (float) kbTop, whiteW, kbHf)
                     .reduced (0.6f, 0.0f);
        g.setColour (combined == cursorPitch ? p.orange : whiteKey);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 2.0f, 0.7f);
        if (whiteSemis (wi) % 12 == 0)   // label each C
        {
            g.setColour (p.kbLabel);
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText (pitchText (0, combined / 12), r.withTrimmedBottom (3.0f),
                        juce::Justification::centredBottom);
        }
    }

    // black keys (drawn on top, between the appropriate whites)
    for (int wi = 0; wi < whiteKeys - 1; ++wi)
    {
        const int io = wi % 7;
        if (io != 0 && io != 1 && io != 3 && io != 4 && io != 5)
            continue;
        const int combined = kbLow + whiteSemis (wi) + 1;
        auto r = juce::Rectangle<float> (lw + (float) (wi + 1) * whiteW - blackW * 0.5f,
                                         (float) kbTop, blackW, blackH);
        g.setColour (combined == cursorPitch ? p.orange : blackKey);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 2.0f, 0.7f);
    }
}

int StepGrid::whiteSemis (int whiteIndex)
{
    static const int table[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return 12 * (whiteIndex / 7) + table[whiteIndex % 7];
}

int StepGrid::keyAt (juce::Point<int> pos) const
{
    const int kbH = getHeight() - kbTop;
    if (kbH <= 0 || pos.y < kbTop)
        return noKey;

    const float whiteW = (float) (getWidth() - labelW) / (float) whiteKeys;
    if (whiteW <= 0.0f)
        return noKey;
    const float blackW = whiteW * 0.62f;
    const float blackH = (float) kbH * 0.6f;

    // black keys sit on top, so test them first
    if ((float) (pos.y - kbTop) < blackH)
    {
        for (int wi = 0; wi < whiteKeys - 1; ++wi)
        {
            const int io = wi % 7;
            if (io != 0 && io != 1 && io != 3 && io != 4 && io != 5)
                continue;
            const float bx = (float) labelW + (float) (wi + 1) * whiteW - blackW * 0.5f;
            if ((float) pos.x >= bx && (float) pos.x < bx + blackW)
                return kbLow + whiteSemis (wi) + 1;
        }
    }

    int wi = (int) (((float) pos.x - (float) labelW) / whiteW);
    wi = juce::jlimit (0, whiteKeys - 1, wi);
    return kbLow + whiteSemis (wi);
}

void StepGrid::advanceCursor()
{
    // The line's own length, not the bar: the cursor writes steps, and steps
    // past the line's end are ones it never plays.
    const int len = proc.sequencer.lengthOf (juce::jlimit (1, 16, proc.sequencer.length.load()));
    cursor = (cursor + 1) % len;
}

void StepGrid::stampCursor (int combined)
{
    auto& s = proc.sequencer.steps[cursor];
    Sequencer303::storePitch (s, combined);
    s.gate.store (true);
    advanceCursor();
}

void StepGrid::writeCursor (int combined)
{
    stampCursor (combined);
    proc.postAudition (Sequencer303::baseNote + combined);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    dragCol = -1;
    gateDragCol = -1;
    gateDragMoved = false;

    const int y = e.y;

    // keyboard region
    if (y >= kbTop)
    {
        const int kbH = getHeight() - kbTop;
        if (e.x < labelW)
        {
            if (e.y < kbTop + kbH / 2)
                kbLow = juce::jlimit (-12, 24,
                                      kbLow + (e.x < labelW / 2 ? -12 : 12));
            else   // REST: leave the step silent, advance
            {
                proc.sequencer.steps[cursor].gate.store (false);
                advanceCursor();
            }
        }
        else
        {
            const int k = keyAt ({ e.x, e.y });
            if (k != noKey)
            {
                // Armed, a key press starts a note the playhead then carries;
                // otherwise it writes one step and moves on, as it always has.
                if (isHoldArmed())
                    startLatchedNote (k);
                else
                    writeCursor (k);
            }
        }
        repaint();
        return;
    }

    // A press on the line's end grabs it instead of moving the cursor. Checked
    // before the column, and against a hit zone a few pixels wide, so the
    // boundary stays reachable without the LEDs either side of it going.
    //
    // This sets how many steps the *line* runs, not the bar — LENGTH still owns
    // the bar, and dragging here no longer drags every drum lane with it.
    {
        const int barLen = juce::jlimit (1, 16, proc.sequencer.length.load());
        const int len = proc.sequencer.lengthOf (barLen);
        if (y < ledH && patternEndHandle (len).expanded (2, 0).contains (e.getPosition()))
        {
            if (e.mods.isRightButtonDown())
            {
                proc.sequencer.patternLength.store (Sequencer303::followBar);
                proc.sequencer.patternFit.store (false);
            }
            else
            {
                draggingLength = true;
            }
            repaint();
            return;
        }
    }

    const int col = columnAt (e.x);
    if (col < 0)
        return;
    auto& step = proc.sequencer.steps[col];
    cursor = col;   // aim the keyboard at the step you just touched

    if (y < ledH)
    {
        // clicking the LED row just moves the cursor
    }
    else if (y < ledH + gateH)
    {
        // Alt walks how many times the gate fires inside its own step, the same
        // "click walks the ring" idiom the drum grid's ratchets use. Checked
        // before the on/off toggle, which is deferred to mouseUp — otherwise
        // splitting a gate would first have to survive a length drag.
        if (e.mods.isAltDown())
        {
            const int count = proc.sequencer.ratchetAt (col);
            step.gate.store (true);   // asking for repeats on a rest can only mean one thing
            step.ratchet.store (count % Sequencer303::maxRatchet + 1);
        }
        else
        {
            // Defer the on/off toggle to mouseUp: a horizontal drag from here sets
            // the note's length instead (see mouseDrag / mouseUp).
            gateDragCol = col;
        }
    }
    else if (y < ledH + gateH + pitchH)
    {
        dragCol = col;
        dragStartPitch = Sequencer303::loadPitch (step);
        dragStartY = e.y;
    }
    else if (y < ledH + gateH + pitchH + flagH)
    {
        // ACC cycles normal -> accent -> soft -> normal. Accent stays one click
        // from normal, which is the gesture this row exists for; soft sits where
        // the second click used to turn the accent back off. Right-click walks
        // the same ring backwards, so undoing an accent is still one click.
        // indexed by dyn + 1, so entry 0 is Soft, 1 is Normal, 2 is Hard
        static constexpr int forward[]  = { dyn303::Normal, dyn303::Hard,
                                            dyn303::Soft };
        static constexpr int backward[] = { dyn303::Hard, dyn303::Soft,
                                            dyn303::Normal };
        const int dyn = dyn303::clampDyn (step.dyn.load());
        step.dyn.store ((e.mods.isRightButtonDown() ? backward : forward)[dyn + 1]);
    }
    else
    {
        step.slide.store (! step.slide.load());
    }
    repaint();
}

void StepGrid::setHoldLatch (bool shouldArm)
{
    proc.holdArmed.store (shouldArm);
    endLatchedNote();          // disarming mid-note shouldn't leave one sounding
    repaint();
}

void StepGrid::startLatchedNote (int combined)
{
    latchNote = combined;
    latchKeyDown = true;

    // The key sounds for as long as it is held, running or not, and the write
    // head on the audio thread picks it up from here — the same route a note
    // arriving over MIDI takes.
    proc.postHeldKey (Sequencer303::baseNote + combined);

    // Nothing is playing, so there is no playhead for the write head to follow.
    // The key still sounds; it just writes one step and moves on, as the
    // keyboard always has.
    if (proc.sequencer.playingStep.load() < 0)
        stampCursor (combined);
    else
        cursor = proc.sequencer.playingStep.load();

    repaint();
}

void StepGrid::endLatchedNote()
{
    latchKeyDown = false;
    proc.releaseHeldKey();
}

// Double-clicking the line's end switches it between running on the sixteenth
// grid and being fitted across the bar — the same gesture, in the same place, as
// a drum lane's.
void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y >= ledH)
        return;

    const int barLen = juce::jlimit (1, 16, proc.sequencer.length.load());
    const int len = proc.sequencer.lengthOf (barLen);
    if (! patternEndHandle (len).expanded (2, 0).contains (e.getPosition()))
        return;

    // The press that opened the double-click armed a length drag; dropped here
    // so the release doesn't leave the end grabbed.
    draggingLength = false;
    proc.sequencer.patternFit.store (! proc.sequencer.patternFit.load());
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    // Sliding onto another key while holding starts that note instead, so a run
    // of notes can be played in without releasing between them.
    if (latchKeyDown)
    {
        if (e.y >= kbTop && e.x >= labelW)
            if (const int k = keyAt ({ e.x, e.y }); k != noKey && k != latchNote)
                startLatchedNote (k);
        return;
    }

    // Length drag: rounded to the nearest boundary rather than to the column the
    // pointer is over, so the end lands where it is drawn. One step is the
    // shortest a pattern can be, as the LENGTH control's own floor is.
    if (draggingLength)
    {
        const int cellW = (getWidth() - labelW) / 16;
        if (cellW > 0)
        {
            proc.sequencer.patternLength.store (
                juce::jlimit (1, 16, (e.x - labelW + cellW / 2) / cellW));
            repaint();
        }
        return;
    }

    // GATE-row drag: extend the note to cover the columns dragged across.
    if (gateDragCol >= 0)
    {
        const int col = columnAt (e.x);
        if (col >= 0)
        {
            auto& head = proc.sequencer.steps[gateDragCol];
            if (col != gateDragCol)
            {
                head.gate.store (true);   // dragging implies a note here
                gateDragMoved = true;
            }
            const int len = proc.sequencer.lengthOf (juce::jlimit (1, 16, proc.sequencer.length.load()));
            const int maxRun = juce::jmax (1, len - gateDragCol);   // don't cross the loop end
            head.hold.store (juce::jlimit (1, maxRun, col - gateDragCol + 1));
            repaint();
        }
        return;
    }

    if (dragCol < 0)
        return;
    auto& step = proc.sequencer.steps[dragCol];
    const int delta = (dragStartY - e.y) / 6;   // 6 px per semitone
    Sequencer303::storePitch (step, dragStartPitch + delta);
    repaint();
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    // Letting go of a key ends the note it was holding, wherever the release
    // happens to land.
    endLatchedNote();

    draggingLength = false;

    // A plain click on the GATE row (no drag) toggles the note on/off.
    if (gateDragCol >= 0 && ! gateDragMoved)
    {
        auto& step = proc.sequencer.steps[gateDragCol];
        step.gate.store (! step.gate.load());
        step.hold.store (1);
        // and back to a single hit, the way the length resets: a step that gets
        // its gate back later should come back plain rather than as whatever
        // split it happened to be carrying before
        step.ratchet.store (1);
    }
    gateDragCol = -1;
    gateDragMoved = false;
    dragCol = -1;
    repaint();
}

//==============================================================================
// DrumGrid

DrumGrid::View DrumGrid::liveView() const
{
    View v;
    // The master length still comes from the bass pattern, which owns the bar.
    v.length  = proc.sequencer.length.load();

    juce::uint64 h = 1469598103934665603ull;
    const auto mix = [&h] (juce::uint64 value) { h = (h ^ value) * 1099511628211ull; };

    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        v.playing[lane] = proc.drumSequencer.lanePlayingStep[lane].load();
        v.laneLen[lane] = proc.drumSequencer.lengthOf (lane, v.length);
        v.fit[lane] = proc.drumSequencer.laneFit[lane].load();
        mix (proc.drumSequencer.stepMask[lane].load());
        mix (proc.drumSequencer.accentMask[lane].load());
        mix (proc.drumSequencer.softMask[lane].load());
        mix (proc.drumSequencer.ratchetMask[lane].load());
    }
    v.pattern = h;
    return v;
}

juce::Rectangle<int> DrumGrid::playheadCellBounds (int lane, int col) const
{
    if (col < 0 || col > 15 || lane < 0 || lane >= DrumSequencer::numLanes)
        return {};

    const int cellW = (getWidth() - labelW) / 16;
    const int rowH = getHeight() / DrumSequencer::numLanes;
    return { labelW + col * cellW, lane * rowH, cellW, rowH };
}

// The bar at a lane's end, sitting on the boundary between the last step it
// plays and the first it doesn't. Narrow, because it has to be grabbable without
// stealing presses from the cell either side of it.
juce::Rectangle<int> DrumGrid::laneEndHandle (int lane, int len) const
{
    const int cellW = (getWidth() - labelW) / 16;
    const int rowH = getHeight() / DrumSequencer::numLanes;
    // Clamped into the component, so a lane running the full sixteen still shows
    // its handle rather than having it half cut off against the right edge.
    const int x = juce::jlimit (labelW, getWidth() - handleW,
                                labelW + len * cellW - handleW / 2);
    return { x, lane * rowH + 2, handleW, rowH - 4 };
}

void DrumGrid::repaintPlayhead (int lane, int col)
{
    if (const auto r = playheadCellBounds (lane, col); ! r.isEmpty())
        repaint (r);
}

void DrumGrid::timerCallback()
{
    const auto now = liveView();
    if (now == shown)
        return;

    if (now.sameApartFromPlayhead (shown))
    {
        // Only the markers moved. Ten small rects at worst, and on the common
        // case of every lane following the master they are the same column.
        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            if (now.playing[lane] == shown.playing[lane])
                continue;
            repaintPlayhead (lane, shown.playing[lane]);
            repaintPlayhead (lane, now.playing[lane]);
        }
        return;
    }

    repaint();
}

void DrumGrid::paint (juce::Graphics& g)
{
    shown = liveView();

    const auto& p = ui303::palette (proc.uiSkin.load());
    const int cellW = (getWidth() - labelW) / 16;
    const int rowH = getHeight() / DrumSequencer::numLanes;
    // The drum line has its own enable, so the playhead comes from the drum
    // sequencer; the master length still comes from the bass pattern, which owns
    // the bar. Each lane may run shorter, and then it has its own end and its own
    // position in it.
    const int masterLen = proc.sequencer.length.load();
    static const char* laneNames[] = { "BD", "SD", "CP", "CH", "OH" };

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));

    // The playhead marker sits on one cell of the top lane, so a repaint for it
    // has no business drawing the other seventy-nine.
    const auto clip = g.getClipBounds();

    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        const int cy = lane * rowH;
        if (! clip.intersects (juce::Rectangle<int> (0, cy, getWidth(), rowH)))
            continue;

        const uint32_t mask = proc.drumSequencer.stepMask[lane].load();
        const int len = proc.drumSequencer.lengthOf (lane, masterLen);
        const int playing = proc.drumSequencer.lanePlayingStep[lane].load();

        if (clip.getX() < labelW)
        {
            g.setColour (p.text);
            g.drawText (laneNames[lane], 0, cy, labelW - 6, rowH,
                        juce::Justification::centredRight);
        }

        for (int col = 0; col < 16; ++col)
        {
            if (! clip.intersects (juce::Rectangle<int> (labelW + col * cellW, cy, cellW, rowH)))
                continue;

            const uint32_t bit = 1u << col;
            const bool on = (mask & bit) != 0;
            const int dyn = proc.drumSequencer.dynAt (lane, col);
            const float dim = col < len ? 1.0f : 0.35f;

            auto cell = juce::Rectangle<float> ((float) (labelW + col * cellW), (float) cy,
                                                (float) cellW, (float) rowH).reduced (2.0f);

            // Brightness is what says how hard the step hits: a soft hit is the
            // normal colour part-lit, an accent is the warmer one lifted clear of
            // it. The hue shift is a second cue riding along, never the only one,
            // so the three still read apart on a skin whose two lit colours are
            // nearly the same and to an eye that can't separate them at all.
            auto colour = ! on   ? p.cellOff
                        : dyn > 0 ? p.orange.brighter (ui303::accentCellLift)
                        : dyn < 0 ? p.cellOff.interpolatedWith (p.red, ui303::softCellStrength)
                                  : p.red;
            const float glow = dyn > 0 ? 1.0f : dyn < 0 ? ui303::softCellStrength : 0.75f;
            if (on && p.retro)
            {
                g.setColour (colour.withAlpha (0.35f * glow * dim));
                g.fillRoundedRectangle (cell.expanded (1.5f), 4.0f);
            }
            g.setColour (colour.withAlpha (dim));
            g.fillRoundedRectangle (cell, 3.0f);
            if (p.retro)
            {
                g.setColour (juce::Colours::black.withAlpha (0.4f * dim));
                g.drawRoundedRectangle (cell, 3.0f, 0.8f);
            }

            // A ratcheted step is sliced into the number of times it fires, cut
            // out of the lit cell rather than drawn over it — the count has to be
            // countable at a glance, and at four slices in a cell this size a
            // added mark would just read as texture.
            if (const int ratchet = proc.drumSequencer.ratchetAt (lane, col); ratchet > 1)
            {
                g.setColour (p.cellOff.withAlpha (0.9f * dim));
                for (int cut = 1; cut < ratchet; ++cut)
                    g.fillRect (cell.getX() + cell.getWidth() * (float) cut / (float) ratchet - 0.8f,
                                cell.getY(), 1.6f, cell.getHeight());
            }

            // A marker per lane rather than one on the top row: with lanes of
            // different lengths there is no single column that says where the
            // kit is, and a marker sitting over a cell six lanes away from the
            // one actually firing is worse than none.
            if (col == playing)
            {
                g.setColour ((p.retro ? p.title : p.text).withAlpha (0.6f));
                g.drawRoundedRectangle (cell, 3.0f, 1.5f);
            }
        }

        // The lane's end, drawn as a grabbable edge. Drawn on every lane, not
        // only on shortened ones: a control you cannot see until after you have
        // used it teaches nobody it exists, and a lane following the master is
        // exactly where you reach to shorten it. Dim while following, lit once
        // the lane has an end of its own.
        // A fitted lane's steps don't sit on the columns they are drawn in — the
        // grid is a sixteen-cell editor whatever clock the lane runs on — so the
        // real boundaries go in as ticks across the row. Without them the only
        // thing on screen saying a lane is fitted is the colour of its handle,
        // and "three cells" would read as three sixteenths rather than as the
        // bar cut in three.
        if (proc.drumSequencer.laneFit[lane].load())
        {
            const double stepW = (double) cellW * (double) masterLen / (double) len;
            g.setColour (p.text.withAlpha (0.4f));
            for (int i = 1; i < len; ++i)
                g.fillRect ((float) labelW + (float) (i * stepW) - 0.5f,
                            (float) cy + 1.0f, 1.0f, (float) rowH - 2.0f);
        }

        if (const auto handle = laneEndHandle (lane, len); clip.intersects (handle))
        {
            // Lit orange for a lane that has an end of its own, dim while it
            // follows the master — and the title colour once it is fitted, since
            // then the handle is setting how many steps divide the bar rather
            // than where the lane stops.
            const bool fit = proc.drumSequencer.laneFit[lane].load();
            g.setColour (fit ? p.title.withAlpha (0.9f)
                             : p.orange.withAlpha (len < masterLen ? 0.85f : 0.3f));
            g.fillRect (handle);
        }
    }

    for (int col = 0; col < 16; col += 4)
    {
        const int x = labelW + col * cellW;
        if (x < clip.getX() || x > clip.getRight())
            continue;

        g.setColour (p.text.withAlpha (0.25f));
        g.drawVerticalLine (x, 0.0f, (float) getHeight());
    }
}

void DrumGrid::applyPaint (int lane, int col)
{
    auto& steps = proc.drumSequencer.stepMask[lane];

    switch (paintMode)
    {
        // A plain hit leaves any level already on the step alone, so dragging a
        // run of hits across an accent doesn't flatten it.
        case Paint::SetHit:   steps.fetch_or (1u << col); break;
        case Paint::ClearHit: proc.drumSequencer.clearStep (lane, col); break;
        case Paint::SetLevel: proc.drumSequencer.setDynAt (lane, col, paintDyn); break;
        case Paint::SetRatchet:
            proc.drumSequencer.setRatchetAt (lane, col, paintRatchet); break;
        case Paint::DragLength:
        case Paint::None:     break;
    }
}

void DrumGrid::mouseDown (const juce::MouseEvent& e)
{
    const int cellW = (getWidth() - labelW) / 16;
    const int rowH = getHeight() / DrumSequencer::numLanes;
    if (e.x < labelW || cellW <= 0 || rowH <= 0)
        return;

    const int col = juce::jlimit (0, 15, (e.x - labelW) / cellW);
    const int lane = juce::jlimit (0, DrumSequencer::numLanes - 1, e.y / rowH);

    // A press on the lane's end grabs it instead of painting. Checked first, and
    // against a hit zone a few pixels wide, so the boundary is reachable without
    // making the two cells either side of it hard to click.
    {
        const int len = proc.drumSequencer.lengthOf (lane, proc.sequencer.length.load());
        if (laneEndHandle (lane, len).expanded (2, 0).contains (e.getPosition()))
        {
            // Right-click puts the lane back on the master. Dragging back to the
            // master length would be the other way to spell it, but then a lane
            // could never be pinned at a length that happens to equal the bar.
            if (e.mods.isRightButtonDown())
            {
                proc.drumSequencer.laneLength[lane].store (DrumSequencer::followMaster);
                // Its clock as well as its length: a lane fitted to the bar in
                // as many steps as the bar has is the identity case, so leaving
                // FIT set would only show as a handle coloured for a mode the
                // lane is no longer meaningfully in.
                proc.drumSequencer.laneFit[lane].store (false);
                paintMode = Paint::None;
            }
            else
            {
                paintMode = Paint::DragLength;
                paintLane = lane;
            }
            repaint();
            return;
        }
    }

    // The pressed cell decides the operation; a drag then repeats it across cells.
    // Left button owns whether there is a hit at all; right (or shift) owns how
    // hard it lands, cycling normal -> accent -> soft the same way the bass ACC
    // row does. An empty cell right-clicks straight to an accent, as it always
    // has — the level ring never turns a hit off, that stays the left button's
    // job.
    // Alt owns how many times the step fires, cycling 1 -> 2 -> 3 -> 4 -> 1, the
    // same "click walks the ring" idiom the level uses. Checked before the level,
    // since alt-shift would otherwise land on whichever was tested first.
    if (e.mods.isAltDown())
    {
        const int count = proc.drumSequencer.ratchetAt (lane, col);
        paintRatchet = proc.drumSequencer.hasHit (lane, col)
                           ? count % DrumSequencer::maxRatchet + 1
                           : 2;   // an empty step alt-clicks straight to a double
        paintMode = Paint::SetRatchet;
    }
    else if (e.mods.isRightButtonDown() || e.mods.isShiftDown())
    {
        // indexed by dyn + 1: entry 0 is Soft, 1 is Normal, 2 is Hard
        static constexpr int next[] = { dyn303::Normal, dyn303::Hard, dyn303::Soft };
        paintDyn = proc.drumSequencer.hasHit (lane, col)
                       ? next[dyn303::clampDyn (proc.drumSequencer.dynAt (lane, col)) + 1]
                       : dyn303::Hard;
        paintMode = Paint::SetLevel;
    }
    else
    {
        paintMode = proc.drumSequencer.hasHit (lane, col) ? Paint::ClearHit : Paint::SetHit;
    }

    paintLane = lane;
    applyPaint (lane, col);
    repaint();
}

// Double-clicking a lane's end switches it between running on the sixteenth
// grid and being fitted across the bar. It lives on the handle because the two
// controls are one idea — the end says how many steps, FIT says what they are
// spread over — and putting it anywhere else would make a lane's timing depend
// on a control nowhere near it.
void DrumGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int rowH = getHeight() / DrumSequencer::numLanes;
    if (rowH <= 0)
        return;

    const int lane = juce::jlimit (0, DrumSequencer::numLanes - 1, e.y / rowH);
    const int len = proc.drumSequencer.lengthOf (lane, proc.sequencer.length.load());
    if (! laneEndHandle (lane, len).expanded (2, 0).contains (e.getPosition()))
        return;

    // The press that opened the double-click armed a length drag; dropped here
    // so the release doesn't leave the lane grabbed.
    paintMode = Paint::None;
    proc.drumSequencer.laneFit[lane].store (! proc.drumSequencer.laneFit[lane].load());
    repaint();
}

void DrumGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int cellW = (getWidth() - labelW) / 16;
    if (paintMode == Paint::None || paintLane < 0 || cellW <= 0)
        return;

    if (paintMode == Paint::DragLength)
    {
        // Rounded to the nearest boundary rather than to the cell the pointer is
        // over, so the end lands where the bar is drawn and a drag doesn't feel
        // half a cell behind the cursor. One step is the shortest a lane can be.
        const int len = juce::jlimit (1, 16, (e.x - labelW + cellW / 2) / cellW);
        proc.drumSequencer.laneLength[paintLane].store (len);
        repaint();
        return;
    }

    if (e.x < labelW)
        return;

    // Locked to the lane the drag began in, so a diagonal drag stays on one voice.
    const int col = juce::jlimit (0, 15, (e.x - labelW) / cellW);
    applyPaint (paintLane, col);
    repaint();
}

//==============================================================================
// EqBands

EqBands::EqBands (BP303AudioProcessor& p, int lineIndex) : proc (p), line (lineIndex)
{
    const char* const* ids = BP303AudioProcessor::eqBandIds (line);

    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        gain[b] = proc.apvts.getRawParameterValue (ids[b]);

        // An attachment rather than a listener, so a node moved by automation
        // or a preset load redraws too.
        if (auto* param = proc.apvts.getParameter (ids[b]))
            att[b] = std::make_unique<juce::ParameterAttachment> (
                *param, [this] (float) { curveValid = false; repaint(); }, nullptr);
    }

    startTimerHz (25);
}

void EqBands::resized()
{
    curveValid = false;
}

juce::Rectangle<int> EqBands::plotArea() const
{
    return getLocalBounds().withTrimmedLeft (axisW).withTrimmedBottom (labelH);
}

float EqBands::freqToX (float hz) const
{
    const auto r = plotArea();
    const float t = (std::log10 (juce::jmax (1.0f, hz)) - std::log10 (loHz))
                  / (std::log10 (hiHz) - std::log10 (loHz));
    return (float) r.getX() + (float) r.getWidth() * juce::jlimit (0.0f, 1.0f, t);
}

float EqBands::gainToY (float db) const
{
    const auto r = plotArea();
    const float t = (juce::jlimit (-plotRangeDb, plotRangeDb, db) + plotRangeDb)
                  / (2.0f * plotRangeDb);
    return (float) r.getBottom() - (float) r.getHeight() * t;
}

float EqBands::yToGain (float y) const
{
    const auto r = plotArea();
    if (r.getHeight() <= 0)
        return 0.0f;

    const float t = ((float) r.getBottom() - y) / (float) r.getHeight();
    return juce::jlimit (-GraphicEq::maxGainDb, GraphicEq::maxGainDb,
                         t * 2.0f * plotRangeDb - plotRangeDb);
}

// The strip a band's meter column occupies: exactly the octave the band covers,
// which is what makes the columns tile the axis without overlapping.
juce::Rectangle<int> EqBands::bandStrip (int b) const
{
    const auto r = plotArea();
    const float half = 1.4142135f;
    const int x0 = juce::roundToInt (freqToX (GraphicEq::centreHz[b] / half));
    const int x1 = juce::roundToInt (freqToX (GraphicEq::centreHz[b] * half));
    return { x0, r.getY(), juce::jmax (1, x1 - x0), r.getHeight() };
}

int EqBands::nearestBand (int x) const
{
    int best = 0;
    float bestD = 1.0e9f;
    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        const float d = std::abs (freqToX (GraphicEq::centreHz[b]) - (float) x);
        if (d < bestD)
        {
            bestD = d;
            best = b;
        }
    }
    return best;
}

void EqBands::ensureCurve()
{
    float now[GraphicEq::numBands];
    bool changed = ! curveValid;
    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        now[b] = gain[b] != nullptr ? gain[b]->load() : 0.0f;
        if (now[b] != curveBuiltFrom[b])
            changed = true;
    }

    if (! changed)
        return;

    std::copy (std::begin (now), std::end (now), std::begin (curveBuiltFrom));
    curveValid = true;

    const auto r = plotArea();
    curve.clear();
    if (r.getWidth() <= 1 || r.getHeight() <= 1)
        return;

    // One sample every two pixels. The response is smooth at octave Q, so
    // finer than that buys nothing a 54px-tall plot could show.
    const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
    const int steps = juce::jmax (2, r.getWidth() / 2);
    const float logLo = std::log10 (loHz), logHi = std::log10 (hiHz);

    for (int i = 0; i <= steps; ++i)
    {
        const float t  = (float) i / (float) steps;
        const float hz = std::pow (10.0f, logLo + t * (logHi - logLo));
        const float x  = (float) r.getX() + (float) r.getWidth() * t;
        const float y  = gainToY (GraphicEq::responseDb (now, hz, sr));

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

}

void EqBands::paint (juce::Graphics& g)
{
    ensureCurve();

    const auto& p = ui303::palette (proc.uiSkin.load());
    const auto r = plotArea();
    if (r.getWidth() <= 1 || r.getHeight() <= 1)
        return;

    const auto clip = g.getClipBounds();

    g.setColour (p.cellOff.darker (0.5f));
    g.fillRect (r);

    // --- the meters: one column an octave wide, behind everything -----------
    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        const auto strip = bandStrip (b);
        if (! clip.intersects (strip))
            continue;

        const int lit = juce::roundToInt (proc.eqBandLevel (line, b)
                                          * (float) strip.getHeight());
        if (lit > 0)
        {
            g.setColour (p.red.withAlpha (0.22f));
            g.fillRect (strip.reduced (1, 0).removeFromBottom (lit));
        }
    }

    // --- grid ---------------------------------------------------------------
    g.setFont (juce::FontOptions (8.0f));
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = gainToY ((float) db);
        g.setColour (db == 0 ? p.outline : p.outline.withAlpha (0.45f));
        g.drawLine ((float) r.getX(), y, (float) r.getRight(), y, 1.0f);

        g.setColour (p.text.withAlpha (0.55f));
        g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db),
                    0, juce::roundToInt (y) - 6, axisW - 3, 12,
                    juce::Justification::centredRight);
    }

    // --- the response curve --------------------------------------------------
    // Stroke only. A filled curve reads well on its own, but the spectrum
    // already owns the fill here, and two translucent washes of the same
    // colour make it impossible to tell which one you are looking at.
    g.setColour (p.red);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // --- nodes and their frequency labels ------------------------------------
    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    static const char* labels[GraphicEq::numBands] = {
        "31", "63", "125", "250", "500", "1K", "2K", "4K", "8K", "16K"
    };

    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        const float db = gain[b] != nullptr ? gain[b]->load() : 0.0f;
        const float x = freqToX (GraphicEq::centreHz[b]);
        const float y = gainToY (db);
        const bool live = (b == dragBand || b == hoverBand);

        // A tick down to the axis, so a node that happens to sit on the 0 dB
        // line is still findable as a band rather than as part of the grid.
        g.setColour (p.outline.withAlpha (live ? 0.9f : 0.35f));
        g.drawLine (x, y, x, (float) r.getBottom(), 1.0f);

        const float rad = live ? (float) nodeR + 1.0f : (float) nodeR;
        g.setColour (p.panel1);
        g.fillEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (live ? p.orange : p.red);
        g.drawEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f, 1.6f);

        g.setColour (live ? p.text : p.text.withAlpha (0.7f));
        g.drawText (labels[b], juce::roundToInt (x) - 20, getHeight() - labelH,
                    40, labelH, juce::Justification::centred);
    }
}

void EqBands::timerCallback()
{
    if (getWidth() <= axisW || getHeight() <= labelH)
        return;

    int moved[GraphicEq::numBands];
    int numMoved = 0;

    for (int b = 0; b < GraphicEq::numBands; ++b)
    {
        const auto strip = bandStrip (b);
        const int lit = juce::roundToInt (proc.eqBandLevel (line, b)
                                          * (float) strip.getHeight());
        if (lit == shownMeter[b])
            continue;

        shownMeter[b] = lit;
        moved[numMoved++] = b;
    }

    if (numMoved == 0)
        return;

    // Repainting a strip re-strokes the whole response path under a narrow
    // clip, so a strip is not a tenth of the plot's cost — it is a third of it.
    // Measured: ten strips are 1.08 ms against 0.32 ms for the plot in one go.
    // With anything playing most bands move every frame, so past a few of them
    // the single repaint is the cheaper answer, and below that the strips still
    // save the quiet bands from costing anything at all.
    if (numMoved > 3)
    {
        repaint (plotArea());
        return;
    }

    for (int i = 0; i < numMoved; ++i)
        repaint (bandStrip (moved[i]));
}

void EqBands::setReadout (int band)
{
    if (! onReadout)
        return;

    if (band < 0)
    {
        onReadout ({});
        return;
    }

    const float hz = GraphicEq::centreHz[band];
    const float db = gain[band] != nullptr ? gain[band]->load() : 0.0f;
    onReadout ((hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "kHz"
                              : juce::String (juce::roundToInt (hz)) + "Hz")
               + " " + (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB");
}

void EqBands::writeBand (int band, const juce::MouseEvent& e)
{
    if (att[band] == nullptr)
        return;

    // Shift is the fine pass: the plot is 54px over 28 dB, so a pixel is half
    // a dB — enough to shape with, not enough to match two bands by eye.
    const float db = e.mods.isShiftDown()
                   ? dragStartGain + (yToGain ((float) e.y)
                                      - yToGain ((float) dragStartY)) * 0.25f
                   : yToGain ((float) e.y);

    att[band]->setValueAsPartOfGesture (
        juce::jlimit (-GraphicEq::maxGainDb, GraphicEq::maxGainDb, db));

    setReadout (band);
}

void EqBands::mouseDown (const juce::MouseEvent& e)
{
    dragBand = nearestBand (e.x);
    dragStartY = e.y;
    dragStartGain = gain[dragBand] != nullptr ? gain[dragBand]->load() : 0.0f;

    if (att[dragBand] != nullptr)
        att[dragBand]->beginGesture();

    writeBand (dragBand, e);
    repaint();
}

void EqBands::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    // Deliberately *not* handed over when the pointer crosses into the next
    // band. A node is at a fixed frequency, so a drag that wandered sideways
    // would drop the band you grabbed and start bending its neighbour — which
    // on a curve, unlike on a fader bank, is nothing like what you asked for.
    writeBand (dragBand, e);
}

void EqBands::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0 && att[dragBand] != nullptr)
        att[dragBand]->endGesture();

    dragBand = -1;
    repaint();
}

void EqBands::mouseMove (const juce::MouseEvent& e)
{
    const int b = nearestBand (e.x);
    if (b == hoverBand)
        return;

    hoverBand = b;
    setReadout (b);
    repaint();
}

void EqBands::mouseExit (const juce::MouseEvent&)
{
    if (dragBand >= 0)
        return;

    hoverBand = -1;
    setReadout (-1);
    repaint();
}

void EqBands::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = nearestBand (e.x);
    if (att[b] != nullptr)
        att[b]->setValueAsCompleteGesture (0.0f);
}

void EqBands::flatten()
{
    for (auto& a : att)
        if (a != nullptr)
            a->setValueAsCompleteGesture (0.0f);
}

//==============================================================================
// XyPad

XyPad::XyPad (BP303AudioProcessor& p) : proc (p)
{
    xVal  = proc.apvts.getRawParameterValue ("padx");
    yVal  = proc.apvts.getRawParameterValue ("pady");
    onVal = proc.apvts.getRawParameterValue ("padon");

    // Attachments rather than listeners, for the same reason the EQ's nodes use
    // them: a pad moved by automation or by a preset load has to redraw too.
    const auto redraw = [this] (float) { repaint(); };
    if (auto* param = proc.apvts.getParameter ("padx"))    xAtt    = std::make_unique<juce::ParameterAttachment> (*param, redraw, nullptr);
    if (auto* param = proc.apvts.getParameter ("pady"))    yAtt    = std::make_unique<juce::ParameterAttachment> (*param, redraw, nullptr);
    if (auto* param = proc.apvts.getParameter ("padon"))   onAtt   = std::make_unique<juce::ParameterAttachment> (*param, redraw, nullptr);
    if (auto* param = proc.apvts.getParameter ("padmode")) modeAtt = std::make_unique<juce::ParameterAttachment> (*param, redraw, nullptr);

    startTimerHz (25);
}

juce::Rectangle<int> XyPad::padArea() const
{
    // Square, and as tall as the section's content allows. The readout column
    // takes what is left, which is what the 300px section width was chosen to
    // leave room for.
    auto r = getLocalBounds().reduced (margin);
    return r.removeFromLeft (r.getHeight());
}

void XyPad::writeAxes (const juce::MouseEvent& e)
{
    const auto r = padArea();
    if (r.getWidth() <= 1 || r.getHeight() <= 1)
        return;

    const auto norm = [] (float v, float lo, float len)
    {
        return juce::jlimit (-1.0f, 1.0f, (v - lo) / juce::jmax (1.0f, len) * 2.0f - 1.0f);
    };

    // Y is inverted: up the pad is more of whatever the axis is, which is the
    // direction every other control in the plugin moves in.
    const float nx =  norm ((float) e.x, (float) r.getX(), (float) r.getWidth());
    const float ny = -norm ((float) e.y, (float) r.getY(), (float) r.getHeight());

    if (xAtt != nullptr) xAtt->setValueAsPartOfGesture (nx);
    if (yAtt != nullptr) yAtt->setValueAsPartOfGesture (ny);
}

void XyPad::setHeld (bool held)
{
    if (onAtt != nullptr)
        onAtt->setValueAsCompleteGesture (held ? 1.0f : 0.0f);
}

void XyPad::mouseDown (const juce::MouseEvent& e)
{
    if (! padArea().contains (e.getPosition()))
        return;

    dragging = true;

    // HOLD before the axes: the mode's units have to be on by the time the
    // first offset lands, or the opening instant of a gesture is inaudible.
    setHeld (true);

    if (xAtt != nullptr) xAtt->beginGesture();
    if (yAtt != nullptr) yAtt->beginGesture();
    writeAxes (e);
    repaint();
}

void XyPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        writeAxes (e);
}

void XyPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;

    const bool latched = proc.apvts.getRawParameterValue ("padlatch")->load() >= 0.5f;

    if (xAtt != nullptr) xAtt->endGesture();
    if (yAtt != nullptr) yAtt->endGesture();

    if (! latched)
    {
        // The spring back to centre is its own complete gesture rather than the
        // tail of the drag, so a host recording automation writes the return as
        // a deliberate move. Dropping HOLD alone would leave the axes parked at
        // the edge and the next touch would jump the sound there before the
        // pointer had moved.
        if (xAtt != nullptr) xAtt->setValueAsCompleteGesture (0.0f);
        if (yAtt != nullptr) yAtt->setValueAsCompleteGesture (0.0f);
        setHeld (false);
    }

    repaint();
}

void XyPad::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! padArea().contains (e.getPosition()))
        return;

    // The way out of a latched gesture without having to find the centre by
    // hand — the same role FLAT plays on the EQ.
    if (xAtt != nullptr) xAtt->setValueAsCompleteGesture (0.0f);
    if (yAtt != nullptr) yAtt->setValueAsCompleteGesture (0.0f);
    setHeld (false);
    repaint();
}

void XyPad::spawnSparks (juce::Point<float> at, float speed)
{
    // Faster gestures throw more. Capped well below maxSparks per tick so a
    // flick across the pad leaves a trail rather than one dense clump.
    const int wanted = juce::jlimit (0, 5, juce::roundToInt (speed * 0.5f));

    for (int made = 0; made < wanted; ++made)
    {
        Spark* slot = nullptr;
        for (auto& s : sparks)
            if (s.life <= 0.0f) { slot = &s; break; }

        if (slot == nullptr)
            return;   // all in flight; the burst is already as dense as it gets

        const float ang = sparkRng.nextFloat() * juce::MathConstants<float>::twoPi;
        const float spd = 0.6f + sparkRng.nextFloat() * 1.5f;

        slot->x  = at.x + (sparkRng.nextFloat() - 0.5f) * 6.0f;
        slot->y  = at.y + (sparkRng.nextFloat() - 0.5f) * 6.0f;
        slot->vx = std::cos (ang) * spd;
        slot->vy = std::sin (ang) * spd;
        slot->life = 0.75f + sparkRng.nextFloat() * 0.25f;
        slot->size = 1.2f + sparkRng.nextFloat() * 1.8f;
    }
}

bool XyPad::advanceSparks()
{
    bool alive = false;

    for (auto& s : sparks)
    {
        if (s.life <= 0.0f)
            continue;

        s.x += s.vx;
        s.y += s.vy;
        // Drag plus a little rise, so a burst reads as a puff lifting off the
        // handle rather than as debris thrown at the walls.
        s.vx *= 0.90f;
        s.vy = s.vy * 0.90f - 0.05f;
        s.life -= 0.06f;
        alive = true;
    }

    return alive;
}

void XyPad::timerCallback()
{
    const auto r = padArea();
    if (r.getWidth() <= 1 || xVal == nullptr || yVal == nullptr)
        return;

    // The knobs the pad is pushing track it from here, rather than from the
    // mouse, so a pad driven by automation moves them too.
    //
    // While it is held they are refreshed every tick even if the pad has not
    // moved: the user can turn a knob under a parked gesture, and the offset is
    // clamped against wherever that knob now sits, so it is not a function of
    // the pad alone. The listeners early-out on an unchanged offset, so a still
    // pad costs twelve comparisons a frame and no repaint.
    const float nx = xVal->load(), ny = yVal->load();
    const bool  held = onVal != nullptr && onVal->load() >= 0.5f;
    const bool  axesMoved = (nx != lastX || ny != lastY || held != lastHeld);

    if (onPadMoved != nullptr && (held || axesMoved))
        onPadMoved();

    lastX = nx;
    lastY = ny;
    lastHeld = held;

    const int px = r.getX() + juce::roundToInt ((nx * 0.5f + 0.5f) * (float) r.getWidth());
    const int py = r.getBottom() - juce::roundToInt ((ny * 0.5f + 0.5f) * (float) r.getHeight());

    // Sparks come off how far the handle actually travelled on screen, so a
    // fast automation ramp throws the same burst a fast drag does, and a
    // sub-pixel crawl throws none.
    if (held && shownX >= 0)
    {
        const float dx = (float) (px - shownX), dy = (float) (py - shownY);
        spawnSparks ({ (float) px, (float) py }, std::sqrt (dx * dx + dy * dy));
    }

    const bool sparksAlive = advanceSparks();
    const bool handleMoved = (px != shownX || py != shownY);

    shownX = px;
    shownY = py;

    // A moved handle changes the readout column too, so that is the whole
    // component. Sparks on their own are only ever inside the square, and they
    // are what keeps the frame going after the gesture has stopped moving until
    // the last one has faded.
    if (axesMoved || handleMoved)
        repaint();
    else if (sparksAlive)
        repaint (r);
}

LfoScope::LfoScope (BP303AudioProcessor& p) : proc (p)
{
    startTimerHz (25);
}

void LfoScope::timerCallback()
{
    const auto l = proc.readLfo();
    const double phase = proc.lfoPhaseNow.load();

    // A still LFO is a still picture. Repainting one 25 times a second is the
    // kind of cost that made this plugin's CPU an editor problem in the first
    // place, so only a moved dot or a changed drawing earns a frame.
    const bool moved = std::abs (phase - lastPhase) > 1.0e-4;
    if (! moved && l.shape == lastShape && l.depth == lastDepth && l.active() == lastOn
        && l.loopLen() == lastLen)
        return;

    lastPhase = phase;
    lastShape = l.shape;
    lastDepth = l.depth;
    lastOn    = l.active();
    lastLen   = l.loopLen();

    // The transition to inactive reaches here too, which is what clears a stale
    // ring off a knob the LFO has stopped pushing.
    if (onLfoMoved != nullptr)
        onLfoMoved();

    repaint();
}

juce::Rectangle<float> LfoScope::plotArea() const
{
    return getLocalBounds().reduced (plotInset).toFloat();
}

int LfoScope::stepAt (juce::Point<float> p) const
{
    const auto r = plotArea();
    if (r.getWidth() <= 0.0f)
        return 0;

    const float t = (p.x - r.getX()) / r.getWidth();
    return juce::jlimit (0, lfo::customSteps - 1, (int) (t * lfo::customSteps));
}

float LfoScope::levelAt (juce::Point<float> p) const
{
    const auto r = plotArea();
    if (r.getHeight() <= 0.0f)
        return 0.0f;

    // The inverse of the height the wave is drawn at, so a step lands under the
    // pointer rather than near it.
    return juce::jlimit (-1.0f, 1.0f,
                         (r.getCentreY() - p.y) / (r.getHeight() * 0.42f));
}

void LfoScope::paintStep (juce::Point<float> p)
{
    if (proc.apvts.getRawParameterValue ("lfo1shape")->load() != (float) lfo::Custom)
        return;

    proc.lfoShape[stepAt (p)].store (levelAt (p));

    // Repaint now rather than waiting for the tick: an LFO that is switched off
    // publishes no phase, so the timer's early-out would sit on the drawing
    // until something moved and the shape would appear to lag the mouse.
    repaint();
    if (onLfoMoved != nullptr)
        onLfoMoved();
}

int LfoScope::drawLen() const
{
    if (auto* v = proc.apvts.getRawParameterValue ("lfo1len"))
        return juce::jlimit (2, lfo::customSteps, (int) v->load());
    return lfo::customSteps;
}

float LfoScope::endMarkerX() const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * (float) drawLen() / lfo::customSteps;
}

void LfoScope::setLenFrom (juce::Point<float> p)
{
    const auto r = plotArea();
    if (r.getWidth() <= 0.0f)
        return;

    // Snap to a step boundary: the loop point is a count of steps, so it can
    // only fall between them. Round rather than truncate so the nearest edge
    // wins, and clamp to at least two — a one-step loop is a constant, which is
    // what a flat drawing already gives.
    const int len = juce::jlimit (2, lfo::customSteps,
                                  juce::roundToInt ((p.x - r.getX()) / r.getWidth()
                                                    * lfo::customSteps));
    if (auto* param = proc.apvts.getParameter ("lfo1len"))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) len));

    repaint();
}

void LfoScope::mouseDown (const juce::MouseEvent& e)
{
    // Grab the end marker if the press landed on it; otherwise paint. Only the
    // drawn shape has a marker, so for every other shape a press always paints
    // (and paintStep itself no-ops off the drawn shape, so it does nothing).
    const bool drawable =
        proc.apvts.getRawParameterValue ("lfo1shape")->load() == (float) lfo::Custom;

    draggingEnd = drawable && std::abs (e.position.x - endMarkerX()) <= 5.0f;

    if (draggingEnd)
        setLenFrom (e.position);
    else
        paintStep (e.position);
}

void LfoScope::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingEnd)
        setLenFrom (e.position);
    else
        paintStep (e.position);
}

void LfoScope::mouseUp (const juce::MouseEvent&) { draggingEnd = false; }

void LfoScope::paint (juce::Graphics& g)
{
    const auto& p = ui303::palette (proc.uiSkin.load());
    const auto r = plotArea();
    if (r.getWidth() <= 2.0f || r.getHeight() <= 2.0f)
        return;

    g.setColour (p.cellOff.darker (0.5f));
    g.fillRect (r);

    g.setColour (p.outline.withAlpha (0.45f));
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);

    const auto l = proc.readLfo();
    const bool live = l.active();
    const bool drawable = l.shape == lfo::Custom;

    // The slots you are painting into. Without them "somewhere along the line"
    // is the only aim you get, and a sixteen-step shape drawn against nothing
    // reads as a freehand curve that happens to have corners in it. Same
    // reasoning as the fitted drum lane's step ticks across its row.
    if (drawable)
    {
        g.setColour (p.outline.withAlpha (0.25f));
        for (int i = 1; i < lfo::customSteps; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / lfo::customSteps;
            g.drawLine (x, r.getY(), x, r.getBottom(), 1.0f);
        }
    }

    const double phase = proc.lfoPhaseNow.load();

    // The drawn shape draws slots, not time — the same rule the fitted drum lane
    // follows. All sixteen steps keep their own column whatever the loop length,
    // so the loop plays across the columns up to the marker and the steps past
    // it stay visible to edit, dimmed. Drawing the loop stretched to full width
    // instead would hide the steps outside it and leave the marker mid-column
    // with nothing to point at.
    if (drawable)
    {
        const int   len   = drawLen();
        const float markX = endMarkerX();

        // The loop region is exactly what plays — smoothed or stepped, over
        // `len` steps — and valueAt maps that loop across [0,1), so mapping u
        // onto [left, marker] lands each step on its own 1/16 column.
        juce::Path loop;
        const int res = juce::jmax (2, (int) (markX - r.getX()));
        for (int i = 0; i <= res; ++i)
        {
            const double u = (double) i / res;
            const float  v = l.valueAt (u);
            const float  x = r.getX() + (float) u * (markX - r.getX());
            const float  y = r.getCentreY() - v * r.getHeight() * 0.42f;
            if (i == 0) loop.startNewSubPath (x, y);
            else        loop.lineTo (x, y);
        }
        const float w = live ? juce::jmax (0.6f, std::abs (l.depth)) : 0.6f;
        g.setColour (p.orange.withAlpha (w));
        g.strokePath (loop, juce::PathStrokeType (1.6f));

        // The steps beyond the loop are still there to edit, just not played —
        // drawn dimmed and stepped, read straight off the table so a drag on
        // them still shows.
        g.setColour (p.text.withAlpha (0.3f));
        const float colW = r.getWidth() / lfo::customSteps;
        for (int s = len; s < lfo::customSteps; ++s)
        {
            const float v = juce::jlimit (-1.0f, 1.0f, proc.lfoShape[s].load());
            const float y = r.getCentreY() - v * r.getHeight() * 0.42f;
            g.drawLine (r.getX() + s * colW, y, r.getX() + (s + 1) * colW, y, 1.6f);
        }

        // The marker: a bar with a grab notch top and bottom, so it reads as
        // something to pull rather than one more divider.
        g.setColour (p.orange.withAlpha (0.85f));
        g.drawLine (markX, r.getY(), markX, r.getBottom(), 2.0f);
        g.fillRect (markX - 3.0f, r.getY(), 6.0f, 4.0f);
        g.fillRect (markX - 3.0f, r.getBottom() - 4.0f, 6.0f, 4.0f);

        if (! live)
            return;

        const double t = phase - std::floor (phase);
        const float dx = r.getX() + (float) t * (markX - r.getX());
        const float dy = r.getCentreY() - l.valueAt (t) * r.getHeight() * 0.42f;
        g.setColour (p.orange.withAlpha (0.35f));
        g.drawLine (dx, r.getY(), dx, r.getBottom(), 1.0f);
        g.setColour (p.orange);
        g.fillEllipse (dx - 3.0f, dy - 3.0f, 6.0f, 6.0f);
        return;
    }

    // How much of the LFO to show. One cycle for the periodic shapes, because
    // "where am I in the cycle" is the question a dot on a sine answers.
    //
    // Sample & hold gets four, and has to: its cycle is one held value, so a
    // one-cycle window draws a flat line — truthfully, and uselessly. Its shape
    // is not in a period, it emerges across several, so the window is anchored
    // to the last four *actual* holds rather than to an arbitrary four. That
    // matters because the held value is a hash of the cycle index: draw cycles
    // 0-3 while the audio is on cycle 97 and the picture would be a plausible
    // random pattern that is not the one being played.
    const bool  steppy = l.shape == lfo::SampleHold;
    const double span  = steppy ? 4.0 : 1.0;
    const double start = steppy ? std::floor (phase) - 3.0 : 0.0;

    // Drawn from `lfo::Lfo::valueAt` itself, so the picture is the generator's
    // own output rather than a second opinion on what a sine looks like — the
    // same reason `EqBands` takes its curve from the audio path's coefficients.
    juce::Path wave;
    const int steps = juce::jmax (2, (int) r.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const double u = (double) i / steps;
        const float  v = l.valueAt (start + u * span);
        const float  x = r.getX() + (float) u * r.getWidth();
        const float  y = r.getCentreY() - v * r.getHeight() * 0.42f;
        if (i == 0) wave.startNewSubPath (x, y);
        else        wave.lineTo (x, y);
    }

    // Depth scales the drawing's opacity rather than its height: at a glance
    // what matters is whether this is doing anything, and a shape squashed to a
    // flat line would be indistinguishable from the wrong shape. (The drawn
    // shape returns above, so this is only the periodic ones.)
    const float weight = live ? juce::jlimit (0.25f, 1.0f, std::abs (l.depth)) : 0.18f;
    g.setColour ((live ? p.orange : p.text).withAlpha (weight));
    g.strokePath (wave, juce::PathStrokeType (1.6f));

    if (! live)
        return;

    // The dot rides the wave at the phase the audio thread last used, so a
    // synced LFO's dot is where the bar says it is — including after a host
    // loop, which is the whole reason that phase is derived rather than counted.
    // On the stepped window it lands in the last quarter, on the hold currently
    // sounding, with the three before it to its left.
    const double u = (phase - start) / span;
    const float dx = r.getX() + (float) u * r.getWidth();
    const float dy = r.getCentreY() - l.valueAt (phase) * r.getHeight() * 0.42f;

    g.setColour (p.orange.withAlpha (0.35f));
    g.drawLine (dx, r.getY(), dx, r.getBottom(), 1.0f);
    g.setColour (p.orange);
    g.fillEllipse (dx - 3.0f, dy - 3.0f, 6.0f, 6.0f);
}

void XyPad::paint (juce::Graphics& g)
{
    const auto& p = ui303::palette (proc.uiSkin.load());
    const auto r = padArea();
    if (r.getWidth() <= 1 || r.getHeight() <= 1)
        return;

    const bool held = onVal != nullptr && onVal->load() >= 0.5f;
    const float x = xVal != nullptr ? xVal->load() : 0.0f;
    const float y = yVal != nullptr ? yVal->load() : 0.0f;

    const int mode = proc.apvts.getRawParameterValue ("padmode") != nullptr
                   ? (int) proc.apvts.getRawParameterValue ("padmode")->load()
                   : macropad::Acid;
    const auto& spec = macropad::spec (mode);

    // --- the pad itself ------------------------------------------------------
    g.setColour (p.cellOff.darker (0.5f));
    g.fillRect (r);

    g.setColour (p.outline.withAlpha (0.45f));
    g.drawLine ((float) r.getCentreX(), (float) r.getY(),
                (float) r.getCentreX(), (float) r.getBottom(), 1.0f);
    g.drawLine ((float) r.getX(), (float) r.getCentreY(),
                (float) r.getRight(), (float) r.getCentreY(), 1.0f);

    g.setColour (p.outline.withAlpha (0.7f));
    g.drawRect (r, 1);

    const float hx = (float) r.getX() + (x * 0.5f + 0.5f) * (float) r.getWidth();
    const float hy = (float) r.getBottom() - (y * 0.5f + 0.5f) * (float) r.getHeight();

    // Crosshairs out to the edges, so the two axis values can be read off the
    // pad at a glance instead of only from the numbers beside it.
    g.setColour ((held ? p.orange : p.outline).withAlpha (held ? 0.35f : 0.25f));
    g.drawLine (hx, (float) r.getY(), hx, (float) r.getBottom(), 1.0f);
    g.drawLine ((float) r.getX(), hy, (float) r.getRight(), hy, 1.0f);

    // Sparks, under the handle so the handle always reads clearly through them.
    // Clipped to the square: they are thrown outward and would otherwise land in
    // the readout column beside it.
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (r);

        for (const auto& s : sparks)
        {
            if (s.life <= 0.0f)
                continue;

            // Squared, so a spark holds its brightness most of the way and then
            // goes quickly — a linear fade reads as a smear that never quite
            // leaves.
            const float a = s.life * s.life;
            g.setColour (p.orange.withAlpha (a * 0.9f));
            g.fillEllipse (s.x - s.size, s.y - s.size, s.size * 2.0f, s.size * 2.0f);
        }
    }

    // Held is the state that matters: it is the difference between a pad that
    // is colouring the sound and one that is only remembering where it was left.
    const float rad = held ? 6.0f : 4.5f;
    g.setColour (held ? p.orange : p.outline.withAlpha (0.8f));
    g.fillEllipse (hx - rad, hy - rad, rad * 2.0f, rad * 2.0f);

    if (held)
    {
        g.setColour (p.orange.withAlpha (0.45f));
        g.drawEllipse (hx - rad * 2.2f, hy - rad * 2.2f, rad * 4.4f, rad * 4.4f, 1.5f);
    }

    // --- the readout column --------------------------------------------------
    auto col = getLocalBounds().reduced (margin).withTrimmedLeft (r.getWidth() + 10);
    if (col.getWidth() < 40)
        return;

    // Percentages rather than the destination values: one axis moves up to three
    // parameters, so there is no single number to print, and what the gesture is
    // worth is how far it has been pushed.
    const auto axis = [&] (juce::Rectangle<int> box, const juce::String& label, float v)
    {
        g.setColour (p.text.withAlpha (0.75f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (label, box.removeFromTop (12), juce::Justification::centredLeft);

        g.setColour (held && v != 0.0f ? p.orange : p.text.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ((v >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %",
                    box.removeFromTop (13), juce::Justification::centredLeft);
    };

    axis (col.removeFromTop (25), juce::String ("X  ") + spec.xLabel, x);
    col.removeFromTop (4);
    axis (col.removeFromTop (25), juce::String ("Y  ") + spec.yLabel, y);
}

//==============================================================================
// PatternKeys

PatternKeys::PatternKeys (BP303AudioProcessor& p, Role r) : proc (p), role (r)
{
    viewBank = juce::jlimit (0, numBanks - 1, current() / keysPerBank);
    startTimerHz (8);
}

int PatternKeys::current() const
{
    return role == Role::Bass ? proc.getCurrentBassPattern() : proc.getCurrentDrumPattern();
}

int PatternKeys::pending() const
{
    return role == Role::Bass ? proc.getPendingBassPattern() : proc.getPendingDrumPattern();
}

void PatternKeys::request (int idx)
{
    if (role == Role::Bass) proc.requestBassPattern (idx);
    else                    proc.requestDrumPattern (idx);
}

void PatternKeys::timerCallback()
{
    const int cur = current();
    const bool blinking = pending() != cur;

    // `wasBlinking` earns one last frame when a queued switch lands, so the
    // blink ring is cleared rather than left frozen on the key.
    if (! blinking && ! wasBlinking && cur == shownCurrent && viewBank == shownBank)
        return;

    blinkOn      = blinking && ! blinkOn;
    wasBlinking  = blinking;
    shownCurrent = cur;
    shownBank    = viewBank;
    repaint();
}

void PatternKeys::paint (juce::Graphics& g)
{
    const auto& p = ui303::palette (proc.uiSkin.load());
    const int cur = current();
    const int pend = pending();
    auto area = getLocalBounds();

    // --- A/B/C bank row ---
    auto bankRow = area.removeFromTop (bankRowH);
    const int bankW = bankRow.getWidth() / numBanks;
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (int b = 0; b < numBanks; ++b)
    {
        auto cell = bankRow.removeFromLeft (b == numBanks - 1 ? bankRow.getWidth() : bankW)
                        .reduced (2).toFloat();
        const bool isView = b == viewBank;
        // The viewed bank is raised out of the panel; the *current* bank is the
        // one ringed in the accent below, so this must not use the accent itself.
        g.setColour (isView ? (p.flatControls ? p.lcdBg
                                              : p.retro ? juce::Colour (0xff5e5e68)
                                                        : juce::Colour (0xff3a3a36))
                            : p.cellOff);
        g.fillRoundedRectangle (cell, 4.0f);
        if (cur / keysPerBank == b)
        {
            g.setColour (p.orange);
            g.drawRoundedRectangle (cell, 4.0f, 1.6f);
        }
        g.setColour (isView ? (p.flatControls ? p.lcdText
                                              : p.retro ? p.title
                                                        : juce::Colour (0xfff2f0ea))
                            : p.text);
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + b)),
                    cell, juce::Justification::centred);
    }

    // --- 3x3 key grid ---
    area.removeFromTop (gap);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const int cellW = area.getWidth() / cols;
    const int cellH = area.getHeight() / rows;
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const int k = row * cols + col;
            const int idx = viewBank * keysPerBank + k;
            const bool isCurrent = idx == cur;
            auto cell = juce::Rectangle<int> (area.getX() + col * cellW, area.getY() + row * cellH,
                                              cellW, cellH).reduced (3).toFloat();

            if (isCurrent && p.retro)
            {
                g.setColour (p.orange.withAlpha (0.35f));
                g.fillRoundedRectangle (cell.expanded (2.0f), 6.0f);
            }
            g.setColour (isCurrent ? p.orange : p.cellOff);
            g.fillRoundedRectangle (cell, 5.0f);
            if (p.retro)
            {
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (cell, 5.0f, 0.8f);
            }
            if (idx == pend && pend != cur && blinkOn)
            {
                g.setColour (p.red);
                g.drawRoundedRectangle (cell.reduced (1.0f), 5.0f, 2.0f);
            }
            g.setColour (isCurrent ? juce::Colour (0xff2a2a26) : p.text);
            g.drawText (juce::String (k + 1), cell, juce::Justification::centred);
        }
    }
}

void PatternKeys::mouseDown (const juce::MouseEvent& e)
{
    auto area = getLocalBounds();

    auto bankRow = area.removeFromTop (bankRowH);
    const int bankW = bankRow.getWidth() / numBanks;
    for (int b = 0; b < numBanks; ++b)
    {
        auto cell = bankRow.removeFromLeft (b == numBanks - 1 ? bankRow.getWidth() : bankW);
        if (cell.contains (e.getPosition()))
        {
            viewBank = b;
            repaint();
            return;
        }
    }

    area.removeFromTop (gap);
    const int cellW = area.getWidth() / cols;
    const int cellH = area.getHeight() / rows;
    if (cellW <= 0 || cellH <= 0)
        return;
    const int col = juce::jlimit (0, cols - 1, (e.x - area.getX()) / cellW);
    const int row = juce::jlimit (0, rows - 1, (e.y - area.getY()) / cellH);
    if (! area.contains (e.getPosition()))
        return;

    // Held until mouseUp: a press that turns into a drag copies the pattern
    // into the song instead of switching the live one.
    pressedSlot = viewBank * keysPerBank + row * cols + col;
    dragStarted = false;
}

void PatternKeys::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedSlot < 0 || dragStarted || e.getDistanceFromDragStart() < 5)
        return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
    if (container == nullptr)
        return;

    dragStarted = true;

    // A small chip showing the pattern being dragged, rather than an image of
    // the whole keypad.
    const auto& p = ui303::palette (proc.uiSkin.load());
    juce::Image chip (juce::Image::ARGB, 46, 22, true);
    {
        juce::Graphics g (chip);
        g.setColour (p.orange);
        g.fillRoundedRectangle (chip.getBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (juce::Colour (0xff2a2a26));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (SongList::slotName (pressedSlot), chip.getBounds(),
                    juce::Justification::centred);
    }

    container->startDragging (SongList::dragDescription (role == Role::Bass, pressedSlot),
                              this, juce::ScaledImage (chip), true);
}

void PatternKeys::mouseUp (const juce::MouseEvent&)
{
    const int slot = pressedSlot;
    pressedSlot = -1;

    if (slot < 0 || dragStarted)
    {
        dragStarted = false;
        return;
    }

    request (slot);
    repaint();
}

//==============================================================================
// HelpOverlay

HelpOverlay::HelpOverlay (BP303AudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (true, true);   // the tour blocks the UI beneath it
    setWantsKeyboardFocus (true);

    for (auto* b : { &backButton, &nextButton, &closeButton })
        addAndMakeVisible (*b);

    backButton.onClick  = [this] { showStep (current - 1); };
    nextButton.onClick  = [this] { showStep (current + 1); };
    closeButton.onClick = [this] { setVisible (false); };
}

void HelpOverlay::setSteps (std::vector<Step> newSteps)
{
    steps = std::move (newSteps);
    current = 0;
}

void HelpOverlay::start()
{
    if (steps.empty())
        return;

    setVisible (true);
    toFront (true);
    showStep (0);
    grabKeyboardFocus();
}

void HelpOverlay::showStep (int index)
{
    if (steps.empty())
        return;

    if (index >= (int) steps.size())
    {
        setVisible (false);   // past the last step: done
        return;
    }

    current = juce::jlimit (0, (int) steps.size() - 1, index);
    backButton.setEnabled (current > 0);
    nextButton.setButtonText (current == (int) steps.size() - 1 ? "DONE" : "NEXT");
    layoutCallout();
    repaint();
}

juce::AttributedString HelpOverlay::bodyText (int index) const
{
    juce::AttributedString text;
    if (index >= 0 && index < (int) steps.size())
        text.setText (steps[(size_t) index].body);
    text.setFont (juce::FontOptions (11.5f));
    text.setColour (juce::Colour (0xffd6d2ca));
    text.setJustification (juce::Justification::topLeft);
    text.setLineSpacing (1.5f);
    return text;
}

int HelpOverlay::bodyHeight (int index) const
{
    juce::TextLayout layout;
    layout.createLayout (bodyText (index), (float) (calloutW - 2 * innerPad));
    return (int) std::ceil (layout.getHeight());
}

juce::Rectangle<int> HelpOverlay::calloutBounds() const
{
    const int wanted = innerPad + titleH + 4 + bodyHeight (current)
                     + buttonGap + buttonH + innerPad;
    const int h = juce::jlimit (110, juce::jmax (110, getHeight() - 40), wanted);
    auto area = juce::Rectangle<int> (0, 0, calloutW, h);

    if (steps.empty() || steps[(size_t) current].target.isEmpty())
        return area.withCentre (getLocalBounds().getCentre());

    const auto target = steps[(size_t) current].target;
    int x = target.getCentreX() - calloutW / 2;
    int y = target.getBottom() + pad;

    if (y + h > getHeight() - 10)
        y = target.getY() - h - pad;      // no room below: go above
    if (y < 10)
        y = (getHeight() - h) / 2;        // nor above: centre vertically

    return area.withPosition (juce::jlimit (10, juce::jmax (10, getWidth() - calloutW - 10), x),
                              juce::jlimit (10, juce::jmax (10, getHeight() - h - 10), y));
}

void HelpOverlay::calloutParts (juce::Rectangle<int>& title,
                                juce::Rectangle<int>& body,
                                juce::Rectangle<int>& buttons) const
{
    auto inner = calloutBounds().reduced (innerPad);
    title = inner.removeFromTop (titleH);
    inner.removeFromTop (4);
    buttons = inner.removeFromBottom (buttonH);
    inner.removeFromBottom (buttonGap);
    body = inner;
}

void HelpOverlay::layoutCallout()
{
    juce::Rectangle<int> title, body, row;
    calloutParts (title, body, row);

    closeButton.setBounds (row.removeFromLeft (62));
    nextButton.setBounds (row.removeFromRight (62));
    row.removeFromRight (6);
    backButton.setBounds (row.removeFromRight (62));
}

void HelpOverlay::resized()
{
    layoutCallout();
}

bool HelpOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey)
    {
        showStep (current + 1);
        return true;
    }
    if (key == juce::KeyPress::leftKey)
    {
        showStep (current - 1);
        return true;
    }
    return false;
}

void HelpOverlay::paint (juce::Graphics& g)
{
    if (steps.empty())
        return;

    const auto& pal = ui303::palette (proc.uiSkin.load());
    const auto& step = steps[(size_t) current];

    // Dim everything, punching a hole over the part being explained.
    juce::Path shade;
    shade.addRectangle (getLocalBounds());
    if (! step.target.isEmpty())
    {
        shade.setUsingNonZeroWinding (false);
        shade.addRoundedRectangle (step.target.toFloat(), 5.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.74f));
    g.fillPath (shade);

    if (! step.target.isEmpty())
    {
        g.setColour (pal.orange);
        g.drawRoundedRectangle (step.target.toFloat(), 5.0f, 2.0f);
    }

    // Callout
    const auto box = calloutBounds();
    auto boxf = box.toFloat();
    g.setColour (pal.flatControls ? pal.lcdBg : juce::Colour (0xff26231f));
    g.fillRoundedRectangle (boxf, 7.0f);
    g.setColour (pal.orange);
    g.drawRoundedRectangle (boxf, 7.0f, 1.6f);

    juce::Rectangle<int> titleRow, bodyArea, buttonRow;
    calloutParts (titleRow, bodyArea, buttonRow);

    g.setColour (pal.orange);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (step.title, titleRow, juce::Justification::topLeft);

    g.setColour (pal.text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String (current + 1) + " / " + juce::String ((int) steps.size()),
                titleRow, juce::Justification::topRight);

    bodyText (current).draw (g, bodyArea.toFloat());
}

//==============================================================================
// SongTransport

bool SongTransport::isRunning() const
{
    return proc.isSongPlaying() || proc.isHostSynced();
}

void SongTransport::timerCallback()
{
    // The mode can change from anywhere (automation, the mode switch), so
    // repaint on either the transport or the enabled state moving.
    const bool now = isRunning() || proc.isSongMode();
    if (lastRunning != now)
    {
        lastRunning = now;
        repaint();
    }
}

juce::Rectangle<int> SongTransport::buttonArea (int index) const
{
    const int w = getWidth() / numButtons;
    return { index * w, 0, w, getHeight() };
}

void SongTransport::paint (juce::Graphics& g)
{
    const auto& p = ui303::palette (proc.uiSkin.load());
    const bool running = isRunning();

    // Only live in song mode — the RUN button drives Ext/Seq.
    const float enabled = proc.isSongMode() ? 1.0f : 0.35f;

    for (int i = 0; i < numButtons; ++i)
    {
        auto cell = buttonArea (i).reduced (2, 1).toFloat();
        const bool lit = proc.isSongMode() && ((i == Play && running)
                                               || (i == Stop && ! running));

        g.setColour ((lit ? p.orange : p.cellOff).withMultipliedAlpha (enabled));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (cell, 3.0f, 0.8f);

        // icons, drawn to a nominal 10px box in the middle of the button
        const auto c = cell.getCentre();
        const float s = 4.6f;
        g.setColour ((lit ? juce::Colour (0xff2a2a26) : p.text).withMultipliedAlpha (enabled));

        juce::Path path;
        auto triangle = [&path] (float cx, float cy, float size, bool pointsRight)
        {
            const float dir = pointsRight ? 1.0f : -1.0f;
            path.addTriangle (cx - dir * size * 0.6f, cy - size,
                              cx - dir * size * 0.6f, cy + size,
                              cx + dir * size * 0.9f, cy);
        };

        switch (i)
        {
            case Rewind:
                triangle (c.x - s * 0.55f, c.y, s, false);
                triangle (c.x + s * 0.75f, c.y, s, false);
                break;
            case Play:
                triangle (c.x, c.y, s, true);
                break;
            case Stop:
                path.addRectangle (c.x - s * 0.8f, c.y - s * 0.8f, s * 1.6f, s * 1.6f);
                break;
            case Forward:
                triangle (c.x - s * 0.75f, c.y, s, true);
                triangle (c.x + s * 0.55f, c.y, s, true);
                break;
            default:
                break;
        }
        g.fillPath (path);
    }
}

void SongTransport::mouseDown (const juce::MouseEvent& e)
{
    if (! proc.isSongMode())
        return;

    int hit = -1;
    for (int i = 0; i < numButtons; ++i)
        if (buttonArea (i).contains (e.getPosition()))
            hit = i;

    if (hit < 0)
        return;

    switch (hit)
    {
        case Play:
            proc.startSong();
            break;

        case Stop:
            proc.stopSong();
            break;

        case Rewind:
        case Forward:
        {
            const int count = proc.song.getCount();
            if (count == 0)
                break;

            const int current = juce::jmax (0, proc.getSongStep());
            const int target = juce::jlimit (0, count - 1,
                                             current + (hit == Forward ? 1 : -1));
            proc.jumpSongToStep (target);
            if (onCue)
                onCue();
            break;
        }

        default:
            break;
    }

    repaint();
}

//==============================================================================
// SongList

namespace
{
    // Which lines some row before `upTo` has already named. Until a line is
    // named, a dash on it is a *leading* hold — nothing to carry on from — and
    // the engine keeps that line silent (see the note in processBlock), so the
    // list has to draw those rows as inactive rather than as playing.
    void namedBefore (const SongPlayer& song, int upTo, bool& bass, bool& drum)
    {
        bass = drum = false;
        upTo = juce::jmin (upTo, song.getCount());   // getStep() past the end reads as slot 0

        for (int row = 0; row < upTo; ++row)
        {
            const auto s = song.getStep (row);
            bass = bass || s.bassSlot != SongPlayer::hold;
            drum = drum || s.drumSlot != SongPlayer::hold;
        }
    }
}

SongList::SongList (BP303AudioProcessor& p) : proc (p)
{
    startTimerHz (25);
}

juce::String SongList::slotName (int slot)
{
    if (slot == SongPlayer::hold)
        return "-";

    slot = juce::jlimit (0, BP303AudioProcessor::numBassPatterns - 1, slot);
    return juce::String::charToString ((juce::juce_wchar) ('A' + slot / 9))
         + juce::String (slot % 9 + 1);
}

void SongList::columnRects (juce::Rectangle<int> row, juce::Rectangle<int>* out)
{
    // #, BASS, DRUM, REP, B-mute, D-mute
    const float weights[numCols] = { 0.11f, 0.24f, 0.24f, 0.15f, 0.13f, 0.13f };
    const int total = row.getWidth();
    int x = row.getX();

    for (int c = 0; c < numCols; ++c)
    {
        const int w = c == numCols - 1 ? row.getRight() - x
                                       : (int) ((float) total * weights[c]);
        out[c] = { x, row.getY(), w, row.getHeight() };
        x += w;
    }
}

SongList::Col SongList::columnAt (int x) const
{
    juce::Rectangle<int> cols[numCols];
    columnRects (getLocalBounds().withHeight (rowH), cols);

    for (int c = 0; c < numCols; ++c)
        if (x >= cols[c].getX() && x < cols[c].getRight())
            return (Col) ((int) Col::Index + c);

    return Col::None;
}

int SongList::visibleRows() const
{
    return juce::jmax (1, (getHeight() - headerH) / rowH);
}

int SongList::rowAt (int y) const
{
    if (y < headerH)
        return -1;

    const int row = scrollTop + (y - headerH) / rowH;
    return row < proc.song.getCount() ? row : -1;
}

void SongList::scrollTo (int row)
{
    const int maxTop = juce::jmax (0, proc.song.getCount() - visibleRows());
    if (row < scrollTop)
        scrollTop = row;
    else if (row >= scrollTop + visibleRows())
        scrollTop = row - visibleRows() + 1;
    scrollTop = juce::jlimit (0, maxTop, scrollTop);
}

void SongList::select (int row)
{
    selected = row == selected ? -1 : row;   // clicking the selected row clears it
    if (selected >= 0)
        scrollTo (selected);
    changed();
}

void SongList::changed()
{
    if (onSongChanged)
        onSongChanged();
    repaint();
}

void SongList::timerCallback()
{
    // Edge auto-scroll while a pattern is being dragged over the list.
    if (dragScroll != 0)
    {
        const int maxTop = juce::jmax (0, proc.song.getCount() - visibleRows());
        scrollTop = juce::jlimit (0, maxTop, scrollTop + dragScroll);
    }

    // Follow the playhead, but only when it moves, so a manual scroll sticks
    // until the song advances.
    const int playing = proc.getSongStep();
    if (playing != lastPlaying)
    {
        lastPlaying = playing;
        if (playing >= 0)
            scrollTo (playing);
    }

    const auto now = liveView();
    if (! (now == shown))
        repaint();
}

SongList::View SongList::liveView() const
{
    View v;
    v.playing    = proc.getSongStep();
    v.count      = proc.song.getCount();
    v.scrollTop  = scrollTop;
    v.selected   = selected;
    v.dropRow    = dropRow;
    v.dropInsert = dropInsert;

    juce::uint64 h = 1469598103934665603ull;
    const auto mix = [&h] (juce::uint64 value) { h = (h ^ value) * 1099511628211ull; };

    // The leading-hold dimming depends on rows above the viewport, so fold that
    // in too — otherwise naming a line up there wouldn't repaint what's visible.
    bool bassNamed = false, drumNamed = false;
    namedBefore (proc.song, scrollTop, bassNamed, drumNamed);
    mix ((juce::uint64) ((bassNamed ? 1 : 0) | (drumNamed ? 2 : 0)));

    const int last = juce::jmin (v.count, scrollTop + visibleRows());
    for (int row = juce::jmax (0, scrollTop); row < last; ++row)
    {
        const auto step = proc.song.getStep (row);
        mix ((juce::uint64) (step.bassSlot + 1));
        mix ((juce::uint64) (step.drumSlot + 1));
        mix ((juce::uint64) step.repeats);
        mix ((juce::uint64) ((step.bassMute ? 1 : 0) | (step.drumMute ? 2 : 0)));
    }
    v.rows = h;
    return v;
}

void SongList::paint (juce::Graphics& g)
{
    shown = liveView();

    const auto& p = ui303::palette (proc.uiSkin.load());
    const int n = proc.song.getCount();
    const int playing = proc.getSongStep();
    auto area = getLocalBounds();

    juce::Rectangle<int> cols[numCols];

    // --- column captions ---
    auto hdr = area.removeFromTop (headerH);
    columnRects (hdr, cols);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.setColour (p.text.withAlpha (0.65f));
    const char* caps[numCols] = { "#", "BASS", "DRUM", "REP", "B", "D" };
    for (int c = 0; c < numCols; ++c)
        g.drawText (caps[c], cols[c], juce::Justification::centred);

    if (n == 0)
    {
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (juce::Colour (0xffa9a49c));
        g.drawText ("Drag and drop patterns", area, juce::Justification::centred);
    }

    bool bassNamed = false, drumNamed = false;
    namedBefore (proc.song, scrollTop, bassNamed, drumNamed);

    const int last = juce::jmin (n, scrollTop + visibleRows());
    for (int row = scrollTop; row < last; ++row)
    {
        const auto step = proc.song.getStep (row);

        // A row naming a line counts for that row itself, not just later ones.
        bassNamed = bassNamed || step.bassSlot != SongPlayer::hold;
        drumNamed = drumNamed || step.drumSlot != SongPlayer::hold;
        auto rowArea = juce::Rectangle<int> (area.getX(),
                                             area.getY() + (row - scrollTop) * rowH,
                                             area.getWidth(), rowH);
        columnRects (rowArea.reduced (0, 1), cols);

        const bool isPlaying = row == playing;
        const bool isSelected = row == selected;

        if (isPlaying)
        {
            g.setColour (p.orange.withAlpha (0.30f));
            g.fillRoundedRectangle (rowArea.reduced (1, 1).toFloat(), 3.0f);
        }
        if (isSelected)
        {
            g.setColour (p.red);
            g.drawRoundedRectangle (rowArea.reduced (1, 1).toFloat(), 3.0f, 1.4f);
        }

        // index — also the row's jump handle
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (isPlaying ? p.orange : p.text.withAlpha (0.7f));
        g.drawText (juce::String (row + 1), cols[0], juce::Justification::centred);

        // pattern + repeat cells, LCD-styled like the pitch row
        // `named` false means a leading hold: the dash is faded to show the line
        // has no pattern yet and so isn't sounding.
        auto cell = [&] (juce::Rectangle<int> r, const juce::String& text,
                         bool named = true)
        {
            auto box = r.reduced (2, 2).toFloat();
            g.setColour (p.pitchBg);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (named ? p.pitchText : p.pitchText.withAlpha (0.35f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (text, r, juce::Justification::centred);
        };

        cell (cols[1], slotName (step.bassSlot), bassNamed);
        cell (cols[2], slotName (step.drumSlot), drumNamed);
        cell (cols[3], juce::String (step.repeats));

        // Play pads — lit means the line sounds on this step, so a row reads as
        // "both on" at a glance and clicking one drops that line for a
        // breakdown. Lit-for-on matches the LED convention the toggle buttons
        // use; the stored flag stays a *mute* (default false), so it is still
        // "off by exception" underneath and old songs load unchanged.
        auto playPad = [&] (juce::Rectangle<int> r, bool plays, const char* label)
        {
            auto box = r.reduced (5, 3).toFloat();
            g.setColour (plays ? p.orange : p.cellOff);
            g.fillRoundedRectangle (box, 3.0f);
            // An unlit pad still needs an edge, or an empty cell reads as blank
            // space rather than as a control you can click.
            g.setColour (p.text.withAlpha (plays ? 0.35f : 0.5f));
            g.drawRoundedRectangle (box, 3.0f, 1.0f);
            g.setColour (plays ? juce::Colour (0xff2a2a26) : p.text.withAlpha (0.45f));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (label, r, juce::Justification::centred);
        };

        // An un-named line can't sound whatever the flag says, so show it unlit.
        playPad (cols[4], bassNamed && ! step.bassMute, "B");
        playPad (cols[5], drumNamed && ! step.drumMute, "D");
    }

    // --- feedback for a pattern being dragged in ---
    if (dropRow >= 0)
    {
        const int offset = juce::jlimit (0, visibleRows(), dropRow - scrollTop);

        if (dropInsert)
        {
            const int y = area.getY() + offset * rowH;
            g.setColour (p.orange);
            g.fillRect (area.getX() + 2, y - 1, area.getWidth() - 4, 2);
        }
        else
        {
            auto rowArea = juce::Rectangle<int> (area.getX(), area.getY() + offset * rowH,
                                                 area.getWidth(), rowH);
            columnRects (rowArea.reduced (0, 1), cols);
            g.setColour (p.orange);
            g.drawRoundedRectangle (cols[dropIsBass ? 1 : 2].reduced (2, 2).toFloat(),
                                    3.0f, 2.0f);
        }
    }
}

void SongList::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row < 0)
    {
        select (-1);
        return;
    }

    const Col col = columnAt (e.x);
    auto step = proc.song.getStep (row);

    switch (col)
    {
        case Col::Index:
            // drop the playhead here (ignored while host-synced)
            proc.jumpSongToStep (row);
            selected = row;
            changed();
            return;

        case Col::BassMute:
            step.bassMute = ! step.bassMute;
            proc.song.setStep (row, step);
            changed();
            return;

        case Col::DrumMute:
            step.drumMute = ! step.drumMute;
            proc.song.setStep (row, step);
            changed();
            return;

        case Col::Bass:
        case Col::Drum:
        case Col::Reps:
            // start a vertical drag on the value, and select the row so the
            // pattern keypads assign into it
            dragCol = col;
            dragRow = row;
            dragStartY = e.y;
            dragStartValue = col == Col::Bass ? step.bassSlot
                           : col == Col::Drum ? step.drumSlot
                                              : step.repeats;
            if (selected != row)
            {
                selected = row;
                changed();
            }
            else
            {
                select (row);   // second click on the same row clears the arm
            }
            return;

        case Col::None:
        default:
            return;
    }
}

void SongList::mouseDrag (const juce::MouseEvent& e)
{
    if (dragCol == Col::None || dragRow < 0 || dragRow >= proc.song.getCount())
        return;

    const int delta = (dragStartY - e.y) / 6;
    if (delta == 0 && e.getDistanceFromDragStartY() != 0)
        return;

    auto step = proc.song.getStep (dragRow);

    if (dragCol == Col::Reps)
    {
        step.repeats = juce::jlimit (1, SongPlayer::maxRepeats, dragStartValue + delta);
    }
    else
    {
        const int slot = juce::jlimit (SongPlayer::hold,
                                       BP303AudioProcessor::numBassPatterns - 1,
                                       dragStartValue + delta);
        if (dragCol == Col::Bass) step.bassSlot = slot;
        else                      step.drumSlot = slot;
    }

    proc.song.setStep (dragRow, step);
    changed();
}

void SongList::mouseUp (const juce::MouseEvent&)
{
    dragCol = Col::None;
    dragRow = -1;
}

void SongList::mouseWheelMove (const juce::MouseEvent&,
                               const juce::MouseWheelDetails& wheel)
{
    const int maxTop = juce::jmax (0, proc.song.getCount() - visibleRows());
    scrollTop = juce::jlimit (0, maxTop, scrollTop - juce::roundToInt (wheel.deltaY * 6.0f));
    repaint();
}

juce::String SongList::dragDescription (bool bass, int slot)
{
    return juce::String ("BP303PAT:") + (bass ? "B" : "D") + juce::String (slot);
}

bool SongList::parseDrag (const juce::var& description, bool& bass, int& slot)
{
    const auto text = description.toString();
    if (! text.startsWith ("BP303PAT:"))
        return false;

    const auto body = text.fromFirstOccurrenceOf (":", false, false);
    bass = body.startsWithChar ('B');
    slot = body.substring (1).getIntValue();
    return slot >= 0 && slot < BP303AudioProcessor::numBassPatterns;
}

bool SongList::isInterestedInDragSource (const SourceDetails& details)
{
    bool bass = false;
    int slot = 0;
    return parseDrag (details.description, bass, slot);
}

void SongList::updateDropTarget (const SourceDetails& details)
{
    int slot = 0;
    parseDrag (details.description, dropIsBass, slot);

    const int n = proc.song.getCount();
    const int y = details.localPosition.y;

    if (y < headerH)
    {
        dropRow = 0;
        dropInsert = true;
    }
    else
    {
        const int index = scrollTop + (y - headerH) / rowH;
        if (index >= n)
        {
            dropRow = n;            // past the last row: append
            dropInsert = true;
        }
        else
        {
            dropRow = index;
            dropInsert = (y - headerH) % rowH < insertBand;
        }
    }

    // Nudge the list when hovering near either edge, so a drag can reach rows
    // that are scrolled out of view.
    dragScroll = y < headerH + rowH ? -1
               : y > getHeight() - rowH ? 1
                                        : 0;
    repaint();
}

void SongList::itemDragEnter (const SourceDetails& details) { updateDropTarget (details); }
void SongList::itemDragMove  (const SourceDetails& details) { updateDropTarget (details); }

void SongList::itemDragExit (const SourceDetails&)
{
    dropRow = -1;
    dragScroll = 0;
    repaint();
}

void SongList::itemDropped (const SourceDetails& details)
{
    bool bass = true;
    int slot = 0;
    const bool ok = parseDrag (details.description, bass, slot);

    const int row = dropRow;
    const bool insert = dropInsert;
    dropRow = -1;
    dragScroll = 0;

    if (! ok)
    {
        repaint();
        return;
    }

    if (insert)
    {
        // The other line holds, so dragging a run of bass patterns arranges the
        // bass without disturbing the drums, and vice versa.
        SongPlayer::Step step;
        step.bassSlot = bass ? slot : SongPlayer::hold;
        step.drumSlot = bass ? SongPlayer::hold : slot;

        const int placed = proc.song.insertStep (row, step);
        if (placed >= 0)
        {
            selected = placed;
            scrollTo (placed);
        }
    }
    else if (row >= 0 && row < proc.song.getCount())
    {
        auto step = proc.song.getStep (row);
        if (bass) step.bassSlot = slot;
        else      step.drumSlot = slot;
        proc.song.setStep (row, step);
        selected = row;
    }

    changed();
}

void SongList::followPlayhead()
{
    const int playing = proc.getSongStep();
    if (playing >= 0)
    {
        lastPlaying = playing;
        scrollTo (playing);
        repaint();
    }
}

void SongList::insertRow()
{
    // A new row defaults to what is playing now, so building a song is mostly
    // "get a pair sounding, press INS".
    SongPlayer::Step step;
    step.bassSlot = proc.getCurrentBassPattern();
    step.drumSlot = proc.getCurrentDrumPattern();

    const int at = selected < 0 ? proc.song.getCount() : selected + 1;
    const int placed = proc.song.insertStep (at, step);
    if (placed >= 0)
    {
        selected = placed;
        scrollTo (placed);
    }
    changed();
}

void SongList::deleteRow()
{
    if (selected < 0)
        return;

    proc.song.removeStep (selected);
    selected = juce::jmin (selected, proc.song.getCount() - 1);
    changed();
}

void SongList::duplicateRow()
{
    if (selected < 0)
        return;

    const int placed = proc.song.insertStep (selected + 1, proc.song.getStep (selected));
    if (placed >= 0)
    {
        selected = placed;
        scrollTo (placed);
    }
    changed();
}

void SongList::moveRow (int direction)
{
    const int to = selected + direction;
    if (selected < 0 || to < 0 || to >= proc.song.getCount())
        return;

    const auto a = proc.song.getStep (selected);
    proc.song.setStep (selected, proc.song.getStep (to));
    proc.song.setStep (to, a);
    selected = to;
    scrollTo (to);
    changed();
}

void SongList::clearSong()
{
    proc.song.clear();
    selected = -1;
    scrollTop = 0;
    changed();
}

//==============================================================================
// Editor

void BP303AudioProcessorEditor::Knob::init (juce::Component& parent,
                                            juce::AudioProcessorValueTreeState& apvts,
                                            const juce::String& paramId,
                                            const juce::String& text,
                                            int hotEnd,
                                            bool bipolar)
{
    parent.addAndMakeVisible (slider);
    this->paramId = paramId;
    // The look-and-feel draws every knob through one function and has no idea
    // which parameter it is looking at, so the hot end rides along as a property.
    // So does whether the value reads out from the centre or from the bottom,
    // and how far the XY pad is currently pushing it.
    slider.getProperties().set ("hotEnd", hotEnd);
    slider.getProperties().set ("bipolar", bipolar);
    att = std::make_unique<SliderAtt> (apvts, paramId, slider);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    parent.addAndMakeVisible (label);
}

void BP303AudioProcessorEditor::Knob::setBounds (juce::Rectangle<int> r)
{
    label.setBounds (r.removeFromBottom (13));
    slider.setBounds (r);
}

void BP303AudioProcessorEditor::Choice::init (juce::Component& parent,
                                              juce::AudioProcessorValueTreeState& apvts,
                                              const juce::String& paramId,
                                              const juce::String& text)
{
    parent.addAndMakeVisible (box);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    att = std::make_unique<ComboAtt> (apvts, paramId, box);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    parent.addAndMakeVisible (label);
}

void BP303AudioProcessorEditor::Choice::setBounds (juce::Rectangle<int> r)
{
    label.setBounds (r.removeFromBottom (13));
    box.setBounds (r.reduced (0, juce::jmax (0, (r.getHeight() - 24) / 2)));
}

void BP303AudioProcessorEditor::Switch::init (juce::Component& parent, BP303AudioProcessor& proc,
                                              juce::AudioProcessorValueTreeState& apvts,
                                              const juce::String& paramId,
                                              const juce::String& text)
{
    sw = std::make_unique<SegmentedSwitch> (proc, apvts, paramId);
    parent.addAndMakeVisible (*sw);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    parent.addAndMakeVisible (label);
}

void BP303AudioProcessorEditor::Switch::setBounds (juce::Rectangle<int> r)
{
    label.setBounds (r.removeFromBottom (13));
    sw->setBounds (r.reduced (0, juce::jmax (0, (r.getHeight() - 24) / 2)));
}

BP303AudioProcessorEditor::BP303AudioProcessorEditor (BP303AudioProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    // The key-colour dial is restored before the first paint, snapped rather
    // than faded — a window should open looking the way it was left, not play
    // an animation about it.
    ui303::setKeyHue (BP303AudioProcessor::loadGlobalKeyHue (0), true);

    look.setSkin (proc.uiSkin.load());
    setLookAndFeel (&look);

    // For the shift+arrow half of the easter egg. Hosts often keep arrow keys
    // for themselves, so the logo drag is the dependable way in.
    setWantsKeyboardFocus (true);

    hueFade.onTick = [this]
    {
        if (ui303::advanceKeyHueFade())
        {
            // Everything palette-driven picks the in-between colour up on the
            // repaint; the look-and-feel carries copies of a few of them, so it
            // is re-pushed each frame too. The full applySkin() — with its
            // walk of every child — waits until the colour lands.
            look.setSkin (proc.uiSkin.load());
            chrome.repaint();
            repaint();
        }
        else
        {
            hueFade.stopTimer();
            applySkin();
        }
    };

    // Everything else is a child of `content`, which the editor scales.
    addAndMakeVisible (content);
    content.onResized = [this] { layoutContent(); };

    // Added before any control, so the cached chrome stays at the back.
    content.addAndMakeVisible (chrome);
    chrome.onPaint = [this] (juce::Graphics& g) { paintContent (g); };

    // The skin picker has no control of its own — right-clicking the BADPACKETS
    // logo in the header opens it. Nothing is laid out over that corner, so the
    // click lands on `content` itself. A *left* press on the same legend starts
    // the key-colour drag: sweep sideways and the accent colour dials round the
    // wheel, one stop per few pixels, fading as it goes.
    content.onMouseDown = [this] (const juce::MouseEvent& e) {
        if (! ui303::skinMenuHotspot().contains (e.getPosition()))
            return;

        if (e.mods.isPopupMenu())
        {
            showSkinMenu();
        }
        else if (e.mods.isLeftButtonDown())
        {
            hueDragging = true;
            hueDragStartX = e.getPosition().x;
            hueDragStartStop = ui303::keyHue();
        }
    };
    content.onMouseDrag = [this] (const juce::MouseEvent& e) {
        if (hueDragging)
            setKeyHueTarget (hueDragStartStop
                             + (e.getPosition().x - hueDragStartX) / hueDragPixelsPerStop);
    };
    content.onMouseUp = [this] (const juce::MouseEvent&) {
        // saved once the sweep settles, not on every stop it passed through
        if (hueDragging)
            BP303AudioProcessor::saveGlobalKeyHue (ui303::keyHue());
        hueDragging = false;
    };

    wave.init (content, proc, proc.apvts, "wave", "WAVE");
    tuning.init (content, proc.apvts, "tuning", "TUNING");
    cutoff.init (content, proc.apvts, "cutoff", "CUT OFF");
    resonance.init (content, proc.apvts, "resonance", "RESONANCE", ui303::HotTop);
    envmod.init (content, proc.apvts, "envmod", "ENV MOD");
    attack.init (content, proc.apvts, "attack", "ATTACK");
    decay.init (content, proc.apvts, "decay", "DECAY");
    accent.init (content, proc.apvts, "accent", "ACCENT");
    volume.init (content, proc.apvts, "volume", "VOLUME");
    uniVoices.init (content, proc.apvts, "unisonvoices", "VOICES");
    uniDetune.init (content, proc.apvts, "unisondetune", "DETUNE");
    uniSpread.init (content, proc.apvts, "unisonspread", "SPREAD");

    playMode.init (content, proc, proc.apvts, "playmode", "PLAY MODE");
    content.addAndMakeVisible (runButton);
    runAtt = std::make_unique<ButtonAtt> (proc.apvts, "run", runButton);
    content.addAndMakeVisible (recButton);
    recAtt = std::make_unique<ButtonAtt> (proc.apvts, "rec", recButton);
    content.addAndMakeVisible (metroButton);
    metroAtt = std::make_unique<ButtonAtt> (proc.apvts, "metro", metroButton);
    intBpm.init (content, proc.apvts, "intbpm", "INT. BPM");
    shuffle.init (content, proc.apvts, "shuffle", "SHUFFLE");

    lengthSlider.setRange (1.0, 16.0, 1.0);
    lengthSlider.setValue (proc.sequencer.length.load(), juce::dontSendNotification);
    lengthSlider.onValueChange = [this] {
        proc.sequencer.length.store ((int) lengthSlider.getValue());
    };
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 28, 20);
    content.addAndMakeVisible (lengthSlider);
    lengthLabel.setText ("LENGTH", juce::dontSendNotification);
    lengthLabel.setJustificationType (juce::Justification::centred);
    lengthLabel.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    content.addAndMakeVisible (lengthLabel);

    // --- bass fx tabs: DIST / DELAY / FILTER / COMP / CHORUS / REVERB ---
    bassDistPage.addAndMakeVisible (distOn);
    distOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "diston", distOn);
    bassDistType.init (bassDistPage, proc.apvts, "bdisttype", "TYPE");
    bassDistLows.init (bassDistPage, proc.apvts, "bdistlows", "LOWS");

    distDrive.init (bassDriveGroup, proc.apvts, "distdrive", "DRIVE", ui303::HotTop);
    distColor.init (bassDriveGroup, proc.apvts, "distcolor", "COLOR");
    bassCrushBits.init (bassCrushGroup, proc.apvts, "bcrbits", "BITS", ui303::HotBottom);
    bassCrushRate.init (bassCrushGroup, proc.apvts, "bcrrate", "RATE", ui303::HotBottom);
    bassFoldAmt.init (bassFoldGroup, proc.apvts, "bfoldamt", "FOLD", ui303::HotTop);
    bassFoldSym.init (bassFoldGroup, proc.apvts, "bfoldsym", "SYM");
    bassRectAmt.init (bassRectGroup, proc.apvts, "brectamt", "AMOUNT", ui303::HotTop);
    bassRectTone.init (bassRectGroup, proc.apvts, "brecttone", "TONE");

    for (auto* g : { &bassDriveGroup, &bassCrushGroup, &bassFoldGroup, &bassRectGroup })
        bassDistPage.addAndMakeVisible (*g);
    bassDistType.box.onChange = [this] { updateDistGroups(); };

    bassDelayPage.addAndMakeVisible (delayOn);
    delayOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "delayon", delayOn);
    delayType.init (bassDelayPage, proc.apvts, "delaytype", "TYPE");
    delayTime.init (bassDelayPage, proc.apvts, "delaytime", "TIME");
    delayFb.init (bassDelayPage, proc.apvts, "delayfb", "FEEDBACK", ui303::HotTop);
    delayMix.init (bassDelayPage, proc.apvts, "delaymix", "MIX");

    bassFilterPage.addAndMakeVisible (bassFltOn);
    bassFltOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "bflton", bassFltOn);
    bassFltMode.init (bassFilterPage, proc, proc.apvts, "bfltmode", "MODE");
    bassFltCut.init (bassFilterPage, proc.apvts, "bfltcut", "CUTOFF");
    bassFltRes.init (bassFilterPage, proc.apvts, "bfltres", "RES", ui303::HotTop);
    bassFltEnv.init (bassFilterPage, proc.apvts, "bfltenv", "ENV");

    bassCompPage.addAndMakeVisible (bassCompOn);
    bassCompOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "bcompon", bassCompOn);
    bassCompThr.init (bassCompPage, proc.apvts, "bcompthr", "THRESH");
    bassCompRat.init (bassCompPage, proc.apvts, "bcomprat", "RATIO");
    bassCompMk.init (bassCompPage, proc.apvts, "bcompmk", "MAKEUP");

    bassChorusPage.addAndMakeVisible (bassChorusOn);
    bassChorusOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "bchron", bassChorusOn);
    bassChorusRate.init (bassChorusPage, proc.apvts, "bchrrate", "RATE");
    bassChorusDepth.init (bassChorusPage, proc.apvts, "bchrdepth", "DEPTH");
    bassChorusMix.init (bassChorusPage, proc.apvts, "bchrmix", "MIX");

    bassReverbPage.addAndMakeVisible (bassReverbOn);
    bassReverbOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "brevon", bassReverbOn);
    bassReverbSize.init (bassReverbPage, proc.apvts, "brevsize", "SIZE");
    bassReverbDamp.init (bassReverbPage, proc.apvts, "brevdamp", "DAMP");
    bassReverbMix.init (bassReverbPage, proc.apvts, "brevmix", "MIX");

    for (auto* pg : { &bassDistPage, &bassDelayPage, &bassFilterPage, &bassCompPage,
                      &bassChorusPage, &bassReverbPage })
        bassFx.addPage (*pg);
    content.addAndMakeVisible (bassFx);

    // --- drums (tabbed): MIX / TUNE / DECAY / BALANCE -------------------------
    static const char* laneIds[] = { "bdlvl", "sdlvl", "cplvl", "chlvl", "ohlvl" };
    static const char* laneTexts[] = { "BD", "SD", "CP", "CH", "OH" };
    for (int i = 0; i < 5; ++i)
        laneKnobs[i].init (drumMixPage, proc.apvts, laneIds[i], laneTexts[i]);
    drumVol.init (drumMixPage, proc.apvts, "drumvol", "DRUM VOL");

    // The pans and the SPREAD that scales them belong together, so SPREAD sits
    // here rather than on MIX with the levels.
    static const char* panIds[] = { "bdpan", "sdpan", "cppan", "chpan", "ohpan" };
    for (int i = 0; i < 5; ++i)
        panKnobs[i].init (drumBalancePage, proc.apvts, panIds[i], laneTexts[i],
                          ui303::HotNone, true);
    drumSpread.init (drumBalancePage, proc.apvts, "drumspread", "SPREAD");

    bdTune.init (drumTunePage, proc.apvts, "bdtune", "BD TUNE");
    sdTune.init (drumTunePage, proc.apvts, "sdtune", "SD TUNE");
    cpTune.init (drumTunePage, proc.apvts, "cptune", "CP TUNE");
    hatTune.init (drumTunePage, proc.apvts, "hattune", "HAT TUNE");

    bdDecay.init (drumDecayPage, proc.apvts, "bddecay", "BD DECAY");
    sdDecay.init (drumDecayPage, proc.apvts, "sddecay", "SD DECAY");
    chDecay.init (drumDecayPage, proc.apvts, "chdecay", "CH DECAY");
    ohDecay.init (drumDecayPage, proc.apvts, "ohdecay", "OH DECAY");

    for (auto* pg : { &drumMixPage, &drumTunePage, &drumDecayPage, &drumBalancePage })
        drums.addPage (*pg);
    content.addAndMakeVisible (drums);

    // Added after the section so it sits on top of it: KIT belongs to the panel
    // rather than to any one page, and the pages leave its column clear.
    kit.init (content, proc, proc.apvts, "kit", "KIT");

    // --- drum fx tabs: DIST / DELAY / FILTER / COMP / CHORUS / REVERB ---
    drumDistPage.addAndMakeVisible (drumDistOn);
    drumDistOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "ddiston", drumDistOn);
    drumDistType.init (drumDistPage, proc.apvts, "ddisttype", "TYPE");
    drumDistLows.init (drumDistPage, proc.apvts, "ddistlows", "LOWS");

    drumDrive.init (drumDriveGroup, proc.apvts, "drumdrive", "DRIVE", ui303::HotTop);
    drumDistColor.init (drumDriveGroup, proc.apvts, "ddistcolor", "COLOR");
    drumCrushBits.init (drumCrushGroup, proc.apvts, "dcrbits", "BITS", ui303::HotBottom);
    drumCrushRate.init (drumCrushGroup, proc.apvts, "dcrrate", "RATE", ui303::HotBottom);
    drumFoldAmt.init (drumFoldGroup, proc.apvts, "dfoldamt", "FOLD", ui303::HotTop);
    drumFoldSym.init (drumFoldGroup, proc.apvts, "dfoldsym", "SYM");
    drumRectAmt.init (drumRectGroup, proc.apvts, "drectamt", "AMOUNT", ui303::HotTop);
    drumRectTone.init (drumRectGroup, proc.apvts, "drecttone", "TONE");

    for (auto* g : { &drumDriveGroup, &drumCrushGroup, &drumFoldGroup, &drumRectGroup })
        drumDistPage.addAndMakeVisible (*g);
    drumDistType.box.onChange = [this] { updateDistGroups(); };

    updateDistGroups();

    drumDelayPage.addAndMakeVisible (drumDelayOn);
    drumDelayOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "ddelayon", drumDelayOn);
    drumDelayType.init (drumDelayPage, proc.apvts, "ddelaytype", "TYPE");
    drumDelayTime.init (drumDelayPage, proc.apvts, "ddelaytime", "TIME");
    drumDelayFb.init (drumDelayPage, proc.apvts, "ddelayfb", "FEEDBACK", ui303::HotTop);
    drumDelayMix.init (drumDelayPage, proc.apvts, "ddelaymix", "MIX");

    drumFilterPage.addAndMakeVisible (drumFltOn);
    drumFltOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "dflton", drumFltOn);
    drumFltMode.init (drumFilterPage, proc, proc.apvts, "dfltmode", "MODE");
    drumFltCut.init (drumFilterPage, proc.apvts, "dfltcut", "CUTOFF");
    drumFltRes.init (drumFilterPage, proc.apvts, "dfltres", "RES", ui303::HotTop);
    drumFltEnv.init (drumFilterPage, proc.apvts, "dfltenv", "ENV");

    drumCompPage.addAndMakeVisible (drumCompOn);
    drumCompOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "dcompon", drumCompOn);
    drumCompThr.init (drumCompPage, proc.apvts, "dcompthr", "THRESH");
    drumCompRat.init (drumCompPage, proc.apvts, "dcomprat", "RATIO");
    drumCompMk.init (drumCompPage, proc.apvts, "dcompmk", "MAKEUP");

    drumChorusPage.addAndMakeVisible (drumChorusOn);
    drumChorusOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "dchron", drumChorusOn);
    drumChorusRate.init (drumChorusPage, proc.apvts, "dchrrate", "RATE");
    drumChorusDepth.init (drumChorusPage, proc.apvts, "dchrdepth", "DEPTH");
    drumChorusMix.init (drumChorusPage, proc.apvts, "dchrmix", "MIX");

    drumReverbPage.addAndMakeVisible (drumReverbOn);
    drumReverbOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "drevon", drumReverbOn);
    drumReverbSize.init (drumReverbPage, proc.apvts, "drevsize", "SIZE");
    drumReverbDamp.init (drumReverbPage, proc.apvts, "drevdamp", "DAMP");
    drumReverbMix.init (drumReverbPage, proc.apvts, "drevmix", "MIX");

    for (auto* pg : { &drumDistPage, &drumDelayPage, &drumFilterPage, &drumCompPage,
                      &drumChorusPage, &drumReverbPage })
        drumFx.addPage (*pg);
    content.addAndMakeVisible (drumFx);

    // --- graphic EQ: one page a line, a response curve on each --------------
    {
        EqBands* bands[] = { &bassEqBands, &drumEqBands };
        FxPage*  pages[] = { &bassEqPage, &drumEqPage };

        for (int i = 0; i < 2; ++i)
        {
            pages[i]->addAndMakeVisible (*bands[i]);
            bands[i]->onReadout = [this] (const juce::String& text)
            {
                eqReadout.setText (text, juce::dontSendNotification);
            };
            eqSection.addPage (*pages[i]);
        }
    }
    content.addAndMakeVisible (eqSection);

    // Header controls, added after the section so they sit on top of its tab
    // bar the way KIT sits on top of the drums panel.
    eqReadout.setJustificationType (juce::Justification::centredRight);
    eqReadout.setFont (juce::FontOptions (10.0f));
    eqReadout.setInterceptsMouseClicks (false, false);
    content.addAndMakeVisible (eqReadout);

    eqFlat.onClick = [this] { shownEqBands().flatten(); };
    content.addAndMakeVisible (eqFlat);

    bassEqOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "beqon", bassEqOn);
    drumEqOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "deqon", drumEqOn);
    content.addAndMakeVisible (bassEqOn);
    content.addChildComponent (drumEqOn);

    // The readout describes a node under the pointer, so it means nothing once
    // the tab it belonged to is gone.
    eqSection.onTabChanged = [this] (int tab)
    {
        bassEqOn.setVisible (tab == 0);
        drumEqOn.setVisible (tab == 1);
        eqReadout.setText ({}, juce::dontSendNotification);
    };

    // --- performance XY pad -------------------------------------------------
    // One page, four tabs: the tab picks the mode, and the pad is the same
    // component whichever mode is chosen.
    padSection.addPage (xyPad);
    content.addAndMakeVisible (padSection);

    padLatchAtt = std::make_unique<ButtonAtt> (proc.apvts, "padlatch", padLatch);
    content.addAndMakeVisible (padLatch);

    // What the pad reaches, so each of those knobs can show how far it is being
    // pushed. The dests come from MacroPad.h and the parameter ids from the
    // knobs themselves, so this table cannot name an id that does not exist.
    padKnobs = {
        { &cutoff,          macropad::Cutoff },
        { &resonance,       macropad::Resonance },
        { &envmod,          macropad::EnvMod },
        { &distDrive,       macropad::DistDrive },
        { &distColor,       macropad::DistColor },
        { &bassDistLows,    macropad::DistLows },
        { &delayMix,        macropad::DelayMix },
        { &delayFb,         macropad::DelayFb },
        { &bassReverbMix,   macropad::RevMix },
        { &bassReverbSize,  macropad::RevSize },
        { &drumDrive,       macropad::DrumDrive },
        { &drumFltCut,      macropad::DrumFltCut },
    };

    // --- LFO 1 ---------------------------------------------------------------
    for (auto* b : { &lfoOn, &lfoSync, &lfoSmooth })
        content.addAndMakeVisible (b);
    lfoOnAtt     = std::make_unique<ButtonAtt> (proc.apvts, "lfo1on",     lfoOn);
    lfoSyncAtt   = std::make_unique<ButtonAtt> (proc.apvts, "lfo1sync",   lfoSync);
    lfoSmoothAtt = std::make_unique<ButtonAtt> (proc.apvts, "lfo1smooth", lfoSmooth);

    lfoShape.init (content, proc, proc.apvts, "lfo1shape", "SHAPE");
    lfoDiv .init (content, proc.apvts, "lfo1div",  "DIVISION");
    lfoDest.init (content, proc.apvts, "lfo1dest", "DESTINATION");
    lfoRate.init (content, proc.apvts, "lfo1rate", "RATE");
    // Bipolar: AMOUNT is a displacement either side of the knob, so its ring
    // reads out from the centre rather than up from zero.
    lfoAmt .init (content, proc.apvts, "lfo1amt",  "AMOUNT", ui303::HotNone, true);

    content.addAndMakeVisible (lfoScope);

    // RATE only means anything free-running and DIVISION only means anything
    // synced. The dead one greys rather than hiding, so the row does not reflow
    // every time SYNC is clicked.
    const auto followSync = [this]
    {
        const bool synced = proc.apvts.getRawParameterValue ("lfo1sync")->load() >= 0.5f;
        lfoRate.slider.setEnabled (! synced);
        lfoRate.label.setAlpha (synced ? 0.4f : 1.0f);
        lfoDiv.box.setEnabled (synced);
        lfoDiv.label.setAlpha (synced ? 1.0f : 0.4f);
    };
    lfoSync.onStateChange = followSync;
    followSync();

    // SMOOTH only means anything with DRAW selected. Greyed rather than hidden,
    // for the reason RATE and DIVISION are.
    const auto followShape = [this]
    {
        const bool drawn = proc.apvts.getRawParameterValue ("lfo1shape")->load()
                               == (float) lfo::Custom;
        lfoSmooth.setEnabled (drawn);
        lfoSmooth.setAlpha (drawn ? 1.0f : 0.4f);
        lfoScope.repaint();
    };
    if (auto* shapeParam = proc.apvts.getParameter ("lfo1shape"))
        lfoShapeAtt = std::make_unique<juce::ParameterAttachment> (
            *shapeParam, [followShape] (float) { followShape(); }, nullptr);
    followShape();

    lfoScope.onLfoMoved = [this] { updatePadKnobs(); };

    xyPad.onPadMoved = [this] { updatePadKnobs(); };

    if (auto* modeParam = proc.apvts.getParameter ("padmode"))
    {
        padSection.onTabChanged = [this, modeParam] (int tab)
        {
            // Guarded: the attachment below moves the tab when the parameter
            // changes, and writing it back from here would be a loop.
            if ((int) modeParam->convertFrom0to1 (modeParam->getValue()) != tab)
                padModeAtt->setValueAsCompleteGesture ((float) tab);
        };

        padModeAtt = std::make_unique<juce::ParameterAttachment> (
            *modeParam,
            [this] (float v)
            {
                padSection.setTab ((int) v);
                // Immediately, not on the next tick: switching mode releases the
                // knobs the old one was pushing, and leaving their arcs up for
                // 40 ms reads as the pad still being on them.
                updatePadKnobs();
            },
            nullptr);

        padSection.setTab ((int) proc.apvts.getRawParameterValue ("padmode")->load());
    }

    // An editor opening onto a latched gesture has to show its arcs straight
    // away rather than on the first timer tick — and a host that reopens the
    // window mid-automation is exactly the case where waiting 40 ms for the
    // knobs to catch up looks like they are not connected at all.
    updatePadKnobs();

    // --- page layouts (invoked when each page is sized by its FxSection) ---
    // ACTIVE | TYPE, then three equal knob columns: LOWS plus the selected
    // type's own pair. The group is handed exactly two columns so its own split
    // lands on the same width, keeping all three evenly spaced whichever type is
    // showing — the same three-across layout the COMP and REVERB tabs use.
    // Every drum page leaves the KIT column on the left clear, since KIT is drawn
    // over the section rather than belonging to a page.
    // Takes a vector, not an initializer_list: the list does not own its backing
    // array, so capturing one in a lambda that outlives the call leaves every
    // pointer dangling and the editor crashes on its first resize.
    // A fixed column width rather than an even split of whatever the section is
    // given: the section is now sized to exactly these columns so the EQ can
    // have the rest of the row, and a fixed width also stops the four-knob pages
    // spreading their knobs to different places than the six-knob ones.
    // The row grew to fit the XY pad, and a knob on these pages takes its size
    // from the page height — so left alone they would have grown with it, out of
    // scale with every other knob in the window. The strip keeps the height it
    // had and sits centred in what is now a taller page.
    const auto drumPageLayout = [] (std::vector<Knob*> knobs) {
        return [knobs = std::move (knobs)] (juce::Rectangle<int> r) {
            r = r.reduced (6, 6).withSizeKeepingCentre (
                    r.reduced (6, 6).getWidth(),
                    juce::jmin (r.reduced (6, 6).getHeight(), drumKnobRowH));
            r.removeFromLeft (drumKitColumnW);
            for (auto* k : knobs)
                k->setBounds (r.removeFromLeft (drumKnobColumnW).reduced (6, 0));
        };
    };
    drumMixPage.layoutFn = drumPageLayout ({ &laneKnobs[0], &laneKnobs[1], &laneKnobs[2],
                                             &laneKnobs[3], &laneKnobs[4], &drumVol });
    drumTunePage.layoutFn = drumPageLayout ({ &bdTune, &sdTune, &cpTune, &hatTune });
    drumDecayPage.layoutFn = drumPageLayout ({ &bdDecay, &sdDecay, &chDecay, &ohDecay });
    drumBalancePage.layoutFn = drumPageLayout ({ &panKnobs[0], &panKnobs[1], &panKnobs[2],
                                                &panKnobs[3], &panKnobs[4], &drumSpread });

    // The whole page is the plot — ACTIVE, FLAT and the readout are in the
    // header. Captured as a pointer by value rather than as a reference: a
    // lambda that captures a reference *parameter* by reference is the same
    // trap the drum page layout above documents.
    const auto eqPageLayout = [] (EqBands* bands) {
        return [bands] (juce::Rectangle<int> r) { bands->setBounds (r.reduced (6, 5)); };
    };
    bassEqPage.layoutFn = eqPageLayout (&bassEqBands);
    drumEqPage.layoutFn = eqPageLayout (&drumEqBands);

    bassDistPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (6, 6);
        distOn.setBounds (r.removeFromLeft (78).reduced (0, 18));
        bassDistType.setBounds (r.removeFromLeft (80).reduced (2, 6));

        const int knobW = r.getWidth() / 3;
        bassDistLows.setBounds (r.removeFromLeft (knobW).reduced (8, 0));
        const auto groupArea = r.removeFromLeft (knobW * 2);
        for (auto* g : { &bassDriveGroup, &bassCrushGroup, &bassFoldGroup, &bassRectGroup })
            g->setBounds (groupArea);
    };
    auto pairLayout = [] (Knob& a, Knob& b) {
        return [&a, &b] (juce::Rectangle<int> r) {
            a.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (8, 0));
            b.setBounds (r.reduced (8, 0));
        };
    };
    bassDriveGroup.layoutFn = pairLayout (distDrive, distColor);
    bassCrushGroup.layoutFn = pairLayout (bassCrushBits, bassCrushRate);
    bassFoldGroup.layoutFn  = pairLayout (bassFoldAmt, bassFoldSym);
    bassRectGroup.layoutFn  = pairLayout (bassRectAmt, bassRectTone);
    // ACTIVE | TYPE, then three equal columns — the same shape as the DIST tab,
    // so the two type-bearing tabs read the same way.
    bassDelayPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (6, 6);
        delayOn.setBounds (r.removeFromLeft (78).reduced (0, 18));
        delayType.setBounds (r.removeFromLeft (84).reduced (2, 6));
        const int w = r.getWidth() / 3;
        delayTime.setBounds (r.removeFromLeft (w).reduced (8, 8));
        delayFb.setBounds (r.removeFromLeft (w).reduced (8, 0));
        delayMix.setBounds (r.reduced (8, 0));
    };
    bassFilterPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (6, 6);
        bassFltOn.setBounds (r.removeFromLeft (86).reduced (0, 18));
        bassFltMode.setBounds (r.removeFromLeft (88).reduced (6, 6));
        const int w = r.getWidth() / 3;
        bassFltCut.setBounds (r.removeFromLeft (w).reduced (6, 0));
        bassFltRes.setBounds (r.removeFromLeft (w).reduced (6, 0));
        bassFltEnv.setBounds (r.reduced (6, 0));
    };
    bassCompPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (6, 6);
        bassCompOn.setBounds (r.removeFromLeft (90).reduced (0, 18));
        const int w = r.getWidth() / 3;
        bassCompThr.setBounds (r.removeFromLeft (w).reduced (8, 0));
        bassCompRat.setBounds (r.removeFromLeft (w).reduced (8, 0));
        bassCompMk.setBounds (r.reduced (8, 0));
    };
    bassChorusPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (6, 6);
        bassChorusOn.setBounds (r.removeFromLeft (90).reduced (0, 18));
        const int w = r.getWidth() / 3;
        bassChorusRate.setBounds (r.removeFromLeft (w).reduced (8, 0));
        bassChorusDepth.setBounds (r.removeFromLeft (w).reduced (8, 0));
        bassChorusMix.setBounds (r.reduced (8, 0));
    };
    bassReverbPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (6, 6);
        bassReverbOn.setBounds (r.removeFromLeft (90).reduced (0, 18));
        const int w = r.getWidth() / 3;
        bassReverbSize.setBounds (r.removeFromLeft (w).reduced (8, 0));
        bassReverbDamp.setBounds (r.removeFromLeft (w).reduced (8, 0));
        bassReverbMix.setBounds (r.reduced (8, 0));
    };

    // drum DIST tab, mirroring the bass one
    drumDistPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (6, 6);
        drumDistOn.setBounds (r.removeFromLeft (78).reduced (0, 18));
        drumDistType.setBounds (r.removeFromLeft (80).reduced (2, 6));

        const int knobW = r.getWidth() / 3;
        drumDistLows.setBounds (r.removeFromLeft (knobW).reduced (8, 0));
        const auto groupArea = r.removeFromLeft (knobW * 2);
        for (auto* g : { &drumDriveGroup, &drumCrushGroup, &drumFoldGroup, &drumRectGroup })
            g->setBounds (groupArea);
    };
    drumDriveGroup.layoutFn = pairLayout (drumDrive, drumDistColor);
    drumCrushGroup.layoutFn = pairLayout (drumCrushBits, drumCrushRate);
    drumFoldGroup.layoutFn  = pairLayout (drumFoldAmt, drumFoldSym);
    drumRectGroup.layoutFn  = pairLayout (drumRectAmt, drumRectTone);

    // drum pages are narrow; use compact layouts
    drumDelayPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (5, 5);
        drumDelayOn.setBounds (r.removeFromLeft (58).reduced (0, 16));
        drumDelayType.setBounds (r.removeFromLeft (80).reduced (3, 8));
        drumDelayTime.setBounds (r.removeFromLeft (68).reduced (3, 8));
        const int w = r.getWidth() / 2;
        drumDelayFb.setBounds (r.removeFromLeft (w).reduced (4, 0));
        drumDelayMix.setBounds (r.reduced (4, 0));
    };
    drumFilterPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (5, 4);
        auto left = r.removeFromLeft (92);
        drumFltOn.setBounds (left.removeFromTop (22).reduced (0, 2));
        drumFltMode.setBounds (left.reduced (2, 2));
        const int w = r.getWidth() / 3;
        drumFltCut.setBounds (r.removeFromLeft (w).reduced (3, 0));
        drumFltRes.setBounds (r.removeFromLeft (w).reduced (3, 0));
        drumFltEnv.setBounds (r.reduced (3, 0));
    };
    drumCompPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (5, 5);
        drumCompOn.setBounds (r.removeFromLeft (64).reduced (0, 16));
        const int w = r.getWidth() / 3;
        drumCompThr.setBounds (r.removeFromLeft (w).reduced (4, 0));
        drumCompRat.setBounds (r.removeFromLeft (w).reduced (4, 0));
        drumCompMk.setBounds (r.reduced (4, 0));
    };
    drumChorusPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (5, 5);
        drumChorusOn.setBounds (r.removeFromLeft (64).reduced (0, 16));
        const int w = r.getWidth() / 3;
        drumChorusRate.setBounds (r.removeFromLeft (w).reduced (4, 0));
        drumChorusDepth.setBounds (r.removeFromLeft (w).reduced (4, 0));
        drumChorusMix.setBounds (r.reduced (4, 0));
    };
    drumReverbPage.layoutFn = [this] (juce::Rectangle<int> r) {
        r = r.reduced (5, 5);
        drumReverbOn.setBounds (r.removeFromLeft (64).reduced (0, 16));
        const int w = r.getWidth() / 3;
        drumReverbSize.setBounds (r.removeFromLeft (w).reduced (4, 0));
        drumReverbDamp.setBounds (r.removeFromLeft (w).reduced (4, 0));
        drumReverbMix.setBounds (r.reduced (4, 0));
    };

    content.addAndMakeVisible (bassOnButton);
    bassOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "basson", bassOnButton);
    content.addAndMakeVisible (drumsOnButton);
    drumsOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "drumson", drumsOnButton);

    // --- pattern management ---
    // process-wide clipboards: survive editor reopen, shared between instances
    struct BassClip
    {
        bool has = false;
        int pitch[16];
        bool gate[16], slide[16];
        int  dyn[16];
        int hold[16];
        int length = 16;
    };
    struct DrumClip
    {
        bool has = false;
        uint32_t steps[DrumSequencer::numLanes], accents[DrumSequencer::numLanes];
        uint32_t softs[DrumSequencer::numLanes];
    };
    static BassClip bassClip;
    static DrumClip drumClip;

    for (auto* b : { &bassCopy, &bassPaste, &bassClear, &bassTransDown, &bassTransUp,
                     &bassShiftL, &bassShiftR, &bassHold,
                     &drumCopy, &drumPaste, &drumClear, &drumShiftL, &drumShiftR })
        content.addAndMakeVisible (*b);

    bassCopy.onClick = [this] {
        auto& seq = proc.sequencer;
        for (int i = 0; i < 16; ++i)
        {
            bassClip.pitch[i]  = Sequencer303::loadPitch (seq.steps[i]);
            bassClip.gate[i]   = seq.steps[i].gate.load();
            bassClip.dyn[i]    = seq.steps[i].dyn.load();
            bassClip.slide[i]  = seq.steps[i].slide.load();
            bassClip.hold[i]   = seq.steps[i].hold.load();
        }
        bassClip.length = seq.length.load();
        bassClip.has = true;
    };
    bassPaste.onClick = [this] {
        if (! bassClip.has)
            return;
        auto& seq = proc.sequencer;
        for (int i = 0; i < 16; ++i)
        {
            Sequencer303::storePitch (seq.steps[i], bassClip.pitch[i]);
            seq.steps[i].gate.store (bassClip.gate[i]);
            seq.steps[i].dyn.store (dyn303::clampDyn (bassClip.dyn[i]));
            seq.steps[i].slide.store (bassClip.slide[i]);
            seq.steps[i].hold.store (juce::jlimit (1, 16, bassClip.hold[i]));
        }
        seq.length.store (bassClip.length);
        lengthSlider.setValue (bassClip.length, juce::dontSendNotification);
    };
    bassClear.onClick = [this] {
        for (auto& step : proc.sequencer.steps)
        {
            step.gate.store (false);
            step.dyn.store (dyn303::Normal);
            step.slide.store (false);
            step.hold.store (1);
        }
    };
    bassTransDown.onClick = [this] { transposeBass (-1); };
    bassTransUp.onClick   = [this] { transposeBass (1); };
    bassShiftL.onClick    = [this] { shiftBass (-1); };
    bassShiftR.onClick    = [this] { shiftBass (1); };

    // HOLD latches: it lights up, and from then on a key held on the on-screen
    // keyboard writes a note that the playhead carries along until you let go.
    // Deliberately not a parameter — it is a performance mode, and one that
    // overwrites steps, so it should never be automated or come back armed when
    // a session is reopened.
    bassHold.setClickingTogglesState (true);
    // The mode lives on the processor and outlives this editor, so the button
    // shows what is actually armed rather than assuming it reopens off.
    bassHold.setToggleState (proc.holdArmed.load(), juce::dontSendNotification);
    bassHold.onClick = [this] { stepGrid.setHoldLatch (bassHold.getToggleState()); };

    drumCopy.onClick = [this] {
        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            drumClip.steps[lane]   = proc.drumSequencer.stepMask[lane].load();
            drumClip.accents[lane] = proc.drumSequencer.accentMask[lane].load();
            drumClip.softs[lane]   = proc.drumSequencer.softMask[lane].load();
        }
        drumClip.has = true;
    };
    drumPaste.onClick = [this] {
        if (! drumClip.has)
            return;
        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            proc.drumSequencer.stepMask[lane].store (drumClip.steps[lane]);
            proc.drumSequencer.accentMask[lane].store (drumClip.accents[lane]);
            proc.drumSequencer.softMask[lane].store (drumClip.softs[lane]);
        }
        proc.drumSequencer.normalise();
    };
    drumClear.onClick = [this] {
        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            proc.drumSequencer.stepMask[lane].store (0);
            proc.drumSequencer.accentMask[lane].store (0);
            proc.drumSequencer.softMask[lane].store (0);
        }
    };
    drumShiftL.onClick = [this] { shiftDrums (-1); };
    drumShiftR.onClick = [this] { shiftDrums (1); };

    // --- song / arrangement ---
    content.addAndMakeVisible (songTransport);
    songTransport.onCue = [this] { songList.followPlayhead(); };

    content.addAndMakeVisible (songLoopButton);
    songLoopButton.setToggleState (proc.song.isLooping(), juce::dontSendNotification);
    songLoopButton.onClick = [this] {
        proc.song.setLooping (songLoopButton.getToggleState());
    };

    for (auto* b : { &songIns, &songDel, &songDup, &songUp, &songDown, &songClear })
        content.addAndMakeVisible (*b);

    songIns.onClick   = [this] { songList.insertRow(); };
    songDel.onClick   = [this] { songList.deleteRow(); };
    songDup.onClick   = [this] { songList.duplicateRow(); };
    songUp.onClick    = [this] { songList.moveRow (-1); };
    songDown.onClick  = [this] { songList.moveRow (1); };
    songClear.onClick = [this] { songList.clearSong(); };

    content.addAndMakeVisible (songInfo);
    songInfo.setJustificationType (juce::Justification::centred);
    songInfo.setFont (juce::FontOptions (10.5f));

    content.addAndMakeVisible (songList);
    songList.onSongChanged = [this] { updateSongInfo(); };

    // --- song library ---
    content.addAndMakeVisible (songLibrary);
    songLibrary.setTextWhenNothingSelected ("(UNSAVED)");
    songLibrary.onOpen = [this] { refreshLibrary(); };
    songLibrary.onChange = [this]
    {
        const int i = songLibrary.getSelectedId() - 1;
        if (i >= 0 && i < libraryFiles.size())
        {
            proc.loadSongFromFile (libraryFiles[i]);
            updateSongInfo();
        }
    };

    dragWatcher.onTick = [this] { checkDragLeftWindow(); };

    content.addAndMakeVisible (songDragMidi);
    songDragMidi.onDragOut = [this]
    {
        if (proc.song.getCount() <= 0)
            return;

        const auto& p = ui303::palette (proc.uiSkin.load());
        juce::Image chip (juce::Image::ARGB, 62, 22, true);
        {
            juce::Graphics g (chip);
            g.setColour (p.orange);
            g.fillRoundedRectangle (chip.getBounds().toFloat().reduced (1.0f), 4.0f);
            g.setColour (juce::Colour (0xff2a2a26));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (songMidiMode == bassOnly  ? "BASS"
                      : songMidiMode == drumsOnly ? "DRUMS" : "SONG",
                        chip.getBounds(), juce::Justification::centred);
        }
        startDragging (songDragDescription, &songDragMidi, juce::ScaledImage (chip), true);
    };
    songDragMidi.onClick = [this]
    {
        if (! songDragMidi.isEndingDrag())
            showSongMidiMenu();
    };

    content.addAndMakeVisible (songSave);
    songSave.onClick = [this]
    {
        auto dir = BP303AudioProcessor::songLibraryFolder();
        dir.createDirectory();

        auto name = proc.getSongName();
        if (name.isEmpty())
            name = "Untitled";

        const juce::String pattern = juce::String ("*") + BP303AudioProcessor::songFileSuffix;
        chooser = std::make_unique<juce::FileChooser> (
            "Save song", dir.getChildFile (name + BP303AudioProcessor::songFileSuffix), pattern);

        // Async: a plugin editor must never spin a modal loop.
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File())
                    return;

                proc.saveSongToFile (file.withFileExtension (BP303AudioProcessor::songFileSuffix));
                refreshLibrary();
            });
    };

    refreshLibrary();

    content.addAndMakeVisible (bassKeys);
    content.addAndMakeVisible (drumKeys);
    content.addAndMakeVisible (stepGrid);
    content.addAndMakeVisible (drumGrid);

    content.addAndMakeVisible (helpButton);
    helpButton.onClick = [this] { startHelp(); };

    // Added last so it sits above everything it explains.
    content.addChildComponent (help);

    updateSongInfo();
    applySkin();

    startTimerHz (10);   // keep controls in sync with pattern switches
    // Read the remembered width first: setResizeLimits() constrains the current
    // (still empty) bounds straight away, which fires resized() and would
    // otherwise overwrite this with the minimum size.
    const int rememberedWidth = proc.editorWidth.load();

    // Resizable, but only uniformly: the layout is a fixed design that scales.
    //
    // **The size limits have to be computed against the screen, not fixed.** A
    // window taller than the display puts its resize corner off the bottom of
    // that display, and a corner you cannot reach is a window you cannot shrink
    // — the plugin is then stuck at a size the user never chose and has no way
    // to leave. That is not hypothetical: growing the design height for the LFO
    // row did exactly this on a screen that had been fitting the old one
    // exactly. So the ceiling is whatever fits here, and the floor is allowed
    // below the nominal 60% when even 60% would not.
    const double aspect = (double) nativeWidth / (double) nativeHeight;

    int minWidth = juce::roundToInt (nativeWidth * 0.6);
    int maxWidth = nativeWidth;

    // Only trust the display when it reports something plausible — a headless or
    // not-yet-configured one reports an empty area, and clamping to that would
    // open every window at the minimum.
    const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (screen != nullptr && screen->userArea.getWidth() > 400
                          && screen->userArea.getHeight() > 300)
    {
        // Generous allowance for the host's own chrome: Logic frames a plugin
        // window with a title bar *and* a header strip of its own, and being a
        // little smaller than necessary costs some pixels while being a little
        // too big costs the user the resize corner.
        const auto usable = screen->userArea;
        const int fits = juce::jmin (usable.getWidth() - 60,
                                     juce::roundToInt ((usable.getHeight() - 140) * aspect));

        // 35% keeps a genuine floor — below that the text stops being legible
        // and a window that small is its own kind of unusable.
        maxWidth = juce::jlimit (juce::roundToInt (nativeWidth * 0.35), nativeWidth, fits);
        minWidth = juce::jmin (minWidth, maxWidth);
    }

    setResizable (true, true);
    setResizeLimits (minWidth, juce::roundToInt (minWidth / aspect),
                     maxWidth, juce::roundToInt (maxWidth / aspect));
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (aspect);

    // Reopen at the size this project was left at, clamped to what fits here.
    // The remembered width is in the *old* design's pixels for any project that
    // predates a change to nativeHeight, which is another way this can arrive
    // too tall — clamping it is what makes such a project openable at all.
    const int width = juce::jlimit (minWidth, maxWidth, rememberedWidth);
    setSize (width, juce::roundToInt (width / aspect));

    // In the standalone app, centre the window on the display it opens on so the
    // whole UI is visible (hosts position the plugin window themselves, so we
    // leave those alone). Deferred until the window exists and is sized to us.
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        juce::Component::SafePointer<juce::Component> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr)
                return;
            auto* top = safe->getTopLevelComponent();
            if (top == nullptr)
                return;
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            auto* display = displays.getDisplayForRect (top->getBounds());
            if (display == nullptr)
                display = displays.getPrimaryDisplay();
            if (display == nullptr)
                return;
            const auto ua = display->userArea;
            top->setTopLeftPosition (ua.getCentreX() - top->getWidth() / 2,
                                     ua.getY() + juce::jmax (0, (ua.getHeight() - top->getHeight()) / 2));
        });
    }
    // In a plugin the window belongs to the host, and we deliberately leave it
    // alone. This used to reach the native NSWindow behind the editor and centre
    // it, on delayed timers after construction — which is exactly the window the
    // host is still setting up when it rebuilds editors after an audio-device
    // change, and moving it then could send it to another screen or off the
    // visible frame. Hosts place and remember plugin windows themselves.

    // Switch the EQ's band metering on for as long as this window lives.
    proc.addEqMeterClient (1);
}

BP303AudioProcessorEditor::~BP303AudioProcessorEditor()
{
    // The band meters cost a bandpass pair and a follower per band per line,
    // which is a lot of arithmetic for something nobody can see. They run only
    // while a window is open.
    proc.addEqMeterClient (-1);
    setLookAndFeel (nullptr);
}

void BP303AudioProcessorEditor::timerCallback()
{
    const int len = proc.sequencer.length.load();
    if ((int) lengthSlider.getValue() != len)
        lengthSlider.setValue (len, juce::dontSendNotification);

    if (songLoopButton.getToggleState() != proc.song.isLooping())
        songLoopButton.setToggleState (proc.song.isLooping(), juce::dontSendNotification);

    // The song can change under us (a project or song file loading), so keep the
    // summary in step rather than only refreshing it on edits.
    updateSongInfo();
}

void BP303AudioProcessorEditor::startHelp()
{
    // Targets are taken from the live components, so the tour follows the
    // layout instead of repeating its geometry.
    auto around = [] (std::initializer_list<juce::Component*> comps)
    {
        juce::Rectangle<int> united;
        for (auto* c : comps)
            if (c != nullptr)
                united = united.isEmpty() ? c->getBounds()
                                          : united.getUnion (c->getBounds());
        return united.expanded (6);
    };

    std::vector<HelpOverlay::Step> steps;

    steps.push_back ({ {}, "BadPackets BP-303",
        "A 303-style bass line and a drum machine, with a pattern bank for each "
        "and a song arranger to chain them together.\n\n"
        "This tour covers the parts that aren't obvious. Use NEXT and BACK, or the "
        "arrow keys; ESC closes it at any point." });

    steps.push_back ({ around ({ playMode.sw.get(), &playMode.label }), "Play mode",
        "EXT - the synth plays notes arriving from your DAW or MIDI keyboard. The "
        "sequencer stays quiet.\n\n"
        "SEQ - the built-in 16-step sequencer plays the current bass and drum "
        "patterns. This is where you build patterns.\n\n"
        "SONG - the arrangement in the SONG panel chooses which patterns play." });

    steps.push_back ({ around ({ &runButton, &recButton, &metroButton }),
        "Run, record, click",
        "RUN starts the internal clock when there is no host transport. Inside a "
        "DAW the host's transport drives everything instead.\n\n"
        "REC quantises notes you play into the current pattern while it loops - "
        "play a line in rather than drawing it.\n\n"
        "CLICK is a metronome for doing that." });

    steps.push_back ({ around ({ &intBpm.slider, &intBpm.label, &shuffle.slider,
                                 &shuffle.label, &lengthSlider, &lengthLabel }),
        "Tempo, shuffle and length",
        "INT. BPM is used only when no host tempo is available.\n\n"
        "SHUFFLE delays every other 16th to swing the groove.\n\n"
        "LENGTH sets how many steps the pattern loops over, from 1 to 16. The drum "
        "pattern follows the bass pattern's length." });

    steps.push_back ({ around ({ &uniVoices.slider, &uniDetune.slider,
                                 &uniSpread.slider, &uniSpread.label }),
        "Unison, and where the width comes from",
        "VOICES stacks the bass oscillator up to seven times. DETUNE pulls the "
        "copies apart in pitch for the thickness, and SPREAD places them across "
        "the image - so the line gets wide without anything downstream having to "
        "widen it.\n\n"
        "Three things in the whole instrument make stereo and nothing else does: "
        "this, the drum kit's per-voice positions on the DRUMS BALANCE page, and "
        "the ping-pong delay. All three start off or centred, so an untouched "
        "instance is dead mono and every effect leaves it that way.\n\n"
        "On the drums, SPREAD scales the BALANCE positions rather than replacing "
        "them: at zero the kit collapses to the centre whatever BALANCE says, and "
        "turning it up opens the kit out to where you placed it." });

    steps.push_back ({ around ({ &bassFx, &drumFx }), "The effects, and the TYPE selectors",
        "Bass and drums each get their own chain - drive, delay, filter, comp, "
        "chorus and reverb - a tab at a time.\n\n"
        "DIST has a TYPE selector, and the knobs change with it: SOFT is the "
        "classic 303 overdrive, FUZZ an industrial hard clip, CRUSH bit and "
        "sample-rate reduction, FOLD a wavefolder whose tone follows how hard the "
        "line is playing, RECT an octave-up rectifier. Each type keeps its own "
        "settings, so you can flip between them and compare.\n\n"
        "LOWS holds the bottom end back out of the distortion - it is how a bass "
        "line gets filthy without losing its sub.\n\n"
        "DELAY has a TYPE too, and the same three controls serve both. MONO is one "
        "echo line down the middle. STEREO ping-pongs it, throwing each repeat to "
        "the opposite side - it opens a line right out without touching the dry "
        "signal, which stays centred." });

    steps.push_back ({ around ({ &padSection }), "The XY pad",
        "One gesture across several controls at once. The tab picks what the two "
        "axes reach, and the readout beside the pad names them.\n\n"
        "ACID is the one a 303 exists for: X opens the filter, Y rides resonance "
        "and env mod together. GRIT is drive against how much bottom end you let "
        "into the shaper. SPACE is delay against reverb. KIT drives and filters "
        "the drum bus.\n\n"
        "It offsets your knobs rather than writing them, so nothing you set gets "
        "dragged about - the knobs turn to what you are hearing and a small mark "
        "stays behind at the setting underneath. Let go and they come back to it.\n\n"
        "Holding the pad switches on whatever effects the mode needs, and lets go "
        "of them after - your own ACTIVE switches read the same as before. That is "
        "the point of it: SPACE makes a sound on an instance with every effect "
        "still switched off.\n\n"
        "LATCH parks the gesture where you leave it instead of springing back. "
        "Double-click the pad to drop a latched one. Only the pad's own two axes "
        "reach your DAW's automation, so you get two lanes rather than a dozen." });

    steps.push_back ({ around ({ &lfoScope, lfoShape.sw.get(), &lfoDest.box,
                                 &lfoRate.slider, &lfoAmt.slider, &lfoOn,
                                 &lfoSync, &lfoSmooth }), "The LFO",
        "The pad's twin: where the pad is a gesture you make, the LFO is one that "
        "runs on its own. Pick a DESTINATION and it rides that one control for you, "
        "up and down, for as long as it is on.\n\n"
        "AMOUNT is how far it swings, either side of where you left the knob - like "
        "the pad it offsets rather than writes, so your setting stays put with a "
        "mark on it and the knob moves around it. SYNC locks the swing to the host, "
        "DIVISION picking the rate from a bar down to a 16th; switch SYNC off and "
        "RATE sets a free speed instead.\n\n"
        "It switches on whatever effect its destination lives in and lets go after, "
        "exactly as the pad does - so routing to the drum filter moves the drums "
        "even with that filter's own ACTIVE off, and your switch reads the same "
        "once the LFO stops.\n\n"
        "The scope is what tells you the thing is moving and where in its cycle it "
        "is - it draws the shape from the LFO itself, so it can't show a wave the "
        "sound isn't making. SHAPE runs from sine through square and sample-and-"
        "hold, and DRAW turns the scope into a 16-step surface you paint your own "
        "shape into, SMOOTH curving between the steps. Drag the marker in to end "
        "the loop early: a short loop repeats faster and drifts against the bar, "
        "the way a short drum lane does." });

    steps.push_back ({ around ({ &eqSection }), "The EQ",
        "Ten octave bands a line, drawn as a response curve rather than a bank of "
        "faders. Drag a node to move its band; double-click one to zero it, or FLAT "
        "to zero the lot.\n\n"
        "The curve is the reason it is worth the room. Octave bands overlap, so two "
        "neighbours at +6 do not make two +6 bumps - they make one +10 shelf, and a "
        "row of faders never shows you that.\n\n"
        "The bars behind the curve are what each band is actually passing, measured "
        "after the EQ. They only run while this window is open.\n\n"
        "It sits last on each line, after the delay and reverb, which makes it a "
        "channel EQ: what you dial is what leaves the line." });

    steps.push_back ({ around ({ &stepGrid, &bassHold }), "HOLD: play the pattern in",
        "HOLD latches on and lights up. With the sequencer running, hold a note - "
        "on the keyboard below the grid, or on whatever is playing MIDI in - and "
        "it sustains for as long as you keep it down, written where the playhead "
        "is and growing across the steps as they go by.\n\n"
        "Let go, hold another note, and the next one starts from wherever the "
        "playhead has got to - so you fill a pattern by playing it rather than "
        "drawing it. Rolling straight onto the next note works too, without "
        "releasing first.\n\n"
        "It is not a kind of REC and doesn't need REC armed: REC drops single "
        "steps in as you play them, HOLD lays down sustained notes instead. It "
        "keeps working with the plugin window closed, so an armed HOLD and a "
        "MIDI keyboard is all you need.\n\n"
        "It writes over what was there: any step a held note runs across is "
        "cleared and swallowed into it. Hold one key for a whole cycle and you "
        "replace the pattern with that one note.\n\n"
        "A note can't cross the loop, so a key held through the end of the pattern "
        "starts again at the top. With nothing playing there is no playhead to "
        "follow: a key still sustains while you hold it, but it writes a single "
        "step, the way the keyboard always has." });

    steps.push_back ({ around ({ &stepGrid }), "Pitch, dynamics and slide",
        "Drag a cell in the PITCH row up or down to change the note.\n\n"
        "ACC sets how hard the step plays, and clicking it cycles through three "
        "states. Click once for an accent - louder, with more filter envelope, "
        "lit full orange. Click again for a soft step, shown in a dimmer orange: "
        "quieter and duller, for ghost notes that sit behind the line. A third "
        "click returns it to normal, and right-click walks back the other way.\n\n"
        "SLIDE ties the step into the next one, giving the portamento that a 303 "
        "is known for. Slide and accent together is the classic sound.\n\n"
        "Alt-click a GATE cell to split it, cycling 1 - 2 - 3 - 4. The step fires "
        "that many times inside its own slot, so it is a roll rather than a change "
        "of timing. Split a slid step and only the last repeat ties forward - you "
        "get a stutter that glides out of its final note instead of one long note "
        "with retriggers buried in it." });

    steps.push_back ({ around ({ &stepGrid }), "The keyboard writes notes",
        "The keyboard along the bottom is a writing tool, not just an audition "
        "keyboard. Clicking a key writes that note into the step the cursor is on "
        "and moves the cursor forward, the way you'd program the hardware. With "
        "HOLD armed the same keys play notes in against the playhead instead.\n\n"
        "REST advances leaving the step silent. The arrows to its left shift which "
        "octaves the keyboard covers." });

    steps.push_back ({ around ({ &drumGrid }), "Drum grid",
        "Click a cell to toggle a hit.\n\n"
        "Shift-click or right-click sets how hard it lands, cycling the same way "
        "the bass ACC row does: an accent first, then a soft ghost hit, then back "
        "to normal. Brightness tells you which is which - a soft hit is part-lit, "
        "a normal one full, an accent brighter still and warmer. On an empty cell "
        "it drops an accent straight in.\n\n"
        "Press and drag across a lane to paint - whatever the first cell did "
        "(adding, removing, or setting a level) is applied to every step you drag "
        "over, so you can lay in a hat pattern in one gesture.\n\n"
        "Alt-click sets how many times a step fires, cycling 1 - 2 - 3 - 4. A "
        "ratcheted cell is sliced into that many pieces, so you can count them at "
        "a glance. The repeats fill the step itself, which makes them rolls and "
        "stutters rather than a change of meter." });

    steps.push_back ({ around ({ &drumGrid, &stepGrid }), "End markers: drift, or divide",
        "Every lane ends at the marker on its right, and so does the bass line - "
        "the marker on the GATE row does the same three things. LENGTH still owns "
        "the bar; these say how the line fills it.\n\n"
        "Drag the marker to shorten the line and it free-runs against the bar. "
        "Pull the hats in to six and they cycle every three eighths while the kick "
        "stays in four, landing somewhere new each bar and coming back round after "
        "three. That is polymeter: the pulse is unchanged and the line drifts.\n\n"
        "Double-click the marker instead and the line's steps divide the bar "
        "evenly, so it keeps the bar and changes the pulse. Three steps are three "
        "even hits a bar; twelve are eighth-note triplets. That is polyrhythm, and "
        "it is the opposite answer to the same question. Ticks along the row show "
        "where the real step boundaries fall, since the cells still take a column "
        "each.\n\n"
        "Right-click the marker to put the line back on the bar, clock included.\n\n"
        "Splitting a step and fitting a line stack rather than overlap: three "
        "repeats inside a sixteenth are 48ths, while triplets are what fitting is "
        "for. Steps past a line's end go dim but keep what you wrote on them." });

    steps.push_back ({ around ({ &bassKeys, &drumKeys }), "Pattern banks",
        "Three banks of nine, so 27 patterns per line. Bass and drums are separate "
        "banks and switch independently.\n\n"
        "While the sequencer is running a switch is queued rather than immediate: "
        "the key blinks, and the new pattern takes over at the end of the current "
        "one so you stay in time." });

    steps.push_back ({ around ({ &bassKeys, &drumKeys, &songList }),
        "Drag patterns into the song",
        "Drag a pattern key over to the SONG list to arrange with it.\n\n"
        "Drop it on a row to assign that pattern to the row. Drop on the thin band "
        "at a row's top edge, or below the last row, to insert a new row there.\n\n"
        "The bass keypad fills the BASS column, the drum keypad the DRUM column." });

    steps.push_back ({ around ({ &bassKeys, &drumKeys }), "Drag out as MIDI",
        "The same keys drag out to your DAW. Take a key past the edge of the "
        "plugin window, hold for a moment over a track, and that pattern arrives "
        "as a MIDI clip - timing, accents and slides included.\n\n"
        "It is the slot you dragged that travels, not whatever is playing, so any "
        "pattern in the bank can go out without switching to it first." });

    steps.push_back ({ around ({ &songTransport }), "Song transport",
        "The song plays only in SONG mode, and only once you press PLAY - "
        "selecting SONG never starts it on its own.\n\n"
        "STOP returns to the top. The cue buttons step through the arrangement and "
        "work while stopped, so you can line up a section and play from there.\n\n"
        "Under a DAW transport the song follows the host instead." });

    steps.push_back ({ around ({ &songList }), "Song rows",
        "REP is how many times that step repeats - drag it up or down.\n\n"
        "B and D are lit while that line plays. Click one to drop the bass or "
        "drums for that step, for breakdowns.\n\n"
        "A dash means hold: that line carries on with whatever it was already "
        "playing, so you can change drums while the bass runs on. A faded dash "
        "means no row has picked a pattern for that line yet, so it stays "
        "silent until one does.\n\n"
        "Click a row's number to move the playhead to it." });

    steps.push_back ({ around ({ &songLibrary, &songDragMidi, &songSave }), "Saving songs",
        "SAVE writes a .bp303song file to your Music folder, under BP303/Songs. "
        "The dropdown lists what's in there.\n\n"
        "A song file carries copies of every pattern it uses, so it plays correctly "
        "even in a project whose banks hold something else. Loading a song will "
        "overwrite those pattern slots.\n\n"
        "Drag MIDI out to your track to get the whole arrangement - repeats "
        "expanded, holds resolved, mutes applied. Bass and drums come out as a "
        "track each, so the drums can land on a drum instrument.\n\n"
        "Click MIDI to hand over just one of the two lines instead. The button "
        "shows what is armed. Drag a pattern key when you only want one line of "
        "one pattern." });

    help.setSteps (std::move (steps));
    help.start();
}

void BP303AudioProcessorEditor::refreshLibrary()
{
    libraryFiles.clear();
    BP303AudioProcessor::songLibraryFolder()
        .findChildFiles (libraryFiles, juce::File::findFiles, false,
                         juce::String ("*") + BP303AudioProcessor::songFileSuffix);

    // Sort by name so the list is stable between scans.
    struct ByName
    {
        static int compareElements (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareIgnoreCase (b.getFileName());
        }
    };
    ByName sorter;
    libraryFiles.sort (sorter);

    songLibrary.clear (juce::dontSendNotification);
    const auto current = proc.getSongName();
    for (int i = 0; i < libraryFiles.size(); ++i)
    {
        const auto name = libraryFiles[i].getFileNameWithoutExtension();
        songLibrary.addItem (name, i + 1);
        if (name == current)
            songLibrary.setSelectedId (i + 1, juce::dontSendNotification);
    }
}

void BP303AudioProcessorEditor::updateSongInfo()
{
    const int steps = proc.song.getCount();
    const auto slotSteps = [this] (int slot) { return proc.slotLengthSteps (slot); };
    const double bars = proc.song.totalBeats (slotSteps, proc.sequencer.length.load()) / 4.0;

    const auto barText = std::abs (bars - std::round (bars)) < 0.01
                             ? juce::String ((int) std::round (bars))
                             : juce::String (bars, 1);

    songInfo.setText (juce::String (steps) + (steps == 1 ? " STEP  /  " : " STEPS  /  ")
                          + barText + " BARS",
                      juce::dontSendNotification);
}

// The exporters read a sequencer, so a stored slot is loaded into a scratch one
// rather than duplicating the timing rules for patterns that aren't playing.
// Shared by the single-pattern drag and the song drag, so the two can never
// disagree about how a pattern is voiced.
juce::MidiMessageSequence BP303AudioProcessorEditor::bassSlotSequence (int slot, int& lengthSteps) const
{
    const float shuffle = proc.apvts.getRawParameterValue ("shuffle")->load();
    const auto pat = proc.snapshotBassPattern (slot);

    Sequencer303 seq;
    for (int i = 0; i < Sequencer303::maxSteps; ++i)
    {
        Sequencer303::storePitch (seq.steps[i], pat.bass[i].pitch);
        seq.steps[i].gate.store (pat.bass[i].gate);
        seq.steps[i].dyn.store (dyn303::clampDyn (pat.bass[i].dyn));
        seq.steps[i].slide.store (pat.bass[i].slide);
        seq.steps[i].hold.store (juce::jlimit (1, Sequencer303::maxSteps, pat.bass[i].hold));
        // as much the step as its pitch is — a rebuild that drops the split
        // hands the host one note where the plugin plays four
        seq.steps[i].ratchet.store (juce::jlimit (1, Sequencer303::maxRatchet,
                                                  pat.bass[i].ratchet));
    }
    lengthSteps = juce::jlimit (1, Sequencer303::maxSteps, pat.length);
    seq.length.store (lengthSteps);
    // the line's own cycle too — a rebuild that drops it renders a short line as
    // if it filled the bar, which is not the pattern
    seq.patternLength.store (juce::jlimit (Sequencer303::followBar, Sequencer303::maxSteps,
                                           pat.lineLength));
    seq.patternFit.store (pat.lineFit);

    return bp303::bassSequence (seq, shuffle);
}

// A drum pattern carries no length of its own — it runs to whatever the bass
// line's length is, so the caller passes the length it should fill.
juce::MidiMessageSequence BP303AudioProcessorEditor::drumSlotSequence (int slot, int lengthSteps) const
{
    const float shuffle = proc.apvts.getRawParameterValue ("shuffle")->load();
    const auto pat = proc.snapshotDrumPattern (slot);

    // Everything the slot holds, not just the masks: a lane's length, its clock
    // and its ratchets are as much the pattern as which steps are lit, and a
    // rebuild that quietly drops them hands the host a region that is not what
    // the plugin plays.
    DrumSequencer drums;
    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        drums.stepMask[lane].store (pat.drumSteps[lane]);
        drums.accentMask[lane].store (pat.drumAccents[lane]);
        drums.softMask[lane].store (pat.drumSofts[lane]);
        drums.ratchetMask[lane].store (pat.drumRatchets[lane]);
        drums.laneLength[lane].store (pat.laneLength[lane]);
        drums.laneFit[lane].store (pat.laneFit[lane]);
    }
    drums.normalise();

    return bp303::drumSequence (drums, lengthSteps, shuffle);
}

juce::File BP303AudioProcessorEditor::writeSlotMidiFile (bool bass, int slot)
{
    std::vector<juce::MidiMessageSequence> tracks;
    juce::String name;
    int len = 0;

    if (bass)
    {
        tracks.push_back (bassSlotSequence (slot, len));
        name = "BP303 Bass " + SongList::slotName (slot);
    }
    else
    {
        len = juce::jlimit (1, Sequencer303::maxSteps, proc.sequencer.length.load());
        tracks.push_back (drumSlotSequence (slot, len));
        name = "BP303 Drums " + SongList::slotName (slot);
    }

    // Written to a temp folder we own; the host copies it into the project.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("BP303");
    dir.createDirectory();

    const auto file = dir.getChildFile (name + ".mid");
    if (! bp303::writeMidiFile (file, tracks, len))
        return {};

    return file;
}

// The whole arrangement as one region: every row's patterns stamped down the
// timeline at the tick that row starts on, repeats expanded, holds resolved and
// mutes honoured — the same walk SongPlayer does, but over ticks rather than
// transport phase.
//
// Bass and drums go out as a track each. They used to share one, on the grounds
// that the file drops back onto a BP303 — which reads bass on channel 1 and drums
// on channel 10 — and plays the arrangement complete. That is the wrong shape for
// every other target: a host places regions per track, not per channel, so both
// lines landed on whichever single instrument the pointer was over. Splitting
// costs the round-trip its one-drop convenience; landing the drums on a drum
// instrument is worth more than that.
juce::File BP303AudioProcessorEditor::writeSongMidiFile()
{
    const int rows = proc.song.getCount();
    if (rows <= 0)
        return {};

    const bool wantBass  = songMidiMode != drumsOnly;
    const bool wantDrums = songMidiMode != bassOnly;

    juce::MidiMessageSequence bassOut, drumOut;
    double tick = 0.0;
    int totalSteps = 0;

    // A slot still reading `hold` means no row up to this point has named that
    // line. There is nothing to carry on from, so the line stays silent — the
    // same call the processor makes, and the reason a song plays the same way
    // twice regardless of what was loaded when you pressed play.
    int heldBass = SongPlayer::hold, heldDrum = SongPlayer::hold;

    for (int row = 0; row < rows; ++row)
    {
        const auto step = proc.song.getStep (row);

        if (step.bassSlot != SongPlayer::hold) heldBass = step.bassSlot;
        if (step.drumSlot != SongPlayer::hold) heldDrum = step.drumSlot;

        // The bass pattern owns the loop length, so it sets the row's length even
        // when the bass itself is muted. With no bass named yet there is nothing
        // to ask, so the live sequencer length stands in — as it does in locate().
        // Built even when the mode is about to throw it away: it is the thing
        // that reports the length, so a drums-only export has to lay out on the
        // same grid the other modes do.
        int lengthSteps = juce::jlimit (1, Sequencer303::maxSteps,
                                        proc.sequencer.length.load());
        juce::MidiMessageSequence bassSeq;
        if (heldBass != SongPlayer::hold)
            bassSeq = bassSlotSequence (heldBass, lengthSteps);

        juce::MidiMessageSequence drumSeq;
        if (wantDrums && heldDrum != SongPlayer::hold && ! step.drumMute)
            drumSeq = drumSlotSequence (heldDrum, lengthSteps);

        const int repeats = juce::jmax (1, step.repeats);
        for (int r = 0; r < repeats; ++r)
        {
            if (wantBass && heldBass != SongPlayer::hold && ! step.bassMute)
                bp303::appendAt (bassOut, bassSeq, tick);
            if (wantDrums && heldDrum != SongPlayer::hold && ! step.drumMute)
                bp303::appendAt (drumOut, drumSeq, tick);

            tick += (double) lengthSteps * bp303::ticksPer16th;
            totalSteps += lengthSteps;
        }
    }

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("BP303");
    dir.createDirectory();

    auto name = proc.getSongName();
    if (name.isEmpty())
        name = "Song";

    // A track name is what a host labels the region with once there is more than
    // one, so the two tracks don't both arrive called after the file.
    const auto named = [] (juce::MidiMessageSequence s, const juce::String& trackName)
    {
        s.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0);
        s.updateMatchedPairs();
        s.sort();
        return s;
    };

    std::vector<juce::MidiMessageSequence> tracks;
    juce::String suffix;

    if (songMidiMode == splitTracks)
    {
        // A track with no notes in it still arrives as a region, and a silent one
        // reads as half the drag having failed. A song with no drums in it should
        // just hand over the bass.
        if (bp303::hasNotes (bassOut)) tracks.push_back (named (bassOut, "BP303 Bass"));
        if (bp303::hasNotes (drumOut)) tracks.push_back (named (drumOut, "BP303 Drums"));
    }
    else if (songMidiMode == bassOnly)
    {
        tracks.push_back (named (bassOut, "BP303 Bass"));
        suffix = " Bass";
    }
    else
    {
        tracks.push_back (named (drumOut, "BP303 Drums"));
        suffix = " Drums";
    }

    const auto file = dir.getChildFile ("BP303 " + name + suffix + ".mid");
    if (! bp303::writeMidiFile (file, tracks, totalSteps))
        return {};

    return file;
}

bool BP303AudioProcessorEditor::shouldDropFilesWhenDraggedExternally (
        const juce::DragAndDropTarget::SourceDetails& details,
        juce::StringArray& files, bool& canMoveFiles)
{
    // JUCE offers the external drag as soon as the pointer is over no JUCE
    // component, and takes the internal drag away the moment we accept. Inside a
    // host window that test is not the same as having left the plugin — only
    // desktop components are searched, so a gap between our own components can
    // look identical to open desktop. Accepting there would kill a drag on its
    // way to the SONG list, so the pointer has to be genuinely outside first.
    //
    // Refusing does *not* mean "ask again later", though, which is what this used
    // to assume. DragAndDropContainer::checkForExternalDrag sets its
    // hasCheckedForExternalDrag flag before it calls this and never clears it, so
    // the offer comes exactly once per gesture: one refusal and the drag can
    // never become a file, however far outside the window it then travels. It
    // ends as an internal drag with no target, which JUCE dismisses by animating
    // the chip back to the key it came from. That is the bounce-back, and it is
    // why the export worked only when the pointer's path out of the window
    // happened to miss every interior dead spot. The watcher below is the second
    // chance JUCE doesn't give.
    if (externalDragFired || getScreenBounds().contains (pointerPosition()))
        return false;

    if (! buildDragFiles (details.description, files, canMoveFiles))
        return false;

    externalDragFired = true;   // so the watcher doesn't hand the same drag over twice
    return true;
}

// A song drag hands over the whole arrangement; a pattern-key drag hands over one
// slot. Anything else that leaves the window is left as a plain internal drag
// that goes nowhere.
bool BP303AudioProcessorEditor::buildDragFiles (const juce::var& description,
                                                juce::StringArray& files, bool& canMoveFiles)
{
    juce::File file;
    if (description.toString() == songDragDescription)
    {
        file = writeSongMidiFile();
    }
    else
    {
        bool bass = true;
        int slot = 0;
        if (! SongList::parseDrag (description, bass, slot))
            return false;

        file = writeSlotMidiFile (bass, slot);
    }
    if (! file.existsAsFile())
        return false;

    files.add (file.getFullPathName());
    canMoveFiles = false;   // the host copies it, so our temp file stays put
    return true;
}

// Watches a running drag for the pointer genuinely leaving the window, and hands
// the file over itself if JUCE's one offer was already spent inside. Polled
// rather than driven off mouseDrag, because once startDragging has taken the
// mouse the source component stops hearing about it.
void BP303AudioProcessorEditor::dragOperationStarted (const juce::DragAndDropTarget::SourceDetails& details)
{
    draggingDescription = details.description;
    externalDragFired = false;
    dragWatcher.startTimerHz (30);
}

void BP303AudioProcessorEditor::dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&)
{
    dragWatcher.stopTimer();
    draggingDescription = juce::var();
}

void BP303AudioProcessorEditor::checkDragLeftWindow()
{
    if (externalDragFired || getScreenBounds().contains (pointerPosition()))
        return;

    juce::StringArray files;
    bool canMoveFiles = false;
    if (! buildDragFiles (draggingDescription, files, canMoveFiles))
    {
        // Nothing to hand over — an empty pattern, say. Stop looking rather than
        // rebuilding the same missing file thirty times a second.
        dragWatcher.stopTimer();
        return;
    }

    externalDragFired = true;
    dragWatcher.stopTimer();
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, canMoveFiles, this);
}

void BP303AudioProcessorEditor::updateDistGroups()
{
    // SOFT and FUZZ are both drive-and-color shapers, so they share a group.
    auto show = [] (int type, FxPage& drive, FxPage& crush, FxPage& fold, FxPage& rect)
    {
        drive.setVisible (type == Distortion::Soft || type == Distortion::Fuzz);
        crush.setVisible (type == Distortion::Crush);
        fold .setVisible (type == Distortion::Fold);
        rect .setVisible (type == Distortion::Rect);
    };

    show (bassDistType.box.getSelectedItemIndex(),
          bassDriveGroup, bassCrushGroup, bassFoldGroup, bassRectGroup);
    show (drumDistType.box.getSelectedItemIndex(),
          drumDriveGroup, drumCrushGroup, drumFoldGroup, drumRectGroup);
}

void BP303AudioProcessorEditor::transposeBass (int semitones)
{
    for (auto& step : proc.sequencer.steps)
        Sequencer303::storePitch (step, Sequencer303::loadPitch (step) + semitones);
}

void BP303AudioProcessorEditor::shiftBass (int direction)
{
    auto& seq = proc.sequencer;
    // Rotates the steps the line plays, so it turns with the line's own length
    // rather than with the bar once the two differ.
    const int len = seq.lengthOf (juce::jlimit (1, 16, seq.length.load()));

    struct StepData { int pitch; bool gate; int dyn; bool slide; int hold; int ratchet; };
    StepData old[16];
    for (int i = 0; i < len; ++i)
        old[i] = { Sequencer303::loadPitch (seq.steps[i]),
                   seq.steps[i].gate.load(),
                   seq.steps[i].dyn.load(),
                   seq.steps[i].slide.load(),
                   seq.steps[i].hold.load(),
                   seq.steps[i].ratchet.load() };

    for (int i = 0; i < len; ++i)
    {
        const auto& src = old[((i - direction) % len + len) % len];
        Sequencer303::storePitch (seq.steps[i], src.pitch);
        seq.steps[i].gate.store (src.gate);
        seq.steps[i].dyn.store (dyn303::clampDyn (src.dyn));
        seq.steps[i].slide.store (src.slide);
        seq.steps[i].hold.store (src.hold);
        // carried with the step it belongs to, or shifting a pattern would
        // quietly flatten every split gate in it
        seq.steps[i].ratchet.store (src.ratchet);
    }
}

void BP303AudioProcessorEditor::shiftDrums (int direction)
{
    const int len = juce::jlimit (1, 16, proc.sequencer.length.load());
    const uint32_t lenMask = len >= 32 ? 0xffffffffu : ((1u << len) - 1u);

    auto rotate = [&] (uint32_t m) -> uint32_t {
        m &= lenMask;
        if (direction > 0)
            return ((m << 1) | (m >> (len - 1))) & lenMask;
        return ((m >> 1) | (m << (len - 1))) & lenMask;
    };

    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        proc.drumSequencer.stepMask[lane].store (rotate (proc.drumSequencer.stepMask[lane].load()));
        proc.drumSequencer.accentMask[lane].store (rotate (proc.drumSequencer.accentMask[lane].load()));
        proc.drumSequencer.softMask[lane].store (rotate (proc.drumSequencer.softMask[lane].load()));
    }
}

void BP303AudioProcessorEditor::showSkinMenu()
{
    juce::PopupMenu menu;
    const int current = proc.uiSkin.load();
    for (int i = 0; i < ui303::numSkins; ++i)
        menu.addItem (i + 1, ui303::skinName (i), true, i == current);

    menu.setLookAndFeel (&look);
    // Anchored at the pointer rather than at the component: `content` is drawn
    // through a scale transform, and the menu wants plain screen coordinates.
    const auto pos = pointerPosition();
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea ({ pos.x, pos.y, 1, 1 }),
                        [this] (int result)
                        {
                            if (result <= 0)
                                return;
                            proc.setSkinGlobally (result - 1);
                            applySkin();
                        });
}

// The label says what you will get rather than what the button is, since the
// mode is only visible here — the drag itself gives no chance to ask.
void BP303AudioProcessorEditor::setSongMidiMode (int mode)
{
    songMidiMode = juce::jlimit ((int) splitTracks, (int) drumsOnly, mode);
    songDragMidi.setButtonText (songMidiMode == bassOnly  ? "BASS"
                              : songMidiMode == drumsOnly ? "DRUM" : "MIDI");
}

void BP303AudioProcessorEditor::showSongMidiMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Drag song out as");
    menu.addItem (1, "Bass + drums, separate tracks", true, songMidiMode == splitTracks);
    menu.addItem (2, "Bass only",                     true, songMidiMode == bassOnly);
    menu.addItem (3, "Drums only",                    true, songMidiMode == drumsOnly);

    menu.setLookAndFeel (&look);
    // Anchored at the pointer for the same reason showSkinMenu is: `content` is
    // drawn through a scale transform, and the menu wants screen coordinates.
    const auto pos = pointerPosition();
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea ({ pos.x, pos.y, 1, 1 }),
                        [this] (int result)
                        {
                            if (result > 0)
                                setSongMidiMode (result - 1);
                        });
}

void BP303AudioProcessorEditor::setKeyHueTarget (int stop)
{
    if (((stop % ui303::hueStops) + ui303::hueStops) % ui303::hueStops == ui303::keyHue())
        return;

    ui303::setKeyHue (stop);
    // 30 Hz is plenty for a ~⅓-second colour fade, and keeps the transient
    // whole-window repaints to a fraction of what the grids already cost.
    if (! hueFade.isTimerRunning())
        hueFade.startTimerHz (30);
}

bool BP303AudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isShiftDown())
    {
        const int dir = key.isKeyCode (juce::KeyPress::rightKey) ? 1
                      : key.isKeyCode (juce::KeyPress::leftKey)  ? -1
                                                                 : 0;
        if (dir != 0)
        {
            setKeyHueTarget (ui303::keyHue() + dir);
            BP303AudioProcessor::saveGlobalKeyHue (ui303::keyHue());
            return true;
        }
    }
    return false;
}

void BP303AudioProcessorEditor::applySkin()
{
    const int skin = proc.uiSkin.load();
    look.setSkin (skin);

    const auto& pal = ui303::palette (skin);
    // classic's lcdBg is near-white, so this reads well for every skin
    lengthSlider.setColour (juce::Slider::textBoxBackgroundColourId, pal.lcdBg);
    lengthSlider.setColour (juce::Slider::textBoxTextColourId, pal.lcdText);
    lengthSlider.setColour (juce::Slider::textBoxOutlineColourId, pal.outline);

    // Colours live in the look & feel, so every child has to be told to re-read
    // them; the components that paint straight from the palette just repaint.
    // The chrome's cached image survives a parent repaint, so it needs its own.
    sendLookAndFeelChange();
    chrome.repaint();
    repaint();
}

void BP303AudioProcessorEditor::paintContent (juce::Graphics& g)
{
    const auto& p = ui303::palette (proc.uiSkin.load());

    if (p.retro)
    {
        g.setGradientFill (juce::ColourGradient (p.winBg1, 0.0f, 0.0f,
                                                 p.winBg2, 0.0f, (float) content.getHeight(), false));
        g.fillRect (content.getLocalBounds());
        if (proc.uiSkin.load() == 1)   // subtle CRT scanlines on the retro skin only
        {
            g.setColour (juce::Colours::white.withAlpha (0.02f));
            for (int yy = 0; yy < content.getHeight(); yy += 3)
                g.drawHorizontalLine (yy, 0.0f, (float) content.getWidth());
        }
    }
    else if (p.beveledPanels)   // studio: warm plastic body with a soft gradient
    {
        g.setGradientFill (juce::ColourGradient (p.winBg1, 0.0f, 0.0f,
                                                 p.winBg2, 0.0f, (float) content.getHeight(), false));
        g.fillRect (content.getLocalBounds());
    }
    else
    {
        g.fillAll (p.winBg1);
    }

    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title) {
        ui303::drawPanel (g, r, title, p);
    };

    auto area = content.getLocalBounds().reduced (8);
    panel (area.removeFromTop (120), "BADPACKETS  BP-303");
    area.removeFromTop (6);

    // row2: PERFORMANCE + BASS FX + DRUM FX (the FxSection children draw their own frames)
    auto row2 = area.removeFromTop (120);
    panel (row2.removeFromLeft (perfSectionW), "PERFORMANCE");
    area.removeFromTop (6);

    // LFO row: a plain titled panel, like PERFORMANCE
    //
    // This walk has to stay in step with layoutContent's, row for row — the two
    // are separate passes over one layout, and a row added to one and not the
    // other slides every frame below it out from under its contents without
    // moving the contents. That is what a new row breaks first, and it is
    // invisible until something is rendered.
    panel (area.removeFromTop (lfoRowH), "LFO 1");
    area.removeFromTop (6);

    // drum row: DRUMS, PAD and EQ all draw their own frames, like the FX panels
    area.removeFromTop (drumRowH);
    area.removeFromTop (6);

    // lower region: sequencer grids on the left, then the per-line keypads, then
    // the song arrangement down the right-hand edge
    auto lower = area;
    auto songCol = lower.removeFromRight (songColumnW);
    lower.removeFromRight (panelGap);
    auto keysCol = lower.removeFromRight (keysColumnW);
    lower.removeFromRight (panelGap);

    auto bassPanel = lower.removeFromTop (205);
    lower.removeFromTop (6);
    auto drumPanel = lower;

    panel (bassPanel, "BASS PATTERN");
    panel (drumPanel, "DRUM PATTERN");

    auto bassKeysPanel = keysCol.removeFromTop (205);
    keysCol.removeFromTop (6);
    panel (bassKeysPanel, "BASS PATTERNS");
    panel (keysCol, "DRUM PATTERNS");

    panel (songCol, "SONG");
}

void BP303AudioProcessorEditor::updatePadKnobs()
{
    // The same Pad the audio thread reads, asked the same question through the
    // same `apply` — so the arc round a knob is the offset that is actually
    // reaching the DSP, not a second guess at it. That includes the clamping: a
    // knob already at the top of its travel shows no arc, because the pad is
    // genuinely doing nothing to it.
    const auto pad = proc.readPad();

    // The LFO composes on top, in the same order the audio thread composes them
    // — pad first, then LFO — so the ring shows where the two together have
    // pushed the knob, clamps included. One property, because a knob with two
    // indicator systems would need every skin to know about both.
    const auto lfoState = proc.readLfo();
    const double lfoPhase = proc.lfoPhaseNow.load();

    for (const auto& pk : padKnobs)
    {
        float offset = 0.0f;

        if (auto* param = proc.apvts.getParameter (pk.knob->paramId))
        {
            const float base = param->getValue();
            offset = param->convertTo0to1 (
                         lfoState.apply (pk.dest,
                                         pad.apply (pk.dest, param->convertFrom0to1 (base)),
                                         lfoPhase)) - base;
        }

        // A tenth of a degree of arc is not a thing anyone can see, and a knob
        // whose offset has not really moved does not get to cost a repaint.
        auto& props = pk.knob->slider.getProperties();
        if (std::abs ((float) props.getWithDefault ("padOffset", 0.0f) - offset) < 0.002f)
            continue;

        props.set ("padOffset", offset);
        pk.knob->slider.repaint();
    }
}

juce::Point<int> BP303AudioProcessorEditor::nativeSize()
{
    return { nativeWidth, nativeHeight };
}

void BP303AudioProcessorEditor::layoutContent()
{
    auto area = content.getLocalBounds().reduced (8);

    chrome.setBounds (content.getLocalBounds());
    help.setBounds (content.getLocalBounds());
    helpButton.setBounds (content.getWidth() - 74, 12, 58, 20);

    // --- synth row ---
    auto synthRow = area.removeFromTop (120).reduced (10, 16);
    const int knobW = synthRow.getWidth() / 12;
    wave.setBounds (synthRow.removeFromLeft (knobW).reduced (6, 14));
    for (auto* k : { &tuning, &cutoff, &resonance, &envmod, &attack, &decay, &accent,
                     &volume, &uniVoices, &uniDetune, &uniSpread })
        k->setBounds (synthRow.removeFromLeft (knobW).reduced (4, 0));
    area.removeFromTop (6);

    // --- performance / dist / delay ---
    auto row2 = area.removeFromTop (120);

    auto perf = row2.removeFromLeft (perfSectionW).reduced (10, 16);
    const int perfW = perf.getWidth() / 3;
    auto perfCol1 = perf.removeFromLeft (perfW);
    playMode.setBounds (perfCol1.removeFromTop (46).reduced (4, 2));
    runButton.setBounds (perfCol1.removeFromTop (21).reduced (8, 1));
    recButton.setBounds (perfCol1.reduced (8, 1));
    auto perfCol2 = perf.removeFromLeft (perfW);
    metroButton.setBounds (perfCol2.removeFromTop (20).reduced (4, 1));
    intBpm.setBounds (perfCol2.reduced (6, 0));
    auto perfCol3 = perf;
    shuffle.setBounds (perfCol3.removeFromTop (62).reduced (10, 0));
    lengthLabel.setBounds (perfCol3.removeFromBottom (13));
    lengthSlider.setBounds (perfCol3.reduced (2, 0));
    row2.removeFromLeft (panelGap);

    // BASS FX and DRUM FX are the same width rather than a split of whatever the
    // row has left, which is what puts DRUM FX's left edge on the keypad rule
    // and BASS FX's right edge on the grid rule.
    bassFx.setBounds (row2.removeFromLeft (fxSectionW));
    row2.removeFromLeft (panelGap);
    drumFx.setBounds (row2);
    area.removeFromTop (6);

    // --- LFO row: full width, because the scope is what fills it -------------
    // Above the drums row rather than below it, so the two modulators — the pad
    // and the LFO — are not separated by the whole kit. Full content width
    // rather than sized to its controls: the controls take the left, and the
    // scope takes the rest, which is the part that says whether the patch is
    // moving at all.
    {
        // The title strip the panel frame draws into, then one row of controls
        // with the scope taking whatever is left. Fixed widths rather than a
        // share of the row, for the reason the drum pages use fixed columns: the
        // controls should not move when the scope beside them changes size.
        auto lfoRow = area.removeFromTop (lfoRowH).reduced (10, 0)
                          .withTrimmedTop (22).withTrimmedBottom (8);

        const auto col = [&lfoRow] (int w)
        {
            auto c = lfoRow.removeFromLeft (w);
            lfoRow.removeFromLeft (8);
            return c;
        };

        // Three stacked toggles now, so the column divides in thirds.
        auto switches = col (84);
        const int switchH = switches.getHeight() / 3;
        lfoOn.setBounds (switches.removeFromTop (switchH).reduced (0, 1));
        lfoSync.setBounds (switches.removeFromTop (switchH).reduced (0, 1));
        lfoSmooth.setBounds (switches.reduced (0, 1));

        lfoShape.setBounds (col (210));
        lfoDest .setBounds (col (150));
        lfoDiv  .setBounds (col (90));
        lfoRate .setBounds (col (76));
        lfoAmt  .setBounds (col (76));

        lfoScope.setBounds (lfoRow);
    }
    area.removeFromTop (6);

    // --- drums row: DRUMS on the left, then the PAD, then the EQ ---
    // DRUMS takes exactly the width its widest page needs — the page's own 6px
    // margins, the KIT column, then six knob columns — and the static_assert
    // beside padSectionW is what keeps those three widths landing the EQ on the
    // keypad rule, directly under DRUM FX.
    //
    // The EQ gave up the width: at 784 it was drawing ten octave bands across
    // most of the window and a ±14 dB curve through 67px, which is the wrong
    // aspect ratio in both directions. At 490 a band is still 45px wide, and
    // the taller row takes the curve from 2.4 px/dB to 4.
    {
        auto drumRow = area.removeFromTop (drumRowH);
        const int drumsW = 12 + drumKitColumnW + 6 * drumKnobColumnW;
        drums.setBounds (drumRow.removeFromLeft (drumsW));
        drumRow.removeFromLeft (panelGap);
        padSection.setBounds (drumRow.removeFromLeft (padSectionW));
        drumRow.removeFromLeft (panelGap);
        eqSection.setBounds (drumRow);

        // LATCH goes at the foot of the pad's readout column. Measured from the
        // section's own content area rather than from the pad child, so it does
        // not depend on the child having been resized first.
        auto padCol = padSection.contentArea().reduced (6);
        padCol.removeFromLeft (padCol.getHeight() + 10);   // clear the pad square
        padLatch.setBounds (padCol.removeFromBottom (20).withWidth (74)
                            + padSection.getPosition());

        // ACTIVE / FLAT / readout, right-aligned in the part of the EQ's tab
        // bar the two tabs leave empty. Measured from the section's own bar so
        // they stay put if the tab cap or the section width ever moves.
        auto bar = eqSection.tabBarFreeArea() + eqSection.getPosition();
        bar.removeFromRight (4);
        auto activeBox = bar.removeFromRight (78);
        bassEqOn.setBounds (activeBox);
        drumEqOn.setBounds (activeBox);
        bar.removeFromRight (6);
        eqFlat.setBounds (bar.removeFromRight (46).reduced (0, 1));
        bar.removeFromRight (10);
        eqReadout.setBounds (bar.removeFromRight (juce::jmax (0, bar.getWidth())));
    }
    // KIT sits in the column every page leaves clear. Placed after the section is
    // sized, since it is measured from the section's own content area.
    // KIT is centred on the same strip the knobs keep, for the same reason: the
    // taller row would otherwise stretch a three-segment switch to 94px.
    {
        auto kitArea = drums.contentArea().reduced (6, 6);
        kitArea = kitArea.withSizeKeepingCentre (
                      kitArea.getWidth(), juce::jmin (kitArea.getHeight(), drumKnobRowH));
        kit.setBounds (kitArea.removeFromLeft (drumKitColumnW).reduced (4, 8)
                       + drums.getPosition());
    }
    area.removeFromTop (6);

    // --- lower region: grids, then the keypads, then the song arrangement ---
    // These two columns are the rules everything above lines up on; see the
    // note by keysLeft.
    auto songCol = area.removeFromRight (songColumnW);
    area.removeFromRight (panelGap);
    auto keysCol = area.removeFromRight (keysColumnW);
    area.removeFromRight (panelGap);

    {
        auto songArea = songCol.reduced (10, 0).withTrimmedTop (24).withTrimmedBottom (10);

        auto hdr = songArea.removeFromTop (22);
        songTransport.setBounds (hdr.removeFromLeft (172));
        hdr.removeFromLeft (8);
        songLoopButton.setBounds (hdr);
        songArea.removeFromTop (5);

        auto bar = songArea.removeFromTop (20);
        const int btnW = bar.getWidth() / 6;
        for (auto* b : { &songIns, &songDel, &songDup, &songUp, &songDown, &songClear })
            b->setBounds (bar.removeFromLeft (btnW).reduced (1, 0));
        songArea.removeFromTop (5);

        songInfo.setBounds (songArea.removeFromBottom (15));

        auto lib = songArea.removeFromBottom (21);
        songSave.setBounds (lib.removeFromRight (52).reduced (1, 1));
        lib.removeFromRight (3);
        songDragMidi.setBounds (lib.removeFromRight (46).reduced (1, 1));
        lib.removeFromRight (4);
        songLibrary.setBounds (lib.reduced (1, 1));
        songArea.removeFromBottom (4);

        songList.setBounds (songArea);
    }

    // grids (left)
    auto patternPanel = area.removeFromTop (205);
    bassOnButton.setBounds (patternPanel.getRight() - 66, patternPanel.getY() + 3, 58, 18);
    auto bassStrip = patternPanel.reduced (10, 0).withTrimmedTop (20).removeFromTop (24);
    for (auto* b : { &bassCopy, &bassPaste, &bassClear, &bassTransDown, &bassTransUp,
                     &bassShiftL, &bassShiftR, &bassHold })
    {
        b->setBounds (bassStrip.removeFromLeft (60).reduced (0, 2));
        bassStrip.removeFromLeft (6);
    }
    stepGrid.setBounds (patternPanel.reduced (10, 0).withTrimmedTop (50).withTrimmedBottom (12));

    area.removeFromTop (6);
    drumsOnButton.setBounds (area.getRight() - 66, area.getY() + 3, 58, 18);
    auto drumStrip = area.reduced (10, 0).withTrimmedTop (20).removeFromTop (24);
    for (auto* b : { &drumCopy, &drumPaste, &drumClear, &drumShiftL, &drumShiftR })
    {
        b->setBounds (drumStrip.removeFromLeft (60).reduced (0, 2));
        drumStrip.removeFromLeft (6);
    }
    drumGrid.setBounds (area.reduced (10, 0).withTrimmedTop (50).withTrimmedBottom (12));

    // keypads (right), aligned with their grids
    auto bassKeysPanel = keysCol.removeFromTop (205);
    keysCol.removeFromTop (6);
    bassKeys.setBounds (bassKeysPanel.reduced (12, 0).withTrimmedTop (24).withTrimmedBottom (12));
    drumKeys.setBounds (keysCol.reduced (12, 0).withTrimmedTop (24).withTrimmedBottom (12));
}

void BP303AudioProcessorEditor::paint (juce::Graphics& g)
{
    // Letterbox behind the scaled content, in case a host forces a size that
    // doesn't quite match the aspect ratio.
    g.fillAll (ui303::palette (proc.uiSkin.load()).winBg2);
}

void BP303AudioProcessorEditor::resized()
{
    // Content keeps its native size and is scaled to fit, so the layout code
    // never has to know what size the window actually is.
    const float scale = juce::jmin ((float) getWidth()  / (float) nativeWidth,
                                    (float) getHeight() / (float) nativeHeight);

    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, nativeWidth, nativeHeight);

    // Reopen at the size you left it — but a host tearing an editor down can
    // size it to nothing on the way out, and remembering that would reopen the
    // next one at the minimum. Only real sizes count.
    if (getWidth() >= juce::roundToInt (nativeWidth * 0.6))
        proc.editorWidth.store (getWidth());
}
