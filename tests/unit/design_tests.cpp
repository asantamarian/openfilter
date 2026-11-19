#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/design/MatchedDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace of;
using Catch::Matchers::WithinAbs;

namespace
{
const char* shapeName (Shape s)
{
    constexpr const char* names[] = { "Bell", "LowShelf", "HighShelf", "LowCut", "HighCut", "Notch", "BandPass", "TiltShelf", "FlatTilt", "AllPass" };
    return names[static_cast<int> (s)];
}

/** Largest |digital - analog| in dB up to 20 kHz (or 0.9 Nyquist), over the part of the
    response that is above -40 dB. */
double worstError (const BandParams& p, double fs)
{
    const auto proto = makePrototype (p);
    const auto dig = designBand (p, fs);
    double worst = 0.0;
    const double top = std::min (0.45 * fs, 20000.0);
    for (int i = 0; i < 300; ++i)
    {
        const double f = 10.0 * std::pow (top / 10.0, i / 299.0);
        const double a = proto.magnitudeDb (f);
        if (a >= -40.0)
            worst = std::max (worst, std::abs (dig.magnitudeDb (f, fs) - a));
    }
    return worst;
}
} // namespace

TEST_CASE ("prototype: bell centre gain and symmetry", "[design]")
{
    for (auto slope : { Slope::S12, Slope::S24, Slope::S48, Slope::S96 })
        for (double gain : { -18.0, -6.0, 6.0, 18.0 })
        {
            BandParams p { true, true, Shape::Bell, slope, Placement::Stereo, 1000.0, gain, 1.4 };
            BandParams inv = p;
            inv.gainDb = -gain;
            CHECK_THAT (makePrototype (p).magnitudeDb (1000.0), WithinAbs (gain, 1.0e-6));
            for (double f : { 30.0, 300.0, 900.0, 1100.0, 5000.0 })
                CHECK_THAT (makePrototype (p).magnitudeDb (f) + makePrototype (inv).magnitudeDb (f), WithinAbs (0.0, 1.0e-6));
        }
}

TEST_CASE ("prototype: shelves reach their gain and are half-way at the corner", "[design]")
{
    for (auto slope : { Slope::S6, Slope::S12, Slope::S18, Slope::S24, Slope::S48 })
        for (double gain : { -12.0, 9.0 })
        {
            BandParams hi { true, true, Shape::HighShelf, slope, Placement::Stereo, 1000.0, gain, 1.0 };
            BandParams lo = hi;
            lo.shape = Shape::LowShelf;
            CHECK_THAT (makePrototype (hi).magnitudeDb (1.0e6), WithinAbs (gain, 1.0e-3));
            CHECK_THAT (makePrototype (hi).magnitudeDb (0.01), WithinAbs (0.0, 1.0e-3));
            CHECK_THAT (makePrototype (hi).magnitudeDb (1000.0), WithinAbs (0.5 * gain, 1.0e-6));
            CHECK_THAT (makePrototype (lo).magnitudeDb (0.01), WithinAbs (gain, 1.0e-3));
            CHECK_THAT (makePrototype (lo).magnitudeDb (1000.0), WithinAbs (0.5 * gain, 1.0e-6));

            BandParams tilt = hi;
            tilt.shape = Shape::TiltShelf;
            CHECK_THAT (makePrototype (tilt).magnitudeDb (1.0e6), WithinAbs (0.5 * gain, 1.0e-3));
            CHECK_THAT (makePrototype (tilt).magnitudeDb (0.01), WithinAbs (-0.5 * gain, 1.0e-3));
        }
}

TEST_CASE ("prototype: cuts are Butterworth at q = 0.7071", "[design]")
{
    for (auto slope : { Slope::S6, Slope::S12, Slope::S18, Slope::S24, Slope::S36, Slope::S48, Slope::S72, Slope::S96 })
    {
        const double q = 0.70710678118654752;
        BandParams lc { true, true, Shape::LowCut, slope, Placement::Stereo, 1000.0, 0.0, q };
        BandParams hc = lc;
        hc.shape = Shape::HighCut;
        CHECK_THAT (makePrototype (lc).magnitudeDb (1000.0), WithinAbs (-3.0103, 1.0e-3));
        CHECK_THAT (makePrototype (hc).magnitudeDb (1000.0), WithinAbs (-3.0103, 1.0e-3));

        // Nominal slope between 2 and 3 octaves into the stop band.
        const double expected = slopeDb (slope);
        CHECK_THAT (makePrototype (hc).magnitudeDb (4000.0) - makePrototype (hc).magnitudeDb (8000.0), WithinAbs (expected, 0.5));
        CHECK_THAT (makePrototype (lc).magnitudeDb (250.0) - makePrototype (lc).magnitudeDb (125.0), WithinAbs (expected, 0.5));
    }
}

