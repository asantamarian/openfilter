#include <catch2/catch_test_macros.hpp>

#include "dsp/kernel/ChannelFilter.h"

#include <cmath>
#include <random>
#include <vector>

using namespace of;

TEST_CASE ("kernel: state-variable form equals the direct-form biquad", "[kernel]")
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    double worst = 0.0;

    for (int trial = 0; trial < 10000; ++trial)
    {
        Biquad b;
        if (trial % 4 == 0)   // real poles
        {
            const double p1 = 0.999 * (2.0 * uni (rng) - 1.0) * 0.5 + 0.4995, p2 = 0.98 * uni (rng);
            b.a1 = -(p1 + p2);
            b.a2 = p1 * p2;
        }
        else
        {
            const double r = 0.2 + 0.7995 * uni (rng), th = 0.002 + 3.1 * uni (rng);
            b.a1 = -2.0 * r * std::cos (th);
            b.a2 = r * r;
        }
        b.b0 = 2.0 * uni (rng) - 1.0;
        b.b1 = 2.0 * uni (rng) - 1.0;
        b.b2 = 2.0 * uni (rng) - 1.0;

        const int n = 512;
        std::vector<double> ref (n), svf (n, 0.0);
        svf[0] = 1.0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x = i == 0 ? 1.0 : 0.0;
            const double y = b.b0 * x + b.b1 * x1 + b.b2 * x2 - b.a1 * y1 - b.a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            ref[static_cast<size_t> (i)] = y;
            peak = std::max (peak, std::abs (y));
        }

        ChannelFilter f;
        const auto c = toSvf (b);
        f.setCoeffs (&c, 1, 1);
        f.fadeTo (true, 1);
        f.process (svf.data(), n);

        for (int i = 0; i < n; ++i)
            worst = std::max (worst, std::abs (svf[static_cast<size_t> (i)] - ref[static_cast<size_t> (i)]) / std::max (1.0, peak));
    }
    INFO ("worst relative deviation " << worst);
    CHECK (worst < 1.0e-9);
}

TEST_CASE ("kernel: identity costs nothing and is exact; ramps stay finite", "[kernel]")
{
    ChannelFilter f;
    std::vector<double> x (256), y;
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = std::sin (0.1 * static_cast<double> (i));
    y = x;
    f.process (y.data(), 256);
    CHECK (y == x);
    CHECK_FALSE (f.active());

    BandParams p { true, true, Shape::Bell, Slope::S48, Placement::Stereo, 2000.0, 18.0, 8.0 };
    const auto d = designBand (makePrototype (p), 48000.0);
    std::array<SvfCoeffs, kMaxSections> c;
    for (int i = 0; i < d.count; ++i)
        c[static_cast<size_t> (i)] = toSvf (d.biquads[static_cast<size_t> (i)]);

    f.setCoeffs (c.data(), d.count, 1);
    f.fadeTo (true, 480);
    CHECK (f.active());
    for (int block = 0; block < 40; ++block)
    {
        y = x;
        f.process (y.data(), 256);
        for (double v : y)
            REQUIRE (std::isfinite (v));
    }

    // Fade back to dry: the filter must go idle and become exact again.
    f.fadeTo (false, 480);
    for (int block = 0; block < 4; ++block)
    {
        y = x;
        f.process (y.data(), 256);
    }
    CHECK_FALSE (f.active());
    y = x;
    f.process (y.data(), 256);
    CHECK (y == x);
}
