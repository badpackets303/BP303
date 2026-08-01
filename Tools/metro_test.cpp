// Offline test for the Metronome: verifies clicks land on beat boundaries,
// the downbeat is louder, and disabling silences it.
// Build: clang++ -std=c++17 -O2 Tools/metro_test.cpp -o metro_test

#include "../Source/Metronome.h"

#include <cstdio>
#include <vector>

int main()
{
    const double sr = 44100.0;
    const double bpm = 120.0;                 // 1 beat = 0.5 s = 22050 samples
    const int beatSamples = (int) (sr * 60.0 / bpm);

    Metronome m;
    m.prepare (sr);

    // render 4 beats in blocks, tracking beat phase
    const int block = 512;
    const int total = beatSamples * 4 + 1000;
    std::vector<float> out;
    out.reserve (total);
    std::vector<float> buf (block);

    double beats = 0.0;
    const double beatsPerBlock = bpm / 60.0 * block / sr;
    for (int done = 0; done < total; done += block)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        m.process (buf.data(), block, beats, bpm, true);
        beats += beatsPerBlock;
        out.insert (out.end(), buf.begin(), buf.end());
    }

    int failures = 0;

    // find click onsets with a refractory window (a click is a decaying sine,
    // so ignore ~50 ms after each detected onset)
    std::vector<int> onsets;
    const int refractory = (int) (sr * 0.05);
    int lastOnset = -refractory * 2;
    for (int i = 0; i < (int) out.size(); ++i)
    {
        if (std::fabs (out[i]) > 0.05f && i - lastOnset > refractory)
        {
            onsets.push_back (i);
            lastOnset = i;
        }
    }

    std::printf ("onsets found: %zu\n", onsets.size());
    if (onsets.size() < 4) ++failures;

    // each onset should sit within a few ms of a beat boundary
    for (size_t k = 0; k < onsets.size() && k < 4; ++k)
    {
        const int nearestBeat = (int) llround ((double) onsets[k] / beatSamples) * beatSamples;
        const int err = std::abs (onsets[k] - nearestBeat);
        std::printf ("  onset %zu at %d, beat error %d samples\n", k, onsets[k], err);
        if (err > (int) (sr * 0.003)) ++failures;   // within 3 ms
    }

    // downbeat (beat 0) should be louder than beat 1
    auto peakNear = [&] (int beat) {
        const int c = beat * beatSamples;
        float pk = 0.0f;
        for (int i = c; i < c + 2000 && i < (int) out.size(); ++i)
            pk = std::max (pk, std::fabs (out[i]));
        return pk;
    };
    const float p0 = peakNear (0), p1 = peakNear (1);
    std::printf ("downbeat peak=%.3f, beat2 peak=%.3f\n", p0, p1);
    if (! (p0 > p1 * 1.2f)) ++failures;

    // disabled -> silence
    {
        Metronome m2; m2.prepare (sr);
        std::vector<float> b (block, 0.0f);
        m2.process (b.data(), block, 0.0, bpm, false);
        float pk = 0.0f;
        for (float v : b) pk = std::max (pk, std::fabs (v));
        std::printf ("disabled peak=%.4f\n", pk);
        if (pk > 0.0f) ++failures;
    }

    std::printf (failures == 0 ? "METRO-TEST OK\n" : "METRO-TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
