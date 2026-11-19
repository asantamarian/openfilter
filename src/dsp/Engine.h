#pragma once

#include "kernel/ChannelFilter.h"

#include <atomic>
#include <vector>

namespace of
{
/** One band: smoothing of its continuous parameters, redesign at control rate, and four
    channel filters (lane A: left, right; lane B: mid, side). */
class BandProcessor
{
public:
    void prepare (double sampleRate, int controlBlock) noexcept;
    void reset() noexcept;

    /** Called once per control block with the effective band settings. */
    void update (const BandParams& target, bool mono) noexcept;

    bool laneAActive() const noexcept { return filters[0].active() || filters[1].active(); }
    bool laneBActive() const noexcept { return filters[2].active() || filters[3].active(); }

    void processLaneA (double* left, double* right, int n) noexcept;
    void processLaneB (double* mid, double* side, int n) noexcept;

private:
    std::array<ChannelFilter, 4> filters {};
    double fs = 48000.0;
    int controlLen = 32;
    double smoothCoef = 0.0;

    BandParams last {};          // last target seen (discrete fields and raw values)
    bool wasAudible = false;
    bool fresh = true;           // no block processed since reset
    bool lastMono = false;
    double logFreq = 0.0, gainDb = 0.0, logQ = 0.0;   // smoothed
    int fadeLen = 256;           // 5 ms, a whole number of control blocks
    bool switching = false;      // faded out, waiting to come back with a new shape
};

/** The whole audio path of milestone M1: IIR lanes, output section, bypass and delta.
    No allocation after prepare(). */
class Engine
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels);
    void reset() noexcept;

    /** In-place. numSamples must not exceed maxBlock given to prepare(). */
    void process (double* const* io, int numChannels, int numSamples, const ParamSnapshot& params) noexcept;

    int latencySamples() const noexcept { return 0; }
    int controlBlockSize() const noexcept { return controlLen; }

    /** Peak since the last call (linear). Called by the UI. */
    float takePeak (int channel) noexcept { return peaks[static_cast<size_t> (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }

private:
    void updateTargets (const ParamSnapshot& params, bool mono) noexcept;
    void processSegment (double* const* io, int numChannels, int offset, int n) noexcept;

    std::array<BandProcessor, kMaxBands> bands {};
    std::vector<double> dry[2];
    double fs = 48000.0;
    int controlLen = 32;
    int controlPhase = 0;
    int rampLen = 480;
    bool fresh = true;

    // Output section, smoothed per sample.
    double gainL = 1.0, gainR = 1.0, gainTargetL = 1.0, gainTargetR = 1.0;
    double bypassMix = 0.0, bypassTarget = 0.0;
    double deltaMix = 0.0, deltaTarget = 0.0;

    std::array<std::atomic<float>, 2> peaks {};
};
} // namespace of
