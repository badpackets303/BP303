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
            const bool  lit = a <= angle + 1.0e-3f;
            const float s = std::sin (a), c = std::cos (a);
            // Each tick is tinted for its own place on the dial, not for where
            // the knob currently sits, so the collar is a ramp that turning
            // uncovers rather than a block of colour that changes wholesale.
            //
            // Ticks in the extreme zone lengthen and thicken as well as shift
            // colour. On a mark this small the hue change alone is almost
            // invisible, and it would carry nothing at all to someone who reads
            // the two hues as the same — the size change does the work.
            const float warn  = lit ? hotAmount (tt, hot) : 0.0f;
            const float inner = ri - warn * (ro - ri) * 0.55f;
            g.setColour (lit ? valueTint (accent, tt, hot) : p.tickArc.withAlpha (0.7f));
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

        if (angle > startAngle + 1.0e-3f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                                 startAngle, angle, true);
            g.setColour (valueTint (accent, sliderPos, hot).withAlpha (0.28f));
            g.strokePath (value, juce::PathStrokeType (thick + 3.0f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::butt));

            // A ColourGradient is linear or radial and never angular, so the
            // ramp round the ring is laid down as a short run of arcs. They
            // overlap by a hair; drawn exactly end to end the joins read as
            // notches in the ring.
            constexpr int segments = 12;
            const float span = angle - startAngle;
            for (int i = 0; i < segments; ++i)
            {
                const float a0 = startAngle + span * (float) i / (float) segments;
                const float a1 = juce::jmin (angle,
                                             startAngle + span * (float) (i + 1)
                                                 / (float) segments + 0.015f);
                juce::Path seg;
                seg.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, a0, a1, true);
                g.setColour (valueTint (accent, sliderPos * ((float) i + 0.5f)
                                                   / (float) segments, hot));
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

juce::Rectangle<int> FxSection::contentArea() const
{
    return getLocalBounds().withTrimmedTop (titleH + tabH + 2).withTrimmedBottom (4);
}

