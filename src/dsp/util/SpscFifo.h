#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace of
{
/** Lock-free single-producer single-consumer ring of floats. The audio thread pushes, one
    other thread pops. A push that does not fit is dropped whole, never blocks. */
class SpscFifo
{
public:
    explicit SpscFifo (std::size_t capacityPow2 = 65536) : data (capacityPow2), mask (capacityPow2 - 1) {}

    /** Producer side. Returns false if the block was dropped. */
    bool push (const float* src, std::size_t n) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);
        if (n > data.size() - (w - r))
            return false;
        for (std::size_t i = 0; i < n; ++i)
            data[(w + i) & mask] = src[i];
        writePos.store (w + n, std::memory_order_release);
        return true;
    }

    /** Consumer side. Returns the number of samples copied. */
    std::size_t pop (float* dst, std::size_t maxCount) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_acquire);
        const auto n = std::min (maxCount, w - r);
        for (std::size_t i = 0; i < n; ++i)
            dst[i] = data[(r + i) & mask];
        readPos.store (r + n, std::memory_order_release);
        return n;
    }

    /** Consumer side: discard everything queued. */
    void clear() noexcept { readPos.store (writePos.load (std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<float> data;
    std::size_t mask;
    std::atomic<std::size_t> writePos { 0 }, readPos { 0 };
};
} // namespace of
