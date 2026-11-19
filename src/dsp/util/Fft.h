#pragma once

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace of
{
/** In-place radix-2 complex FFT in double precision. One instance serves every power-of-two
    size up to the one it was prepared for. transform() does not allocate. */
class Fft
{
public:
    using Complex = std::complex<double>;

    void prepare (int maxSize)
    {
        size = 1;
        while (size < maxSize)
            size <<= 1;
        twiddles.resize (static_cast<size_t> (size / 2));
        for (int k = 0; k < size / 2; ++k)
            twiddles[static_cast<size_t> (k)] = std::polar (1.0, -2.0 * std::numbers::pi * k / size);
    }

    int maxSize() const noexcept { return size; }

    /** n must be a power of two no larger than maxSize(). The inverse is scaled by 1/n. */
    void transform (Complex* data, int n, bool inverse) const noexcept
    {
        // Bit reversal.
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (data[i], data[j]);
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, stride = size / len;
            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = twiddles[static_cast<size_t> (k * stride)];
                    if (inverse)
                        w = std::conj (w);
                    const Complex u = data[start + k], v = data[start + k + half] * w;
                    data[start + k] = u + v;
                    data[start + k + half] = u - v;
                }
        }

        if (inverse)
        {
            const double scale = 1.0 / n;
            for (int i = 0; i < n; ++i)
                data[i] *= scale;
        }
    }

private:
    int size = 0;
    std::vector<Complex> twiddles;
};
} // namespace of
