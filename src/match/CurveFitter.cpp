#include "CurveFitter.h"

#include "dsp/design/Prototype.h"

#include <algorithm>
#include <cmath>

namespace of::fit
{
namespace
{
const double kOctaves = std::log2 (1000.0);   // 20 Hz to 20 kHz
const double kPointsPerOctave = (kGridSize - 1) / kOctaves;

Curve responseOf (const BandParams& band)
{
    Curve c;
    const auto proto = makePrototype (band);
    for (int i = 0; i < kGridSize; ++i)
        c[static_cast<size_t> (i)] = proto.magnitudeDb (gridFrequency (i));
    return c;
}

BandParams makeBand (Shape shape, double freq, double gain, double q)
{
    BandParams b;
    b.used = true;
    b.shape = shape;
    b.slope = Slope::S12;
    b.freq = std::clamp (freq, 20.0, 20000.0);
    b.gainDb = std::clamp (gain, -24.0, 24.0);
    b.q = std::clamp (q, 0.3, 8.0);
    return b;
}

double mean (const Curve& c, double fromHz, double toHz)
{
    double sum = 0.0;
    int n = 0;
    for (int i = 0; i < kGridSize; ++i)
        if (gridFrequency (i) >= fromHz && gridFrequency (i) <= toHz)
        {
            sum += c[static_cast<size_t> (i)];
            ++n;
        }
    return n > 0 ? sum / n : 0.0;
}

double sumSquares (const Curve& target, const Curve& model)
{
    double s = 0.0;
    for (int i = 0; i < kGridSize; ++i)
    {
        const double e = target[static_cast<size_t> (i)] - model[static_cast<size_t> (i)];
        s += e * e;
    }
    return s;
}

/** Solves A x = b for a small symmetric positive system in place. Returns false if singular. */
bool solve (std::vector<double>& a, std::vector<double>& b, int n)
{
    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < n; ++r)
            if (std::abs (a[static_cast<size_t> (r * n + col)]) > std::abs (a[static_cast<size_t> (pivot * n + col)]))
                pivot = r;
        if (std::abs (a[static_cast<size_t> (pivot * n + col)]) < 1.0e-12)
            return false;
        if (pivot != col)
        {
            for (int c = 0; c < n; ++c)
                std::swap (a[static_cast<size_t> (col * n + c)], a[static_cast<size_t> (pivot * n + c)]);
            std::swap (b[static_cast<size_t> (col)], b[static_cast<size_t> (pivot)]);
        }
        for (int r = col + 1; r < n; ++r)
        {
            const double f = a[static_cast<size_t> (r * n + col)] / a[static_cast<size_t> (col * n + col)];
            for (int c = col; c < n; ++c)
                a[static_cast<size_t> (r * n + c)] -= f * a[static_cast<size_t> (col * n + c)];
            b[static_cast<size_t> (r)] -= f * b[static_cast<size_t> (col)];
        }
    }
    for (int r = n - 1; r >= 0; --r)
    {
        double s = b[static_cast<size_t> (r)];
        for (int c = r + 1; c < n; ++c)
            s -= a[static_cast<size_t> (r * n + c)] * b[static_cast<size_t> (c)];
        b[static_cast<size_t> (r)] = s / a[static_cast<size_t> (r * n + r)];
    }
    return true;
}

/** Joint refinement of every band's frequency, gain and Q: Levenberg-Marquardt on the dB
    error over the grid, with a numeric Jacobian (each column only needs one band's response). */