void FxSection::showTab (int i)
{
    current = juce::jlimit (0, (int) pages.size() - 1, i);
    for (int k = 0; k < (int) pages.size(); ++k)
        pages[(size_t) k]->setVisible (k == current);
    repaint();
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
    const float segW = bar.getWidth() / (float) n;
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
    const int i = juce::jlimit (0, n - 1, (e.x - bar.getX()) * n / juce::jmax (1, bar.getWidth()));
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
    v.length  = proc.sequencer.length.load();
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
    const int len = proc.sequencer.length.load();

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
    const int len = juce::jlimit (1, 16, proc.sequencer.length.load());
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
        // Defer the on/off toggle to mouseUp: a horizontal drag from here sets the
        // note's length instead (see mouseDrag / mouseUp).
        gateDragCol = col;
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
            const int len = juce::jlimit (1, 16, proc.sequencer.length.load());
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

    // A plain click on the GATE row (no drag) toggles the note on/off.
    if (gateDragCol >= 0 && ! gateDragMoved)
    {
        auto& step = proc.sequencer.steps[gateDragCol];
        step.gate.store (! step.gate.load());
        step.hold.store (1);
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
    v.playing = proc.drumSequencer.playingStep.load();
    // The length still comes from the bass pattern, which owns it.
    v.length  = proc.sequencer.length.load();

    juce::uint64 h = 1469598103934665603ull;
    const auto mix = [&h] (juce::uint64 value) { h = (h ^ value) * 1099511628211ull; };

    for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
    {
        mix (proc.drumSequencer.stepMask[lane].load());
        mix (proc.drumSequencer.accentMask[lane].load());
        mix (proc.drumSequencer.softMask[lane].load());
    }
    v.pattern = h;
    return v;
}

juce::Rectangle<int> DrumGrid::playheadCellBounds (int col) const
{
    if (col < 0 || col > 15)
        return {};

    const int cellW = (getWidth() - labelW) / 16;
    return { labelW + col * cellW, 0, cellW, getHeight() / DrumSequencer::numLanes };
}

void DrumGrid::repaintPlayhead (int col)
{
    if (const auto r = playheadCellBounds (col); ! r.isEmpty())
        repaint (r);
}

void DrumGrid::timerCallback()
{
    const auto now = liveView();
    if (now == shown)
        return;

    if (now.sameApartFromPlayhead (shown))
    {
        // Only the marker on the top lane moves.
        repaintPlayhead (shown.playing);
        repaintPlayhead (now.playing);
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
    // sequencer; the length still comes from the bass pattern, which owns it.
    const int playing = proc.drumSequencer.playingStep.load();
    const int len = proc.sequencer.length.load();
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

            if (col == playing && lane == 0)
            {
                g.setColour ((p.retro ? p.title : p.text).withAlpha (0.6f));
                g.drawRoundedRectangle (cell, 3.0f, 1.5f);
            }
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

    // The pressed cell decides the operation; a drag then repeats it across cells.
    // Left button owns whether there is a hit at all; right (or shift) owns how
    // hard it lands, cycling normal -> accent -> soft the same way the bass ACC
    // row does. An empty cell right-clicks straight to an accent, as it always
    // has — the level ring never turns a hit off, that stays the left button's
    // job.
    if (e.mods.isRightButtonDown() || e.mods.isShiftDown())
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

void DrumGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int cellW = (getWidth() - labelW) / 16;
    if (paintMode == Paint::None || paintLane < 0 || cellW <= 0 || e.x < labelW)
        return;

    // Locked to the lane the drag began in, so a diagonal drag stays on one voice.
    const int col = juce::jlimit (0, 15, (e.x - labelW) / cellW);
    applyPaint (paintLane, col);
    repaint();
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
                                            int hotEnd)
{
    parent.addAndMakeVisible (slider);
    // The look-and-feel draws every knob through one function and has no idea
    // which parameter it is looking at, so the hot end rides along as a property.
    slider.getProperties().set ("hotEnd", hotEnd);
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
    vibSpeed.init (content, proc.apvts, "vibspeed", "VIB SPEED");
    vibDepth.init (content, proc.apvts, "vibdepth", "VIB DEPTH");

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

    kit.init (content, proc, proc.apvts, "kit", "KIT");
    bdTune.init (content, proc.apvts, "bdtune", "BD TUNE");
    sdTune.init (content, proc.apvts, "sdtune", "SD TUNE");
    cpTune.init (content, proc.apvts, "cptune", "CP TUNE");
    hatTune.init (content, proc.apvts, "hattune", "HAT TUNE");
    bdDecay.init (content, proc.apvts, "bddecay", "BD DECAY");
    drumVol.init (content, proc.apvts, "drumvol", "DRUM VOL");
    static const char* laneIds[] = { "bdlvl", "sdlvl", "cplvl", "chlvl", "ohlvl" };
    static const char* laneTexts[] = { "BD", "SD", "CP", "CH", "OH" };
    for (int i = 0; i < 5; ++i)
        laneKnobs[i].init (content, proc.apvts, laneIds[i], laneTexts[i]);

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

    // --- page layouts (invoked when each page is sized by its FxSection) ---
    // ACTIVE | TYPE, then three equal knob columns: LOWS plus the selected
    // type's own pair. The group is handed exactly two columns so its own split
    // lands on the same width, keeping all three evenly spaced whichever type is
    // showing — the same three-across layout the COMP and REVERB tabs use.
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
    // The lower limit is about 60%, which fits a 1366x768 laptop with room to
    // spare; the upper is the native size, past which it would just be soft.
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (nativeWidth * 0.6),
                     juce::roundToInt (nativeHeight * 0.6),
                     nativeWidth, nativeHeight);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) nativeWidth / (double) nativeHeight);

    // Reopen at the size this project was left at, clamped to what fits here.
    const auto screen = juce::Desktop::getInstance().getDisplays()
                            .getPrimaryDisplay();
    int width = juce::jlimit (juce::roundToInt (nativeWidth * 0.6), nativeWidth,
                              rememberedWidth);
    // Only shrink to fit when we actually know the screen size — a headless or
    // not-yet-configured display reports an empty area, and clamping to that
    // would open every window at the minimum.
    if (screen != nullptr && screen->userArea.getWidth() > 400
                          && screen->userArea.getHeight() > 300)
    {
        // Leave room for the host's window frame.
        const auto usable = screen->userArea;
        width = juce::jmin (width, usable.getWidth() - 40,
                            juce::roundToInt ((usable.getHeight() - 80)
                                              * (double) nativeWidth / nativeHeight));
        width = juce::jmax (juce::roundToInt (nativeWidth * 0.6), width);
    }
    setSize (width, juce::roundToInt (width * (double) nativeHeight / nativeWidth));

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
}

