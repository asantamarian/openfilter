#pragma once

#include "dynamics/Detector.h"
#include "kernel/ChannelFilter.h"
#include "lanes/FirLane.h"
#include "lanes/SpectralLane.h"
#include "util/Halfband.h"

#include <atomic>
#include <thread>
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
    bool wantsExternal() const noexcept { return last.dyn.external; }
    void detect (const double* left, const double* right, int n) noexcept { detector.process (left, right, n, last.place); }
    double dynamicGainDb() const noexcept { return detecting ? detector.current() : 0.0; }
    void prepareDetector (double hostRate, int hostControlBlock) noexcept { detector.prepare (hostRate, hostControlBlock); }

    /** For a band that lives in the linear-phase lane: run only its detector and return the
        dynamic gain in dB. The band's own filters stay out of it. */
    double detectOnly (const BandParams& target) noexcept;
    void stopDetectOnly() noexcept { if (! wasAudible) detecting = false; }

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
    Engine() = default;
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void prepare (double sampleRate, int maxBlock, int numChannels);
    void reset() noexcept;

    /** In-place. numSamples must not exceed maxBlock given to prepare(). sidechain may be
        null; with one sidechain channel it is used for both sides. */
    void process (double* const* io, int numChannels, int numSamples, const ParamSnapshot& params,
                  const double* const* sidechain = nullptr, int sidechainChannels = 0) noexcept;

    /** Band audition: the output becomes the part of the input that the band works on.
        -1 switches it off. Not a parameter: never saved, never automated. */
    void setSoloBand (int band) noexcept { soloRequest.store (band, std::memory_order_relaxed); }
    int soloBand() const noexcept { return soloRequest.load (std::memory_order_relaxed); }

    /** Latency of the mode currently running. Changes only when the phase mode changes. */
    int latencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

    /** Latency a mode will have at a sample rate: lets the plugin tell the host from the
        message thread. Phase modes: 0 zero latency, 1 natural, 2 linear, 3 mixed. linQuality
        (0..4) only matters for linear and mixed. */
    static int latencyFor (int phaseMode, double sampleRate, int linQuality = 1, bool spectral = false, int lookahead = 0, int character = 0,
                           bool linearDynamics = false) noexcept;
    static int lookaheadSamples (int lookahead, double sampleRate) noexcept;

    /** For tests: true once the linear-phase kernel matches the bands last sent to it. */
    bool linearSettled() const noexcept;
    static int oversamplingFor (int phaseMode, double sampleRate) noexcept;
    int controlBlockSize() const noexcept { return controlLen; }

    /** Gain auto gain is applying right now, in dB, for the display. */
    float autoGainDb() const noexcept { return autoGainShown.load (std::memory_order_relaxed); }

    /** Current dynamic gain of a band in dB, for the display. */
    float dynamicGainDb (int band) const noexcept
    {
        const float d = dynGains[static_cast<size_t> (band)].load (std::memory_order_relaxed);
        return d != 0.0f || ! spectralOn ? d : spectral.activityDb (band);   // spectral bands report their strongest bin
    }

    /** Peak since the last call (linear). Called by the UI. */
    float takePeak (int channel) noexcept { return peaks[static_cast<size_t> (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }

private:
    void updateTargets (const ParamSnapshot& params, bool mono) noexcept;
    void processSegment (double* const* io, int numChannels, int offset, int n,
                         const double* const* sidechain, int sidechainChannels) noexcept;
    void updateSolo (const ParamSnapshot& params) noexcept;
    double curveAutoGainDb (const ParamSnapshot& params) const noexcept;

    // Auto gain (docs/spec/03-dsp.md section 10).
    struct KWeight   // BS.1770 K-weighting: a high shelf, then a high-pass
    {
        double b[2][3] {}, a[2][2] {};
        double z[2][2][2] {};   // [channel][stage][state]
        void prepare (double sampleRate) noexcept;
        double tick (int ch, double x) noexcept;
        void reset() noexcept;
    };
    KWeight kDry, kWet;
    double msDry = 0.0, msWet = 0.0, loudCoef = 0.0;
    int autoMode = 0, autoCountdown = 0;
    double autoTargetDb = 0.0, autoDb = 0.0;
    ParamSnapshot autoSeen {};
    bool autoSeenValid = false;
    std::atomic<float> autoGainShown { 0.0f };
    void configure (int mode, int quality, bool withSpectral, int lookaheadSetting, int characterMode, bool linearDynamics) noexcept;
    void processCharacter (double* left, double* right, int n) noexcept;
    void postFirRequest (const ParamSnapshot& params) noexcept;
    void workerLoop();
    void stopWorker();
    bool runBands (double* left, double* right, int n) noexcept;

    static constexpr int kSteepHalf = 69, kWideHalf = 13;

    // Per channel: the two half-band stages, their work buffers and the dry delay line.
    struct Stages
    {
        Halfband first, second;
        std::array<double, 256> mid {};
        std::array<double, 512> work {};
        std::vector<double> delay;   // dry delay line, sized for the longest latency
        double pad = 0.0;
    };
    std::array<Stages, 2> stages {};
    int delayPos = 0, delayMask = 0;
    int phaseMode = 0, linQuality = 1, factor = 1;

    // Linear-phase path. Kernels are designed on a worker thread; the audio thread posts the
    // band set through a small mailbox guarded by a try-lock it never waits on.
    struct FirRequest
    {
        std::array<BandParams, kMaxBands> bands {};
        int count = 0, quality = 1;
        bool operator== (const FirRequest&) const = default;
    };
    SpectralLane spectral;
    bool spectralOn = false;
    // Character stage (docs/spec/03-dsp.md section 9): saturation at 4x (2x at 88.2/96 kHz).
    struct CharChannel
    {
        Halfband first, second;
        std::array<double, 256> mid {};
        std::array<double, 512> work {};
        double pad = 0.0;
        double shelf = 0.0, unshelf = 0.0, dcIn = 0.0, dcOut = 0.0;
    };
    std::array<CharChannel, 2> charStages {};
    int character = 0, charFactor = 1;
    double driveNow = 0.3, driveTarget = 0.3;

    int lookahead = 0, lookaheadLen = 0, lookPos = 0;
    std::vector<double> lookBuf[2];
    bool soloFromSidechain = false;
    FirLane fir;
    bool firMode = false, firRepost = false, firDynamics = false;
    FirRequest lastRequest {}, mail {};
    std::atomic_flag mailLock = ATOMIC_FLAG_INIT;
    std::atomic<unsigned> mailSerial { 0 }, doneSerial { 0 };
    std::thread worker;
    std::atomic<bool> workerRunning { false };
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

    // Solo (audition) filter on the delayed input.
    std::atomic<int> soloRequest { -1 };
    Placement soloPlace = Placement::Stereo;
    double sb0 = 1.0, sb1 = 0.0, sb2 = 0.0, sa1 = 0.0, sa2 = 0.0;
    double soloState[2][2] {};
    double soloMix = 0.0, soloTarget = 0.0;

    std::array<std::atomic<float>, 2> peaks {};
    std::array<std::atomic<float>, kMaxBands> dynGains {};
};
} // namespace of
