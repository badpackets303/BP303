// Offline test that LFO 1 is wired from the parameter tree through to the
// audio, and — the half that matters more — that it costs nothing at all until
// it is used.
//
// `lfo_test.cpp` pins the LFO core's own arithmetic standalone. This one pins
// the thing the core cannot see: `processBlock` takes a *different path* when
// an LFO is live, rendering the voice in `lfo::modChunk` pieces instead of one,
// and re-setting the voice's parameters at every piece. That path has to be
// unreachable while nothing is routed, because the moment it is taken every
// cutoff in the project goes through the skew round trip inside `apply`, which
// is not exact in float. Ten thousand saved projects re-voicing very slightly
// is exactly what the flat-EQ branch, `macropad::Pad::apply` and
// `DrumSequencer::laneClock` each exist to prevent, and this is the same
// guarantee for the LFO.
//
// So: bit-identical, not close. The three inert states are each a state a real
// patch sits in — a user who switched the LFO off, one who picked a destination
// but never turned up the depth, one who turned the depth down to nothing — and
// all three have to render exactly what an instance with no LFO renders.
//
// Most checks run the LFO free rather than synced, because a synced one derives
// its phase from the transport and this harness has no playhead to derive from.
// `lfo_test.cpp` covers the derivation arithmetic itself.
//
// But `lfo1sync` *defaults to on*, so the free-running checks skip the state a
// user actually lands in, and that is where a real bug hid: a stopped transport
// reports beat 0 on every block, so deriving from it restarted the LFO at phase
// 0 each block and swept only the fraction of a cycle that fits in one. The
// last check covers it, and it has to measure how far the modulation *reaches* —
// "differs from the reference" passes on the broken version too, because a
// stutter differs from the reference just as surely as a sweep does.
// Built as a console app target (see CMakeLists.txt).

