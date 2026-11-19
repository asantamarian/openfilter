#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>
#include <thread>
#include <vector>

using namespace of;
using Catch::Matchers::WithinAbs;

namespace
{
BandParams band (Shape shape, double f, double g, double q, Placement place = Placement::Stereo, Slope slope = Slope::S12)
{
    BandParams b;
    b.used = true;  b.shape = shape;  b.slope = slope;  b.place = place;  b.freq = f;  b.gainDb = g;  b.q = q;
    return b;
}

/** Feeds silence until the worker's kernel for the current bands is in use. */
bool settle (Engine& e, const ParamSnapshot& p)
{
    std::vector<double> l (512, 0.0), r (512, 0.0);
    for (int tries = 0; tries < 4000; ++tries)
    {
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 512, p);
        if (e.linearSettled())
        {
            for (int flush = 0; flush < 4; ++flush)   // let the crossfade block pass
                e.process (io, 2, 512, p);
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return false;
}

void run (Engine& e, std::vector<double>& l, std::vector<double>& r, const ParamSnapshot& p)
{
    for (size_t pos = 0; pos < l.size(); pos += 512)
    {
        double* io[2] = { l.data() + pos, r.data() + pos };
        e.process (io, 2, static_cast<int> (std::min<size_t> (512, l.size() - pos)), p);
    }
}

double sineGainDb (Engine& e, const ParamSnapshot& p, double fs, double freq)
{
    const size_t total = static_cast<size_t> (fs);
    std::vector<double> l (total), r (total);
    for (size_t i = 0; i < total; ++i)
        l[i] = r[i] = 0.25 * std::sin (2.0 * std::numbers::pi * freq * static_cast<double> (i) / fs);
    run (e, l, r, p);
    double acc = 0.0;
    for (size_t i = total / 2; i < total; ++i)
        acc += l[i] * l[i];
    return 10.0 * std::log10 (acc / static_cast<double> (total - total / 2) / (0.25 * 0.25 * 0.5));
}
} // namespace

TEST_CASE ("linear: latency is reported exactly and the impulse response is symmetric", "[linear]")
{
    for (int quality : { 0, 1 })
    {
        ParamSnapshot p;
        p.phaseMode = 2;
        p.linQuality = quality;
        p.bands[0] = band (Shape::Bell, 1000.0, 9.0, 1.5);
        p.bands[1] = band (Shape::HighShelf, 6000.0, -6.0, 1.0);

        Engine e;
        e.prepare (48000.0, 512, 2);
        REQUIRE (settle (e, p));
        const int lat = e.latencySamples();
        CHECK (lat == (4096 << quality) / 2 + 512);
        CHECK (lat == Engine::latencyFor (2, 48000.0, quality));

        const size_t total = static_cast<size_t> (2 * lat + 4096);
        std::vector<double> l (total, 0.0), r (total, 0.0);
        l[100] = r[100] = 1.0;
        run (e, l, r, p);

        const size_t centre = 100 + static_cast<size_t> (lat);
        const auto peak = static_cast<size_t> (std::max_element (l.begin(), l.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); }) - l.begin());
        CHECK (peak == centre);

        // Linear phase means the response is a mirror image around its centre.
        double worst = 0.0;
        for (size_t k = 1; k < 1500; ++k)
            worst = std::max (worst, std::abs (l[centre + k] - l[centre - k]));
        INFO ("worst asymmetry " << worst);
        CHECK (worst < 1.0e-9);
    }
}

