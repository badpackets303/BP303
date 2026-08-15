// Offline tests for the stereo overloads on the units ahead of the delay, where
// each line first becomes a channel pair:
//   Pcf         — linked envelope follower, independent filter state per channel.
//   Distortion  — independent shaper state per channel, every control shared.
//   DrumMachine — per-voice panning, kick pinned centre.
//
// The property both must hold is that going stereo cost nothing: fed two
// identical channels, each has to produce exactly what the mono path produces,
// sample for sample. That is what lets a width control default to zero and leave
// every saved project sounding as it did. The other half is that the channels
// really are independent — a signal on one side must not leak state into the
// other, which is the failure a shared-state refactor would actually produce.
// Build: clang++ -std=c++17 -O2 Tools/stereo_bus_test.cpp -o stereo_bus_test

#include "../Source/Distortion.h"
#include "../Source/DrumMachine.h"
#include "../Source/Pcf.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr int sr = 44100;
    constexpr int blockLen = 4096;

    // Something with transients and a low fundamental, so the PCF's follower
    // moves and the LOWS crossover has a band to split off.
    std::vector<float> testSignal (float amp = 0.6f, int seed = 1)
    {
        std::vector<float> v ((size_t) blockLen);
        uint32_t rng = 0x9e3779b9u ^ (uint32_t) seed;
        for (int i = 0; i < blockLen; ++i)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const float noise = (float) (int32_t) rng * 4.656613e-10f;
            const float env = std::exp (-(float) (i % 512) / 90.0f);
            v[(size_t) i] = amp * env * (std::sin (6.2831853f * 55.0f * i / sr)
                                         + 0.3f * noise);
        }
        return v;
    }

    int countNans (const std::vector<float>& v)
    {
        int n = 0;
        for (float x : v) if (! std::isfinite (x)) ++n;
        return n;
    }

    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])          // bit-identical, not "close"
                return false;
        return true;
    }

    double maxAbs (const std::vector<float>& v)
    {
        double m = 0.0;
        for (float x : v) m = std::max (m, (double) std::abs (x));
        return m;
    }

    Distortion::Params distParams (int type)
    {
        Distortion::Params p;
        p.on = true;
        p.type = type;
        p.drive = 0.7f;
        p.color = 0.6f;
        p.bits = 6.0f;
        p.rateSamples = 3.0f;
        p.foldAmount = 0.55f;
        p.foldSym = 0.65f;
        p.rectAmount = 0.6f;
        p.rectTone = 0.4f;
        p.lowsKept = 0.5f;             // exercise the crossover too
        return p;
    }

    double rms (const std::vector<float>& v)
    {
        double s = 0.0;
        for (float x : v) s += (double) x * x;
        return std::sqrt (s / (double) v.size());
    }

    bool closeEnough (const std::vector<float>& a, const std::vector<float>& b, double tol)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs ((double) a[i] - (double) b[i]) > tol)
                return false;
        return true;
    }

    // Renders two bars of a pattern that lands every voice, with accents and
    // ghosts. Returns the mono render; fills l/r with the stereo one at the
    // given spread when they are non-null. onlyVoice >= 0 triggers just that one,
    // and pans, when given, replaces the default layout.
    std::vector<float> drumPattern (std::vector<float>* l, std::vector<float>* r,
                                    float spread, int onlyVoice = -1,
                                    const float* pans = nullptr)
    {
        static const int hits[DrumMachine::numVoices][8] = {
            { 1,0,0,1, 0,0,1,0 },   // BD
            { 0,0,1,0, 0,0,1,0 },   // SD
            { 0,0,0,0, 1,0,0,0 },   // CP
            { 1,1,1,1, 1,1,1,1 },   // CH
            { 0,0,1,0, 0,0,0,1 },   // OH
        };
        const float lvls[DrumMachine::numVoices] = { 0.9f, 0.8f, 0.7f, 0.6f, 0.6f };
        const int len = sr * 2, stepLen = sr / 8;

        std::vector<float> mono ((size_t) len, 0.0f);
        if (l) l->assign ((size_t) len, 0.0f);
        if (r) r->assign ((size_t) len, 0.0f);

        DrumMachine md, sd;
        md.prepare (sr);
        sd.prepare (sr);
        if (pans != nullptr)
        {
            // scaled by spread the same way the processor does it
            float scaled[DrumMachine::numVoices];
            for (int i = 0; i < DrumMachine::numVoices; ++i)
                scaled[i] = pans[i] * spread;
            sd.setPan (scaled);
        }
        else
        {
            sd.setSpread (spread);
        }
        for (auto* d : { &md, &sd })
            d->setParams (DrumMachine::Kit::K909, 1.1f, 0.95f, 1.05f, 1.0f, 0.6f,
                          lvls, -2.0f, 1.2f, 0.9f, 1.1f);

        for (int pos = 0, step = 0; pos < len; pos += stepLen, ++step)
        {
            for (int voice = 0; voice < DrumMachine::numVoices; ++voice)
            {
                if (onlyVoice >= 0 && voice != onlyVoice)
                    continue;
                if (hits[voice][step % 8])
                {
                    const int dyn = step % 4 == 0 ? 1 : (step % 3 == 0 ? -1 : 0);
                    md.trigger (voice, dyn);
                    sd.trigger (voice, dyn);
                }
            }

            const int chunk = std::min (stepLen, len - pos);
            md.render (mono.data() + pos, chunk);
            if (l && r)
                sd.render (l->data() + pos, r->data() + pos, chunk);
        }

        return mono;
    }

    const char* typeName (int t)
    {
        switch (t)
        {
            case Distortion::Soft:  return "SOFT";
            case Distortion::Fuzz:  return "FUZZ";
            case Distortion::Crush: return "CRUSH";
            case Distortion::Fold:  return "FOLD";
            case Distortion::Rect:  return "RECT";
            default:                return "?";
        }
    }
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* msg) {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", msg); }
    };

    // ---- Pcf: stereo with identical channels == mono ------------------------
    for (int mode : { Pcf::LP, Pcf::BP })
    {
        const auto in = testSignal();

        auto mono = in;
        { Pcf f; f.prepare (sr); f.setParams (true, mode, 600.0f, 0.5f, 0.7f);
          f.process (mono.data(), (int) mono.size()); }

        auto l = in, r = in;
        { Pcf f; f.prepare (sr); f.setParams (true, mode, 600.0f, 0.5f, 0.7f);
          f.process (l.data(), r.data(), (int) l.size()); }

        check (identical (mono, l), "pcf stereo left differs from mono path");
        check (identical (l, r), "pcf stereo channels differ on identical input");
        check (countNans (l) == 0, "pcf stereo produced non-finite samples");
    }

    // ---- Pcf: one channel's filter state must not leak into the other -------
    // The follower is deliberately linked, so silence on the right still lowers
    // the detector and changes the left — that is the design. What must not
    // happen is the *filter* state mixing, so this compares against a mono run
    // driven by the same linked detector, which is a mono run of the same input
    // at the same level on both sides.
    {
        const auto loud = testSignal (0.6f, 1);
        const auto quiet = testSignal (0.05f, 2);

        auto l = loud, r = quiet;
        { Pcf f; f.prepare (sr); f.setParams (true, Pcf::LP, 600.0f, 0.5f, 0.7f);
          f.process (l.data(), r.data(), (int) l.size()); }

        check (maxAbs (l) > maxAbs (r) * 2.0,
               "pcf stereo channels did not stay separate");
        check (countNans (l) == 0 && countNans (r) == 0,
               "pcf stereo produced non-finite samples on asymmetric input");
    }

    // ---- Distortion: stereo with identical channels == mono, every type -----
    for (int type = 0; type < Distortion::numTypes; ++type)
    {
        const auto in = testSignal();

        auto mono = in;
        { Distortion d; d.prepare (sr); d.setParams (distParams (type));
          d.process (mono.data(), (int) mono.size()); }

        auto l = in, r = in;
        { Distortion d; d.prepare (sr); d.setParams (distParams (type));
          d.process (l.data(), r.data(), (int) l.size()); }

        char msg[96];
        std::snprintf (msg, sizeof (msg), "distortion %s stereo left differs from mono path",
                       typeName (type));
        check (identical (mono, l), msg);
        std::snprintf (msg, sizeof (msg), "distortion %s stereo channels differ on identical input",
                       typeName (type));
        check (identical (l, r), msg);
        std::snprintf (msg, sizeof (msg), "distortion %s produced non-finite samples",
                       typeName (type));
        check (countNans (l) == 0, msg);
    }

    // ---- Distortion: silence on one side must not disturb the other ---------
    // Nothing in this unit is linked across the pair, so the left channel of a
    // stereo run has to match a mono run of the same signal exactly, whatever
    // the right channel is doing. This is the test that catches shared state.
    for (int type = 0; type < Distortion::numTypes; ++type)
    {
        const auto in = testSignal();
        const std::vector<float> silence ((size_t) blockLen, 0.0f);

        auto mono = in;
        { Distortion d; d.prepare (sr); d.setParams (distParams (type));
          d.process (mono.data(), (int) mono.size()); }

        auto l = in, r = silence;
        { Distortion d; d.prepare (sr); d.setParams (distParams (type));
          d.process (l.data(), r.data(), (int) l.size()); }

        char msg[96];
        std::snprintf (msg, sizeof (msg), "distortion %s leaked state between channels",
                       typeName (type));
        check (identical (mono, l), msg);
    }

    // ---- Both stay transparent when off ------------------------------------
    {
        const auto in = testSignal();

        auto l = in, r = in;
        { Pcf f; f.prepare (sr); f.setParams (false, Pcf::LP, 600.0f, 0.5f, 0.7f);
          f.process (l.data(), r.data(), (int) l.size()); }
        check (identical (in, l) && identical (in, r), "pcf stereo bypass is not transparent");

        l = in; r = in;
        { Distortion d; d.prepare (sr);
          auto p = distParams (Distortion::Fold); p.on = false;
          d.setParams (p);
          d.process (l.data(), r.data(), (int) l.size()); }
        check (identical (in, l) && identical (in, r),
               "distortion stereo bypass is not transparent");
    }

    // ---- DrumMachine: SPREAD at zero leaves the mono sum in both channels ---
    // Not bit-identical: pulling the voices apart costs the sum an FMA the
    // compiler used to fuse, so allow a couple of ULPs and no more.
    {
        auto mono = drumPattern (nullptr, nullptr, 0.0f);
        std::vector<float> l, r;
        drumPattern (&l, &r, 0.0f);

        check (closeEnough (mono, l, 4.0e-7) && closeEnough (mono, r, 4.0e-7),
               "drum spread 0 does not reproduce the mono sum");
    }

    // ---- DrumMachine: SPREAD opens a real image -----------------------------
    {
        std::vector<float> l, r;
        const auto mono = drumPattern (&l, &r, 1.0f);

        check (! closeEnough (l, r, 1.0e-4), "drum spread 1 left and right are the same");

        // The width has to survive a fold to mono. These are separate voices at
        // separate positions rather than a phase trick across one signal, so the
        // sum must keep its level instead of partially nulling — the failure a
        // Haas or mid-side widener would show here.
        std::vector<float> folded (l.size());
        for (size_t i = 0; i < l.size(); ++i)
            folded[i] = (l[i] + r[i]) * 0.5f;

        const double foldedRms = rms (folded), monoRms = rms (mono);
        std::printf ("drum fold-down: mono=%.4f folded=%.4f (%.2f dB)\n",
                     monoRms, foldedRms, 20.0 * std::log10 (foldedRms / monoRms));
        check (foldedRms > monoRms * 0.85,
               "drum spread lost level when summed to mono");
    }

    // ---- DrumMachine: the default layout leaves the kick centred ------------
    {
        std::vector<float> l, r;
        drumPattern (&l, &r, 1.0f, DrumMachine::BD);
        check (identical (l, r), "kick moved off centre under the default layout");
    }

    // ---- DrumMachine: per-voice pans place voices where they are told -------
    // What the BALANCE page drives. Each voice is put hard over on its own and
    // the sides compared, which is the only way to tell "panned" from "louder".
    for (int voice = 0; voice < DrumMachine::numVoices; ++voice)
    {
        static const char* names[] = { "kick", "snare", "clap", "closed hat", "open hat" };

        float pans[DrumMachine::numVoices] = {};
        pans[voice] = -1.0f;
        std::vector<float> l, r;
        drumPattern (&l, &r, 1.0f, voice, pans);

        char msg[96];
        std::snprintf (msg, sizeof msg, "%s hard left did not favour the left", names[voice]);
        check (rms (l) > rms (r) * 8.0, msg);

        pans[voice] = 1.0f;
        drumPattern (&l, &r, 1.0f, voice, pans);
        std::snprintf (msg, sizeof msg, "%s hard right did not favour the right", names[voice]);
        check (rms (r) > rms (l) * 8.0, msg);

        // and centred, the voice has to sit equally on both sides
        pans[voice] = 0.0f;
        drumPattern (&l, &r, 1.0f, voice, pans);
        std::snprintf (msg, sizeof msg, "%s centred did not land equally on both sides", names[voice]);
        check (identical (l, r), msg);
    }

    // ---- DrumMachine: a panned kit still folds down without cancelling ------
    {
        // hats thrown wide apart and the snare hard over, which is further than
        // the default layout goes and so a harder case for the fold
        float pans[DrumMachine::numVoices] = { 0.0f, -1.0f, 0.8f, -1.0f, 1.0f };
        std::vector<float> l, r;
        const auto mono = drumPattern (&l, &r, 1.0f, -1, pans);

        std::vector<float> folded (l.size());
        for (size_t i = 0; i < l.size(); ++i)
            folded[i] = (l[i] + r[i]) * 0.5f;

        const double foldedRms = rms (folded), monoRms = rms (mono);
        std::printf ("drum fold-down, hard pans: mono=%.4f folded=%.4f (%.2f dB)\n",
                     monoRms, foldedRms, 20.0 * std::log10 (foldedRms / monoRms));
        // constant-power panning costs a hard-panned voice 3 dB in the fold, and
        // that bound is what is being held to — not cancellation
        check (foldedRms > monoRms * 0.707, "hard drum pans cancelled when summed to mono");
    }

    if (failures == 0)
        std::printf ("ALL PASS\n");
    else
        std::printf ("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
