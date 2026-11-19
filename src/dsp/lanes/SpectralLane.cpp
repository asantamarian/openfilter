#include "SpectralLane.h"

#include "../design/Prototype.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace of
{
void SpectralLane::prepare (double sampleRate)
{
    fs = sampleRate;
    frame = frameFor (sampleRate);
    hop = frame / 4;
    bins = frame / 2 + 1;
    fft.prepare (frame);

    window.resize (static_cast<size_t> (frame));
    double sum = 0.0;
    for (int i = 0; i < frame; ++i)   // square root of a periodic Hann window
    {
        window[static_cast<size_t> (i)] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / frame));
        sum += window[static_cast<size_t> (i)];
    }
    powerNorm = (2.0 / sum) * (2.0 / sum);   // a full-scale sine reads 0 dB

    for (int ch = 0; ch < 2; ++ch)
    {
        inRing[ch].assign (static_cast<size_t> (frame), 0.0);
        outRing[ch].assign (static_cast<size_t> (frame), 0.0);
        spec[ch].assign (static_cast<size_t> (frame), Complex {});
    }
    work.assign (static_cast<size_t> (frame), Complex {});
    for (auto& p : power)   p.assign (static_cast<size_t> (bins), 0.0);
    for (auto& t : total)   t.assign (static_cast<size_t> (bins), 0.0);
    powerDb.assign (static_cast<size_t> (bins), -200.0);
    envelope.assign (static_cast<size_t> (bins), -200.0);
    target.assign (static_cast<size_t> (bins), 0.0);
    smooth.assign (static_cast<size_t> (bins), 0.0);
    prefix.assign (static_cast<size_t> (bins) + 1, 0.0);

    bands.resize (kMaxBands);
    for (auto& b : bands)
    {
        b.weight.assign (static_cast<size_t> (bins), 0.0);
        b.gainDb.assign (static_cast<size_t> (bins), 0.0);
    }
    numBands = 0;
    reset();
}

void SpectralLane::reset() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (inRing[ch].begin(), inRing[ch].end(), 0.0);
        std::fill (outRing[ch].begin(), outRing[ch].end(), 0.0);
    }
    for (auto& b : bands)
    {
        std::fill (b.gainDb.begin(), b.gainDb.end(), 0.0);
        b.slowPower = 1.0e-3;
    }
    for (auto& a : activity)
        a.store (0.0f, std::memory_order_relaxed);
    ringPos = sinceFrame = 0;
}

void SpectralLane::computeWeights (Band& band) noexcept
{
    // The band's own curve, drawn with its range as the gain, scaled to 0..1.
    BandParams shape = band.params;
    shape.gainDb = band.params.dyn.rangeDb;
    const auto proto = makePrototype (shape);
    const double range = std::abs (band.params.dyn.rangeDb);
    for (int k = 0; k < bins; ++k)
    {
        const double f = std::max (1.0, fs * k / frame);
        band.weight[static_cast<size_t> (k)] = range > 1.0e-9 ? std::clamp (std::abs (proto.magnitudeDb (f)) / range, 0.0, 1.0) : 0.0;
    }
}

void SpectralLane::setBands (const BandParams* newBands, const int* slotIndex, int count) noexcept
{
    count = std::min (count, kMaxBands);
    for (int i = 0; i < count; ++i)
    {
        auto& b = bands[static_cast<size_t> (i)];
        const bool sameShape = i < numBands && b.slot == slotIndex[i] && b.params.shape == newBands[i].shape && b.params.slope == newBands[i].slope
                            && b.params.freq == newBands[i].freq && b.params.q == newBands[i].q && b.params.dyn.rangeDb == newBands[i].dyn.rangeDb;
        if (i >= numBands || b.slot != slotIndex[i])
        {
            std::fill (b.gainDb.begin(), b.gainDb.end(), 0.0);   // a different band took this place
            b.slowPower = 1.0e-3;
        }
        b.params = newBands[i];
        b.slot = slotIndex[i];
        if (! sameShape)
            computeWeights (b);
    }
    for (int i = count; i < numBands; ++i)
        activity[static_cast<size_t> (bands[static_cast<size_t> (i)].slot)].store (0.0f, std::memory_order_relaxed);
    numBands = count;
}