TEST_CASE ("linear: the response follows the prototype", "[linear]")
{
    ParamSnapshot p;
    p.phaseMode = 2;
    p.linQuality = 1;
    p.bands[0] = band (Shape::Bell, 1000.0, 6.0, 2.0);
    p.bands[1] = band (Shape::HighShelf, 9000.0, 5.0, 1.0);
    p.bands[2] = band (Shape::Bell, 150.0, -4.0, 1.0);

    Engine e;
    e.prepare (48000.0, 512, 2);
    REQUIRE (settle (e, p));
    for (double f : { 60.0, 150.0, 400.0, 1000.0, 1300.0, 5000.0, 12000.0, 19000.0 })
    {
        double expected = 0.0;
        for (const auto& b : p.bands)
            expected += bandMagnitudeDb (b, f);
        INFO ("frequency " << f);
        // No cramping near Nyquist here: the kernel is the prototype's own magnitude.
        CHECK_THAT (sineGainDb (e, p, 48000.0, f), WithinAbs (expected, 0.1));
    }
}

TEST_CASE ("linear: idle is an exact delay; left and side bands stay in their channel", "[linear]")
{
    std::mt19937 rng (4);
    std::uniform_real_distribution<double> uni (-0.5, 0.5);

    // Nothing active: the input, delayed by the latency, bit for bit.
    {
        ParamSnapshot p;
        p.phaseMode = 2;
        p.linQuality = 0;
        Engine e;
        e.prepare (48000.0, 512, 2);
        REQUIRE (settle (e, p));
        std::vector<double> l (8192), r (8192);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = uni (rng); r[i] = uni (rng); }
        const auto refL = l, refR = r;
        run (e, l, r, p);
        const auto lat = static_cast<size_t> (e.latencySamples());
        bool exact = true;
        for (size_t i = lat; i < l.size(); ++i)
            exact = exact && l[i] == refL[i - lat] && r[i] == refR[i - lat];
        CHECK (exact);
    }

    // A Left band leaves the right channel as a pure delay; a Side band leaves a mono
    // (pure mid) signal alone.
    for (auto place : { Placement::Left, Placement::Side })
    {
        ParamSnapshot p;
        p.phaseMode = 2;
        p.linQuality = 0;
        p.bands[0] = band (Shape::Bell, 2000.0, -12.0, 1.0, place);
        Engine e;
        e.prepare (48000.0, 512, 2);
        REQUIRE (settle (e, p));
        std::vector<double> l (8192), r (8192);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = uni (rng); r[i] = place == Placement::Side ? l[i] : uni (rng); }
        const auto refL = l, refR = r;
        run (e, l, r, p);
        const auto lat = static_cast<size_t> (e.latencySamples());
        double worstR = 0.0, changeL = 0.0;
        for (size_t i = lat; i < l.size(); ++i)
        {
            worstR = std::max (worstR, std::abs (r[i] - refR[i - lat]));
            changeL = std::max (changeL, std::abs (l[i] - refL[i - lat]));
        }
        CHECK (worstR < 1.0e-9);
        if (place == Placement::Left)
            CHECK (changeL > 0.01);    // the left channel is equalised
        else
            CHECK (changeL < 1.0e-9);  // no side content, so nothing to change
    }
}

