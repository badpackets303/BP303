// ATTACK: the filter envelope's rise, which a real 303 does not have.
//
// The original jumps the filter envelope straight to the top on every note, and
// three existing behaviours are built on that jump — accents snap, slides don't
// retrigger, and a soft step opens to a lower peak than a normal one. Adding a
// rise in front of the decay could quietly break any of them, so each is pinned
// here alongside the knob's own behaviour.
//
// JUCE-free: this only needs Synth303.
// Build: clang++ -std=c++17 -O2 Tools/attack_test.cpp -o attack_test

#include "../Source/StepDyn.h"
#include "../Source/Synth303.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok)
            ++failures;
    }

    constexpr double sr = 44100.0;

    int ms (double n) { return (int) (sr * n / 1000.0); }

    // Base cutoff two octaves *below* the note being played, env mod at full, no
    // resonance. fc = cutoff * 2^(envMod * 5 * env), so the envelope sweeps 30 Hz
    // to 960 Hz: closed, the 110 Hz fundamental is right down the 4-pole slope
    // and the note is nearly silent; open, it passes. That makes level a strong
    // and monotonic read on the filter envelope, which is the only way to see it
    // from outside the voice.
    Synth303 makeVoice (float attackMs, float decayMs = 800.0f)
    {
        Synth303 s;
        s.prepare (sr);
        //           wave              tune  cutoff res   envMod decay     acc   vol   vibS  vibD  attack
        s.setParams (Synth303::Wave::Saw, 0.0f, 30.0f, 0.0f, 1.0f, decayMs, 0.0f, 0.0f, 0.0f, 0.0f, attackMs);
        return s;
    }

    float rmsOver (Synth303& s, int numSamples)
    {
        std::vector<float> buf ((size_t) numSamples);
        s.render (buf.data(), numSamples);

        double sum = 0.0;
        for (float v : buf)
            sum += (double) v * v;
        return (float) std::sqrt (sum / std::max (1, numSamples));
    }

    // The *amp* envelope has its own fixed ~3 ms rise, to keep note starts from
    // clicking. ATTACK does not touch it, but it makes the first few samples of
    // any note unrepresentative. Render past it before measuring.
    void settleAmp (Synth303& s)
    {
        std::vector<float> buf ((size_t) ms (15));
        s.render (buf.data(), (int) buf.size());
    }
}

int main()
{
    // --- zero attack is the 303, and is what every old project loads with -----
    {
        Synth303 s = makeVoice (0.0f);
        s.noteOn (45, dyn303::Normal, false);
        settleAmp (s);
        const float early = rmsOver (s, ms (10));
        rmsOver (s, ms (380));
        const float late = rmsOver (s, ms (10));
        std::printf ("      [attack 0: at 15ms %.4f  at 400ms %.4f]\n", early, late);
        check (early > late * 1.2f,
               "with ATTACK at zero the note is brightest immediately, then decays");
    }

    // --- a long attack opens the filter gradually ------------------------------
    {
        Synth303 s = makeVoice (200.0f);
        s.noteOn (45, dyn303::Normal, false);
        settleAmp (s);
        const float early = rmsOver (s, ms (10));
        rmsOver (s, ms (80));
        const float mid = rmsOver (s, ms (10));
        rmsOver (s, ms (75));
        const float atTop = rmsOver (s, ms (10));
        std::printf ("      [attack 200ms: 15ms %.4f  105ms %.4f  190ms %.4f]\n",
                     early, mid, atTop);
        check (early < mid && mid < atTop,
               "with ATTACK up the note opens gradually instead of snapping");
        check (early < atTop * 0.8f, "and starts well below where it ends up");
    }

    // --- the ramp takes the time the knob says ---------------------------------
    // A one-pole would only be 63% of the way there after its own time constant;
    // this is the assertion that stops anyone quietly swapping the ramp for one.
    {
        Synth303 slow = makeVoice (100.0f, 4000.0f);   // slow decay, so the top is flat
        slow.noteOn (45, dyn303::Normal, false);
        rmsOver (slow, ms (100));
        const float atKnobTime = rmsOver (slow, ms (10));

        Synth303 open = makeVoice (0.0f, 4000.0f);
        open.noteOn (45, dyn303::Normal, false);
        settleAmp (open);
        const float wideOpen = rmsOver (open, ms (10));

        std::printf ("      [100ms attack at 100ms %.4f  vs no attack %.4f]\n",
                     atKnobTime, wideOpen);
        check (atKnobTime > wideOpen * 0.9f,
               "a 100 ms attack is essentially open at 100 ms, not a third of the way");
    }

    // --- accents keep their snap ----------------------------------------------
    // The accent's whole character is the immediate jump, so ATTACK deliberately
    // does not apply to it.
    {
        Synth303 s = makeVoice (200.0f);
        s.noteOn (45, dyn303::Hard, false);
        settleAmp (s);
        const float early = rmsOver (s, ms (10));
        rmsOver (s, ms (180));
        const float late = rmsOver (s, ms (10));
        std::printf ("      [accented, attack 200ms: 15ms %.4f  200ms %.4f]\n", early, late);
        check (early > late * 1.2f, "an accented note still snaps open, ignoring ATTACK");
    }

    // --- a slide does not re-attack -------------------------------------------
    // Ties keep the envelope running. If ATTACK restarted it, every slid note
    // would swell from nothing and the classic glide would fall apart.
    {
        Synth303 s = makeVoice (200.0f);
        s.noteOn (45, dyn303::Normal, false);
        rmsOver (s, ms (400));                 // let it reach the top
        const float beforeSlide = rmsOver (s, ms (10));

        s.noteOn (48, dyn303::Normal, true);          // tied
        s.noteOff (45);
        const float afterSlide = rmsOver (s, ms (10));

        std::printf ("      [before slide %.4f  after slide %.4f]\n",
                     beforeSlide, afterSlide);
        check (afterSlide > beforeSlide * 0.7f,
               "a slid note carries the open filter across instead of re-attacking");
    }

    // --- a soft step still opens less than a normal one ------------------------
    // The soft step's lower peak is a dynamics feature, and the attack ramp has
    // to rise *to that peak* rather than past it to the normal one.
    {
        Synth303 normal = makeVoice (50.0f);
        normal.noteOn (45, dyn303::Normal, false);
        rmsOver (normal, ms (60));
        const float normalTop = rmsOver (normal, ms (10));

        Synth303 soft = makeVoice (50.0f);
        soft.noteOn (45, dyn303::Soft, false);
        rmsOver (soft, ms (60));
        const float softTop = rmsOver (soft, ms (10));

        std::printf ("      [normal %.4f  soft %.4f]\n", normalTop, softTop);
        check (softTop < normalTop * 0.95f,
               "a soft step still opens less than a normal one, ATTACK or not");
    }

    // --- a note shorter than its attack never fully opens ----------------------
    {
        Synth303 s = makeVoice (400.0f);
        s.noteOn (45, dyn303::Normal, false);
        settleAmp (s);
        rmsOver (s, ms (35));
        const float partWay = rmsOver (s, ms (10));

        Synth303 open = makeVoice (0.0f);
        open.noteOn (45, dyn303::Normal, false);
        settleAmp (open);
        const float fully = rmsOver (open, ms (10));

        std::printf ("      [50ms into a 400ms attack %.4f  vs fully open %.4f]\n",
                     partWay, fully);
        check (partWay < fully * 0.8f,
               "a note only 50 ms into a 400 ms attack is still well short of open");
    }

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
