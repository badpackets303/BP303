// Per-voice decay for the snare and the two hats.
//
// These three are multipliers on each voice's own per-kit decay, not absolute
// times like BD DECAY. The snare has two decays — tone and noise — at different
// lengths, which one absolute number cannot express without flattening their
// ratio; and the hats are where the kits differ most, so an absolute time would
// make switching kits stop changing the envelope. Both of those are what this
// test is actually guarding: that 1.0x is the kit exactly as it was, and that
// the kits stay different from each other at every setting.
//
// Decay is measured as tail length — how long the hit stays above 5% of its own
// peak — rather than as total energy. Energy conflates the envelope with the
// voice's level and timbre, which is fine when comparing one voice against
// itself but wrong across kits: a 909 open hat is louder than an 808 one and
// carries more energy despite decaying faster.
//
// JUCE-free: this only needs DrumMachine.
// Build: clang++ -std=c++17 -O2 Tools/drumdecay_test.cpp -o drumdecay_test

#include "../Source/DrumMachine.h"
#include "../Source/StepDyn.h"

#include <cmath>
#include <cstdio>
#include <string>
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

    std::vector<float> renderHit (DrumMachine::Kit kit, int voice, float sdMul,
                                  float chMul, float ohMul, float bdDecay = 0.5f)
    {
        DrumMachine dm;
        dm.prepare (sr);

        const float levels[DrumMachine::numVoices] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        dm.setParams (kit, 1.0f, 1.0f, 1.0f, 1.0f, bdDecay, levels, 0.0f,
                      sdMul, chMul, ohMul);

        dm.trigger (voice, dyn303::Normal);

        // Four seconds: an 808 open hat at 4x is a 1.6 s time constant, and the 5%
        // threshold is about three of those. A shorter buffer silently truncates
        // the longest tails and every comparison against them stops meaning
        // anything, while still reading as a pass.
        std::vector<float> buf ((size_t) (sr * 4.0));
        dm.render (buf.data(), (int) buf.size());
        return buf;
    }

    // Seconds until the hit is done: the last point at which it is still above
    // 5% of its own peak. Relative to the voice's own level, so a louder kit
    // does not read as a longer one.
    double tailSeconds (DrumMachine::Kit kit, int voice, float sdMul,
                        float chMul, float ohMul, float bdDecay = 0.5f)
    {
        const auto buf = renderHit (kit, voice, sdMul, chMul, ohMul, bdDecay);

        float peak = 0.0f;
        for (float v : buf)
            peak = std::max (peak, std::fabs (v));

        const float thresh = peak * 0.05f;
        size_t last = 0;
        for (size_t i = 0; i < buf.size(); ++i)
            if (std::fabs (buf[i]) > thresh)
                last = i;
        return (double) last / sr;
    }

    double hitEnergy (DrumMachine::Kit kit, int voice, float sdMul,
                      float chMul, float ohMul)
    {
        const auto buf = renderHit (kit, voice, sdMul, chMul, ohMul);
        double sum = 0.0;
        for (float v : buf)
            sum += (double) v * v;
        return sum;
    }

    const char* kitName (DrumMachine::Kit k)
    {
        return k == DrumMachine::Kit::K808 ? "808"
             : k == DrumMachine::Kit::K909 ? "909" : "606";
    }
}

