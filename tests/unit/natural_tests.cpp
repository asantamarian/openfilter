#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Engine.h"
#include "dsp/util/Halfband.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

using namespace of;
using Catch::Matchers::WithinAbs;

namespace
{
/** Gain in dB of a steady sine through the engine, measured over the second half. */
double sineGainDb (const ParamSnapshot& p, double fs, double freq)
{
    Engine e;
    e.prepare (fs, 512, 2);
    const int total = static_cast<int> (fs);
    std::vector<double> l (static_cast<size_t> (total)), r (static_cast<size_t> (total));
    for (int i = 0; i < total; ++i)
        l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = 0.25 * std::sin (2.0 * std::numbers::pi * freq * i / fs);
    for (int pos = 0; pos < total; pos += 512)
    {
        double* io[2] = { l.data() + pos, r.data() + pos };
        e.process (io, 2, std::min (512, total - pos), p);
    }
    double acc = 0.0;
    for (int i = total / 2; i < total; ++i)
        acc += l[static_cast<size_t> (i)] * l[static_cast<size_t> (i)];
    return 10.0 * std::log10 (acc / (total - total / 2) / (0.25 * 0.25 * 0.5));
}
} // namespace

TEST_CASE ("halfband: round trip is a pure delay with a flat pass band", "[natural]")
{
    for (int half : { 13, 69 })
    {
        Halfband h;
        h.design (half);

        // Impulse: the round trip must peak at exactly `half` samples.
        std::vector<double> x (512, 0.0), up (1024), y (512);
        x[0] = 1.0;
        h.up (x.data(), 512, up.data());
        h.down (up.data(), 512, y.data());
        const auto peak = std::max_element (y.begin(), y.end()) - y.begin();
        CHECK (peak == half);

        // Sines: unity gain across the band the stage is used for. The steep filter keeps
        // 20 kHz of a 44.1 kHz signal; the wide one only ever sees signals that are already
        // oversampled, so a quarter of its input rate is the top of the audio band.
        const double edge = half == 69 ? 20000.0 / 44100.0 : 0.25;
        for (double rel : { 0.01, 0.1, 0.5 * edge, edge })
        {
            h.reset();
            const int n = 8192;
            std::vector<double> s (n), u (2 * n), d (n);
            for (int i = 0; i < n; ++i)
                s[static_cast<size_t> (i)] = std::sin (2.0 * std::numbers::pi * rel * i);
            h.up (s.data(), n, u.data());
            h.down (u.data(), n, d.data());
            double in = 0.0, out = 0.0;
            for (int i = n / 2; i < n; ++i)
            {
                in += s[static_cast<size_t> (i)] * s[static_cast<size_t> (i)];
                out += d[static_cast<size_t> (i)] * d[static_cast<size_t> (i)];
            }
            INFO ("half " << half << ", frequency " << rel << " of the sample rate");
            CHECK_THAT (10.0 * std::log10 (out / in), WithinAbs (0.0, 0.01));
        }
    }
}

TEST_CASE ("natural: idle path is the input delayed by the reported latency", "[natural]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        ParamSnapshot p;
        p.phaseMode = 1;
        Engine e;
        e.prepare (fs, 512, 2);

        std::mt19937 rng (3);
        std::uniform_real_distribution<double> uni (-0.5, 0.5);
        std::vector<double> l (8192), r (8192);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = uni (rng); r[i] = uni (rng); }
        const auto refL = l, refR = r;

        for (int pos = 0; pos < 8192; pos += 512)
        {
            double* io[2] = { l.data() + pos, r.data() + pos };
            e.process (io, 2, 512, p);
        }
        const int lat = e.latencySamples();
        CHECK (lat == Engine::latencyFor (1, fs));
        CHECK (lat == (fs < 70000.0 ? 76 : fs < 140000.0 ? 13 : 0));

        bool exact = true;
        for (size_t i = static_cast<size_t> (lat); i < l.size(); ++i)
            exact = exact && l[i] == refL[i - static_cast<size_t> (lat)] && r[i] == refR[i - static_cast<size_t> (lat)];
        CHECK (exact);
    }
}

TEST_CASE ("natural: latency holds with a band active", "[natural]")
{
    for (double fs : { 44100.0, 96000.0 })
    {
        ParamSnapshot p;
        p.phaseMode = 1;
        p.bands[0] = { true, true, Shape::Bell, Slope::S12, Placement::Stereo, 200.0, 0.5, 1.0 };   // nearly flat

        Engine e;
        e.prepare (fs, 512, 2);
        std::vector<double> l (4096, 0.0), r (4096, 0.0);
        l[100] = r[100] = 1.0;
        for (int pos = 0; pos < 4096; pos += 512)
        {
            double* io[2] = { l.data() + pos, r.data() + pos };
            e.process (io, 2, 512, p);
        }
        const auto peak = std::max_element (l.begin(), l.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); }) - l.begin();
        CHECK (peak == 100 + e.latencySamples());
    }
}

TEST_CASE ("natural: follows the prototype in the top octave where zero latency cannot", "[natural]")
{
    // Ordinary settings near the top of the band at 44.1 kHz.
    struct Case { Shape shape; double f0, gain, q; };
    const Case cases[] = { { Shape::HighShelf, 12000.0, 9.0, 1.0 }, { Shape::Bell, 14000.0, 9.0, 2.0 },
                           { Shape::HighShelf, 9000.0, -9.0, 4.0 }, { Shape::Bell, 9000.0, -9.0, 0.7071 } };
    double worstNatural = 0.0, worstZero = 0.0;

    for (const auto& c : cases)
    {
        ParamSnapshot p;
        p.bands[0] = { true, true, c.shape, Slope::S12, Placement::Stereo, c.f0, c.gain, c.q };
        for (double f : { 3000.0, 7000.0, 10000.0, 13000.0, 16000.0, 19000.0 })
        {
            const double expected = bandMagnitudeDb (p.bands[0], f);
            p.phaseMode = 0;
            worstZero = std::max (worstZero, std::abs (sineGainDb (p, 44100.0, f) - expected));
            p.phaseMode = 1;
            worstNatural = std::max (worstNatural, std::abs (sineGainDb (p, 44100.0, f) - expected));
        }
    }
    std::printf ("  top-octave error at 44.1 kHz: zero latency %.2f dB, natural %.2f dB\n", worstZero, worstNatural);
    CHECK (worstNatural < 0.3);
    CHECK (worstNatural < 0.25 * worstZero);
}

TEST_CASE ("natural: switching modes while running stays finite", "[natural]")
{
    Engine e;
    e.prepare (48000.0, 256, 2);
    ParamSnapshot p;
    p.bands[0] = { true, true, Shape::Bell, Slope::S24, Placement::Mid, 3000.0, 12.0, 4.0 };
    p.bands[1] = { true, true, Shape::LowCut, Slope::S48, Placement::Stereo, 60.0, 0.0, 0.7071 };
    std::mt19937 rng (8);
    std::uniform_real_distribution<double> uni (-0.5, 0.5);
    double peak = 0.0;

    for (int block = 0; block < 600; ++block)
    {
        p.phaseMode = (block / 40) % 2;
        p.bypass = block % 90 > 80;
        std::vector<double> l (256), r (256);
        for (size_t i = 0; i < 256; ++i) { l[i] = uni (rng); r[i] = uni (rng); }
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 256, p);
        for (size_t i = 0; i < 256; ++i)
        {
            REQUIRE (std::isfinite (l[i]));
            peak = std::max (peak, std::abs (l[i]));
        }
    }
    CHECK (peak < 8.0);   // +12 dB bell on +-0.5 noise leaves plenty of room below this
}