TEST_CASE ("mixed: unflagged bands stay minimum phase, flagged ones go linear", "[linear]")
{
    std::mt19937 rng (6);
    std::uniform_real_distribution<double> uni (-0.5, 0.5);
    std::vector<double> noiseL (16384), noiseR (16384);
    for (size_t i = 0; i < noiseL.size(); ++i) { noiseL[i] = uni (rng); noiseR[i] = uni (rng); }

    ParamSnapshot p;
    p.linQuality = 0;
    p.bands[0] = band (Shape::Bell, 800.0, 6.0, 2.0);
    p.bands[1] = band (Shape::LowCut, 90.0, 0.0, 0.7071, Placement::Stereo, Slope::S24);

    // With no band flagged linear, Mixed is Zero Latency plus the delay.
    p.phaseMode = 0;
    Engine zero;
    zero.prepare (48000.0, 512, 2);
    auto zl = noiseL, zr = noiseR;
    run (zero, zl, zr, p);

    p.phaseMode = 3;
    Engine mixed;
    mixed.prepare (48000.0, 512, 2);
    REQUIRE (settle (mixed, p));
    // settle() ran silence through it; the bands are settled, so start comparing afresh.
    auto ml = noiseL, mr = noiseR;
    Engine mixed2;
    mixed2.prepare (48000.0, 512, 2);
    run (mixed2, ml, mr, p);
    const auto lat = static_cast<size_t> (mixed2.latencySamples());
    CHECK (lat == 2048 + 512);
    double worst = 0.0;
    for (size_t i = lat; i < ml.size(); ++i)
        worst = std::max (worst, std::abs (ml[i] - zl[i - lat]));
    INFO ("worst difference " << worst);
    CHECK (worst < 1.0e-9);

    // Flag the bell linear: its gain is still right, and so is the minimum-phase low cut.
    p.bands[0].linear = true;
    Engine both;
    both.prepare (48000.0, 512, 2);
    REQUIRE (settle (both, p));
    CHECK_THAT (sineGainDb (both, p, 48000.0, 800.0), WithinAbs (6.0 + bandMagnitudeDb (p.bands[1], 800.0), 0.1));
    CHECK_THAT (sineGainDb (both, p, 48000.0, 90.0), WithinAbs (bandMagnitudeDb (p.bands[0], 90.0) + bandMagnitudeDb (p.bands[1], 90.0), 0.15));
}

TEST_CASE ("linear: edits while running crossfade without blowing up; engines start and stop cleanly", "[linear]")
{
    ParamSnapshot p;
    p.phaseMode = 2;
    p.linQuality = 0;
    Engine e;
    e.prepare (48000.0, 256, 2);
    std::mt19937 rng (12);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    double peak = 0.0;

    for (int block = 0; block < 1500; ++block)
    {
        if (block % 20 == 0)
            for (int i = 0; i < 4; ++i)
            {
                p.bands[static_cast<size_t> (i)] = band (static_cast<Shape> (static_cast<int> (uni (rng) * 4.999)), 30.0 * std::pow (500.0, uni (rng)),
                                                         24.0 * uni (rng) - 12.0, 0.3 + 6.0 * uni (rng),
                                                         static_cast<Placement> (static_cast<int> (uni (rng) * 4.999)));
            }
        if (block % 300 == 299)
            p.phaseMode = p.phaseMode == 2 ? 3 : 2;
        if (block % 500 == 499)
            p.linQuality = 1 - p.linQuality;

        std::vector<double> l (256), r (256);
        for (size_t i = 0; i < 256; ++i) { l[i] = uni (rng) - 0.5; r[i] = uni (rng) - 0.5; }
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 256, p);
        for (size_t i = 0; i < 256; ++i)
        {
            REQUIRE (std::isfinite (l[i]));
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
        if (block % 7 == 0)
            std::this_thread::sleep_for (std::chrono::microseconds (300));   // give the worker a chance
    }
    INFO ("peak " << peak);
    CHECK (peak < 60.0);   // four bands of at most +12 dB on +-0.5 noise

    // Starting, re-preparing and destroying engines must never hang on the worker thread.
    for (int i = 0; i < 20; ++i)
    {
        Engine temp;
        temp.prepare (i % 2 ? 44100.0 : 96000.0, 512, 2);
        temp.prepare (48000.0, 256, 2);
    }
    SUCCEED();
}