void refine (std::vector<BandParams>& bands, const Curve& target, int iterations)
{
    const int k = static_cast<int> (bands.size());
    if (k == 0)
        return;
    const int n = 3 * k;
    std::vector<Curve> responses (static_cast<size_t> (k));
    Curve model {};
    const auto rebuild = [&]
    {
        model.fill (0.0);
        for (int b = 0; b < k; ++b)
        {
            responses[static_cast<size_t> (b)] = responseOf (bands[static_cast<size_t> (b)]);
            for (int i = 0; i < kGridSize; ++i)
                model[static_cast<size_t> (i)] += responses[static_cast<size_t> (b)][static_cast<size_t> (i)];
        }
    };
    rebuild();
    double error = sumSquares (target, model), lambda = 1.0e-2;

    std::vector<Curve> jac (static_cast<size_t> (n));
    std::vector<double> jtj (static_cast<size_t> (n * n)), rhs (static_cast<size_t> (n));

    for (int it = 0; it < iterations; ++it)
    {
        // Jacobian by forward differences in (log frequency, gain, log Q).
        for (int b = 0; b < k; ++b)
        {
            const auto& base = bands[static_cast<size_t> (b)];
            const double steps[3] = { 0.01, 0.05, 0.02 };
            for (int p = 0; p < 3; ++p)
            {
                BandParams moved = base;
                if (p == 0) moved.freq = base.freq * std::exp (steps[0]);
                if (p == 1) moved.gainDb = base.gainDb + steps[1];
                if (p == 2) moved.q = base.q * std::exp (steps[2]);
                const auto r = responseOf (moved);
                auto& column = jac[static_cast<size_t> (3 * b + p)];
                for (int i = 0; i < kGridSize; ++i)
                    column[static_cast<size_t> (i)] = (r[static_cast<size_t> (i)] - responses[static_cast<size_t> (b)][static_cast<size_t> (i)]) / steps[p];
            }
        }

        bool improved = false;
        for (int attempt = 0; attempt < 6 && ! improved; ++attempt)
        {
            for (int r = 0; r < n; ++r)
            {
                double g = 0.0;
                for (int i = 0; i < kGridSize; ++i)
                    g += jac[static_cast<size_t> (r)][static_cast<size_t> (i)] * (target[static_cast<size_t> (i)] - model[static_cast<size_t> (i)]);
                rhs[static_cast<size_t> (r)] = g;
                for (int c = 0; c <= r; ++c)
                {
                    double s = 0.0;
                    for (int i = 0; i < kGridSize; ++i)
                        s += jac[static_cast<size_t> (r)][static_cast<size_t> (i)] * jac[static_cast<size_t> (c)][static_cast<size_t> (i)];
                    jtj[static_cast<size_t> (r * n + c)] = jtj[static_cast<size_t> (c * n + r)] = s;
                }
            }
            for (int d = 0; d < n; ++d)
                jtj[static_cast<size_t> (d * n + d)] *= 1.0 + lambda;

            auto step = rhs;
            auto system = jtj;
            if (! solve (system, step, n))
            {
                lambda *= 10.0;
                continue;
            }

            const auto saved = bands;
            for (int b = 0; b < k; ++b)
            {
                auto& band = bands[static_cast<size_t> (b)];
                band.freq = std::clamp (band.freq * std::exp (std::clamp (step[static_cast<size_t> (3 * b)], -0.5, 0.5)), 20.0, 20000.0);
                if (hasGain (band.shape))
                    band.gainDb = std::clamp (band.gainDb + std::clamp (step[static_cast<size_t> (3 * b + 1)], -6.0, 6.0), -24.0, 24.0);
                band.q = std::clamp (band.q * std::exp (std::clamp (step[static_cast<size_t> (3 * b + 2)], -0.5, 0.5)), 0.3, 8.0);
            }
            const auto savedResponses = responses;
            const auto savedModel = model;
            rebuild();
            const double tried = sumSquares (target, model);
            if (tried < error)
            {
                improved = true;
                lambda = std::max (lambda * 0.3, 1.0e-6);
                const bool done = error - tried < 1.0e-7 * error;
                error = tried;
                if (done)
                    return;
            }
            else
            {
                bands = saved;
                responses = savedResponses;
                model = savedModel;
                lambda *= 10.0;
            }
        }
        if (! improved)
            return;
    }
}
} // namespace

double gridFrequency (int i) noexcept { return 20.0 * std::pow (1000.0, static_cast<double> (i) / (kGridSize - 1)); }

Curve evaluate (const std::vector<BandParams>& bands)
{
    Curve c {};
    for (const auto& b : bands)
    {
        const auto r = responseOf (b);
        for (int i = 0; i < kGridSize; ++i)
            c[static_cast<size_t> (i)] += r[static_cast<size_t> (i)];
    }
    return c;
}

double rmsError (const Curve& a, const Curve& b) noexcept { return std::sqrt (sumSquares (a, b) / kGridSize); }

