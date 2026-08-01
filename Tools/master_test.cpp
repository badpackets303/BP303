// Headless, JUCE-free test for the master stage.
//
// Guards the property that the master output is a *mix*, not a shared
// nonlinearity: at sane levels the bass has to come out of the summing bus
// sample-for-sample identical whether or not the drums are playing. A bare
// waveshaper across the sum cannot do this — its gain moves with the drum
// waveform, so every kick amplitude-modulates the bass. Pulse shows it worst,
// since a pulse sits at its peak for most of the cycle rather than passing
// through it.
//
// Measured by extracting the bass back out of the mix:
//     extracted = master(bass + drums) - master(drums)
// which in a linear mix is exactly master(bass). Anything left over is the
// drums reaching across onto the bass.
//
// Build: clang++ -std=c++17 -O2 Tools/master_test.cpp -o master_test

#include "../Source/DrumMachine.h"
#include "../Source/DspUtil.h"
#include "../Source/Synth303.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

constexpr int sr = 44100;
constexpr int numSamples = sr * 2;

float rmsOf (const std::vector<float>& v)
{
    double s = 0.0;
    for (float x : v)
        s += (double) x * x;
    return (float) std::sqrt (s / (double) v.size());
}

// A busy accented line: the hardest case for the ceiling, and the one a 303
// actually plays.
void renderBass (std::vector<float>& b, Synth303::Wave w, float volDb)
{
    Synth303 s;
    s.prepare (sr);
    s.setParams (w, 0.0f, 500.0f, 0.5f, 0.5f, 300.0f, 0.5f, volDb, 0.0f, 0.0f);

    const int step = (int) (sr * 60.0 / 130.0);
    for (int k = 0, pos = 0; pos < (int) b.size(); ++k)
    {
        const int len = std::min (step, (int) b.size() - pos);
        const int note = 45 + (k % 2 ? 0 : 12);
        s.noteOn (note, 1, false);
        s.render (b.data() + pos, len / 2);
        s.noteOff (note);
        s.render (b.data() + pos + len / 2, len - len / 2);
        pos += len;
    }
}

// Four-on-the-floor with hats: the kick is what collides with the bass.
void renderDrums (std::vector<float>& d, float volDb)
{
    const float levels[5] = { 0.9f, 0.8f, 0.7f, 0.6f, 0.6f };
    DrumMachine dm;
    dm.prepare (sr);
    dm.setParams (DrumMachine::Kit::K909, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, levels, volDb);

    const int beat = (int) (sr * 60.0 / 130.0);
    for (int k = 0, pos = 0; pos < (int) d.size(); ++k)
    {
        const int len = std::min (beat, (int) d.size() - pos);
        dm.trigger (DrumMachine::BD, true);
        if (k % 4 == 2)
            dm.trigger (DrumMachine::SD, false);
        dm.trigger (DrumMachine::CH, false);
        dm.render (d.data() + pos, len);
        pos += len;
    }
}

// Exactly what processBlock does after the two lines are summed.
void masterStage (std::vector<float>& l, std::vector<float>& r)
{
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] *= dsp303::masterHeadroom;
        r[i] *= dsp303::masterHeadroom;
    }

    dsp303::MasterLimiter lim;
    lim.prepare (sr);
    lim.process (l.data(), r.data(), (int) l.size());
}

// maxDamage is how much the drums are allowed to move the bass, in dB relative
// to the bass itself. At stock levels the answer is "not at all" and the bound
// is the float noise floor. Past the ceiling the limiter has to pull the mix
// down and the bass genuinely does duck with the kick — that is a limiter doing
// its job, and the bound is only there to catch it getting worse. For scale, the
// tanh this replaced measured -10.5 dB at stock levels and -1.8 dB at +12.
int check (const char* name, Synth303::Wave w, float bassDb, float drumDb,
           float maxDamage)
{
    std::vector<float> bass (numSamples, 0.0f), drums (numSamples, 0.0f);
    renderBass (bass, w, bassDb);
    renderDrums (drums, drumDb);

    auto through = [] (const std::vector<float>& mono)
    {
        std::vector<float> l = mono, r = mono;
        masterStage (l, r);
        // the master stage must move both channels together or the image drifts
        for (size_t i = 0; i < l.size(); ++i)
            if (l[i] != r[i])
                return std::vector<float> {};
        return l;
    };

    std::vector<float> sum (numSamples);
    for (int i = 0; i < numSamples; ++i)
        sum[i] = bass[i] + drums[i];

    const auto mixOut   = through (sum);
    const auto drumsOut = through (drums);
    const auto bassOut  = through (bass);

    if (mixOut.empty() || drumsOut.empty() || bassOut.empty())
    {
        printf ("  %-30s FAIL  master stage broke L/R linkage\n", name);
        return 1;
    }

    std::vector<float> err (numSamples);
    float outPeak = 0.0f;
    bool finite = true;
    for (int i = 0; i < numSamples; ++i)
    {
        err[i] = (mixOut[i] - drumsOut[i]) - bassOut[i];
        outPeak = std::max (outPeak, std::abs (mixOut[i]));
        if (! std::isfinite (mixOut[i]))
            finite = false;
    }

    const float damage = 20.0f * std::log10 (std::max (rmsOf (err) / rmsOf (bassOut), 1e-12f));
    const bool  pass   = damage < maxDamage && outPeak <= 1.0f && finite;

    printf ("  %-30s damage %8.1f dB (need < %6.1f)  peak %.3f  %s\n",
            name, damage, maxDamage, outPeak, pass ? "ok" : "FAIL");
    return pass ? 0 : 1;
}

} // namespace

int main()
{
    int failures = 0;

    printf ("master stage: the bass must survive the drums\n");

    // At stock levels nothing reaches the ceiling, so the bass comes back out
    // bit-for-bit and the only thing left is float rounding.
    failures += check ("pulse, defaults",  Synth303::Wave::Pulse,  0.0f, 0.0f, -60.0f);
    failures += check ("square, defaults", Synth303::Wave::Square, 0.0f, 0.0f, -60.0f);
    failures += check ("saw, defaults",    Synth303::Wave::Saw,    0.0f, 0.0f, -60.0f);

    // Pushed past the ceiling on purpose. The limiter engages and the bass moves
    // with it; these bounds just pin how much, so a future change to the
    // threshold or the time constants can't quietly make it worse.
    failures += check ("pulse, drums +6 dB", Synth303::Wave::Pulse, 0.0f,  6.0f, -12.0f);
    failures += check ("pulse, both +12 dB", Synth303::Wave::Pulse, 12.0f, 12.0f, -3.5f);

    // The safety curve is the identity below its knee, exactly, or the quiet
    // case is not really untouched.
    for (float x = -0.9f; x <= 0.9f; x += 0.01f)
        if (dsp303::safetyClip (x) != x)
        {
            printf ("  safetyClip is not exactly linear at %.3f\n", x);
            ++failures;
            break;
        }

    // ...and it must never let anything past full scale, however hard it is hit.
    for (float x = 1.0f; x <= 100.0f; x += 0.5f)
        if (std::abs (dsp303::safetyClip (x)) > 1.0f
            || std::abs (dsp303::safetyClip (-x)) > 1.0f)
        {
            printf ("  safetyClip exceeded full scale at %.1f\n", x);
            ++failures;
            break;
        }

    printf (failures == 0 ? "MASTER-TEST OK\n" : "MASTER-TEST FAILED\n");
    return failures != 0;
}