BP303AudioProcessorEditor::~BP303AudioProcessorEditor()
{
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
        "is known for. Slide and accent together is the classic sound." });

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
        "over, so you can lay in a hat pattern in one gesture." });

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

    steps.push_back ({ around ({ &songLibrary, &songSave }), "Saving songs",
        "SAVE writes a .bp303song file to your Music folder, under BP303/Songs. "
        "The dropdown lists what's in there.\n\n"
        "A song file carries copies of every pattern it uses, so it plays correctly "
        "even in a project whose banks hold something else. Loading a song will "
        "overwrite those pattern slots." });

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

juce::File BP303AudioProcessorEditor::writeSlotMidiFile (bool bass, int slot)
{
    const float shuffle = proc.apvts.getRawParameterValue ("shuffle")->load();

    // The exporters read a sequencer, so the slot is loaded into a scratch one
    // rather than duplicating the timing rules for stored patterns.
    std::vector<juce::MidiMessageSequence> tracks;
    juce::String name;
    int len = 0;

    if (bass)
    {
        const auto pat = proc.snapshotBassPattern (slot);

        Sequencer303 seq;
        for (int i = 0; i < Sequencer303::maxSteps; ++i)
        {
            Sequencer303::storePitch (seq.steps[i], pat.bass[i].pitch);
            seq.steps[i].gate.store (pat.bass[i].gate);
            seq.steps[i].dyn.store (dyn303::clampDyn (pat.bass[i].dyn));
            seq.steps[i].slide.store (pat.bass[i].slide);
            seq.steps[i].hold.store (juce::jlimit (1, Sequencer303::maxSteps, pat.bass[i].hold));
        }
        len = juce::jlimit (1, Sequencer303::maxSteps, pat.length);
        seq.length.store (len);

        tracks.push_back (bp303::bassSequence (seq, shuffle));
        name = "BP303 Bass " + SongList::slotName (slot);
    }
    else
    {
        const auto pat = proc.snapshotDrumPattern (slot);

        DrumSequencer drums;
        for (int lane = 0; lane < DrumSequencer::numLanes; ++lane)
        {
            drums.stepMask[lane].store (pat.drumSteps[lane]);
            drums.accentMask[lane].store (pat.drumAccents[lane]);
            drums.softMask[lane].store (pat.drumSofts[lane]);
        }
        drums.normalise();
        // A drum pattern carries no length of its own — it runs to whatever the
        // bass line's length is, so the live length is what the region gets.
        len = juce::jlimit (1, Sequencer303::maxSteps, proc.sequencer.length.load());

        tracks.push_back (bp303::drumSequence (drums, len, shuffle));
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
    if (getScreenBounds().contains (pointerPosition()))
        return false;

    // Only a pattern-key drag has anything to hand over; anything else that
    // leaves the window is left as a plain internal drag that goes nowhere.
    bool bass = true;
    int slot = 0;
    if (! SongList::parseDrag (details.description, bass, slot))
        return false;

    const auto file = writeSlotMidiFile (bass, slot);
    if (! file.existsAsFile())
        return false;

    files.add (file.getFullPathName());
    canMoveFiles = false;   // the host copies it, so our temp file stays put
    return true;
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
    const int len = juce::jlimit (1, 16, seq.length.load());

    struct StepData { int pitch; bool gate; int dyn; bool slide; int hold; };
    StepData old[16];
    for (int i = 0; i < len; ++i)
        old[i] = { Sequencer303::loadPitch (seq.steps[i]),
                   seq.steps[i].gate.load(),
                   seq.steps[i].dyn.load(),
                   seq.steps[i].slide.load(),
                   seq.steps[i].hold.load() };

    for (int i = 0; i < len; ++i)
    {
        const auto& src = old[((i - direction) % len + len) % len];
        Sequencer303::storePitch (seq.steps[i], src.pitch);
        seq.steps[i].gate.store (src.gate);
        seq.steps[i].dyn.store (dyn303::clampDyn (src.dyn));
        seq.steps[i].slide.store (src.slide);
        seq.steps[i].hold.store (src.hold);
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
    panel (row2.removeFromLeft (330), "PERFORMANCE");
    area.removeFromTop (6);

    // drum row: DRUMS spans the full width
    panel (area.removeFromTop (120), "DRUMS");
    area.removeFromTop (6);

    // lower region: sequencer grids on the left, then the per-line keypads, then
    // the song arrangement down the right-hand edge
    auto lower = area;
    auto songCol = lower.removeFromRight (280);
    lower.removeFromRight (6);
    auto keysCol = lower.removeFromRight (204);
    lower.removeFromRight (6);

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

void BP303AudioProcessorEditor::layoutContent()
{
    auto area = content.getLocalBounds().reduced (8);

    chrome.setBounds (content.getLocalBounds());
    help.setBounds (content.getLocalBounds());
    helpButton.setBounds (content.getWidth() - 74, 12, 58, 20);

    // --- synth row ---
    auto synthRow = area.removeFromTop (120).reduced (10, 16);
    const int knobW = synthRow.getWidth() / 11;
    wave.setBounds (synthRow.removeFromLeft (knobW).reduced (6, 14));
    for (auto* k : { &tuning, &cutoff, &resonance, &envmod, &attack, &decay, &accent,
                     &volume, &vibSpeed, &vibDepth })
        k->setBounds (synthRow.removeFromLeft (knobW).reduced (4, 0));
    area.removeFromTop (6);

    // --- performance / dist / delay ---
    auto row2 = area.removeFromTop (120);

    auto perf = row2.removeFromLeft (330).reduced (10, 16);
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
    row2.removeFromLeft (6);

    // BASS FX and DRUM FX tabbed panels share the rest of the row
    const int fxW = (row2.getWidth() - 6) / 2;
    bassFx.setBounds (row2.removeFromLeft (fxW));
    row2.removeFromLeft (6);
    drumFx.setBounds (row2);
    area.removeFromTop (6);

    // --- drums row: DRUMS panel spans the full width ---
    auto drumRow = area.removeFromTop (120).reduced (10, 16);
    // KIT is a 3-segment switch, so give it more width than a knob column
    kit.setBounds (drumRow.removeFromLeft (96).reduced (4, 14));
    // per-voice tune + decay/vol globals, then the 5 lane levels
    const int drumW = drumRow.getWidth() / 12;
    for (auto* k : { &bdTune, &sdTune, &cpTune, &hatTune, &bdDecay, &drumVol })
        k->setBounds (drumRow.removeFromLeft (drumW).reduced (4, 0));
    for (auto& k : laneKnobs)
        k.setBounds (drumRow.removeFromLeft (drumW).reduced (4, 0));
    area.removeFromTop (6);

    // --- lower region: grids, then the keypads, then the song arrangement ---
    auto songCol = area.removeFromRight (280);
    area.removeFromRight (6);
    auto keysCol = area.removeFromRight (204);
    area.removeFromRight (6);

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
        songSave.setBounds (lib.removeFromRight (56).reduced (1, 1));
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
