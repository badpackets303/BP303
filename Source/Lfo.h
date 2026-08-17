#pragma once

#include <cmath>
#include <cstdint>

#include "MacroPad.h"

// A low-frequency oscillator that offsets a knob, on the same terms the XY pad
// already does.
//
// It borrows `macropad::Dest` and `macropad::range` rather than declaring its
// own destination table. There is only one set of "what a normalised offset
// means on this parameter" answers in the plugin, `Tools/pad_test.cpp` already
// checks that set against `createParameterLayout`, and a second copy would be a
// second thing to keep in step for no benefit. What the LFO adds is that a
// destination is *chosen* rather than hardwired by a mode.
//
// It offsets rather than writes, for the reasons in MacroPad.h: the knobs stay
// where the user left them, and the host gets one automation lane per LFO
// instead of a dozen fighting ones. `apply` takes the same shortcut too — an
// inactive LFO hands the base value straight back without touching the skew
// round trip, which is not exact in float. Same reasoning as the flat-EQ branch
// and `DrumSequencer::laneClock`: the cheap way to guarantee nothing moved is
// not to compute it.
//
// **Phase is derived, not accumulated, whenever there is a transport to derive
// it from.** `SongPlayer` does the same thing and for the same reason: an
// accumulating phase drifts out of step the moment the host loops or the
// playhead jumps, and a synced LFO that no longer lines up with the bar after a
// loop is worse than no sync at all. Free-running has nothing to derive from
// and so does accumulate.
//
// **It engages its destination's unit, exactly as the pad engages its mode's.**
// This was built the other way round first, on the reasoning that an LFO is
// persistent where a pad gesture is momentary, so forcing would hold a unit on
// indefinitely and leave the ACTIVE lamp reporting something untrue.
//
// That was the wrong trade, and it cost two sessions to find out. A routing to
// DRUM FILTER on an instrument whose drum filter is bypassed is silent, and
// from outside — with no UI yet, only the host's automation list — silent is
// indistinguishable from broken. The lamp misreporting is a cosmetic problem
// with a UI fix; a modulator that does nothing is the trap this whole plugin is
// full of, and the one `forcedUnits` exists to spring.
//
// As with the pad, the ACTIVE *parameter* is never written — only OR'd into the
// unit's `on` for as long as the routing is live — so the user's own switch
// position survives untouched and comes back the moment the depth returns to
// zero. `Tools/lfo_wire_test.cpp` pins both halves.
//
// JUCE-free so the test can build it standalone.
namespace lfo
{

// Order is save-file compatibility: `lfoNshape` stores its index, so this may
// only grow at the end.
enum Shape { Sine = 0, Triangle, Saw, Square, SampleHold, Custom, numShapes };

// A drawn shape is sixteen steps, because that is the number this instrument
// counts in everywhere else — the grids, the bar, the pattern length. A
// modulator drawn on a different grid to the one the notes sit on would be a
// second vocabulary for no gain, and sixteen steps across the scope leaves each
// one wide enough to hit with a mouse.
//
// The values live in the struct rather than behind a pointer so `Lfo` stays a
// plain value the audio thread copies once a block, the way it already does.
inline constexpr int customSteps = 16;

// Beats per cycle. Order is save-file compatibility for the same reason. These
// are the divisions worth having rather than every division expressible: a bar
// down to a sixteenth covers the sweep and the per-step wobble, and anything
// faster stops being a modulator and starts being an oscillator the voice
// already has three of.
enum Div { OneBar = 0, Half, Quarter, Eighth, Sixteenth, numDivs };

inline double beatsPerCycle (int d)
{
    switch (d)
    {
        case OneBar:    return 4.0;
        case Half:      return 2.0;
        case Quarter:   return 1.0;
        case Eighth:    return 0.5;
        case Sixteenth: return 0.25;
        default:        return 1.0;
    }
}

// What the audio thread reads. One routing per LFO to start with; the slot
// count is the thing to grow, and growing it does not change any of this.
struct Lfo
{
    bool  on    = false;
    int   shape = Sine;
    bool  sync  = true;
    float rateHz = 2.0f;          // free-running rate, ignored while synced
    int   div   = Eighth;         // synced rate, ignored while free
    macropad::Dest dest = macropad::numDests;
    float depth = 0.0f;           // normalised, -1..+1, a fraction of the knob's travel

    // The drawn shape, -1..+1 a step, and whether to run a line through the
    // points instead of stepping between them. Only read when `shape` is
    // Custom; the default is a sine sampled at sixteen steps, so choosing DRAW
    // and touching nothing gives something that sounds like an LFO rather than
    // a flat line — the "silent is indistinguishable from broken" trap that
    // `forcedUnits` exists to avoid, in its other form.
    float table[customSteps] = {
         0.000f,  0.383f,  0.707f,  0.924f,  1.000f,  0.924f,  0.707f,  0.383f,
         0.000f, -0.383f, -0.707f, -0.924f, -1.000f, -0.924f, -0.707f, -0.383f
    };
    bool smooth = false;

    // How many of the sixteen drawn steps the loop runs before it repeats. This
    // is `DrumSequencer::laneLength` for the LFO: each step keeps its duration,
    // so a loop shorter than sixteen repeats faster and drifts against the bar —
    // *polymeter*, the same drift a short drum lane makes. A length that divides
    // sixteen evenly (eight, four) stays on the grid; anything else phases. Only
    // the drawn shape has steps to loop, so it is ignored for the others.
    int customLength = customSteps;

