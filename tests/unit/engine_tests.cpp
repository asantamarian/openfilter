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
struct Stereo
{
    std::vector<double> l, r;
    double* ptrs[2] {};
    explicit Stereo (size_t n) : l (n), r (n) { ptrs[0] = l.data(); ptrs[1] = r.data(); }
};

Stereo noise (size_t n, unsigned seed)
{
    Stereo s (n);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> uni (-0.5, 0.5);
    for (size_t i = 0; i < n; ++i) { s.l[i] = uni (rng); s.r[i] = uni (rng); }
    return s;
}

/** Runs the engine over the whole signal in blocks of `block` samples. */
void run (Engine& e, Stereo& s, const ParamSnapshot& p, int block = 512, int channels = 2)
{
    const int total = static_cast<int> (s.l.size());
    for (int pos = 0; pos < total; pos += block)
    {
        double* io[2] = { s.l.data() + pos, s.r.data() + pos };
        e.process (io, channels, std::min (block, total - pos), p);
    }
}

double rmsDb (const std::vector<double>& x, size_t from)
{
    double acc = 0.0;
    for (size_t i = from; i < x.size(); ++i) acc += x[i] * x[i];
    return 10.0 * std::log10 (std::max (acc / static_cast<double> (x.size() - from), 1.0e-30));
}

BandParams band (Shape shape, double f, double g, double q, Placement place = Placement::Stereo, Slope slope = Slope::S12)
{
    return { true, true, shape, slope, place, f, g, q };
}
} // namespace

TEST_CASE ("engine: idle path is bit-exact", "[engine]")
{
    Engine e;
    e.prepare (48000.0, 512, 2);
    auto s = noise (48000, 1);
    const auto ref = s;

    ParamSnapshot p;
    run (e, s, p);
    CHECK (s.l == ref.l);
    CHECK (s.r == ref.r);

    // Used bands that do nothing (0 dB gain, bypassed) must not touch the signal either.
    p.bands[0] = band (Shape::Bell, 1000.0, 0.0, 1.0);
    p.bands[1] = band (Shape::HighShelf, 8000.0, 0.0, 1.0, Placement::Side);
    p.bands[2] = band (Shape::LowCut, 100.0, 0.0, 0.7071);
    p.bands[2].on = false;
    p.gainScale = 2.0;
    run (e, s, p);
    CHECK (s.l == ref.l);
    CHECK (s.r == ref.r);
}

TEST_CASE ("engine: steady-state sine gain follows the prototype", "[engine]")
{
    for (double fs : { 44100.0, 96000.0 })
    {
        ParamSnapshot p;
        p.bands[0] = band (Shape::Bell, 1000.0, 6.0, 2.0);
        p.bands[3] = band (Shape::HighShelf, 6000.0, -4.0, 1.0);
        p.bands[7] = band (Shape::LowCut, 80.0, 0.0, 0.7071, Placement::Stereo, Slope::S24);

        for (double f : { 60.0, 1000.0, 1500.0, 9000.0 })
        {
            Engine e;
            e.prepare (fs, 512, 2);
            Stereo s (static_cast<size_t> (fs));
            for (size_t i = 0; i < s.l.size(); ++i)
                s.l[i] = s.r[i] = 0.25 * std::sin (2.0 * std::numbers::pi * f * static_cast<double> (i) / fs);
            const double inDb = rmsDb (s.l, s.l.size() / 2);
            run (e, s, p);

            double expected = 0.0;
            for (const auto& b : p.bands)
                expected += bandMagnitudeDb (b, f);
            CHECK_THAT (rmsDb (s.l, s.l.size() / 2) - inDb, WithinAbs (expected, 0.1));
        }
    }
}

TEST_CASE ("engine: band order does not change the output", "[engine]")
{
    ParamSnapshot a;
    a.bands[0] = band (Shape::LowCut, 90.0, 0.0, 0.9, Placement::Stereo, Slope::S18);
    a.bands[1] = band (Shape::Bell, 400.0, -5.0, 3.0, Placement::Left);
    a.bands[2] = band (Shape::Bell, 2500.0, 4.0, 1.2, Placement::Mid);
    a.bands[3] = band (Shape::HighShelf, 9000.0, 3.0, 1.0, Placement::Side);
    a.bands[4] = band (Shape::Notch, 5000.0, 0.0, 6.0, Placement::Right);

    ParamSnapshot b;
    const int perm[] = { 17, 3, 31, 0, 9 };
    for (int i = 0; i < 5; ++i)
        b.bands[static_cast<size_t> (perm[i])] = a.bands[static_cast<size_t> (i)];

    Engine ea, eb;
    ea.prepare (48000.0, 512, 2);
    eb.prepare (48000.0, 512, 2);
    auto sa = noise (48000, 7), sb = noise (48000, 7);
    run (ea, sa, a);
    run (eb, sb, b);

    double worst = 0.0;
    for (size_t i = 0; i < sa.l.size(); ++i)
        worst = std::max ({ worst, std::abs (sa.l[i] - sb.l[i]), std::abs (sa.r[i] - sb.r[i]) });
    INFO ("worst difference " << worst);
    CHECK (worst < 1.0e-6);   // -120 dBFS
}

