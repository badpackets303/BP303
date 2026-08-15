// Offline tests for the performance XY pad's parameter mapping.
//
// The properties that matter:
//   1. An untouched pad is *bit-identical*, not close. It sits across the 303's
//      own cutoff, resonance and env mod, so if it rounded them every project
//      ever saved would re-voice the moment it loaded. Depths are in normalised
//      units, which means a round trip through the parameter's skew, and that
//      round trip is not exact in float — so `apply` has to hand the base value
//      straight back rather than compute a zero offset. Same reasoning as the
//      flat-EQ branch and DrumSequencer::laneClock.
//   2. A held pad at dead centre is identity too. Engagement is its own flag, so
//      centre is a real state — a finger resting mid-pad — and it must not be a
//      state that quietly rounds the patch.
//   3. Held, a mode reaches only its own destinations. A gesture in SPACE must
//      not move the filter.
//   4. Offsets clamp inside the destination's range from any starting point,
//      including the ends.
//   5. ACID needs no unit switched on, and every other mode declares the units
//      its destinations live in — otherwise the gesture is silent until the user
//      finds an ACTIVE switch, which is the trap this whole design is dodging.
//   6. The axes push the direction the label says.
//
// Build: clang++ -std=c++17 -O2 Tools/pad_test.cpp -o pad_test

#include "../Source/MacroPad.h"

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

    // Every parameter default the pad can reach, in Dest order — the values a
    // fresh instance actually starts on, which is the case that has to stay
    // untouched.
    const float defaults[macropad::numDests] = {
        500.0f,   // Cutoff
        0.5f,     // Resonance
        0.5f,     // EnvMod
        0.4f,     // DistDrive
        0.5f,     // DistColor
        0.0f,     // DistLows
        0.25f,    // DelayMix
        0.45f,    // DelayFb
        0.25f,    // RevMix
        0.55f,    // RevSize
        0.0f,     // DrumDrive
        2000.0f   // DrumFltCut
    };

    bool reaches (int mode, macropad::Dest d)
    {
        const auto& m = macropad::spec (mode);
        for (const auto& t : m.x) if (t.dest == d && t.depth != 0.0f) return true;
        for (const auto& t : m.y) if (t.dest == d && t.depth != 0.0f) return true;
        return false;
    }
}