#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace
{
    int failures = 0;
    constexpr int blockSize = 512;

    // Long enough for a delay echo to come back. The shortest delay is a
    // sixteenth, which is 125 ms at the default tempo, and DELAY MIX modulates
    // a wet signal — so a window shorter than one delay time renders the two
    // delay destinations inert and would fail them for a reason that has
    // nothing to do with whether they are wired.
    constexpr int numBlocks = 32;   // ~370 ms

    void check (bool ok, const char* msg)
    {
        std::printf ("%s: %s\n", msg, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    void setP (BP303AudioProcessor& proc, const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
        {
            std::printf ("missing parameter %s: FAIL\n", id);
            ++failures;
        }
    }

    // A sustained note through the default patch. Long enough to cross several
    // modulation chunks and several LFO cycles.
    //
    // `drums` adds a channel-10 hit, because nothing on the drum line can be
    // heard to change while the drum line is silent — the drum destinations
    // would otherwise "pass" their inert half and fail their live one for the
    // same reason.
    std::vector<float> render (BP303AudioProcessor& proc, bool drums = false)
    {
        proc.prepareToPlay (44100.0, blockSize);

        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);

        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0);
                if (drums)
                    midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110), 0);
            }
            proc.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0),
                        buf.getReadPointer (0) + blockSize);
        }
        return out;
    }

    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }

    // A rolling host transport. Every check above runs without one, which means
    // the *derived* phase path — the one a synced LFO takes inside a host, and
    // so the one that actually matters in Logic — was never covered at all.
    struct FakeHead : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 130.0;
        bool   playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setIsPlaying (playing);
            p.setPpqPosition (ppq);
            return p;
        }
    };

    // As `render`, but with the transport rolling and the ppq advancing the way
    // a host advances it. `seq` also turns the internal sequencers on, which is
    // what puts drums in the buffer without hand-fed MIDI.
    std::vector<float> renderHosted (BP303AudioProcessor& proc, bool seq)
    {
        FakeHead head;
        proc.setPlayHead (&head);
        proc.prepareToPlay (44100.0, blockSize);

        if (seq)
            setP (proc, "run", 1.0f);

        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);
        const double beatsPerBlock = (double) blockSize / 44100.0 * head.bpm / 60.0;

        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0 && ! seq)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0);
                midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110), 0);
            }
            proc.processBlock (buf, midi);
            head.ppq += beatsPerBlock;
            out.insert (out.end(), buf.getReadPointer (0),
                        buf.getReadPointer (0) + blockSize);
        }

        proc.setPlayHead (nullptr);
        return out;
    }

    // Everything an LFO needs except a reason to run, so the inert cases below
    // differ from the live one in exactly one parameter each.
    void arm (BP303AudioProcessor& proc)
    {
        setP (proc, "lfo1sync", 0.0f);    // free-running: no playhead here
        setP (proc, "lfo1rate", 6.0f);
        setP (proc, "lfo1shape", (float) lfo::Sine);
        setP (proc, "lfo1dest", 0.0f);    // CUT OFF
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // The reference: an instance nobody has touched.
    BP303AudioProcessor base;
    const auto reference = render (base);

    // --- inert state 1: switched off, but fully configured -------------------
    {
        BP303AudioProcessor proc;
        arm (proc);
        setP (proc, "lfo1amt", 0.9f);
        setP (proc, "lfo1on", 0.0f);
        check (identical (render (proc), reference),
               "LFO switched off must be bit-identical to no LFO at all");
    }

    // --- inert state 2: on and routed, but at zero depth ---------------------
    {
        BP303AudioProcessor proc;
        arm (proc);
        setP (proc, "lfo1on", 1.0f);
        setP (proc, "lfo1amt", 0.0f);
        check (identical (render (proc), reference),
               "a routed LFO at zero depth must be bit-identical");
    }

    // --- inert state 3: on with depth, but routed nowhere --------------------
    // There is no "off" entry in the destination list, so this is the state a
    // future list with one would land in. It is here to keep `active()`'s third
    // clause honest rather than because a user can currently reach it.
    {
        BP303AudioProcessor proc;
        arm (proc);
        setP (proc, "lfo1on", 1.0f);
        setP (proc, "lfo1amt", 0.9f);

        auto l = proc.readLfo();
        l.dest = macropad::numDests;
        check (! l.active(), "an LFO routed nowhere must report itself inactive");
    }

    // --- live: it reaches the audio -----------------------------------------
    // Without this the three checks above would pass on a build where the LFO
    // was never plumbed in at all.
    {
        BP303AudioProcessor proc;
        arm (proc);
        setP (proc, "lfo1on", 1.0f);
        setP (proc, "lfo1amt", 0.9f);
        check (! identical (render (proc), reference),
               "a live LFO must reach the audio");
    }

    // --- every destination is really wired, and only when its unit is on -----
    //
    // Both halves, for the reason `automation_test.cpp` pins both halves: "I
    // routed an LFO to REVERB MIX and nothing happened" has two very different
    // causes that look identical from outside — a destination that was never
    // plumbed through, and a destination whose unit is switched off.
    //
    // The second is not a bug here, it is the design. The pad forces its mode's
    // units on for the length of a gesture; the LFO deliberately does not,
    // because it is persistent and forcing would hold a unit on forever while
    // the ACTIVE lamp said otherwise (see Lfo.h). So a routing into a bypassed
    // unit doing nothing is a promise, and this is where it is kept.
    //
    // The destination list is an index a saved project stores, so a routing that
    // silently does nothing is a bug that would surface months later in someone
    // else's session. Walking the list by index is what catches an entry added
    // to the choice list without being wired.
    {
        // Which ACTIVE switch each destination needs, in `lfo1dest` order.
        // nullptr means the voice's own controls, which are always live — the
        // same thing that makes ACID the pad's one mode needing no unit.
        //
        // `setupId`/`setupVal` is applied to both sides of the comparison, for
        // units whose default settings hide their own modulation. The delay is
        // the one that needs it: it defaults to 3/16, which at 130 BPM puts the
        // first echo at 346 ms and the second — the first one FEEDBACK can
        // affect at all — at 692 ms, both outside any render window worth
        // waiting for. At 1/16 the echoes land at 115 ms and 231 ms.
        struct DestUnit
        {
            const char* id; bool drumLine;
            const char* setupId = nullptr; float setupVal = 0.0f;
        };
        const DestUnit units[] = {
            { nullptr,   false }, { nullptr,  false }, { nullptr,  false },   // cutoff/res/env
            { "diston",  false }, { "diston", false }, { "diston", false },   // dist
            { "delayon", false, "delaytime", 0.0f },                          // delay mix
            { "delayon", false, "delaytime", 0.0f },                          // delay feedback
            { "brevon",  false }, { "brevon", false },                        // reverb
            { "ddiston", true  }, { "dflton", true  }                         // drum bus
        };
        static_assert (sizeof (units) / sizeof (units[0]) == macropad::numDests,
                       "a destination was added without saying what unit it needs");

        for (int d = 0; d < BP303AudioProcessor::numLfoDests(); ++d)
        {
            const auto& u = units[d];
            char msg[128];

            // The drum line needs something playing before anything on it can
            // be heard to change. A channel-10 note triggers a voice live.
            const bool drums = u.drumLine;

            // Engaged half: the routing is heard even though the unit's own
            // ACTIVE switch is off, because the LFO engages it — and the switch
            // itself is left exactly where the user put it.
            if (u.id != nullptr)
            {
                BP303AudioProcessor proc;
                arm (proc);
                setP (proc, "lfo1on", 1.0f);
                setP (proc, "lfo1amt", 0.9f);
                setP (proc, "lfo1dest", (float) d);
                if (u.setupId != nullptr)
                    setP (proc, u.setupId, u.setupVal);

                BP303AudioProcessor bare;
                if (u.setupId != nullptr)
                    setP (bare, u.setupId, u.setupVal);

                std::snprintf (msg, sizeof (msg),
                               "destination %d must engage %s without it being on", d, u.id);
                check (! identical (render (proc, drums), render (bare, drums)), msg);

                std::snprintf (msg, sizeof (msg),
                               "...and must leave %s switched off", u.id);
                check (proc.apvts.getRawParameterValue (u.id)->load() < 0.5f, msg);
            }

            // Live half: same routing, unit switched on.
            BP303AudioProcessor proc;
            arm (proc);
            setP (proc, "lfo1on", 1.0f);
            setP (proc, "lfo1amt", 0.9f);
            setP (proc, "lfo1dest", (float) d);
            if (u.id != nullptr)
                setP (proc, u.id, 1.0f);
            if (u.setupId != nullptr)
                setP (proc, u.setupId, u.setupVal);

            // The baseline is the same patch with the unit on and the LFO off,
            // so what is being detected is the modulation rather than the unit.
            BP303AudioProcessor still;
            if (u.id != nullptr)
                setP (still, u.id, 1.0f);
            if (u.setupId != nullptr)
                setP (still, u.setupId, u.setupVal);

            std::snprintf (msg, sizeof (msg),
                           "destination %d must reach the audio with %s on", d,
                           u.id != nullptr ? u.id : "no unit needed");
            check (! identical (render (proc, drums), render (still, drums)), msg);
        }
    }

    // --- synced, with nothing driving the transport --------------------------
    // `lfo1sync` defaults to *on*, so this is the state a user who switches an
    // LFO on and touches nothing else lands in — and it is the one the checks
    // above never reach, because `arm` turns sync off so the harness has a
    // phase to compare against.
    //
    // A synced LFO takes its phase from the transport, and a stopped transport
    // sits at beat 0 on every block. Deriving from that restarts the LFO at
    // phase 0 each block and sweeps only the fraction of a cycle that fits
    // inside one — a buzz rather than a sweep.
    //
    // "Different from the reference" does *not* catch that, because the buzz is
    // different too. This asks instead that the modulation reaches about as far
    // as a free-running one does, which the stub misses by a mile. Measured as a
    // ratio rather than an absolute so it does not become a tuning exercise
    // every time the default patch moves.
    {
        const auto reach = [&] (bool sync)
        {
            BP303AudioProcessor proc;
            setP (proc, "lfo1on", 1.0f);
            setP (proc, "lfo1amt", 0.9f);
            setP (proc, "lfo1dest", 0.0f);   // CUT OFF: always live, no unit needed
            setP (proc, "lfo1sync", sync ? 1.0f : 0.0f);
            if (! sync)
                setP (proc, "lfo1rate", 4.3333f);   // what a 1/8 is at 130 BPM

            const auto out = render (proc);
            double worst = 0.0;
            for (size_t i = 0; i < out.size(); ++i)
                worst = std::max (worst, std::abs ((double) out[i] - reference[i]));
            return worst;
        };

        const double synced = reach (true), freeRun = reach (false);
        std::printf ("   synced reach %.3f vs free-running %.3f\n", synced, freeRun);
        check (synced > freeRun * 0.5,
               "a synced LFO with no transport must sweep like a free one, not stutter");
    }

    // --- synced, inside a rolling host --------------------------------------
    // The configuration someone actually uses in Logic: transport rolling, LFO
    // left on its default sync, routed at depth. Nothing above covers the
    // derived-phase path with `running` true, which is precisely the path a
    // host takes.
    {
        // Bass cutoff first: no unit to switch on, so a failure here is the
        // phase derivation rather than anything to do with bypass.
        BP303AudioProcessor still, proc;
        setP (proc, "lfo1on", 1.0f);
        setP (proc, "lfo1amt", 0.9f);
        setP (proc, "lfo1dest", 0.0f);   // CUT OFF
        check (! identical (renderHosted (proc, false), renderHosted (still, false)),
               "a synced LFO must run from a rolling host transport");

        // ...then the drum filter, which is the one that has to be switched on.
        BP303AudioProcessor dStill, dProc;
        setP (dStill, "dflton", 1.0f);
        setP (dProc,  "dflton", 1.0f);
        setP (dProc, "lfo1on", 1.0f);
        setP (dProc, "lfo1amt", 0.9f);
        setP (dProc, "lfo1dest", 11.0f);   // DRUM FILTER
        check (! identical (renderHosted (dProc, false), renderHosted (dStill, false)),
               "a synced LFO on DRUM FILTER must reach a hosted render");

        // And the same with the internal sequencers driving the drums rather
        // than hand-fed MIDI, since that is how a pattern actually plays.
        BP303AudioProcessor sStill, sProc;
        setP (sStill, "dflton", 1.0f);
        setP (sProc,  "dflton", 1.0f);
        setP (sProc, "lfo1on", 1.0f);
        setP (sProc, "lfo1amt", 0.9f);
        setP (sProc, "lfo1dest", 11.0f);
        check (! identical (renderHosted (sProc, true), renderHosted (sStill, true)),
               "...and with the sequencer playing the kit");
    }

    // --- the pad still works with an LFO present -----------------------------
    // Both are displacements from the same knobs, and the pad's identity is
    // pinned by pad_test against a build with no LFO in it. This checks the two
    // compose rather than one having quietly taken over the other's path.
    {
        BP303AudioProcessor proc;
        setP (proc, "padon", 1.0f);
        setP (proc, "padx", 0.8f);
        const auto padOnly = render (proc);
        check (! identical (padOnly, reference),
               "the pad must still reach the audio with the LFO code present");

        BP303AudioProcessor both;
        setP (both, "padon", 1.0f);
        setP (both, "padx", 0.8f);
        arm (both);
        setP (both, "lfo1on", 1.0f);
        setP (both, "lfo1amt", 0.9f);
        check (! identical (render (both), padOnly),
               "an LFO must displace what the pad has already displaced");
    }

    std::printf (failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