TEST_CASE ("engine: mid/side placement only touches its channel", "[engine]")
{
    ParamSnapshot p;
    p.bands[0] = band (Shape::Bell, 1000.0, -12.0, 1.0, Placement::Side);

    Engine e;
    e.prepare (48000.0, 512, 2);
    auto s = noise (24000, 3);
    for (size_t i = 0; i < s.l.size(); ++i)
        s.r[i] = s.l[i];   // pure mid signal
    const auto ref = s;
    run (e, s, p);
    for (size_t i = 0; i < s.l.size(); ++i)
    {
        REQUIRE (std::abs (s.l[i] - ref.l[i]) < 1.0e-12);
        REQUIRE (std::abs (s.r[i] - ref.r[i]) < 1.0e-12);
    }

    // A left band must leave the right channel exactly alone.
    p.bands[0].place = Placement::Left;
    Engine e2;
    e2.prepare (48000.0, 512, 2);
    auto s2 = noise (24000, 4);
    const auto ref2 = s2;
    run (e2, s2, p);
    CHECK (s2.r == ref2.r);
    CHECK (s2.l != ref2.l);
}

TEST_CASE ("engine: block size does not change the output", "[engine]")
{
    ParamSnapshot p;
    p.bands[0] = band (Shape::Bell, 700.0, 7.0, 2.0);
    p.bands[1] = band (Shape::HighCut, 12000.0, 0.0, 0.7071, Placement::Stereo, Slope::S48);

    auto a = noise (20000, 11), b = noise (20000, 11);
    Engine ea, eb;
    ea.prepare (48000.0, 512, 2);
    eb.prepare (48000.0, 512, 2);
    run (ea, a, p, 512);
    run (eb, b, p, 37);
    CHECK (a.l == b.l);
    CHECK (a.r == b.r);
}

TEST_CASE ("engine: parameter storms stay finite and bounded", "[engine]")
{
    Engine e;
    e.prepare (44100.0, 256, 2);
    std::mt19937 rng (99);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    ParamSnapshot p;
    double peak = 0.0;

    for (int block = 0; block < 2000; ++block)
    {
        if (block % 3 == 0)
            for (int i = 0; i < 8; ++i)
            {
                auto& b = p.bands[static_cast<size_t> (i)];
                b.used = uni (rng) > 0.1;
                b.on = uni (rng) > 0.1;
                b.shape = static_cast<Shape> (static_cast<int> (uni (rng) * 9.999));
                b.slope = static_cast<Slope> (static_cast<int> (uni (rng) * 9.999));
                b.place = static_cast<Placement> (static_cast<int> (uni (rng) * 4.999));
                b.freq = 10.0 * std::pow (3000.0, uni (rng));
                b.gainDb = 60.0 * uni (rng) - 30.0;
                b.q = 0.025 * std::pow (1600.0, uni (rng));
            }
        p.bypass = block % 400 > 380;
        p.delta = block % 700 > 650;
        p.outGainDb = block % 500 > 250 ? -6.0 : 0.0;

        auto s = noise (256, static_cast<unsigned> (block));
        run (e, s, p, 256);
        for (size_t i = 0; i < s.l.size(); ++i)
        {
            REQUIRE (std::isfinite (s.l[i]));
            REQUIRE (std::isfinite (s.r[i]));
            peak = std::max ({ peak, std::abs (s.l[i]), std::abs (s.r[i]) });
        }
    }
    INFO ("peak " << peak);
    CHECK (peak < 1.0e6);
}

