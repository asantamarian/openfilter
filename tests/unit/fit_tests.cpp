#include <catch2/catch_test_macros.hpp>

#include "match/CurveFitter.h"

#include <cmath>
#include <cstdio>

using namespace of;

namespace
{
BandParams band (Shape shape, double f, double g, double q)
{
    BandParams b;
    b.used = true;  b.shape = shape;  b.freq = f;  b.gainDb = g;  b.q = q;
    return b;
}
} // namespace

TEST_CASE ("fit: a curve made from six bands is recovered with six bands", "[fit]")
{
    const std::vector<BandParams> truth = {
        band (Shape::LowShelf, 120.0, 4.0, 0.9), band (Shape::Bell, 250.0, -3.5, 1.4), band (Shape::Bell, 900.0, 2.5, 0.9),
        band (Shape::Bell, 3200.0, -5.0, 2.5), band (Shape::Bell, 6500.0, 3.0, 1.2), band (Shape::HighShelf, 9000.0, -4.0, 0.8),
    };
    const auto target = fit::evaluate (truth);
    const auto found = fit::fitCurve (target, 6);
    const double error = fit::rmsError (target, fit::evaluate (found));
    std::printf ("  six-band target fitted with %d bands: %.3f dB RMS error\n", static_cast<int> (found.size()), error);
    CHECK (found.size() <= 6);
    CHECK (error < 0.5);   // acceptance from docs/spec/04-analysis.md section 3.2
}

TEST_CASE ("fit: more bands fit better; a flat target needs none", "[fit]")
{
    // A bumpy target that no small set of bands reproduces exactly.
    fit::Curve target {};
    for (int i = 0; i < fit::kGridSize; ++i)
    {
        const double oct = std::log2 (fit::gridFrequency (i) / 1000.0);
        target[static_cast<size_t> (i)] = 5.0 * std::sin (1.7 * oct) + 3.0 * std::sin (4.1 * oct + 1.0) - 0.8 * oct;
    }
    double previous = 1.0e9;
    for (int k : { 2, 4, 8, 12 })
    {
        const auto bands = fit::fitCurve (target, k);
        const double error = fit::rmsError (target, fit::evaluate (bands));
        std::printf ("  bumpy target with %2d bands: %.2f dB RMS error\n", k, error);
        CHECK (static_cast<int> (bands.size()) <= k);
        CHECK (error < previous + 0.05);
        previous = error;
        for (const auto& b : bands)
        {
            CHECK (std::isfinite (b.freq + b.gainDb + b.q));
            CHECK ((b.freq >= 20.0 && b.freq <= 20000.0 && b.q >= 0.3 && b.q <= 8.0 && std::abs (b.gainDb) <= 24.0));
        }
    }
    CHECK (previous < 1.0);

    CHECK (fit::fitCurve (fit::Curve {}, 8).empty());
}

TEST_CASE ("fit: match target removes level and limits extremes", "[fit]")
{
    fit::Curve source {}, reference {};
    for (int i = 0; i < fit::kGridSize; ++i)
    {
        const double f = fit::gridFrequency (i);
        source[static_cast<size_t> (i)] = -30.0;
        reference[static_cast<size_t> (i)] = -18.0 + (f > 6000.0 ? 6.0 : 0.0) + (f < 40.0 ? -60.0 : 0.0);   // louder, brighter, no sub
    }
    const auto d = fit::matchTarget (source, reference);
    // The 12 dB level difference is gone; the 6 dB of extra top is kept; the missing sub is
    // limited to -18 dB.
    CHECK (std::abs (d[128]) < 0.5);                 // about 630 Hz
    CHECK (std::abs (d[240] - 6.0) < 0.5);           // about 13 kHz
    CHECK (d[2] >= -18.0);
    CHECK (d[2] < -10.0);
}