Curve smooth (const Curve& curve, double octaves)
{
    const int half = std::max (0, static_cast<int> (std::lround (0.5 * octaves * kPointsPerOctave)));
    Curve out {};
    for (int i = 0; i < kGridSize; ++i)
    {
        double sum = 0.0;
        int n = 0;
        for (int j = std::max (0, i - half); j <= std::min (kGridSize - 1, i + half); ++j, ++n)
            sum += curve[static_cast<size_t> (j)];
        out[static_cast<size_t> (i)] = sum / n;
    }
    return out;
}

Curve matchTarget (const Curve& source, const Curve& reference)
{
    const auto s = smooth (source, 1.0 / 3.0), r = smooth (reference, 1.0 / 3.0);
    Curve d {};
    for (int i = 0; i < kGridSize; ++i)
        d[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] - s[static_cast<size_t> (i)];
    const double level = mean (d, 200.0, 5000.0);
    for (auto& v : d)
        v = std::clamp (v - level, -18.0, 18.0);
    return d;
}

std::vector<BandParams> fitCurve (const Curve& target, int maxBands)
{
    maxBands = std::clamp (maxBands, 1, 24);
    std::vector<BandParams> bands;
    Curve residual = target;
    const auto subtract = [&residual] (const BandParams& b)
    {
        const auto r = responseOf (b);
        for (int i = 0; i < kGridSize; ++i)
            residual[static_cast<size_t> (i)] -= r[static_cast<size_t> (i)];
    };

    // Ends: where the bottom or top of the target sits clearly off the middle, a shelf does
    // in one band what bells would need several for.
    const double middle = mean (target, 200.0, 5000.0);
    const double low = mean (target, 20.0, 80.0) - middle, high = mean (target, 8000.0, 20000.0) - middle;
    if (std::abs (low) > 2.0 && static_cast<int> (bands.size()) < maxBands)
    {
        bands.push_back (makeBand (Shape::LowShelf, 150.0, low, 0.9));
        subtract (bands.back());
    }
    if (std::abs (high) > 2.0 && static_cast<int> (bands.size()) < maxBands)
    {
        bands.push_back (makeBand (Shape::HighShelf, 5000.0, high, 0.9));
        subtract (bands.back());
    }

    // Greedy seeding: a bell on the largest remaining error, as wide as that bump is.
    while (static_cast<int> (bands.size()) < maxBands)
    {
        const auto lightly = smooth (residual, 1.0 / 6.0);
        int peak = 0;
        for (int i = 1; i < kGridSize; ++i)
            if (std::abs (lightly[static_cast<size_t> (i)]) > std::abs (lightly[static_cast<size_t> (peak)]))
                peak = i;
        const double gain = lightly[static_cast<size_t> (peak)];
        if (std::abs (gain) < 0.3)
            break;

        int lo = peak, hi = peak;   // half-gain points either side
        while (lo > 0 && lightly[static_cast<size_t> (lo)] * gain > 0.0 && std::abs (lightly[static_cast<size_t> (lo)]) > 0.5 * std::abs (gain)) --lo;
        while (hi < kGridSize - 1 && lightly[static_cast<size_t> (hi)] * gain > 0.0 && std::abs (lightly[static_cast<size_t> (hi)]) > 0.5 * std::abs (gain)) ++hi;
        const double widthOct = std::max (0.1, (hi - lo) / kPointsPerOctave);
        const double ratio = std::pow (2.0, widthOct);
        bands.push_back (makeBand (Shape::Bell, gridFrequency (peak), gain, std::sqrt (ratio) / (ratio - 1.0)));
        subtract (bands.back());

        // A short joint refinement after each new band keeps later seeds honest.
        refine (bands, target, 4);
        residual = target;
        for (const auto& b : bands)
            subtract (b);
    }

    refine (bands, target, 30);

    // Bands that ended up doing next to nothing are dropped, and the rest refitted once.
    const auto before = bands.size();
    bands.erase (std::remove_if (bands.begin(), bands.end(), [] (const BandParams& b) { return std::abs (b.gainDb) < 0.3; }), bands.end());
    if (bands.size() != before)
        refine (bands, target, 15);

    std::sort (bands.begin(), bands.end(), [] (const BandParams& a, const BandParams& b) { return a.freq < b.freq; });
    return bands;
}
} // namespace of::fit