int main()
{
    using namespace macropad;

    // --- 1. an untouched pad is inert, everywhere on the pad ----------------
    // The axes are swept as well as the mode, because a host can leave `padx`
    // parked anywhere with HOLD off — a latched gesture whose HOLD was cleared,
    // or automation that wrote only one of the two.
    {
        for (int mode = 0; mode < numModes; ++mode)
        {
            for (float x = -1.0f; x <= 1.0f; x += 0.25f)
            {
                for (float y = -1.0f; y <= 1.0f; y += 0.25f)
                {
                    Pad pad;
                    pad.mode = mode;
                    pad.x = x;
                    pad.y = y;
                    pad.on = false;

                    for (int d = 0; d < numDests; ++d)
                    {
                        const float base = defaults[d];
                        check (pad.apply ((Dest) d, base) == base,
                               "an unheld pad changed a parameter");
                    }

                    check (pad.forcedUnits() == 0u,
                           "an unheld pad switched a unit on");
                }
            }
        }
    }

    // --- 2. held at dead centre is identity too ------------------------------
    {
        for (int mode = 0; mode < numModes; ++mode)
        {
            Pad pad;
            pad.mode = mode;
            pad.on = true;   // x and y left at 0

            for (int d = 0; d < numDests; ++d)
            {
                const float base = defaults[d];
                check (pad.apply ((Dest) d, base) == base,
                       "a pad held at centre changed a parameter");
            }
        }

        // ...but it does engage its units. That is the whole point of holding.
        Pad space;
        space.mode = Space;
        space.on = true;
        check (space.forcedUnits() == (uBassDelay | uBassRev),
               "a pad held at centre did not engage its units");
    }

    // --- 3. a mode reaches only its own destinations -------------------------
    {
        for (int mode = 0; mode < numModes; ++mode)
        {
            Pad pad;
            pad.mode = mode;
            pad.x = 0.7f;
            pad.y = -0.4f;
            pad.on = true;

            for (int d = 0; d < numDests; ++d)
            {
                const float base = defaults[d];
                const float out  = pad.apply ((Dest) d, base);

                if (reaches (mode, (Dest) d))
                    continue;

                check (out == base, "a mode moved a parameter it does not name");
            }
        }
    }

    // --- 4. offsets clamp inside the range, from any starting point ----------
    {
        for (int mode = 0; mode < numModes; ++mode)
        {
            for (float x : { -1.0f, 1.0f })
            {
                for (float y : { -1.0f, 1.0f })
                {
                    Pad pad;
                    pad.mode = mode;
                    pad.x = x;
                    pad.y = y;
                    pad.on = true;

                    for (int d = 0; d < numDests; ++d)
                    {
                        const auto& r = range ((Dest) d);

                        // both ends and the default: a knob already at the top
                        // of its travel must not be pushed past it
                        for (float base : { r.start, defaults[d], r.end })
                        {
                            const float out = pad.apply ((Dest) d, base);
                            check (out >= r.start - 1.0e-3f && out <= r.end + 1.0e-3f,
                                   "an offset left the parameter's range");
                            check (std::isfinite (out), "an offset was not finite");
                        }
                    }
                }
            }
        }
    }

    // --- 5. the units each mode needs ---------------------------------------
    {
        Pad pad;
        pad.on = true;

        pad.mode = Acid;
        check (pad.forcedUnits() == 0u,
               "ACID asked for a unit — the 303's own filter is always live");

        pad.mode = Grit;
        check (pad.forcedUnits() == uBassDist, "GRIT did not engage the shaper");

        pad.mode = Kit;
        check (pad.forcedUnits() == (uDrumDist | uDrumFilt),
               "KIT did not engage both drum units");

        // Every destination a mode names has to be covered by a unit that mode
        // engages, or the gesture is inaudible until the user finds a switch.
        struct { Dest dest; unsigned unit; } needs[] = {
            { DistDrive, uBassDist },  { DistColor, uBassDist },
            { DistLows,  uBassDist },  { DelayMix,  uBassDelay },
            { DelayFb,   uBassDelay }, { RevMix,    uBassRev },
            { RevSize,   uBassRev },   { DrumDrive, uDrumDist },
            { DrumFltCut, uDrumFilt }
        };

        for (int mode = 0; mode < numModes; ++mode)
            for (const auto& n : needs)
                if (reaches (mode, n.dest))
                    check ((spec (mode).units & n.unit) != 0,
                           "a mode moves a control in a unit it does not engage");
    }

    // --- 6. the axes push the way the labels say -----------------------------
    {
        Pad pad;
        pad.mode = Acid;
        pad.on = true;

        pad.x = 1.0f;
        check (pad.apply (Cutoff, defaults[Cutoff]) > defaults[Cutoff],
               "ACID X did not open the filter");
        pad.x = -1.0f;
        check (pad.apply (Cutoff, defaults[Cutoff]) < defaults[Cutoff],
               "ACID X did not close the filter");

        pad.x = 0.0f;
        pad.y = 1.0f;
        check (pad.apply (Resonance, defaults[Resonance]) > defaults[Resonance]
                   && pad.apply (EnvMod, defaults[EnvMod]) > defaults[EnvMod],
               "ACID Y did not raise resonance and env mod together");

        // GRIT's Y is the one that runs backwards on purpose: up hands the
        // shaper the whole spectrum, so LOWS KEPT falls as Y rises.
        pad.mode = Grit;
        pad.x = 0.0f;
        pad.y = -1.0f;
        check (pad.apply (DistLows, defaults[DistLows]) > defaults[DistLows],
               "GRIT Y down did not keep the lows out of the shaper");
    }

    // --- the skew round trip, which is why property 1 needs the early out ----
    {
        const auto& r = range (Cutoff);
        check (std::fabs (r.to01 (500.0f) - 0.484f) < 0.01f,
               "the cutoff default is not where the knob draws it");
        check (std::fabs (r.from01 (r.to01 (2000.0f)) - 2000.0f) < 1.0f,
               "the skew round trip is not even approximately stable");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
