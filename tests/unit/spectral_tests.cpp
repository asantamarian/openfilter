#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

using namespace of;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;

BandParams spectralBell (double freq, double q, double rangeDb, bool resonance)
{
    BandParams b;
    b.used = true;  b.shape = Shape::Bell;  b.freq = freq;  b.q = q;  b.gainDb = 0.0;
    b.dyn.on = true;  b.dyn.spectral = true;  b.dyn.resonance = resonance;  b.dyn.rangeDb = rangeDb;
    b.dyn.autoThreshold = false;  b.dyn.thresholdDb = -40.0;  b.dyn.ratio = 100.0;  b.dyn.kneeDb = 0.0;
    b.dyn.attackMs = 5.0;  b.dyn.releaseMs = 50.0;
    return b;
}

void run (Engine& e, std::vector<double>& l, std::vector<double>& r, const ParamSnapshot& p)
{
    for (size_t pos = 0; pos < l.size(); pos += 512)
    {
        double* io[2] = { l.data() + pos, r.data() + pos };
        e.process (io, 2, static_cast<int> (std::min<size_t> (512, l.size() - pos)), p);
    }
}

/** Level in dB of one frequency in the second half of a signal (a single-bin Fourier sum). */
double toneDb (const std::vector<double>& x, double freq)
{
    double re = 0.0, im = 0.0;
    const size_t from = x.size() / 2, n = x.size() - from;
    for (size_t i = from; i < x.size(); ++i)
    {
        const double ph = 2.0 * std::numbers::pi * freq * static_cast<double> (i) / fs;
        re += x[i] * std::cos (ph);
        im += x[i] * std::sin (ph);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / static_cast<double> (n) + 1.0e-20);
}
} // namespace

TEST_CASE ("spectral: adds one frame of latency and is transparent when nothing triggers", "[spectral]")
{
    ParamSnapshot p;
    p.bands[0] = spectralBell (2000.0, 1.0, -12.0, false);
    p.bands[0].dyn.thresholdDb = 0.0;   // never reached by this signal

    Engine e;
    e.prepare (fs, 512, 2);
    std::mt19937 rng (2);
    std::uniform_real_distribution<double> uni (-0.1, 0.1);
    std::vector<double> l (16384), r (16384);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = uni (rng); r[i] = uni (rng); }
    const auto refL = l, refR = r;
    run (e, l, r, p);

    const auto lat = static_cast<size_t> (e.latencySamples());
    CHECK (lat == 2048);
    CHECK (e.latencySamples() == Engine::latencyFor (0, fs, 1, true));
    double worst = 0.0;
    for (size_t i = lat + 2048; i < l.size(); ++i)
        worst = std::max ({ worst, std::abs (l[i] - refL[i - lat]), std::abs (r[i] - refR[i - lat]) });
    INFO ("worst reconstruction error " << worst);
    CHECK (worst < 1.0e-9);

    // Without a spectral band there is no such stage and no latency.
    ParamSnapshot none;
    Engine plain;
    plain.prepare (fs, 512, 2);
    std::vector<double> a (1024, 0.0), b (1024, 0.0);
    run (plain, a, b, none);
    CHECK (plain.latencySamples() == 0);
}

TEST_CASE ("spectral: a loud partial is turned down, a quiet neighbour in the same band is not", "[spectral]")
{
    // Two tones inside one wide band: 1800 Hz at -10 dBFS, 2600 Hz at -50 dBFS.
    const auto signal = []
    {
        std::vector<double> x (static_cast<size_t> (fs) * 2);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / fs;
            x[i] = std::pow (10.0, -10.0 / 20.0) * std::sin (2.0 * std::numbers::pi * 1800.0 * t)
                 + std::pow (10.0, -50.0 / 20.0) * std::sin (2.0 * std::numbers::pi * 2600.0 * t);
        }
        return x;
    };

    // Spectral, absolute threshold at -40 dB: only the loud tone is over it.
    {
        ParamSnapshot p;
        p.bands[0] = spectralBell (2200.0, 0.7, -12.0, false);
        p.bands[0].dyn.resolutionOct = 1.0 / 24.0;
        Engine e;
        e.prepare (fs, 512, 2);
        auto l = signal(), r = l;
        run (e, l, r, p);
        const double loud = toneDb (l, 1800.0), quiet = toneDb (l, 2600.0);
        INFO ("spectral: loud tone " << loud << " dB (was -10), quiet tone " << quiet << " dB (was -50)");
        CHECK (loud < -10.0 - 8.0);                   // clearly reduced
        CHECK_THAT (quiet, WithinAbs (-50.0, 1.0));   // left alone
        CHECK (e.dynamicGainDb (0) < -8.0f);          // and the display is told
    }

    // The same settings as an ordinary dynamic band: one gain for the whole band, so the
    // quiet tone goes down with the loud one. This is the difference spectral mode makes.
    {
        ParamSnapshot p;
        p.bands[0] = spectralBell (2200.0, 0.7, -12.0, false);
        p.bands[0].dyn.spectral = false;
        Engine e;
        e.prepare (fs, 512, 2);
        auto l = signal(), r = l;
        run (e, l, r, p);
        INFO ("dynamic: quiet tone " << toneDb (l, 2600.0) << " dB (was -50)");
        CHECK (toneDb (l, 2600.0) < -50.0 - 5.0);
    }
}

