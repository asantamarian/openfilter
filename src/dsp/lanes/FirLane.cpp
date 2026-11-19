#include "FirLane.h"

#include "../design/Prototype.h"

#include <algorithm>
#include <cmath>

namespace of
{
namespace
{
int rateFactor (double fs) noexcept { return fs > 140000.0 ? 4 : fs > 70000.0 ? 2 : 1; }

double bessel0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (0.5 * x / k) * (0.5 * x / k);
        sum += term;
    }
    return sum;
}
} // namespace

int FirLane::tapsFor (int quality, double sampleRate) noexcept
{
    return (4096 << std::clamp (quality, 0, 4)) * rateFactor (sampleRate);
}

int FirLane::partitionFor (double sampleRate) noexcept { return 512 * rateFactor (sampleRate); }

void FirLane::prepare (double sampleRate)
{
    fs = sampleRate;
    partition = partitionFor (sampleRate);
    bins = partition + 1;
    const int maxTaps = tapsFor (4, sampleRate);
    maxPartitions = maxTaps / partition;

    blockFft.prepare (2 * partition);
    designFft.prepare (2 * maxTaps);

    dynTaps = dynamicTapsFor (sampleRate);
    dynParts = dynTaps / partition;
    for (auto& slot : slots)
    {
        for (auto& s : slot.spectra)
            s.assign (static_cast<size_t> (maxPartitions) * static_cast<size_t> (bins), Complex {});
        for (auto& d : slot.dynSpectra)
            d.assign (static_cast<size_t> (dynParts) * static_cast<size_t> (bins), Complex {});
        slot.dynCount = 0;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        prevStatic[ch].assign (static_cast<size_t> (partition), 0.0);
        staticDelay[ch].assign (static_cast<size_t> (dynTaps / 2 + partition), 0.0);
        history2[ch].assign (static_cast<size_t> (dynParts) * static_cast<size_t> (bins), Complex {});
    }
    for (auto* v : { &accC, &accD, &convL, &convR })
        v->assign (static_cast<size_t> (2 * partition), Complex {});
    for (int ch = 0; ch < 2; ++ch)
    {
        inBlock[ch].assign (static_cast<size_t> (partition), 0.0);
        prevBlock[ch].assign (static_cast<size_t> (partition), 0.0);
        outBlock[ch].assign (static_cast<size_t> (partition), 0.0);
        history[ch].assign (static_cast<size_t> (maxPartitions) * static_cast<size_t> (bins), Complex {});
    }
    frame.assign (static_cast<size_t> (2 * partition), Complex {});
    accA.assign (static_cast<size_t> (2 * partition), Complex {});
    accB.assign (static_cast<size_t> (2 * partition), Complex {});
    grid.assign (static_cast<size_t> (2 * maxTaps), Complex {});
    window.assign (static_cast<size_t> (maxTaps), 0.0);
    windowTaps = 0;
    reset (1);
}

void FirLane::reset (int quality, bool withDynamics) noexcept
{
    taps = tapsFor (quality, fs);
    partitions = taps / partition;
    unity = true;   // a design for this quality replaces it when the worker delivers one
    dynStage = withDynamics;
    head2 = delayPos = 0;
    amountCount = 0;
    for (int j = 0; j < kMaxDynamic; ++j)
        amountNow[j] = amountPrev[j] = amountTarget[j] = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (prevStatic[ch].begin(), prevStatic[ch].end(), 0.0);
        std::fill (staticDelay[ch].begin(), staticDelay[ch].end(), 0.0);
        std::fill (history2[ch].begin(), history2[ch].end(), Complex {});
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (inBlock[ch].begin(), inBlock[ch].end(), 0.0);
        std::fill (prevBlock[ch].begin(), prevBlock[ch].end(), 0.0);
        std::fill (outBlock[ch].begin(), outBlock[ch].end(), 0.0);
        std::fill (history[ch].begin(), history[ch].end(), Complex {});
    }
    blockPos = head = 0;
}

