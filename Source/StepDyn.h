#pragma once

#include <algorithm>

// How hard a step plays, shared by both sequencers and by the MIDI import and
// export paths so there is one vocabulary for dynamics across the whole plugin.
//
// One field rather than a pair of flags, because "accented and soft at once" is
// a state nothing downstream could sensibly answer — this way it can't be
// written in the first place. The values are ordered so the field reads as a
// single axis, quiet to hard, which is what the velocity mapping, the grids'
// cell brightness and the voices all want.
namespace dyn303
{
    enum Dyn { Soft = -1, Normal = 0, Hard = 1 };

    inline int clampDyn (int d) { return std::clamp (d, (int) Soft, (int) Hard); }

    // The note/velocity convention the plugin reads and writes everywhere: a
    // hard-played note is an accent, a gently played one is soft. Kept together
    // so an exported clip re-imports as the pattern it came from.
    inline constexpr int hardVelocity = 110, normalVelocity = 90, softVelocity = 55;
    inline constexpr int hardVelocityMin = 100, softVelocityMax = 64;

    inline int dynFromVelocity (int velocity)
    {
        return velocity >= hardVelocityMin ? Hard
             : velocity <= softVelocityMax ? Soft
                                           : Normal;
    }

    inline int velocityForDyn (int d)
    {
        return d > 0 ? hardVelocity : d < 0 ? softVelocity : normalVelocity;
    }
}