    // Clamped, because it comes off a host parameter that a stale project or an
    // out-of-range automation lane could put anywhere.
    int loopLen() const { return std::min (customSteps, std::max (2, customLength)); }

    // The steps in one cycle: the drawn loop when drawn, the whole table (which
    // is the whole cycle) otherwise. `rate` and `phaseFromBeats` scale by
    // sixteen over this, so for every non-drawn shape the factor is exactly one
    // and their timing is bit-identical to before the loop existed.
    int loopSteps() const { return shape == Custom ? loopLen() : customSteps; }

    // Three ways to be doing nothing, and all of them have to take the cheap
    // path: switched off, routed nowhere, or routed somewhere at zero depth.
    bool active() const
    {
        return on && depth != 0.0f && (unsigned) dest < (unsigned) macropad::numDests;
    }

    // The unit this routing needs switched on to be heard, OR'd into that
    // unit's `on` by the caller. Zero for the voice's own controls, which are
    // always live, and zero while inactive — so an LFO nobody is using cannot
    // hold an effect on.
    unsigned forcedUnits() const
    {
        return active() ? macropad::unitFor (dest) : 0u;
    }

    // Cycles per sample, which is what the chunk loop advances by. A short drawn
    // loop repeats faster in the same proportion it is shorter — the step keeps
    // its duration and there are fewer of them — so both the synced and the free
    // rate scale by sixteen over the loop length. For every non-drawn shape that
    // factor is 16/16 = 1 exactly, so their rate is unchanged to the bit.
    double rate (double bpm, double sampleRate) const
    {
        const double base = sync ? (bpm / 60.0) / beatsPerCycle (div)
                                 : (double) rateHz;
        const double hz = base * (double) customSteps / (double) loopSteps();
        return sampleRate > 0.0 ? hz / sampleRate : 0.0;
    }

    // Phase for a position measured in beats — the derived path, used whenever
    // the host gives us a playhead. Scaled by the same factor as `rate`, so a
    // synced short loop stays locked to the beat it drifts against rather than
    // merely running at the right average speed.
    double phaseFromBeats (double beats) const
    {
        return beats / beatsPerCycle (div) * (double) customSteps / (double) loopSteps();
    }

    // -1 .. +1. `phase` is in cycles and may be any size; it is wrapped here.
    //
    // The shapes agree at phase 0 where they can — sine, triangle and saw all
    // leave the centre rising — so changing shape under a running LFO shifts
    // the waveform rather than jumping the value it is currently putting out.
    float valueAt (double phase) const
    {
        const double t = phase - std::floor (phase);

        switch (shape)
        {
            case Triangle:
                return (float) (t < 0.25 ? 4.0 * t
                              : t < 0.75 ? 2.0 - 4.0 * t
                                         : 4.0 * t - 4.0);

            case Saw:
            {
                const double u = t + 0.5;
                return (float) (2.0 * (u - std::floor (u)) - 1.0);
            }

            case Square:
                return t < 0.5 ? 1.0f : -1.0f;

            // Stateless on purpose: the value is a hash of which cycle we are
            // in, so it survives a host loop or a playhead jump exactly the way
            // the derived phase above does. A held random that accumulated
            // would come back different every time round a loop, which is not
            // what a pattern-based instrument wants.
            case SampleHold:
            {
                uint32_t h = (uint32_t) (int32_t) std::floor (phase) * 2654435761u;
                h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                return (float) ((double) h / 2147483648.0 - 1.0);
            }

            // The drawn shape. Stepped, it is a lookup; smoothed, a line from
            // each step to the next — which wraps through step 0 rather than
            // flattening at the end, so a looping shape has no seam in it. The
            // wrap is at the *loop* length, not the full table, so a shortened
            // loop's smoothing joins its own last step back to its first rather
            // than reaching into steps outside the loop.
            case Custom:
            {
                const int n = loopLen();
                const double pos = t * n;
                const int i = std::min ((int) pos, n - 1);
                if (! smooth)
                    return table[i];

                const float a = table[i];
                const float b = table[(i + 1) % n];
                return a + (b - a) * (float) (pos - std::floor (pos));
            }

            case Sine:
            default:
                return (float) std::sin (6.283185307179586 * t);
        }
    }

    // The destination's value with this LFO's offset folded in, or the value
    // untouched if this LFO isn't reaching it.
    float apply (macropad::Dest d, float base, double phase) const
    {
        if (! active() || d != dest)
            return base;

        const float amt = depth * valueAt (phase);
        if (amt == 0.0f)
            return base;

        const auto& r = macropad::range (d);
        return r.from01 (std::min (1.0f, std::max (0.0f, r.to01 (base) + amt)));
    }
};

// How often the audio thread re-evaluates an LFO, in samples.
//
// Measured rather than chosen — `Tools/lfo_rate_test.cpp` renders the ladder
// under a stepped cutoff at every interval and writes the ladder out as WAVs.
// 128 was indistinguishable from per-sample by ear at a 1/16 sweep; 512 and
// 1024 comb audibly. 64 is one step inside what was confirmed, which is what
// buys the fastest division here (a 1/32 at 64 is the same riser as the 1/16
// at 128 that measured clean) rather than extrapolating past the evidence.
//
// It is deliberately coarser than `Eq.h`'s 32: that one is a biquad's
// coefficient glide, where the filter's own state carries the old coefficients
// and a jump lands in the output. A ZDF ladder has no such term.
inline constexpr int modChunk = 64;

} // namespace lfo
