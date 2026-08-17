// Offline tests for the LFO core: its shapes, how it takes its phase, and the
// identity that lets it exist at all.
//
// The properties that matter:
//
//   1. An inactive LFO is *bit-identical*, not close. It reaches the 303's own
//      cutoff, and depths are in normalised units, which means a round trip
//      through the parameter's skew — and that round trip is not exact in
//      float. So `apply` has to hand the base value straight back rather than
//      compute a zero offset. There are three ways to be inactive (switched
//      off, routed nowhere, zero depth) and all three have to take that path,
//      because all three are states a real patch sits in.
//   2. It reaches only its own destination. A routing to CUT OFF must not move
//      resonance.
//   3. Offsets clamp inside the destination's range from any starting point,
//      including from the ends.
//   4. Synced phase is *derived* from the transport, so equal beat positions
//      give equal phase no matter how the playhead got there. This is the
//      property that makes a host loop land the LFO back where the bar says,
//      and it is the whole reason phase is not an accumulator.
//   5. The shapes stay in [-1, 1] and agree at phase 0 where they can, so
//      changing shape under a running LFO shifts the waveform rather than
//      jumping the value it is putting out.
//   6. Sample & hold holds — one value per cycle — and holds the *same* value
//      for the same cycle every time, since it is a hash of the cycle index
//      rather than a running random. A loop that re-randomised would make a
//      pattern unrepeatable.
//
// Build: clang++ -std=c++17 -O2 Tools/lfo_test.cpp -o lfo_test

#include "../Source/Lfo.h"

#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        if (! ok)
        {
            std::printf ("FAIL: %s\n", what);
            ++failures;
        }
    }

    // The defaults a fresh instance actually starts on, for the destinations an
    // LFO can currently be routed to. These are the values that have to stay
    // untouched.
    constexpr float cutoffDefault = 500.0f;
    constexpr float resDefault    = 0.5f;
    constexpr float envDefault    = 0.5f;

    lfo::Lfo live()
    {
        lfo::Lfo l;
        l.on    = true;
        l.dest  = macropad::Cutoff;
        l.depth = 0.5f;
        return l;
    }
}