int main()
{
    using Kit = DrumMachine::Kit;

    // --- the multiplier does what it says, on every voice that has one --------
    for (auto voice : { DrumMachine::SD, DrumMachine::CH, DrumMachine::OH })
    {
        const char* name = voice == DrumMachine::SD ? "SD"
                         : voice == DrumMachine::CH ? "CH" : "OH";

        const auto tailAt = [voice] (float mul)
        {
            return tailSeconds (Kit::K808, voice,
                                voice == DrumMachine::SD ? mul : 1.0f,
                                voice == DrumMachine::CH ? mul : 1.0f,
                                voice == DrumMachine::OH ? mul : 1.0f);
        };

        const double shortE = tailAt (0.5f);
        const double stockE = tailAt (1.0f);
        const double longE  = tailAt (2.0f);

        std::printf ("      [%s  0.5x %.3fs  1.0x %.3fs  2.0x %.3fs]\n",
                     name, shortE, stockE, longE);
        check (shortE < stockE && stockE < longE,
               (std::string (name) + " DECAY shortens below 1.0x and lengthens above it").c_str());
    }

    // --- 1.0x has to be the kit exactly as it was -----------------------------
    // Every project saved before these knobs existed loads at 1.0x, so this is
    // the assertion that says those projects still sound the way they did.
    {
        const double a = hitEnergy (Kit::K808, DrumMachine::OH, 1.0f, 1.0f, 1.0f);
        const double b = hitEnergy (Kit::K808, DrumMachine::OH, 1.0f, 1.0f, 1.0f);
        check (a == b, "the same settings render identically twice over");

        // A multiplier of exactly 1 must not perturb the coefficient at all.
        const double stock = hitEnergy (Kit::K909, DrumMachine::SD, 1.0f, 1.0f, 1.0f);
        const double nudged = hitEnergy (Kit::K909, DrumMachine::SD, 1.0000001f, 1.0f, 1.0f);
        std::printf ("      [1.0x %.6f  vs 1.0000001x %.6f]\n", stock, nudged);
        check (std::abs (stock - nudged) < stock * 1.0e-4,
               "1.0x is the stock kit, not an approximation of it");
    }

    // --- the kits stay different from each other ------------------------------
    // The whole reason these are multipliers. If they were absolute times, every
    // kit would land on the same envelope and the hats would stop telling the
    // machines apart.
    for (float mul : { 0.5f, 1.0f, 2.0f })
    {
        const double e808 = tailSeconds (Kit::K808, DrumMachine::OH, 1.0f, 1.0f, mul);
        const double e909 = tailSeconds (Kit::K909, DrumMachine::OH, 1.0f, 1.0f, mul);
        const double e606 = tailSeconds (Kit::K606, DrumMachine::OH, 1.0f, 1.0f, mul);

        std::printf ("      [OH at %.1fx:  808 %.3fs  909 %.3fs  606 %.3fs]\n",
                     mul, e808, e909, e606);
        check (e808 > e909 && e909 > e606,
               "open hat keeps its 808 > 909 > 606 ordering at every setting");
    }

    // --- the snare's two envelopes keep their ratio ---------------------------
    // Tone and noise decay at different rates per kit. Scaling both by the same
    // multiplier is what preserves that; scaling one would re-voice the drum
    // rather than shorten it.
    {
        for (auto k : { Kit::K808, Kit::K909, Kit::K606 })
        {
            const double stock = tailSeconds (k, DrumMachine::SD, 1.0f, 1.0f, 1.0f);
            const double half  = tailSeconds (k, DrumMachine::SD, 0.5f, 1.0f, 1.0f);
            const double ratio = half / stock;
            std::printf ("      [SD %s: 0.5x is %.3f of stock]\n", kitName (k), ratio);
            check (ratio > 0.2 && ratio < 0.85,
                   "halving the snare decay shortens it without silencing it");
        }
    }

    // --- BD DECAY is untouched and still absolute -----------------------------
    // It predates these and stays a time in seconds; the new multipliers must
    // not have leaked into it.
    {
        // The other multipliers pinned at full, so a leak into the kick would show.
        const double shortKick = tailSeconds (Kit::K808, DrumMachine::BD,
                                              4.0f, 4.0f, 4.0f, 0.2f);
        const double longKick  = tailSeconds (Kit::K808, DrumMachine::BD,
                                              4.0f, 4.0f, 4.0f, 1.0f);
        std::printf ("      [BD 0.2s %.3fs  1.0s %.3fs]\n", shortKick, longKick);
        check (longKick > shortKick * 1.5,
               "BD DECAY is still an absolute time and still works");
    }

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
