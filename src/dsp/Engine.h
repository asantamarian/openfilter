#pragma once

#include "dynamics/Detector.h"
#include "kernel/ChannelFilter.h"
#include "util/Halfband.h"

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

    /** Dynamic bands: feed the detector with the plugin input at the host rate. */
    bool wantsDetection() const noexcept { return detecting; }
    void detect (const double* left, const double* right, int n) noexcept { detector.process (left, right, n, last.place); }
    double dynamicGainDb() const noexcept { return detecting ? detector.current() : 0.0; }
    void prepareDetector (double hostRate, int hostControlBlock) noexcept { detector.prepare (hostRate, hostControlBlock); }

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
    Detector detector;
    bool detecting = false;
    double appliedDyn = 0.0;     // dynamic gain used in the current design
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

    /** Latency of the mode currently running. Changes only when the phase mode changes. */
    int latencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

    /** Latency a mode will have at a sample rate: lets the plugin tell the host from the
        message thread. Phase modes: 0 zero latency, 1 natural. */
    static int latencyFor (int phaseMode, double sampleRate) noexcept;
    static int oversamplingFor (int phaseMode, double sampleRate) noexcept;
    int controlBlockSize() const noexcept { return controlLen; }

    /** Current dynamic gain of a band in dB, for the display. */
    float dynamicGainDb (int band) const noexcept { return dynGains[static_cast<size_t> (band)].load (std::memory_order_relaxed); }

    /** Peak since the last call (linear). Called by the UI. */
    float takePeak (int channel) noexcept { return peaks[static_cast<size_t> (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }

private:
    void updateTargets (const ParamSnapshot& params, bool mono) noexcept;
    void processSegment (double* const* io, int numChannels, int offset, int n) noexcept;
    void configure (int mode) noexcept;
    bool runBands (double* left, double* right, int n) noexcept;

    static constexpr int kSteepHalf = 69, kWideHalf = 13;
    static constexpr int kDelayMask = 255;

    // Per channel: the two half-band stages, their work buffers and the dry delay line.
    struct Stages
    {
        Halfband first, second;
        std::array<double, 256> mid {};
        std::array<double, 512> work {};
        std::array<double, kDelayMask + 1> delay {};
        double pad = 0.0;
    };
    std::array<Stages, 2> stages {};
    int delayPos = 0;
    int phaseMode = 0, factor = 1;
    std::atomic<int> latency { 0 };
    double modeFade = 1.0;

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
    std::array<std::atomic<float>, kMaxBands> dynGains {};
};
} // namespace of