TEST_CASE ("spectral: resonance mode treats what sticks out, at any level", "[spectral]")
{
    // Noise with one narrow tone 20 dB above the noise around it, at two overall levels.
    for (double levelDb : { -12.0, -36.0 })
    {
        ParamSnapshot p;
        p.bands[0] = spectralBell (3000.0, 0.5, -12.0, true);
        p.bands[0].dyn.thresholdDb = -60.0;      // read as "8 dB above the surroundings"
        p.bands[0].dyn.resolutionOct = 1.0 / 24.0;

        std::mt19937 rng (7);
        std::normal_distribution<double> gauss (0.0, 1.0);
        const double amp = std::pow (10.0, levelDb / 20.0);
        std::vector<double> l (static_cast<size_t> (fs) * 3), r;
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = amp * (0.05 * gauss (rng) + 0.5 * std::sin (2.0 * std::numbers::pi * 3000.0 * static_cast<double> (i) / fs));
        r = l;
        const double before = toneDb (l, 3000.0);

        Engine e;
        e.prepare (fs, 512, 2);
        run (e, l, r, p);
        const double after = toneDb (l, 3000.0);
        INFO ("level " << levelDb << " dB: tone went from " << before << " to " << after << " dB");
        CHECK (after < before - 6.0);   // the tone is pulled down at both levels
    }

    // Plain noise has nothing sticking out: resonance mode leaves it nearly alone.
    ParamSnapshot p;
    p.bands[0] = spectralBell (3000.0, 0.5, -12.0, true);
    p.bands[0].dyn.thresholdDb = -30.0;   // 16 dB above the surroundings
    std::mt19937 rng (9);
    std::normal_distribution<double> gauss (0.0, 0.1);
    std::vector<double> l (static_cast<size_t> (fs) * 2), r;
    for (auto& v : l) v = gauss (rng);
    r = l;
    double in = 0.0, out = 0.0;
    for (size_t i = l.size() / 2; i < l.size(); ++i) in += l[i] * l[i];
    Engine e;
    e.prepare (fs, 512, 2);
    run (e, l, r, p);
    for (size_t i = l.size() / 2; i < l.size(); ++i) out += l[i] * l[i];
    INFO ("noise level changed by " << 10.0 * std::log10 (out / in) << " dB");
    CHECK (std::abs (10.0 * std::log10 (out / in)) < 1.0);
}

TEST_CASE ("spectral: placements, mode changes and edits while running stay finite", "[spectral]")
{
    Engine e;
    e.prepare (44100.0, 256, 2);
    std::mt19937 rng (31);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    ParamSnapshot p;
    double peak = 0.0;
    for (int block = 0; block < 1500; ++block)
    {
        if (block % 25 == 0)
            for (int i = 0; i < 3; ++i)
            {
                auto b = spectralBell (100.0 * std::pow (100.0, uni (rng)), 0.3 + 4.0 * uni (rng), 24.0 * uni (rng) - 12.0, uni (rng) > 0.5);
                b.place = static_cast<Placement> (static_cast<int> (uni (rng) * 4.999));
                b.dyn.thresholdDb = -90.0 * uni (rng);
                b.dyn.spectral = uni (rng) > 0.2;   // sometimes none is spectral: the stage comes and goes
                b.gainDb = 12.0 * uni (rng) - 6.0;
                p.bands[static_cast<size_t> (i)] = b;
            }
        p.phaseMode = (block / 400) % 2;
        std::vector<double> l (256), r (256);
        for (size_t i = 0; i < 256; ++i) { l[i] = uni (rng) - 0.5; r[i] = uni (rng) - 0.5; }
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 256, p);
        for (size_t i = 0; i < 256; ++i)
        {
            REQUIRE (std::isfinite (l[i]));
            REQUIRE (std::isfinite (r[i]));
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
    }
    INFO ("peak " << peak);
    CHECK (peak < 100.0);
}