void FirLane::process (double* left, double* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto p = static_cast<size_t> (blockPos);
        const double l = left[i], r = right != nullptr ? right[i] : left[i];
        left[i] = outBlock[0][p];
        if (right != nullptr)
            right[i] = outBlock[1][p];
        inBlock[0][p] = l;
        inBlock[1][p] = r;
        if (++blockPos == partition)
        {
            blockPos = 0;
            processBlock();
        }
    }
}

void FirLane::setDynamicAmounts (const double* amounts, int count) noexcept
{
    amountCount = std::min (count, static_cast<int> (kMaxDynamic));
    for (int j = 0; j < amountCount; ++j)
        amountTarget[j] = std::clamp (amounts[j], 0.0, 1.0);
}

void FirLane::processDynamics (const Slot* slot) noexcept
{
    // The block just produced by the first stage is in outBlock. Its spectrum joins a short
    // delay line; the block itself is delayed by half a dynamic kernel to meet the result.
    head2 = (head2 + 1) % dynParts;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < partition; ++i)
        {
            frame[static_cast<size_t> (i)] = prevStatic[ch][static_cast<size_t> (i)];
            frame[static_cast<size_t> (partition + i)] = outBlock[ch][static_cast<size_t> (i)];
        }
        blockFft.transform (frame.data(), 2 * partition, false);
        std::copy_n (frame.begin(), bins, history2[ch].begin() + static_cast<std::ptrdiff_t> (head2) * bins);
        std::copy (outBlock[ch].begin(), outBlock[ch].end(), prevStatic[ch].begin());
    }

    // Amounts move once per block; the block is rendered with the old and the new amounts
    // and crossfaded, so the gain glides instead of stepping.
    const int count = slot != nullptr ? slot->dynCount : 0;
    const bool usable = count > 0 && count == amountCount;
    bool anything = false;
    for (int j = 0; j < kMaxDynamic; ++j)
    {
        amountPrev[j] = amountNow[j];
        amountNow[j] = usable && j < count ? amountTarget[j] : 0.0;
        anything = anything || amountPrev[j] != 0.0 || amountNow[j] != 0.0;
    }

    const int delay = dynTaps / 2, ringSize = static_cast<int> (staticDelay[0].size());
    if (anything && slot != nullptr)
    {
        // accC, accD: the change to left and right with the old amounts (first half of each
        // array's use) is kept separate from the new by doing two passes.
        for (int pass = 0; pass < 2; ++pass)
        {
            auto& left = pass == 0 ? accC : convL;    // reuse: accC/accD = old, convL/convR = new
            auto& right = pass == 0 ? accD : convR;
            std::fill (left.begin(), left.begin() + bins, Complex {});
            std::fill (right.begin(), right.begin() + bins, Complex {});
            for (int j = 0; j < count; ++j)
            {
                const double amount = pass == 0 ? amountPrev[j] : amountNow[j];
                if (amount == 0.0)
                    continue;
                const auto place = slot->dynPlace[j];
                const Complex* kernel = slot->dynSpectra[j].data();
                for (int k = 0; k < dynParts; ++k)
                {
                    const auto age = static_cast<size_t> ((head2 - k + dynParts) % dynParts) * static_cast<size_t> (bins);
                    const Complex* xl = history2[0].data() + age;
                    const Complex* xr = history2[1].data() + age;
                    const Complex* h = kernel + static_cast<size_t> (k) * static_cast<size_t> (bins);
                    for (int b = 0; b < bins; ++b)
                    {
                        const auto i = static_cast<size_t> (b);
                        const Complex hl = h[b] * xl[b] * amount, hr = h[b] * xr[b] * amount;
                        switch (place)
                        {
                            case Placement::Stereo: left[i] += hl;  right[i] += hr;  break;
                            case Placement::Left:   left[i] += hl;  break;
                            case Placement::Right:  right[i] += hr; break;
                            case Placement::Mid:    { const Complex m = 0.5 * (hl + hr); left[i] += m;  right[i] += m; break; }
                            case Placement::Side:   { const Complex d = 0.5 * (hl - hr); left[i] += d;  right[i] -= d; break; }
                        }
                    }
                }
            }
            for (auto* acc : { &left, &right })
            {
                for (int b = 1; b < partition; ++b)
                    (*acc)[static_cast<size_t> (2 * partition - b)] = std::conj ((*acc)[static_cast<size_t> (b)]);
                blockFft.transform (acc->data(), 2 * partition, true);
            }
        }
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& oldChange = ch == 0 ? accC : accD;
        const auto& newChange = ch == 0 ? convL : convR;
        for (int i = 0; i < partition; ++i)
        {
            const int w = (delayPos + i) % ringSize, r = (delayPos + i - delay + ringSize) % ringSize;
            staticDelay[ch][static_cast<size_t> (w)] = outBlock[ch][static_cast<size_t> (i)];
            double y = staticDelay[ch][static_cast<size_t> (r)];
            if (anything)
            {
                const double t = static_cast<double> (i + 1) / partition;
                y += oldChange[static_cast<size_t> (partition + i)].real() * (1.0 - t) + newChange[static_cast<size_t> (partition + i)].real() * t;
            }
            outBlock[ch][static_cast<size_t> (i)] = y;
        }
    }
    delayPos = (delayPos + partition) % ringSize;
}

