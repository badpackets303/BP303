#pragma once

#include <algorithm>
#include <cmath>

// The performance XY pad: one gesture that moves several parameters at once.
//
// The model is the Kaoss Pad's rather than Alchemy's Transform Pad. A snapshot
// morph interpolates *every* parameter between corner states, which needs a
// capture UI and means nothing for the discrete controls this plugin is full of
// (wave, kit, delay type). Instead the pad has a handful of named modes, and a
// mode hardwires what each axis reaches. No assignment matrix to build, no state
// to save beyond the two axes, and the mode names are a short list of the
// gestures worth having a pad for at all.
//
// The pad *offsets* parameters rather than writing them. Writing would drag the
// user's knobs about and fight host automation; offsetting leaves the patch
// alone and puts two lanes in the automation list instead of a dozen. A knob
// therefore keeps meaning what it says, and the pad is a displacement from it.
//
// Depths are in *normalised* units — a fraction of the knob's own travel — so a
// depth reads the same whether it lands on a linear 0..1 control or on a skewed
// frequency. That means a round trip through the parameter's skew, which is not
// exact in float, so an untouched pad must not take that path at all: `on` is
// false and `apply` hands the base value straight back. Same reasoning as the
// flat-EQ branch and `DrumSequencer::laneClock` — the cheap way to guarantee
// nothing moved is not to compute it. `Tools/pad_test.cpp` pins that.
//
// JUCE-free so the test can build it standalone.
namespace macropad
{

// Order is save-file compatibility: `padmode` stores its index, so this may only
// grow at the end.
enum Mode { Acid = 0, Grit, Space, Kit, numModes };

// Every parameter any mode can reach. Internal to the pad — not stored — so this
// one is free to be reordered.
enum Dest
{
    Cutoff = 0, Resonance, EnvMod,   // the 303 voice itself, always live
    DistDrive, DistColor, DistLows,  // bass distortion
    DelayMix, DelayFb,               // bass delay
    RevMix, RevSize,                 // bass reverb
    DrumDrive, DrumFltCut,           // the drum bus
    numDests
};

// Units a mode needs switched on to be heard at all. Every FX unit in this
// plugin is off by default and ignores its controls while off, so a pad driving
// REVERB MIX would otherwise do nothing until the user found the ACTIVE switch —
// the single most confusing thing about the plugin from a host's automation
// list. The Kaoss answer is the right one here: touching the pad engages the
// units, letting go drops them. The ACTIVE parameter itself is never written, so
// the user's own switch position survives the gesture.
enum Unit : unsigned
{
    uBassDist  = 1u << 0,
    uBassDelay = 1u << 1,
    uBassRev   = 1u << 2,
    uDrumDist  = 1u << 3,
    uDrumFilt  = 1u << 4
};

// Which unit a destination lives in, so anything offsetting that destination can
// engage the unit rather than being silently swallowed by a bypass. The pad
// answers this per *mode* (below); an LFO picks a single destination and needs
// it per destination, and one table beats two.
//
// The first three are 0 because the 303's own filter is always live — the same
// fact that makes ACID the one pad mode needing no unit.
inline unsigned unitFor (int d)
{
    switch (d)
    {
        case DistDrive: case DistColor: case DistLows: return uBassDist;
        case DelayMix:  case DelayFb:                  return uBassDelay;
        case RevMix:    case RevSize:                  return uBassRev;
        case DrumDrive:                                return uDrumDist;
        case DrumFltCut:                               return uDrumFilt;
        default:                                       return 0u;
    }
}

// One parameter an axis moves, and how far. `numDests` is the unused slot.
struct Target
{
    Dest  dest  = numDests;
    float depth = 0.0f;
};

static constexpr int maxTargets = 3;

struct ModeSpec
{
    const char* name;
    unsigned    units;
    const char* xLabel;
    Target      x[maxTargets];
    const char* yLabel;
    Target      y[maxTargets];
};

// The four modes. Depths are measured against each control's own travel, not
// chosen for symmetry: what matters is that the far edge of the pad is somewhere
// worth arriving at and the near edge is still musical.
inline const ModeSpec& spec (int mode)
{
    static const ModeSpec modes[numModes] = {
        // ACID — the one gesture a 303 exists for, and the only mode that needs
        // no unit switched on: cutoff, resonance and env mod are the voice's own
        // and are always live. X is brightness; Y is how much squelch rides on
        // it, which is resonance and env mod together rather than resonance
        // alone — resonance on its own at a fixed env mod gets whistly instead
        // of getting more vocal.
        { "ACID", 0u,
          "CUT OFF",  { { Cutoff, 0.75f } },
          "RESO+ENV", { { Resonance, 0.45f }, { EnvMod, 0.30f } } },

        // GRIT — drive, with the low end as the second axis. Y down keeps the
        // lows out of the shaper (LOWS KEPT up), which is what stops a driven
        // bass line losing its bottom octave; Y up hands the shaper everything.
        { "GRIT", uBassDist,
          "DRIVE",  { { DistDrive, 0.60f } },
          "LOWS",   { { DistColor, 0.40f }, { DistLows, -0.50f } } },

        // SPACE — the two sends as one gesture. Feedback and size ride under
        // mix on each axis at about half its depth, so the tail gets longer as
        // it gets louder instead of the pad only ever changing a wet/dry.
        { "SPACE", uBassDelay | uBassRev,
          "DELAY",  { { DelayMix, 0.55f }, { DelayFb, 0.35f } },
          "REVERB", { { RevMix, 0.55f }, { RevSize, 0.35f } } },

        // KIT — the drum bus. The filter is the useful drum gesture in both
        // directions, so Y is symmetric about where the knob already sits.
        { "KIT", uDrumDist | uDrumFilt,
          "DRIVE",  { { DrumDrive, 0.60f } },
          "FILTER", { { DrumFltCut, 0.55f } } }
    };

    return modes[(unsigned) mode < (unsigned) numModes ? mode : 0];
}

// Each destination's parameter range, mirroring createParameterLayout. Kept here
// rather than read back from the APVTS so the pad can be tested without JUCE;
// `Tools/pad_test.cpp` checks the two agree.
struct Range
{
    float start, end, skew;

