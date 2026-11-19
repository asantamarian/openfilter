// Measures how much of one CPU core the engine uses in typical situations
// (docs/spec/07-build-test-release.md section 5). 48 kHz, 512-sample blocks, stereo.
//   of_bench            prints a table
//   of_bench --check    also fails if a scenario is over its budget
#include "dsp/Engine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

using namespace of;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 512;

BandParams band (Shape shape, double f, double g, double q, Placement place = Placement::Stereo, Slope slope = Slope::S12)
{
    BandParams b;
    b.used = true;  b.shape = shape;  b.slope = slope;  b.place = place;  b.freq = f;  b.gainDb = g;  b.q = q;
    return b;
}

struct Scenario { const char* name; ParamSnapshot params; double budgetPercent; bool moving = false; };

/** Percent of real time taken to process `seconds` of noise. */
double measure (Scenario& sc, double seconds)
{
    Engine e;
    e.prepare (fs, block, 2);
    std::mt19937 rng (1);
    std::uniform_real_distribution<double> uni (-0.25, 0.25);
    std::vector<double> l (block), r (block), noiseL (block * 64), noiseR (block * 64);
    for (auto& v : noiseL) v = uni (rng);
    for (auto& v : noiseR) v = uni (rng);

    // Warm up: let ramps finish and, for linear phase, let the worker deliver its kernel.
    for (int i = 0; i < 400; ++i)
    {
        std::memcpy (l.data(), noiseL.data() + (i % 64) * block, sizeof (double) * block);
        std::memcpy (r.data(), noiseR.data() + (i % 64) * block, sizeof (double) * block);
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, block, sc.params);
        if (! e.linearSettled())
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    const int blocks = static_cast<int> (seconds * fs / block);
    double best = 1.0e9;
    for (int pass = 0; pass < 3; ++pass)   // best of three: scheduling noise only ever adds time
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i)
        {
            std::memcpy (l.data(), noiseL.data() + (i % 64) * block, sizeof (double) * block);
            std::memcpy (r.data(), noiseR.data() + (i % 64) * block, sizeof (double) * block);
            if (sc.moving)   // automation: every band's frequency and gain move every block
                for (int b = 0; b < kMaxBands; ++b)
                    if (sc.params.bands[static_cast<size_t> (b)].used)
                    {
                        sc.params.bands[static_cast<size_t> (b)].freq *= (i / 40) % 2 ? 1.004 : 1.0 / 1.004;
                        sc.params.bands[static_cast<size_t> (b)].gainDb += (i / 40) % 2 ? 0.02 : -0.02;
                    }
            double* io[2] = { l.data(), r.data() };
            e.process (io, 2, block, sc.params);
        }
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        best = std::min (best, elapsed);
    }
    return 100.0 * best / (blocks * block / fs);
}
} // namespace

int main (int argc, char** argv)
{
    const bool check = argc > 1 && std::strcmp (argv[1], "--check") == 0;
    std::vector<Scenario> scenarios;

    scenarios.push_back ({ "Idle (no active bands)", {}, 0.10 });

    {
        Scenario s { "8 static bells, zero latency", {}, 0.4 };
        for (int i = 0; i < 8; ++i)
            s.params.bands[static_cast<size_t> (i)] = band (Shape::Bell, 80.0 * std::pow (2.0, i), i % 2 ? 3.0 : -3.0, 1.5);
        scenarios.push_back (s);
        s.name = "8 bells, all automated every block";
        s.moving = true;
        s.budgetPercent = 2.0;
        scenarios.push_back (s);
    }
    {
        Scenario s { "24 static bands, mixed shapes and placements", {}, 1.5 };
        for (int i = 0; i < 24; ++i)
        {
            const Shape shapes[] = { Shape::Bell, Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::Bell, Shape::Notch };
            s.params.bands[static_cast<size_t> (i)] = band (shapes[i % 6], 40.0 * std::pow (1.3, i), i % 2 ? 4.0 : -4.0, 2.0,
                                                           static_cast<Placement> (i % 5), i % 4 == 0 ? Slope::S24 : Slope::S12);
        }
        s.params.bands[0] = band (Shape::LowCut, 30.0, 0.0, 0.7071, Placement::Stereo, Slope::S48);
        s.params.bands[1] = band (Shape::HighCut, 19000.0, 0.0, 0.7071, Placement::Stereo, Slope::S24);
        scenarios.push_back (s);
        s.name = "The same 24 bands, natural (4x)";
        s.params.phaseMode = 1;
        s.budgetPercent = 6.0;
        scenarios.push_back (s);
        s.name = "The same 24 bands, linear, medium";
        s.params.phaseMode = 2;
        s.params.linQuality = 1;
        s.budgetPercent = 3.0;
        scenarios.push_back (s);
        s.name = "The same 24 bands, linear, max";
        s.params.linQuality = 4;
        s.budgetPercent = 12.0;
        scenarios.push_back (s);
    }
    {
        Scenario s { "8 dynamic bands", {}, 1.5 };
        for (int i = 0; i < 8; ++i)
        {
            auto b = band (Shape::Bell, 100.0 * std::pow (2.0, i), 0.0, 2.0);
            b.dyn.on = true;  b.dyn.rangeDb = -6.0;  b.dyn.autoThreshold = false;  b.dyn.thresholdDb = -40.0;
            s.params.bands[static_cast<size_t> (i)] = b;
        }
        scenarios.push_back (s);
    }
    {
        Scenario s { "4 spectral bands", {}, 4.0 };
        for (int i = 0; i < 4; ++i)
        {
            auto b = band (Shape::Bell, 300.0 * std::pow (3.0, i), 0.0, 1.0);
            b.dyn.on = true;  b.dyn.spectral = true;  b.dyn.rangeDb = -8.0;  b.dyn.thresholdDb = -50.0;
            s.params.bands[static_cast<size_t> (i)] = b;
        }
        scenarios.push_back (s);
    }
    {
        Scenario s { "Character: Tube (4x), no bands", {}, 2.0 };
        s.params.character = 1;
        scenarios.push_back (s);
        s.name = "Loudness auto gain, 8 bells";
        s.params.character = 0;
        s.params.autoGain = 2;
        for (int i = 0; i < 8; ++i)
            s.params.bands[static_cast<size_t> (i)] = band (Shape::Bell, 80.0 * std::pow (2.0, i), 3.0, 1.5);
        s.budgetPercent = 1.0;
        scenarios.push_back (s);
    }

    std::printf ("%-46s %10s %10s\n", "48 kHz, 512-sample blocks, stereo", "CPU", "budget");
    int over = 0;
    for (auto& s : scenarios)
    {
        const double percent = measure (s, 4.0);
        const bool bad = percent > s.budgetPercent;
        over += bad ? 1 : 0;
        std::printf ("%-46s %9.3f%% %9.2f%% %s\n", s.name, percent, s.budgetPercent, bad ? " OVER" : "");
    }
    std::printf ("(percent of one core; best of three runs of 4 s of audio each)\n");
    return check && over > 0 ? 1 : 0;
}