TEST_CASE ("linear: dynamic bands work in the linear-phase path", "[linear]")
{
    // A dynamic bell in Linear mode, and the same band flagged linear in Mixed mode.
    for (int mode : { 2, 3 })
    {
        ParamSnapshot p;
        p.phaseMode = mode;
        p.linQuality = 0;
        p.bands[0] = band (Shape::Bell, 1000.0, 2.0, 2.0);
        p.bands[0].linear = true;
        p.bands[0].dyn.on = true;
        p.bands[0].dyn.rangeDb = -9.0;
        p.bands[0].dyn.autoThreshold = false;
        p.bands[0].dyn.thresholdDb = -30.0;
        p.bands[0].dyn.ratio = 100.0;
        p.bands[0].dyn.kneeDb = 0.0;
        p.bands[0].dyn.attackMs = 5.0;
        p.bands[0].dyn.releaseMs = 100.0;

        // The second stage adds half a 4096-tap kernel to the latency.
        Engine e;
        e.prepare (48000.0, 512, 2);
        REQUIRE (settle (e, p));
        CHECK (e.latencySamples() == 2048 + 512 + 2048);
        CHECK (e.latencySamples() == Engine::latencyFor (mode, 48000.0, 0, false, 0, 0, true));
        CHECK (p.wantsLinearDynamics());

        // Loud at the band: static +2 dB plus the full -9 dB of range.
        Engine loud;
        loud.prepare (48000.0, 512, 2);
        REQUIRE (settle (loud, p));
        const double loudGain = sineGainDb (loud, p, 48000.0, 1000.0);   // 0.25 peak: about -15 dBFS RMS, well over -30
        INFO ("mode " << mode << ": loud tone gain " << loudGain << " dB, dynamic gain " << loud.dynamicGainDb (0));
        CHECK_THAT (loudGain, WithinAbs (2.0 - 9.0, 0.3));
        CHECK_THAT (static_cast<double> (loud.dynamicGainDb (0)), WithinAbs (-9.0, 0.05));
        // Away from the band nothing changes, even while it is fully engaged.
        Engine away;
        away.prepare (48000.0, 512, 2);
        REQUIRE (settle (away, p));

        // Quiet at the band: only the static +2 dB.
        auto quietParams = p;
        quietParams.bands[0].dyn.thresholdDb = 0.0;   // never reached
        Engine quiet;
        quiet.prepare (48000.0, 512, 2);
        REQUIRE (settle (quiet, quietParams));
        CHECK_THAT (sineGainDb (quiet, quietParams, 48000.0, 1000.0), WithinAbs (2.0, 0.1));
        CHECK (quiet.dynamicGainDb (0) == 0.0f);
    }

    // Half-way into the range the result is close to the true half-way curve, not just at
    // the ends: ratio 2 on a tone 9 dB over the threshold gives 4.5 of the 9 dB.
    ParamSnapshot p;
    p.phaseMode = 2;
    p.linQuality = 0;
    p.bands[0] = band (Shape::Bell, 1000.0, 0.0, 2.0);
    p.bands[0].dyn.on = true;
    p.bands[0].dyn.rangeDb = -9.0;
    p.bands[0].dyn.autoThreshold = false;
    p.bands[0].dyn.thresholdDb = -24.0;   // the test tone reads -15 dB: 9 dB over
    p.bands[0].dyn.ratio = 2.0;
    p.bands[0].dyn.kneeDb = 0.0;
    Engine e;
    e.prepare (48000.0, 512, 2);
    REQUIRE (settle (e, p));
    const double gain = sineGainDb (e, p, 48000.0, 1000.0);
    INFO ("half-way gain " << gain << " dB, dynamic gain " << e.dynamicGainDb (0));
    CHECK_THAT (static_cast<double> (e.dynamicGainDb (0)), WithinAbs (-4.5, 0.2));
    CHECK_THAT (gain, WithinAbs (-4.5, 0.3));

    // Without any dynamic band in the lane there is no second stage and no extra latency.
    ParamSnapshot plain;
    plain.phaseMode = 2;
    plain.linQuality = 0;
    plain.bands[0] = band (Shape::Bell, 1000.0, 3.0, 2.0);
    CHECK_FALSE (plain.wantsLinearDynamics());
    Engine s;
    s.prepare (48000.0, 512, 2);
    REQUIRE (settle (s, plain));
    CHECK (s.latencySamples() == 2048 + 512);
}
