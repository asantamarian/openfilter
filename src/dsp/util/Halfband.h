#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace of
{
/** Linear-phase half-band FIR for 2x up- and down-sampling (Kaiser window, about 100 dB
    stop band). A round trip up then down delays the signal by exactly halfLength() samples
    at the lower rate. Real-time safe after design(). */
class Halfband
{
public:
    static constexpr int kMaxHalf = 71;

    /** half must be odd. The filter has 2 * half + 1 taps; a larger half gives a narrower
        transition band. 69 keeps 20 kHz flat at 44.1 kHz; 13 is enough once the signal is
        already at twice the audio rate or more. */
    void design (int half) noexcept
    {
        m = half;
        const double beta = 10.06;   // 0.1102 * (100 dB - 8.7)
        double sum = 0.0;
        for (int i = 0; i <= m; ++i)
        {
            const int k = 2 * i - m;   // odd tap index, -m..m
            const double x = static_cast<double> (k) / static_cast<double> (m);
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - x * x))) / bessel0 (beta);
            taps[static_cast<size_t> (i)] = std::sin (0.5 * std::numbers::pi * k) / (std::numbers::pi * k) * w;
            sum += taps[static_cast<size_t> (i)];
        }
        for (int i = 0; i <= m; ++i)   // odd taps of a half-band filter sum to exactly 1/2
            taps[static_cast<size_t> (i)] *= 0.5 / sum;
        reset();
    }

    void reset() noexcept
    {
        upHist.fill (0.0);
        evenHist.fill (0.0);
        oddHist.fill (0.0);
        upPos = downPos = 0;
    }

    int halfLength() const noexcept { return m; }

    /** n input samples to 2n output samples. in and out must not overlap. */
    void up (const double* in, int n, double* out) noexcept
    {
        const int centre = (m - 1) / 2;
        for (int j = 0; j < n; ++j)
        {
            upPos = (upPos + 1) & kMask;
            upHist[static_cast<size_t> (upPos)] = in[j];
            double acc = 0.0;
            for (int i = 0; i <= m; ++i)
                acc += taps[static_cast<size_t> (i)] * upHist[static_cast<size_t> ((upPos - i) & kMask)];
            out[2 * j] = 2.0 * acc;
            out[2 * j + 1] = upHist[static_cast<size_t> ((upPos - centre) & kMask)];
        }
    }

    /** 2n input samples to n output samples. out may be the start of in. */
    void down (const double* in, int n, double* out) noexcept
    {
        const int centre = (m + 1) / 2;
        for (int j = 0; j < n; ++j)
        {
            downPos = (downPos + 1) & kMask;
            evenHist[static_cast<size_t> (downPos)] = in[2 * j];
            oddHist[static_cast<size_t> (downPos)] = in[2 * j + 1];
            double acc = 0.5 * oddHist[static_cast<size_t> ((downPos - centre) & kMask)];
            for (int i = 0; i <= m; ++i)
                acc += taps[static_cast<size_t> (i)] * evenHist[static_cast<size_t> ((downPos - i) & kMask)];
            out[j] = acc;
        }
    }

private:
    static double bessel0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (0.5 * x / k) * (0.5 * x / k);
            sum += term;
        }
        return sum;
    }

    static constexpr int kMask = 127;
    std::array<double, kMaxHalf + 1> taps {};
    std::array<double, kMask + 1> upHist {}, evenHist {}, oddHist {};
    int m = 13, upPos = 0, downPos = 0;
};
} // namespace of