void FirLane::accumulate (const Slot* slot, int out, Complex* acc) const noexcept
{
    const auto spectrumAt = [this] (int in, int age)
    {
        return history[in].data() + static_cast<size_t> ((head - age + maxPartitions) % maxPartitions) * static_cast<size_t> (bins);
    };

    if (slot == nullptr)
    {
        // Pure delay of taps/2: the input spectrum from partitions/2 blocks ago, untouched.
        std::copy_n (spectrumAt (out, partitions / 2), bins, acc);
        return;
    }

    // out 0: L' = LL * L + LR * R.   out 1: R' = RL * L + RR * R.
    std::fill (acc, acc + bins, Complex {});
    for (int in = 0; in < 2; ++in)
    {
        if (in != out && ! slot->cross)
            continue;
        const Complex* kernel = slot->spectra[out * 2 + in].data();
        for (int k = 0; k < partitions; ++k)
        {
            const Complex* x = spectrumAt (in, k);
            const Complex* h = kernel + static_cast<size_t> (k) * static_cast<size_t> (bins);
            for (int b = 0; b < bins; ++b)
                acc[b] += x[b] * h[b];
        }
    }
}

void FirLane::processBlock() noexcept
{
    // Overlap-save: transform [previous block | this block] for each input.
    head = (head + 1) % maxPartitions;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < partition; ++i)
        {
            frame[static_cast<size_t> (i)] = prevBlock[ch][static_cast<size_t> (i)];
            frame[static_cast<size_t> (partition + i)] = inBlock[ch][static_cast<size_t> (i)];
        }
        blockFft.transform (frame.data(), 2 * partition, false);
        std::copy_n (frame.begin(), bins, history[ch].begin() + static_cast<std::ptrdiff_t> (head) * bins);
        std::swap (prevBlock[ch], inBlock[ch]);
    }

    const int cur = current.load (std::memory_order_relaxed);
    int next = pending.load (std::memory_order_acquire);
    if (next >= 0 && slots[next].taps != taps)
    {
        // A design made for another quality arrived late: drop it.
        pending.store (-1, std::memory_order_release);
        next = -1;
    }

    const Slot* from = unity ? nullptr : &slots[cur];
    const Slot* to = next < 0 ? nullptr : slots[next].unity ? nullptr : &slots[next];

    const auto render = [this] (const Slot* slot, int out, std::vector<Complex>& acc)
    {
        accumulate (slot, out, acc.data());
        // Rebuild the negative frequencies of a real signal, then back to time.
        for (int b = 1; b < partition; ++b)
            acc[static_cast<size_t> (2 * partition - b)] = std::conj (acc[static_cast<size_t> (b)]);
        blockFft.transform (acc.data(), 2 * partition, true);
    };

    for (int out = 0; out < 2; ++out)
    {
        render (from, out, accA);
        if (next >= 0)
        {
            // A new design arrived: crossfade to it across this block.
            render (to, out, accB);
            for (int i = 0; i < partition; ++i)
            {
                const double t = static_cast<double> (i + 1) / partition;
                outBlock[out][static_cast<size_t> (i)] = accA[static_cast<size_t> (partition + i)].real() * (1.0 - t)
                                                       + accB[static_cast<size_t> (partition + i)].real() * t;
            }
        }
        else
            for (int i = 0; i < partition; ++i)
                outBlock[out][static_cast<size_t> (i)] = accA[static_cast<size_t> (partition + i)].real();
    }

    if (dynStage)   // with the kernel that is in effect from this block on
        processDynamics (next >= 0 ? &slots[next] : &slots[cur]);

    if (next >= 0)
    {
        unity = slots[next].unity;
        current.store (next, std::memory_order_release);   // before clearing pending: see design()
        pending.store (-1, std::memory_order_release);
    }
}