void SpectralLane::process (double* left, double* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto p = static_cast<size_t> (ringPos);
        const double l = left[i], r = right != nullptr ? right[i] : left[i];
        left[i] = outRing[0][p];
        if (right != nullptr)
            right[i] = outRing[1][p];
        outRing[0][p] = outRing[1][p] = 0.0;   // this slot starts collecting the next overlaps
        inRing[0][p] = l;
        inRing[1][p] = r;
        ringPos = (ringPos + 1) % frame;
        if (++sinceFrame == hop)
        {
            sinceFrame = 0;
            processFrame();
        }
    }
}

void SpectralLane::smoothAcrossFrequency (const std::vector<double>& src, std::vector<double>& dst, double octaves) noexcept
{
    prefix[0] = 0.0;
    for (int k = 0; k < bins; ++k)
        prefix[static_cast<size_t> (k) + 1] = prefix[static_cast<size_t> (k)] + src[static_cast<size_t> (k)];
    const double spread = std::pow (2.0, 0.5 * octaves) - 1.0;   // half-width as a fraction of the bin's frequency
    for (int k = 0; k < bins; ++k)
    {
        const int half = static_cast<int> (std::lround (k * spread));
        const int lo = std::max (0, k - half), hi = std::min (bins - 1, k + half);
        dst[static_cast<size_t> (k)] = (prefix[static_cast<size_t> (hi) + 1] - prefix[static_cast<size_t> (lo)]) / (hi - lo + 1);
    }
}