    // JUCE's NormalisableRange skew, spelled out: a skew below 1 spreads the low
    // end across more of the travel, which is what puts 500 Hz near the middle
    // of the CUT OFF knob.
    float to01 (float v) const
    {
        const float p = (v - start) / (end - start);
        if (p <= 0.0f) return 0.0f;
        return skew == 1.0f ? p : std::pow (p, skew);
    }

    float from01 (float t) const
    {
        if (skew != 1.0f && t > 0.0f)
            t = std::exp (std::log (t) / skew);
        return start + (end - start) * t;
    }
};

inline const Range& range (Dest d)
{
    static const Range r[numDests] = {
        { 60.0f, 5000.0f, 0.3f },   // Cutoff
        { 0.0f, 1.0f, 1.0f },       // Resonance
        { 0.0f, 1.0f, 1.0f },       // EnvMod
        { 0.0f, 1.0f, 1.0f },       // DistDrive
        { 0.0f, 1.0f, 1.0f },       // DistColor
        { 0.0f, 1.0f, 1.0f },       // DistLows
        { 0.0f, 1.0f, 1.0f },       // DelayMix
        { 0.0f, 0.95f, 1.0f },      // DelayFb
        { 0.0f, 1.0f, 1.0f },       // RevMix
        { 0.0f, 1.0f, 1.0f },       // RevSize
        { 0.0f, 1.0f, 1.0f },       // DrumDrive
        { 60.0f, 12000.0f, 0.3f }   // DrumFltCut
    };

    return r[d];
}

// What the audio thread reads: the mode, the two axes, and whether the pad is
// currently held. Engagement is its own flag rather than "the axes are not both
// zero", so a finger resting at dead centre still counts — otherwise a drag
// through the middle would drop the mode's units for one block.
struct Pad
{
    int   mode = Acid;
    float x = 0.0f, y = 0.0f;   // -1 .. +1, 0 at the centre
    bool  on = false;

    unsigned forcedUnits() const { return on ? spec (mode).units : 0u; }

    float apply (Dest d, float base) const
    {
        if (! on)
            return base;

        float amt = 0.0f;
        const auto& m = spec (mode);
        for (const auto& t : m.x) if (t.dest == d) amt += t.depth * x;
        for (const auto& t : m.y) if (t.dest == d) amt += t.depth * y;

        if (amt == 0.0f)
            return base;

        const auto& r = range (d);
        return r.from01 (std::min (1.0f, std::max (0.0f, r.to01 (base) + amt)));
    }
};

} // namespace macropad
