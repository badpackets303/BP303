// Offline test that drawing into the LFO scope stores what it looks like it
// stores, and that the knob it modulates moves the way the drawing does.
//
// It exists because a sign error in a drawing surface is invisible in the code
// and obvious in the hand: every step of the chain — screen y, stored level,
// `valueAt`, the offset the knob draws — is individually plausible inverted, and
// the four have to agree. Screen y grows downward and a knob's value grows
// upward, so there is exactly one negation in the whole path and it lives in
// `levelAt`. This pins it there.
//
// The last check is the one that answers "why does dragging up turn the knob
// down": with AMOUNT negative that is correct behaviour, not a bug, because a
// bipolar depth is what lets one drawn shape push two destinations in opposite
// directions. So both signs are checked rather than just the intuitive one.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    void setParam (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
        {
            std::printf ("missing parameter %s: FAIL\n", id);
            ++failures;
        }
    }

    template <typename T>
    T* findDescendant (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* hit = dynamic_cast<T*> (child))
                return hit;
            if (auto* found = findDescendant<T> (*child))
                return found;
        }
        return nullptr;
    }

    juce::MouseEvent eventAt (juce::Component& c, juce::Point<float> p)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 p, juce::ModifierKeys::leftButtonModifier,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                 juce::Time::getCurrentTime(), p,
                 juce::Time::getCurrentTime(), 1, false };
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BP303AudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    setParam (proc, "lfo1on", 1.0f);
    setParam (proc, "lfo1sync", 0.0f);
    setParam (proc, "lfo1shape", (float) lfo::Custom);
    setParam (proc, "lfo1dest", 0.0f);      // CUT OFF
    setParam (proc, "lfo1amt", 0.9f);       // positive depth

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    const auto native = BP303AudioProcessorEditor::nativeSize();
    editor->setSize (native.x, native.y);
    editor->setVisible (true);

    auto* scope = findDescendant<LfoScope> (*editor);
    check (scope != nullptr, "the LFO scope is in the editor");
    if (scope == nullptr)
        return 1;

    const auto r = scope->getLocalBounds().toFloat();

    // --- up is up ------------------------------------------------------------
    // A point in the top quarter of the scope, in the first step's column.
    {
        const juce::Point<float> high { r.getX() + r.getWidth() * 0.02f,
                                        r.getY() + r.getHeight() * 0.15f };
        scope->mouseDown (eventAt (*scope, high));
        check (proc.lfoShape[0].load() > 0.3f,
               "dragging into the top of the scope stores a positive level");
    }

    // ...and down is down.
    {
        const juce::Point<float> low { r.getX() + r.getWidth() * 0.02f,
                                       r.getY() + r.getHeight() * 0.85f };
        scope->mouseDown (eventAt (*scope, low));
        check (proc.lfoShape[0].load() < -0.3f,
               "and into the bottom stores a negative one");
    }

    // --- the stored level is the one the shape plays -------------------------
    // `valueAt` has to read back what was painted at the phase that step covers,
    // or the picture and the sound are two different shapes.
    {
        const juce::Point<float> high { r.getX() + r.getWidth() * 0.02f,
                                        r.getY() + r.getHeight() * 0.15f };
        scope->mouseDown (eventAt (*scope, high));

        const auto l = proc.readLfo();
        const double midOfFirstStep = 0.5 / lfo::customSteps;
        check (std::abs (l.valueAt (midOfFirstStep) - proc.lfoShape[0].load()) < 1.0e-6f,
               "the shape plays back the level that was painted");
    }

    // --- a positive depth moves the knob the way the drawing goes ------------
    // `apply` at the phase covering step 0, against the cutoff default.
    {
        const auto& range = macropad::range (macropad::Cutoff);
        const float base = 500.0f;
        const double phase = 0.5 / lfo::customSteps;

        // painted high
        {
            const juce::Point<float> high { r.getX() + r.getWidth() * 0.02f,
                                            r.getY() + r.getHeight() * 0.15f };
            scope->mouseDown (eventAt (*scope, high));
            const auto l = proc.readLfo();
            check (l.apply (macropad::Cutoff, base, phase) > base,
                   "with AMOUNT positive, a step drawn up opens the filter");
        }

        // painted low
        {
            const juce::Point<float> low { r.getX() + r.getWidth() * 0.02f,
                                           r.getY() + r.getHeight() * 0.85f };
            scope->mouseDown (eventAt (*scope, low));
            const auto l = proc.readLfo();
            check (l.apply (macropad::Cutoff, base, phase) < base,
                   "...and a step drawn down closes it");
        }

        // --- and a negative depth deliberately inverts that ------------------
        // Not a bug: a bipolar AMOUNT is what lets the same drawn shape push one
        // destination up while another goes down. Pinned so nobody "fixes" it.
        setParam (proc, "lfo1amt", -0.9f);
        {
            const juce::Point<float> high { r.getX() + r.getWidth() * 0.02f,
                                            r.getY() + r.getHeight() * 0.15f };
            scope->mouseDown (eventAt (*scope, high));
            const auto l = proc.readLfo();
            check (l.apply (macropad::Cutoff, base, phase) < base,
                   "with AMOUNT negative, a step drawn up closes it instead");
        }
        (void) range;
    }

    // --- the end marker and painting are two gestures on one surface ---------
    // A drag near the marker moves the loop point; a drag away from it paints.
    // If the split were wrong, shortening the loop would smear a step instead,
    // or you could never reach the marker to shorten it at all.
    {
        setParam (proc, "lfo1len", 16.0f);   // marker at the right edge
        setParam (proc, "lfo1amt", 0.9f);

        // A press in the middle of the plot, nowhere near the far-right marker,
        // paints — it must not move the length.
        const juce::Point<float> mid { r.getX() + r.getWidth() * 0.5f,
                                       r.getY() + r.getHeight() * 0.2f };
        scope->mouseDown (eventAt (*scope, mid));
        check ((int) proc.apvts.getRawParameterValue ("lfo1len")->load() == 16,
               "painting a step must not move the loop point");

        // A press two-thirds across, then a drag onto it, grabs the marker and
        // sets the length to that column — eight steps of sixteen is halfway.
        const float halfX = r.getX() + r.getWidth() * 0.5f;
        scope->mouseDown (eventAt (*scope, { r.getRight() - 1.0f, r.getCentreY() }));
        scope->mouseDrag (eventAt (*scope, { halfX, r.getCentreY() }));
        check ((int) proc.apvts.getRawParameterValue ("lfo1len")->load() == 8,
               "dragging the end marker to the middle sets an eight-step loop");
        scope->mouseUp (eventAt (*scope, { halfX, r.getCentreY() }));
    }

    std::printf (failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
