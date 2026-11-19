#pragma once

#include "../Types.h"
#include "../util/Fft.h"

#include <atomic>
#include <vector>

namespace of
{
/** The linear-phase path (docs/spec/03-dsp.md section 6).

    All FIR bands of a stereo pair collapse into one 2x2 matrix of zero-phase responses:

        [L']   1 [ (Hm+Hs) Hl   (Hm-Hs) Hr ] [L]
        [R'] = - [ (Hm-Hs) Hl   (Hm+Hs) Hr ] [R]
               2

    where Hl, Hr are the products of the stereo/left/right bands and Hm, Hs of the mid/side
    bands. That is exactly "left/right bands, then mid/side bands" in a single convolution
    stage, so the latency is paid once.

    Each of the four responses becomes a linear-phase kernel of `taps` samples, applied by
    uniformly partitioned FFT convolution. Latency: taps/2 for the kernel plus one partition
    for blocking.

    Threads: design() runs on a worker and writes the spare kernel slot; process() runs on
    the audio thread and swaps to it at a block boundary with a one-block crossfade. Nothing
    is allocated after prepare(). */
class FirLane
{
public:
    /** Number of taps for a quality level 0..4 at a sample rate. */
    static int tapsFor (int quality, double sampleRate) noexcept;
    static int partitionFor (double sampleRate) noexcept;
    static int latencyFor (int quality, double sampleRate) noexcept { return tapsFor (quality, sampleRate) / 2 + partitionFor (sampleRate); }

    /** Allocates for the largest quality. Not real-time safe. */
    void prepare (double sampleRate);

    /** Audio thread: clears the signal history and starts as a pure delay for the given
        quality. Kernel slots are never written from the audio thread. */
    void reset (int quality) noexcept;

    int latency() const noexcept { return taps / 2 + partition; }

    /** Audio thread. In place; right may be null. */
    void process (double* left, double* right, int n) noexcept;

    /** True while the lane does anything other than delay the signal. */
    bool shaping() const noexcept { return ! unity; }

    /** Worker thread: designs the kernels for these bands (already filtered to the FIR
        members, gain scale applied) at a quality level and publishes them. With no bands it
        publishes "pure delay". Returns false if the previous design has not been picked up
        yet; call again later. */
    bool design (const BandParams* bands, int count, int quality) noexcept;

private:
    using Complex = Fft::Complex;

    struct Slot
    {
        // Four kernels (LL, LR, RL, RR), each `partitions` spectra of `bins` values.
        std::vector<Complex> spectra[4];
        bool cross = false;     // LR and RL are non-zero
        bool unity = true;      // no bands: stands for a pure delay, spectra unused
        int taps = 0;           // length this design was made for
    };

    void processBlock() noexcept;
    void accumulate (const Slot* slot, int out, Complex* acc) const noexcept;

    double fs = 48000.0;
    int partition = 512, bins = 513, maxPartitions = 128;
    int taps = 8192, partitions = 16;

    Fft blockFft, designFft;
    Slot slots[2];
    std::atomic<int> current { 0 };
    std::atomic<int> pending { -1 };

    // Signal state (audio thread).
    std::vector<double> inBlock[2], prevBlock[2], outBlock[2];
    std::vector<Complex> history[2];   // frequency-domain delay line: maxPartitions spectra per input
    std::vector<Complex> frame, accA, accB;
    int blockPos = 0, head = 0;
    bool unity = true;   // audio thread: currently a pure delay

    // Design scratch (worker thread).
    std::vector<Complex> grid;
    std::vector<double> window;
    int windowTaps = 0;
};
} // namespace of