TEST_CASE ("engine: smooth sweeps do not click", "[engine]")
{
    // A slow sine through a bell whose frequency, gain and Q are swept as fast as a host can
    // automate (new value every block). The output must stay as smooth as a sine can be.
    Engine e;
    e.prepare (48000.0, 64, 2);
    ParamSnapshot p;
    p.bands[0] = band (Shape::Bell, 200.0, 0.5, 1.0);
    double prev = 0.0, worstStep = 0.0;
    long n = 0;

    for (int block = 0; block < 3000; ++block)
    {
        const double t = block / 3000.0;
        p.bands[0].freq = 200.0 * std::pow (40.0, t);
        p.bands[0].gainDb = 12.0 * std::sin (40.0 * t) + 0.5;
        p.bands[0].q = 0.5 + 8.0 * t;

        Stereo s (64);
        for (size_t i = 0; i < 64; ++i, ++n)
            s.l[i] = s.r[i] = 0.5 * std::sin (2.0 * std::numbers::pi * 100.0 * static_cast<double> (n) / 48000.0);
        run (e, s, p, 64);
        for (size_t i = 0; i < 64; ++i)
        {
            worstStep = std::max (worstStep, std::abs (s.l[i] - prev));
            prev = s.l[i];
        }
    }
    // A 100 Hz sine at 0.5 moves 0.0065 per sample; allow for +12.5 dB of boost and some slack.
    INFO ("worst sample-to-sample step " << worstStep);
    CHECK (worstStep < 0.05);
}

TEST_CASE ("engine: bypass and mono", "[engine]")
{
    ParamSnapshot p;
    p.bands[0] = band (Shape::Bell, 1000.0, 12.0, 1.0, Placement::Side);
    p.bypass = true;

    Engine e;
    e.prepare (48000.0, 512, 2);
    auto s = noise (4096, 5);
    const auto ref = s;
    run (e, s, p);
    for (size_t i = 0; i < s.l.size(); ++i)
        REQUIRE (std::abs (s.l[i] - ref.l[i]) < 1.0e-12);

    // Mono: a Side band is treated as a plain band on the single channel.
    p.bypass = false;
    Engine m;
    m.prepare (48000.0, 512, 1);
    Stereo sine (48000);
    for (size_t i = 0; i < sine.l.size(); ++i)
        sine.l[i] = 0.25 * std::sin (2.0 * std::numbers::pi * 1000.0 * static_cast<double> (i) / 48000.0);
    const double inDb = rmsDb (sine.l, 24000);
    run (m, sine, p, 512, 1);
    CHECK_THAT (rmsDb (sine.l, 24000) - inDb, WithinAbs (12.0, 0.1));
}

TEST_CASE ("auto gain: the curve estimate and the loudness match both level a boost", "[autogain]")
{
    // Pink-ish noise: white noise through a gentle low-pass, so lows carry more energy.
    const auto pinkish = [] (size_t n, unsigned seed)
    {
        auto s = noise (n, seed);
        double zl = 0.0, zr = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            zl += (s.l[i] - zl) * 0.05;  s.l[i] = zl * 4.0;
            zr += (s.r[i] - zr) * 0.05;  s.r[i] = zr * 4.0;
        }
        return s;
    };
    const auto levelDb = [] (const std::vector<double>& x) { return rmsDb (x, x.size() / 2); };

    ParamSnapshot p;
    p.bands[0] = band (Shape::LowShelf, 300.0, 9.0, 1.0);
    p.bands[1] = band (Shape::Bell, 2500.0, 6.0, 1.0);

    auto ref = pinkish (48000 * 8, 21);
    const double inDb = levelDb (ref.l);

    // Off: the boost makes it clearly louder.
    {
        Engine e;
        e.prepare (48000.0, 512, 2);
        auto s = pinkish (48000 * 8, 21);
        run (e, s, p);
        CHECK (levelDb (s.l) - inDb > 4.0);
        CHECK (e.autoGainDb() == 0.0f);
    }
    // Curve: an estimate from the curve alone, applied at once and steady.
    {
        p.autoGain = 1;
        Engine e;
        e.prepare (48000.0, 512, 2);
        auto s = pinkish (48000 * 8, 21);
        run (e, s, p);
        INFO ("curve mode: output is " << levelDb (s.l) - inDb << " dB from the input, auto gain " << e.autoGainDb());
        CHECK (e.autoGainDb() < -2.0f);
        CHECK (std::abs (levelDb (s.l) - inDb) < 3.0);
    }
    // Loudness: measured, so it lands close.
    {
        p.autoGain = 2;
        Engine e;
        e.prepare (48000.0, 512, 2);
        auto s = pinkish (48000 * 8, 21);
        run (e, s, p);
        INFO ("loudness mode: output is " << levelDb (s.l) - inDb << " dB from the input, auto gain " << e.autoGainDb());
        CHECK (std::abs (levelDb (s.l) - inDb) < 1.5);
    }
    // With a flat EQ auto gain must do nothing in either mode.
    for (int mode : { 1, 2 })
    {
        ParamSnapshot flat;
        flat.autoGain = mode;
        Engine e;
        e.prepare (48000.0, 512, 2);
        auto s = pinkish (48000 * 4, 22);
        run (e, s, flat);
        CHECK (std::abs (e.autoGainDb()) < 0.05f);
    }
}