TEST_CASE ("prototype: notch, band pass, flat tilt, brickwall", "[design]")
{
    for (auto slope : { Slope::S12, Slope::S24, Slope::S48 })
    {
        BandParams n { true, true, Shape::Notch, slope, Placement::Stereo, 1000.0, 0.0, 2.0 };
        CHECK (makePrototype (n).magnitudeDb (1000.0) < -200.0);
        CHECK_THAT (makePrototype (n).magnitudeDb (20.0), WithinAbs (0.0, 0.01));
        CHECK_THAT (makePrototype (n).magnitudeDb (20000.0), WithinAbs (0.0, 0.05));

        BandParams bp = n;
        bp.shape = Shape::BandPass;
        CHECK_THAT (makePrototype (bp).magnitudeDb (1000.0), WithinAbs (0.0, 1.0e-6));
        CHECK (makePrototype (bp).magnitudeDb (100.0) < -20.0);
    }

    BandParams tilt { true, true, Shape::FlatTilt, Slope::S12, Placement::Stereo, 1000.0, 12.0, 1.0 };
    const auto proto = makePrototype (tilt);
    for (double f = 20.0; f <= 20000.0; f *= 1.3)
        CHECK_THAT (proto.magnitudeDb (f), WithinAbs (2.0 * std::log2 (f / 1000.0), 0.25));

    BandParams bw { true, true, Shape::HighCut, Slope::Brickwall, Placement::Stereo, 1000.0, 0.0, 0.7071 };
    CHECK (std::abs (makePrototype (bw).magnitudeDb (500.0)) < 0.06);
    CHECK (makePrototype (bw).magnitudeDb (1200.0) < -99.0);
    bw.shape = Shape::LowCut;
    CHECK (std::abs (makePrototype (bw).magnitudeDb (2000.0)) < 0.06);
    CHECK (makePrototype (bw).magnitudeDb (800.0) < -99.0);
}

TEST_CASE ("matched design follows the analog prototype", "[design]")
{
    // Regression guard at the accuracy measured for milestone M1. The spec target of
    // +-0.1 dB is not met yet: see research item R-6 in docs/spec/03-dsp.md.
    //   typical: q 0.7..4, 9 dB, band frequency below fs/10  -> per-shape limit below
    //   any:     every setting up to 0.46 fs                 -> never grossly wrong
    const double typicalLimit[kNumShapes] = { 0.4, 0.45, 0.45, 0.5, 1.5, 1.0, 6.0, 0.45, 0.5, 0.01 };
    const double anyLimit[kNumShapes] = { 6.0, 12.0, 12.0, 7.0, 18.0, 18.0, 18.0, 12.0, 1.0, 0.01 };
    double typical[kNumShapes] {}, any[kNumShapes] {};

    for (int shape = 0; shape < kNumShapes; ++shape)
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (auto slope : { Slope::S6, Slope::S12, Slope::S24, Slope::S48, Slope::S96, Slope::Brickwall })
                for (double ratio = 0.004; ratio < 0.46; ratio += 0.019)
                    for (double q : { 0.3, 0.7071, 1.0, 2.0, 4.0, 12.0 })
                        for (double gain : { -18.0, -9.0, 9.0, 18.0 })
                        {
                            BandParams p { true, true, static_cast<Shape> (shape), slope, Placement::Stereo, ratio * fs, gain, q };
                            if (slope == Slope::Brickwall && ! isCut (p.shape))
                                continue;
                            const double e = worstError (p, fs);
                            any[shape] = std::max (any[shape], e);
                            if (ratio < 0.1 && q >= 0.7 && q <= 4.0 && std::abs (gain) < 12.0)
                                typical[shape] = std::max (typical[shape], e);
                        }

    for (int shape = 0; shape < kNumShapes; ++shape)
    {
        std::printf ("  %-10s worst error: %6.3f dB typical, %6.3f dB over every setting\n",
                     shapeName (static_cast<Shape> (shape)), typical[shape], any[shape]);
        CHECK (typical[shape] < typicalLimit[shape]);
        CHECK (any[shape] < anyLimit[shape]);
    }
}

TEST_CASE ("matched design: a boost followed by the same cut is flat", "[design]")
{
    for (auto shape : { Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::TiltShelf, Shape::FlatTilt })
        for (auto slope : { Slope::S6, Slope::S12, Slope::S24, Slope::S96 })
            for (double fs : { 44100.0, 96000.0 })
                for (double f0 : { 40.0, 1000.0, 9000.0, 18000.0 })
                    for (double q : { 0.3, 1.0, 8.0 })
                    {
                        BandParams up { true, true, shape, slope, Placement::Stereo, f0, 14.0, q };
                        BandParams down = up;
                        down.gainDb = -14.0;
                        const auto a = designBand (up, fs), b = designBand (down, fs);
                        for (double f = 20.0; f < 0.49 * fs; f *= 1.7)
                            REQUIRE_THAT (a.magnitudeDb (f, fs) + b.magnitudeDb (f, fs), WithinAbs (0.0, 1.0e-4));
                    }
}

TEST_CASE ("matched design: digital notch is a true null, digital designs are stable", "[design]")
{
    for (double fs : { 44100.0, 96000.0 })
        for (double f0 : { 50.0, 1000.0, 12000.0 })
        {
            BandParams n { true, true, Shape::Notch, Slope::S12, Placement::Stereo, f0, 0.0, 4.0 };
            CHECK (designBand (makePrototype (n), fs).magnitudeDb (f0, fs) < -90.0);
        }

    for (int shape = 0; shape < kNumShapes; ++shape)
        for (double f0 : { 10.0, 1000.0, 21000.0, 30000.0 })
            for (double q : { 0.025, 1.0, 40.0 })
            {
                BandParams p { true, true, static_cast<Shape> (shape), Slope::S48, Placement::Stereo, f0, 30.0, q };
                const auto d = designBand (makePrototype (p), 44100.0);
                for (int i = 0; i < d.count; ++i)
                {
                    const auto& b = d.biquads[static_cast<size_t> (i)];
                    CHECK (std::isfinite (b.b0 + b.b1 + b.b2 + b.a1 + b.a2));
                    CHECK (std::abs (b.a2) < 1.0);
                    CHECK (std::abs (b.a1) < 1.0 + b.a2);
                    const auto c = toSvf (b);
                    CHECK ((c.g > 0.0 && c.k > 0.0 && std::isfinite (c.m0 + c.m1 + c.m2)));
                }
            }
}
