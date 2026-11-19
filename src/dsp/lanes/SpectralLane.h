#pragma once

#include "../Types.h"
#include "../util/Fft.h"

#include <atomic>
#include <vector>

namespace of
{
/** Spectral dynamics (docs/spec/03-dsp.md section 8).

    One short-time Fourier transform serves every spectral band. Each band works out a gain
    for every frequency bin inside its own shape: a bin is turned down (or up) only as far as
    it, on its own, is over the threshold. So one ringing partial can be pulled down while its
    neighbours are left alone, which a dynamic band (one gain for the whole band) cannot do.

    - Absolute mode: a bin is judged against the threshold.
    - Resonance mode: a bin is judged against the smoothed spectrum around it, so only what
      sticks out is treated, at any level.

    Frame 2048 samples at 44.1/48 kHz (scaled with the rate), hop a quarter, square-root Hann
    windows on both sides. Latency: one frame. Real-time safe after prepare(). */
class SpectralLane
{
public:
    static int frameFor (double sampleRate) noexcept { return 2048 * (sampleRate > 140000.0 ? 4 : sampleRate > 70000.0 ? 2 : 1); }

    void prepare (double sampleRate);
    void reset() noexcept;
    int latency() const noexcept { return frame; }

    /** Control rate: the spectral bands (range already scaled). Cheap when nothing changed. */
    void setBands (const BandParams* bands, const int* slotIndex, int count) noexcept;

    /** In place; right may be null. */
    void process (double* left, double* right, int n) noexcept;

    /** Largest gain change the band in this slot applied in the last frame, in dB. */
    float activityDb (int slot) const noexcept { return activity[static_cast<size_t> (slot)].load (std::memory_order_relaxed); }

private:
    using Complex = Fft::Complex;

    struct Band
    {
        BandParams params {};
        int slot = 0;
        std::vector<double> weight;    // 0..1 per bin: the band's own shape
        std::vector<double> gainDb;    // smoothed gain per bin
        double slowPower = 1.0e-3;     // long-term level inside the band, for auto threshold
    };

    void processFrame() noexcept;
    void computeWeights (Band& band) noexcept;
    /** Average of src over a window whose half-width grows with frequency (constant in octaves). */
    void smoothAcrossFrequency (const std::vector<double>& src, std::vector<double>& dst, double octaves) noexcept;

    double fs = 48000.0;
    int frame = 2048, hop = 512, bins = 1025;
    Fft fft;
    std::vector<double> window;
    std::vector<double> inRing[2], outRing[2];
    int ringPos = 0, sinceFrame = 0;
    std::vector<Complex> spec[2], work;
    std::vector<double> power[4], powerDb, envelope, target, smooth, prefix, total[4];
    double powerNorm = 1.0;

    std::vector<Band> bands;   // kMaxBands, allocated once
    int numBands = 0;
    std::array<std::atomic<float>, kMaxBands> activity {};
};
} // namespace of