int main()
{
    // 1. The three ways of doing nothing, each bit-identical at every phase.
    {
        lfo::Lfo off = live();      off.on = false;
        lfo::Lfo nowhere = live();  nowhere.dest = macropad::numDests;
        lfo::Lfo silent = live();   silent.depth = 0.0f;

        bool identical = true;
        for (int i = 0; i <= 64; ++i)
        {
            const double ph = i / 64.0;
            for (const auto& l : { off, nowhere, silent })
            {
                identical = identical
                    && l.apply (macropad::Cutoff,    cutoffDefault, ph) == cutoffDefault
                    && l.apply (macropad::Resonance, resDefault,    ph) == resDefault
                    && l.apply (macropad::EnvMod,    envDefault,    ph) == envDefault;
            }
            check (! off.active() && ! nowhere.active() && ! silent.active(),
                   "an LFO doing nothing must report itself inactive");
        }
        check (identical, "an inactive LFO must be bit-identical at every phase");
    }

    // ...and the other end, or the identity above would be passing for the
    // wrong reason.
    {
        const auto l = live();
        bool moved = false;
        for (int i = 0; i <= 64; ++i)
            moved = moved || l.apply (macropad::Cutoff, cutoffDefault, i / 64.0)
                                 != cutoffDefault;
        check (moved, "a live LFO must actually move its destination");
    }

    // 2. Only its own destination.
    {
        const auto l = live();   // routed to Cutoff
        bool others = true;
        for (int i = 0; i <= 64; ++i)
        {
            const double ph = i / 64.0;
            others = others
                && l.apply (macropad::Resonance, resDefault, ph) == resDefault
                && l.apply (macropad::EnvMod,    envDefault, ph) == envDefault
                && l.apply (macropad::DelayMix,  0.25f,      ph) == 0.25f;
        }
        check (others, "an LFO must not reach a destination it is not routed to");
    }

    // 3. Clamped inside the range from anywhere, including from the ends and at
    // full depth in both directions.
    {
        bool inRange = true;
        for (float d : { -1.0f, -0.5f, 0.5f, 1.0f })
        {
            auto l = live();
            l.depth = d;

            for (auto dest : { macropad::Cutoff, macropad::Resonance, macropad::EnvMod })
            {
                l.dest = dest;
                const auto& r = macropad::range (dest);

                for (float base : { r.start, r.from01 (0.5f), r.end })
                    for (int i = 0; i <= 64; ++i)
                    {
                        const float v = l.apply (dest, base, i / 64.0);
                        inRange = inRange && v >= r.start - 1.0e-3f
                                          && v <= r.end   + 1.0e-3f;
                    }
            }
        }
        check (inRange, "offsets must clamp inside the destination's range");
    }

    // 4. Derived phase: the same beat gives the same phase, whatever route the
    // playhead took to get there. Phrased as "a loop returns it" because that
    // is the failure being guarded against.
    {
        lfo::Lfo l;
        l.sync = true;
        l.div  = lfo::Eighth;

        const auto wrapped = [] (double p) { return p - std::floor (p); };

        const double atBarStart = l.phaseFromBeats (8.0);
        const double afterLoop  = l.phaseFromBeats (16.0);   // two bars later

        check (std::abs (wrapped (atBarStart) - wrapped (afterLoop)) < 1.0e-12,
               "a synced LFO must land on the same phase at the same bar position");

        // ...and the rate is the division it claims to be.
        check (std::abs (l.phaseFromBeats (0.5) - 1.0) < 1.0e-12,
               "a 1/8 LFO must complete one cycle in half a beat");
        check (std::abs (lfo::beatsPerCycle (lfo::OneBar) - 4.0) < 1.0e-12,
               "a 1 BAR LFO must take four beats");
    }

    // 5. Shapes: bounded, and agreeing at phase 0 where they can. Square is the
    // exception and has to be — it has no centre to leave from.
    {
        bool bounded = true;
        for (int s = 0; s < lfo::numShapes; ++s)
        {
            lfo::Lfo l;
            l.shape = s;
            for (int i = 0; i <= 512; ++i)
            {
                const float v = l.valueAt (i / 64.0);   // several cycles
                bounded = bounded && v >= -1.0f && v <= 1.0f;
            }
        }
        check (bounded, "every shape must stay inside [-1, 1]");

        for (int s : { lfo::Sine, lfo::Triangle, lfo::Saw })
        {
            lfo::Lfo l;
            l.shape = s;
            check (std::abs (l.valueAt (0.0)) < 1.0e-6f,
                   "sine, triangle and saw must leave the centre at phase 0");
            check (l.valueAt (0.1) > 0.0f,
                   "...and must leave it rising");
        }
    }

    // 6. Sample & hold holds, and holds repeatably.
    {
        lfo::Lfo l;
        l.shape = lfo::SampleHold;

        bool held = true, repeats = true, varies = false;
        for (int cycle = 0; cycle < 16; ++cycle)
        {
            const float first = l.valueAt (cycle + 0.01);
            for (int i = 1; i < 32; ++i)
                held = held && l.valueAt (cycle + i / 32.0) == first;

            repeats = repeats && l.valueAt (cycle + 0.5) == first;
            varies  = varies || (cycle > 0 && first != l.valueAt (0.01));
        }
        check (held, "sample & hold must hold one value for a whole cycle");
        check (repeats, "...the same value every time that cycle comes round");
        check (varies, "...and a different one from cycle to cycle");
    }

    // 7. The drawn shape. Stepped it is a lookup; smoothed it is a line through
    // the points that wraps, so a looping shape has no seam where the last step
    // meets the first.
    {
        lfo::Lfo l;
        l.shape = lfo::Custom;
        for (int i = 0; i < lfo::customSteps; ++i)
            l.table[i] = (i % 2 == 0) ? 1.0f : -1.0f;

        // Stepped: constant across a step, and the step under a phase is the one
        // the scope draws there — the editor derives the index the same way, so
        // this is what stops a painted step landing somewhere else.
        bool held = true;
        for (int i = 0; i < lfo::customSteps; ++i)
            for (int k = 0; k < 8; ++k)
            {
                const double t = (i + (k + 0.5) / 8.0) / lfo::customSteps;
                held = held && l.valueAt (t) == l.table[i];
            }
        check (held, "a stepped drawn shape must hold each step's own value");

        // Smoothed: halfway between two steps is halfway between their values.
        l.smooth = true;
        const double stepW = 1.0 / lfo::customSteps;
        check (std::abs (l.valueAt (0.5 * stepW) - 0.0f) < 1.0e-5f,
               "a smoothed drawn shape must run a line between its steps");

        // ...and the wrap. Halfway through the last step is halfway from the
        // last value back to the first, not a flat run to the end.
        const double lastMid = (lfo::customSteps - 1 + 0.5) * stepW;
        const float a = l.table[lfo::customSteps - 1], b = l.table[0];
        check (std::abs (l.valueAt (lastMid) - (a + b) * 0.5f) < 1.0e-5f,
               "...including from the last step back round to the first");

        // The phase at exactly 1.0 wraps to step 0 rather than reading off the
        // end of the table, which would be a buffer overrun rather than a bug
        // you would hear.
        check (l.valueAt (1.0) == l.valueAt (0.0),
               "a drawn shape must wrap at the cycle boundary");
    }

    // ...and an untouched drawn shape has to sound like something. A table of
    // zeros would make picking DRAW silent, which is the same "silent is
    // indistinguishable from broken" trap the forced units exist to avoid.
    {
        const lfo::Lfo fresh;
        float lo = 1.0f, hi = -1.0f;
        for (float v : fresh.table) { lo = std::min (lo, v); hi = std::max (hi, v); }
        check (hi - lo > 1.5f, "the default drawn shape must not be flat");
    }

    // 8. A shortened loop is polymeter: it repeats faster in proportion, keeps
    // step duration, and never reads a step outside the loop.
    {
        lfo::Lfo l;
        l.shape = lfo::Custom;
        for (int i = 0; i < lfo::customSteps; ++i)
            l.table[i] = (float) i / lfo::customSteps;   // a ramp, every step distinct
        l.customLength = 8;

        // The loop only ever shows its own eight steps, never 8..15.
        bool inLoop = true;
        for (int k = 0; k <= 200; ++k)
        {
            const float v = l.valueAt (k / 200.0);   // several loops
            inLoop = inLoop && v < l.table[8] - 1.0e-6f;   // table[8] is the first excluded
        }
        check (inLoop, "a shortened loop must never read a step past its end");

        // Repeats faster in proportion: eight of sixteen steps means twice the
        // rate and twice the phase per beat. 128 is a fresh full-length one.
        lfo::Lfo full;
        full.shape = lfo::Custom;   // customLength defaults to sixteen

        const double r8  = l.rate (130.0, 44100.0);
        const double r16 = full.rate (130.0, 44100.0);
        check (std::abs (r8 - 2.0 * r16) < 1.0e-12,
               "an eight-step loop must run at twice a sixteen-step loop's rate");
        check (std::abs (l.phaseFromBeats (1.0) - 2.0 * full.phaseFromBeats (1.0)) < 1.0e-12,
               "...and advance twice the phase per beat");

        // Even divisor stays on the grid: eight steps at a 1 BAR division is
        // exactly half a bar, so two whole loops fit a bar with no remainder.
        l.div = lfo::OneBar;
        check (std::abs (l.phaseFromBeats (4.0) - 2.0) < 1.0e-12,
               "an eight-step loop over a bar completes exactly twice");
    }

    // ...and the loop length must not touch a shape that has no steps. Every
    // periodic shape's rate and phase have to be bit-identical whatever
    // customLength says, because the identity guarantees downstream lean on it.
    {
        for (int shape : { lfo::Sine, lfo::Triangle, lfo::Saw, lfo::Square, lfo::SampleHold })
        {
            lfo::Lfo full;  full.shape = shape;
            lfo::Lfo cut;   cut.shape  = shape;  cut.customLength = 5;
            check (full.rate (130.0, 44100.0) == cut.rate (130.0, 44100.0)
                   && full.phaseFromBeats (2.7) == cut.phaseFromBeats (2.7),
                   "loop length must not affect a shape that has no steps");
        }
    }

    std::printf (failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