void SpectralLane::processFrame() noexcept
{
    // Analysis: the last `frame` samples of each channel, windowed.
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < frame; ++i)
            spec[ch][static_cast<size_t> (i)] = inRing[ch][static_cast<size_t> ((ringPos + i) % frame)] * window[static_cast<size_t> (i)];
        fft.transform (spec[ch].data(), frame, false);
    }

    // Power per bin as each placement hears it: left, right, mid, side.
    for (int k = 0; k < bins; ++k)
    {
        const auto l = spec[0][static_cast<size_t> (k)], r = spec[1][static_cast<size_t> (k)];
        power[0][static_cast<size_t> (k)] = std::norm (l) * powerNorm;
        power[1][static_cast<size_t> (k)] = std::norm (r) * powerNorm;
        power[2][static_cast<size_t> (k)] = std::norm (0.5 * (l + r)) * powerNorm;
        power[3][static_cast<size_t> (k)] = std::norm (0.5 * (l - r)) * powerNorm;
    }
    for (auto& t : total)
        std::fill (t.begin(), t.end(), 0.0);

    const double hopSeconds = hop / fs;
    for (int bi = 0; bi < numBands; ++bi)
    {
        auto& band = bands[static_cast<size_t> (bi)];
        const auto& d = band.params.dyn;
        const auto place = band.params.place;

        // What the band listens to. Stereo bands follow the louder side.
        for (int k = 0; k < bins; ++k)
        {
            const auto i = static_cast<size_t> (k);
            const double p = place == Placement::Left ? power[0][i] : place == Placement::Right ? power[1][i]
                           : place == Placement::Mid ? power[2][i] : place == Placement::Side ? power[3][i]
                           : std::max (power[0][i], power[1][i]);
            smooth[i] = p;   // linear power for now
            powerDb[i] = 10.0 * std::log10 (p + 1.0e-20);
        }

        double reference = d.thresholdDb;   // Absolute, manual
        if (d.resonance)
        {
            // The local spectral envelope: power averaged over an octave around each bin.
            smoothAcrossFrequency (smooth, envelope, 1.0);
            for (auto& e : envelope)
                e = 10.0 * std::log10 (e + 1.0e-20);
            reference = (d.thresholdDb + 90.0) / 90.0 * 24.0;   // how far a bin must stick out, 0..24 dB
        }
        else if (d.autoThreshold)
        {
            // Long-term level inside the band, weighted by its shape.
            double num = 0.0, den = 1.0e-12;
            for (int k = 0; k < bins; ++k)
            {
                num += smooth[static_cast<size_t> (k)] * band.weight[static_cast<size_t> (k)];
                den += band.weight[static_cast<size_t> (k)];
            }
            const double level = num / den;
            if (level > 1.0e-9)   // frozen below -90 dB
                band.slowPower += (level - band.slowPower) * (1.0 - std::exp (-hopSeconds / 3.0));
            reference = 10.0 * std::log10 (band.slowPower + 1.0e-20) + (d.thresholdDb + 90.0) / 90.0 * 60.0 - 30.0;
        }

        const double knee = std::max (d.kneeDb, 0.0), slope = 1.0 - 1.0 / std::max (d.ratio, 1.0);
        const double range = std::abs (d.rangeDb), sign = d.rangeDb < 0.0 ? -1.0 : 1.0;
        for (int k = 0; k < bins; ++k)
        {
            const auto i = static_cast<size_t> (k);
            double x = powerDb[i] - (d.resonance ? envelope[i] + reference : reference);
            if (d.below)
                x = -x;
            double over = 0.0;
            if (2.0 * x >= knee)       over = x;
            else if (2.0 * x > -knee)  over = (x + 0.5 * knee) * (x + 0.5 * knee) / (2.0 * knee);
            target[i] = sign * std::min (over * slope, range) * band.weight[i];
        }

        // One partial lands on several neighbouring bins (the analysis window's main lobe), and
        // only the middle one reads its full level. Give each bin the strongest verdict within
        // two bins either side, so the whole partial is treated, not just its peak.
        for (int k = 0; k < bins; ++k)
        {
            double strongest = 0.0;
            for (int j = std::max (0, k - 2); j <= std::min (bins - 1, k + 2); ++j)
                if (std::abs (target[static_cast<size_t> (j)]) > std::abs (strongest))
                    strongest = target[static_cast<size_t> (j)];
            powerDb[static_cast<size_t> (k)] = strongest;   // powerDb is free to reuse here
        }
        std::copy (powerDb.begin(), powerDb.end(), target.begin());

        // Smooth the gains across frequency, then in time with the band's attack and release.
        smoothAcrossFrequency (target, smooth, std::clamp (d.resolutionOct, 1.0 / 24.0, 1.0));
        const double attack = std::exp (-hopSeconds / (std::max (d.attackMs, 0.01) * 0.001));
        const double release = std::exp (-hopSeconds / (std::max (d.releaseMs, 0.01) * 0.001));
        const int component = place == Placement::Left ? 0 : place == Placement::Right ? 1 : place == Placement::Mid ? 2 : place == Placement::Side ? 3 : -1;
        double peak = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const auto i = static_cast<size_t> (k);
            const double coef = std::abs (smooth[i]) > std::abs (band.gainDb[i]) ? attack : release;
            band.gainDb[i] = smooth[i] + (band.gainDb[i] - smooth[i]) * coef;
            if (std::abs (band.gainDb[i]) > std::abs (peak))
                peak = band.gainDb[i];
            if (component < 0)   // stereo: both sides
            {
                total[0][i] += band.gainDb[i];
                total[1][i] += band.gainDb[i];
            }
            else
                total[static_cast<size_t> (component)][i] += band.gainDb[i];
        }
        activity[static_cast<size_t> (band.slot)].store (static_cast<float> (peak), std::memory_order_relaxed);
    }

    // Apply: left and right gains, then mid and side gains, all zero phase.
    for (int k = 0; k < bins; ++k)
    {
        const auto i = static_cast<size_t> (k);
        auto l = spec[0][i] * std::pow (10.0, total[0][i] / 20.0);
        auto r = spec[1][i] * std::pow (10.0, total[1][i] / 20.0);
        if (total[2][i] != 0.0 || total[3][i] != 0.0)
        {
            const auto m = 0.5 * (l + r) * std::pow (10.0, total[2][i] / 20.0);
            const auto s = 0.5 * (l - r) * std::pow (10.0, total[3][i] / 20.0);
            l = m + s;
            r = m - s;
        }
        spec[0][i] = l;
        spec[1][i] = r;
    }

    // Synthesis: back to time, window again, overlap-add. Four overlapping Hann windows sum
    // to 2, hence the half.
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int k = 1; k < frame / 2; ++k)
            spec[ch][static_cast<size_t> (frame - k)] = std::conj (spec[ch][static_cast<size_t> (k)]);
        fft.transform (spec[ch].data(), frame, true);
        for (int i = 0; i < frame; ++i)
            outRing[ch][static_cast<size_t> ((ringPos + i) % frame)] += 0.5 * spec[ch][static_cast<size_t> (i)].real() * window[static_cast<size_t> (i)];
    }
}
} // namespace of