bool FirLane::design (const BandParams* bands, int count, int quality) noexcept
{
    if (pending.load (std::memory_order_acquire) >= 0)
        return false;   // the audio thread has not taken the last one yet
    // The audio thread only reads the slot named by `current` (and, during a crossfade, the
    // pending one), and it updates `current` before it clears `pending`. So with nothing
    // pending, the other slot is ours.
    const int target = 1 - current.load (std::memory_order_acquire);
    auto& slot = slots[target];

    const int n = tapsFor (quality, fs);   // kernel length
    const int parts = n / partition;
    const int gridSize = 2 * n;            // twice oversampled in frequency, then windowed down
    slot.taps = n;
    count = std::min (count, kMaxBands);
    slot.unity = count <= 0;
    slot.dynCount = 0;
    if (slot.unity)
    {
        pending.store (target, std::memory_order_release);
        return true;
    }
    if (windowTaps != n)   // Kaiser window, beta 9
    {
        const double i0 = bessel0 (9.0);
        // Centred on tap n/2, the kernel's own centre, so the kernel stays exactly symmetric
        // and a flat response stays an exact impulse.
        for (int i = 0; i < n; ++i)
        {
            const double x = (i - 0.5 * n) / (0.5 * n);
            window[static_cast<size_t> (i)] = bessel0 (9.0 * std::sqrt (std::max (0.0, 1.0 - x * x))) / i0;
        }
        windowTaps = n;
    }

    // Prototypes once per band.
    Prototype protos[kMaxBands];
    Placement places[kMaxBands];
    bool anyMidSide = false;
    for (int i = 0; i < count; ++i)
    {
        protos[i] = makePrototype (bands[i]);
        places[i] = bands[i].place;
        anyMidSide = anyMidSide || places[i] == Placement::Mid || places[i] == Placement::Side;
    }

    slot.cross = anyMidSide;

    // The four zero-phase responses are real, so one kernel at a time: fill the grid with the
    // response, inverse transform, window the centre and cut it into partition spectra.
    for (int which = 0; which < 4; ++which)
    {
        const int out = which / 2, in = which % 2;
        auto& spectra = slot.spectra[which];
        if (in != out && ! anyMidSide)
        {
            std::fill (spectra.begin(), spectra.begin() + static_cast<std::ptrdiff_t> (parts) * bins, Complex {});
            continue;
        }

        for (int k = 0; k <= gridSize / 2; ++k)
        {
            const double f = fs * k / gridSize;
            double dbIn = 0.0, dbMid = 0.0, dbSide = 0.0;   // Hl or Hr, Hm, Hs in dB
            for (int i = 0; i < count; ++i)
            {
                const auto place = places[i];
                const bool forInput = place == Placement::Stereo || (place == Placement::Left && in == 0) || (place == Placement::Right && in == 1);
                if (forInput)                          dbIn += protos[i].magnitudeDb (f);
                else if (place == Placement::Mid)      dbMid += protos[i].magnitudeDb (f);
                else if (place == Placement::Side)     dbSide += protos[i].magnitudeDb (f);
            }
            const double hIn = std::pow (10.0, std::max (dbIn, -200.0) / 20.0);
            const double hm = std::pow (10.0, std::max (dbMid, -200.0) / 20.0), hs = std::pow (10.0, std::max (dbSide, -200.0) / 20.0);
            const double value = 0.5 * (in == out ? hm + hs : hm - hs) * hIn;
            grid[static_cast<size_t> (k)] = value;
            if (k > 0 && k < gridSize / 2)
                grid[static_cast<size_t> (gridSize - k)] = value;
        }
        designFft.transform (grid.data(), gridSize, true);

        // grid now holds the zero-phase impulse response centred on index 0 (wrapping), so
        // the n taps we keep live in [0, n/2) and [3n/2, 2n). The middle of the grid is not
        // read again and serves as the transform buffer for each partition. (The audio
        // thread's own buffers must not be touched from here.)
        Complex* work = grid.data() + gridSize / 2 - partition;
        for (int p = 0; p < parts; ++p)
        {
            for (int i = 0; i < 2 * partition; ++i)
                work[i] = Complex {};
            for (int i = 0; i < partition; ++i)
            {
                const int tap = p * partition + i;   // 0..n-1, centre at n/2
                const int src = ((tap - n / 2) % gridSize + gridSize) % gridSize;
                work[i] = grid[static_cast<size_t> (src)].real() * window[static_cast<size_t> (tap)];
            }
            blockFft.transform (work, 2 * partition, false);
            std::copy_n (work, bins, spectra.data() + static_cast<size_t> (p) * static_cast<size_t> (bins));
        }
    }

    // Dynamic bands: a short kernel each, K = H(G0 + range) / H(G0) - 1 (zero phase, real).
    const int dn = dynTaps, dGrid = 2 * dn;
    for (int i = 0; i < count && slot.dynCount < kMaxDynamic; ++i)
    {
        if (! (bands[i].dyn.on && std::abs (bands[i].dyn.rangeDb) > 1.0e-6))
            continue;
        BandParams moved = bands[i];
        moved.gainDb = bands[i].gainDb + bands[i].dyn.rangeDb;
        const auto full = makePrototype (moved);
        for (int k = 0; k <= dGrid / 2; ++k)
        {
            const double f = fs * k / dGrid;
            const double value = std::pow (10.0, std::clamp (full.magnitudeDb (f) - protos[i].magnitudeDb (f), -80.0, 80.0) / 20.0) - 1.0;
            grid[static_cast<size_t> (k)] = value;
            if (k > 0 && k < dGrid / 2)
                grid[static_cast<size_t> (dGrid - k)] = value;
        }
        designFft.transform (grid.data(), dGrid, true);

        auto& spectra = slot.dynSpectra[slot.dynCount];
        Complex* work = grid.data() + dGrid / 2 - partition;
        const double i0 = bessel0 (9.0);
        for (int p = 0; p < dynParts; ++p)
        {
            for (int j = 0; j < 2 * partition; ++j)
                work[j] = Complex {};
            for (int j = 0; j < partition; ++j)
            {
                const int tap = p * partition + j;
                const int src = ((tap - dn / 2) % dGrid + dGrid) % dGrid;
                const double x = (tap - 0.5 * dn) / (0.5 * dn);
                work[j] = grid[static_cast<size_t> (src)].real() * (bessel0 (9.0 * std::sqrt (std::max (0.0, 1.0 - x * x))) / i0);
            }
            blockFft.transform (work, 2 * partition, false);
            std::copy_n (work, bins, spectra.data() + static_cast<size_t> (p) * static_cast<size_t> (bins));
        }
        slot.dynPlace[slot.dynCount] = bands[i].place;
        ++slot.dynCount;
    }

    pending.store (target, std::memory_order_release);
    return true;
}
} // namespace of
