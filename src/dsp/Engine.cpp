#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <numbers>

namespace of
{
namespace
{
int controlBlockFor (double fs) noexcept
{
    return fs > 140000.0 ? 128 : fs > 70000.0 ? 64 : 32;
}

bool includes (Placement p, int filterIndex, bool mono) noexcept
{
    if (mono)
        return filterIndex == 0;
    switch (p)
    {
        case Placement::Stereo: return filterIndex == 0 || filterIndex == 1;
        case Placement::Left:   return filterIndex == 0;
        case Placement::Right:  return filterIndex == 1;
        case Placement::Mid:    return filterIndex == 2;
        case Placement::Side:   return filterIndex == 3;
    }
    return false;
}
} // namespace

//==============================================================================
void BandProcessor::prepare (double sampleRate, int controlBlock) noexcept
{
    fs = sampleRate;
    controlLen = controlBlock;
    smoothCoef = std::exp (-controlBlock / (0.015 * sampleRate));
    fadeLen = std::max (1, static_cast<int> (std::ceil (0.005 * sampleRate / controlBlock))) * controlBlock;
    reset();
}

void BandProcessor::reset() noexcept
{
    for (auto& f : filters)
        f.reset();
    last = {};
    wasAudible = false;
    switching = false;
    fresh = true;
    detecting = false;
    appliedDyn = 0.0;
    detector.reset();
}

void BandProcessor::update (const BandParams& target, bool mono) noexcept
{
    const bool audible = target.audible();
    const bool first = fresh;   // the first block after a reset starts settled, with no fade
    fresh = false;

    if (! audible && ! wasAudible)
        return;   // nothing to do: the filters are idle or already fading out

    const int fade = first ? 1 : fadeLen;

    if (! audible)
    {
        for (auto& f : filters)
            f.fadeTo (false, fade);
        detecting = false;
        appliedDyn = 0.0;
        switching = false;
        wasAudible = false;
        last = target;
        lastMono = mono;
        return;
    }

    const double tf = std::log (target.freq), tq = std::log (target.q);
    bool redesign = false;

    // A different shape or slope is an unrelated set of sections: fade the band out to dry,
    // then bring the new design in from dry.
    if (wasAudible && ! switching && (target.shape != last.shape || target.slope != last.slope))
    {
        for (auto& f : filters)
            f.fadeTo (false, fade);
        switching = true;
    }

    const bool starting = switching || ! wasAudible;
    if (starting)
    {
        last = target;
        lastMono = mono;
        for (const auto& f : filters)
            if (f.active())
                return;   // still fading out: try again next control block

        switching = false;
        logFreq = tf;  gainDb = target.gainDb;  logQ = tq;   // no sweep-in from stale values
        redesign = true;
    }
    else
    {
        const auto step = [this, &redesign] (double& cur, double goal, double eps)
        {
            if (cur == goal)
                return;
            cur = goal + (cur - goal) * smoothCoef;
            if (std::abs (cur - goal) < eps)
                cur = goal;
            redesign = true;
        };
        step (logFreq, tf, 1.0e-6);
        step (gainDb, target.gainDb, 1.0e-5);
        step (logQ, tq, 1.0e-6);
    }

    // Dynamics: the detector's verdict for this control block is added to the static gain.
    const bool wasDetecting = detecting;
    detecting = target.dynamic();
    double dynNow = 0.0;
    if (detecting)
    {
        if (! wasDetecting)
            detector.reset();
        if (! wasDetecting || redesign || target.shape != last.shape)
            detector.setFilter (target.shape, std::exp (logFreq), std::exp (logQ));
        dynNow = detector.update (target.dyn, target.dyn.rangeDb);
    }
    if (std::abs (dynNow - appliedDyn) > 1.0e-4)
        redesign = true;

    const bool routing = starting || target.place != last.place || mono != lastMono;

    if (redesign || routing)
    {
        appliedDyn = dynNow;
        BandParams p = target;
        p.freq = std::exp (logFreq);
        p.gainDb = std::clamp (gainDb + dynNow, -60.0, 60.0);
        p.q = std::exp (logQ);
        const auto design = designBand (p, fs);
        std::array<SvfCoeffs, kMaxSections> coeffs;
        for (int i = 0; i < design.count; ++i)
            coeffs[static_cast<size_t> (i)] = toSvf (design.biquads[static_cast<size_t> (i)]);

        for (int i = 0; i < 4; ++i)
        {
            auto& f = filters[static_cast<size_t> (i)];
            const bool wanted = includes (target.place, i, mono);
            if (wanted || f.active())
                f.setCoeffs (coeffs.data(), design.count, controlLen);
            if (routing)
                f.fadeTo (wanted, fade);
        }
    }

    last = target;
    wasAudible = true;
    lastMono = mono;
}

void BandProcessor::processLaneA (double* left, double* right, int n) noexcept
{
    filters[0].process (left, n);
    if (right != nullptr)
        filters[1].process (right, n);
}

void BandProcessor::processLaneB (double* mid, double* side, int n) noexcept
{
    filters[2].process (mid, n);
    filters[3].process (side, n);
}

//==============================================================================
void Engine::KWeight::prepare (double sampleRate) noexcept
{
    // Stage 1: high shelf, +4 dB above about 1.7 kHz. Stage 2: high-pass at 38 Hz.
    const double pi = std::numbers::pi;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / sampleRate), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        b[0][0] = (Vh + Vb * K / Q + K * K) / a0;
        b[0][1] = 2.0 * (K * K - Vh) / a0;
        b[0][2] = (Vh - Vb * K / Q + K * K) / a0;
        a[0][0] = 2.0 * (K * K - 1.0) / a0;
        a[0][1] = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / sampleRate), a0 = 1.0 + K / Q + K * K;
        b[1][0] = 1.0;  b[1][1] = -2.0;  b[1][2] = 1.0;
        a[1][0] = 2.0 * (K * K - 1.0) / a0;
        a[1][1] = (1.0 - K / Q + K * K) / a0;
    }
    reset();
}

void Engine::KWeight::reset() noexcept
{
    for (auto& ch : z)
        for (auto& stage : ch)
            stage[0] = stage[1] = 0.0;
}

double Engine::KWeight::tick (int ch, double x) noexcept
{
    for (int stage = 0; stage < 2; ++stage)
    {
        auto& s = z[ch][stage];
        const double y = b[stage][0] * x + s[0];
        s[0] = b[stage][1] * x - a[stage][0] * y + s[1];
        s[1] = b[stage][2] * x - a[stage][1] * y;
        x = y;
    }
    return x;
}

double Engine::curveAutoGainDb (const ParamSnapshot& params) const noexcept
{
    // Estimate how much louder the curve makes typical programme: the power gain averaged
    // over 48 log-spaced points, weighted like pink noise heard through K-weighting.
    Prototype protos[kMaxBands];
    double weight[kMaxBands];
    int count = 0;
    for (const auto& band : params.bands)
    {
        BandParams p = band;
        if (hasGain (p.shape))
            p.gainDb = std::clamp (p.gainDb * params.gainScale, -60.0, 60.0);
        p.dyn = {};
        if (! p.audible())
            continue;
        protos[count] = makePrototype (p);
        weight[count] = p.place == Placement::Stereo ? 1.0 : 0.5;   // a one-channel band moves half the power
        ++count;
    }
    if (count == 0)
        return 0.0;

    double num = 0.0, den = 0.0;
    for (int i = 0; i < 48; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 47.0);
        double db = 0.0;
        for (int b = 0; b < count; ++b)
            db += weight[b] * protos[b].magnitudeDb (f);
        const double shelf = 4.0 / (1.0 + (1500.0 / f) * (1500.0 / f));
        const double f4 = f * f * f * f;
        const double w = std::pow (10.0, shelf / 10.0) * f4 / (f4 + 38.0 * 38.0 * 38.0 * 38.0);
        num += w * std::pow (10.0, std::clamp (db, -60.0, 60.0) / 10.0);
        den += w;
    }
    return std::clamp (-10.0 * std::log10 (num / den), -24.0, 24.0);
}

int Engine::oversamplingFor (int phaseMode, double sampleRate) noexcept
{
    if (phaseMode != 1)   // only Natural oversamples
        return 1;
    return sampleRate < 70000.0 ? 4 : sampleRate < 140000.0 ? 2 : 1;
}

int Engine::latencyFor (int phaseMode, double sampleRate, int linQuality) noexcept
{
    if (phaseMode == 2 || phaseMode == 3)
        return FirLane::latencyFor (linQuality, sampleRate);
    switch (oversamplingFor (phaseMode, sampleRate))
    {
        case 4:  return kSteepHalf + (kWideHalf + 1) / 2;   // 69 + 7
        case 2:  return kWideHalf;                          // 13
        default: return 0;
    }
}

Engine::~Engine() { stopWorker(); }

void Engine::stopWorker()
{
    workerRunning.store (false);
    if (worker.joinable())
        worker.join();
}

void Engine::workerLoop()
{
    FirRequest local;
    while (workerRunning.load (std::memory_order_relaxed))
    {
        const unsigned serial = mailSerial.load (std::memory_order_acquire);
        if (serial != doneSerial.load (std::memory_order_relaxed))
        {
            while (mailLock.test_and_set (std::memory_order_acquire))
                std::this_thread::yield();
            local = mail;
            const unsigned taken = mailSerial.load (std::memory_order_relaxed);
            mailLock.clear (std::memory_order_release);

            if (fir.design (local.bands.data(), local.count, local.quality))
                doneSerial.store (taken, std::memory_order_release);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
}

bool Engine::linearSettled() const noexcept
{
    return ! firMode || (! firRepost && mailSerial.load() == doneSerial.load() && fir.shaping() == (lastRequest.count > 0));
}

void Engine::prepare (double sampleRate, int maxBlock, int)
{
    stopWorker();   // the worker must not be designing while buffers are reallocated

    fs = sampleRate;
    controlLen = controlBlockFor (sampleRate);
    rampLen = std::max (1, static_cast<int> (0.010 * sampleRate));
    for (auto& d : dry)
        d.assign (static_cast<size_t> (std::max (1, maxBlock)), 0.0);

    fir.prepare (sampleRate);
    kDry.prepare (sampleRate);
    kWet.prepare (sampleRate);
    loudCoef = 1.0 - std::exp (-1.0 / (1.5 * sampleRate));   // about a 3 s window
    int delaySize = 256;
    while (delaySize < FirLane::latencyFor (4, sampleRate) + 16)
        delaySize <<= 1;
    delayMask = delaySize - 1;
    for (auto& ch : stages)
        ch.delay.assign (static_cast<size_t> (delaySize), 0.0);

    doneSerial.store (mailSerial.load());
    configure (phaseMode, linQuality);
    reset();

    workerRunning.store (true);
    worker = std::thread ([this] { workerLoop(); });
}

void Engine::configure (int mode, int quality) noexcept
{
    phaseMode = mode;
    linQuality = quality;
    firMode = mode == 2 || mode == 3;
    factor = oversamplingFor (mode, fs);
    latency.store (latencyFor (mode, fs, quality), std::memory_order_relaxed);
    for (auto& ch : stages)
    {
        ch.first.design (factor == 4 ? kSteepHalf : kWideHalf);
        ch.second.design (kWideHalf);
        ch.pad = 0.0;
        std::fill (ch.delay.begin(), ch.delay.end(), 0.0);
    }
    delayPos = 0;
    if (firMode)
    {
        fir.reset (quality);   // starts as a pure delay until the worker delivers a kernel
        lastRequest = {};
        firRepost = true;
    }
    for (auto& b : bands)
    {
        b.prepare (fs * factor, controlLen * factor);   // also resets the band
        b.prepareDetector (fs, controlLen);             // detectors always run at the host rate
    }
}

void Engine::postFirRequest (const ParamSnapshot& params) noexcept
{
    // The linear-phase members: every audible band in Linear, the flagged ones in Mixed.
    FirRequest req;
    req.quality = linQuality;
    for (const auto& band : params.bands)
    {
        BandParams p = band;
        if (hasGain (p.shape))
            p.gainDb = std::clamp (p.gainDb * params.gainScale, -60.0, 60.0);
        p.dyn = {};   // dynamics do not run in the linear-phase path
        if (p.audible() && (phaseMode == 2 || p.linear))
            req.bands[static_cast<size_t> (req.count++)] = p;
    }
    if (req == lastRequest && ! firRepost)
        return;

    if (! mailLock.test_and_set (std::memory_order_acquire))   // never wait for the worker
    {
        mail = req;
        mailSerial.fetch_add (1, std::memory_order_release);
        mailLock.clear (std::memory_order_release);
        lastRequest = req;
        firRepost = false;
    }
    else
        firRepost = true;   // try again next control block
}

void Engine::reset() noexcept
{
    for (auto& b : bands)
        b.reset();
    for (auto& ch : stages)
    {
        ch.first.reset();
        ch.second.reset();
        ch.pad = 0.0;
        std::fill (ch.delay.begin(), ch.delay.end(), 0.0);
    }
    delayPos = 0;
    if (firMode)
    {
        fir.reset (linQuality);
        lastRequest = {};
        firRepost = true;
    }
    controlPhase = 0;
    fresh = true;
    modeFade = 1.0;
    kDry.reset();
    kWet.reset();
    msDry = msWet = 0.0;
    autoDb = autoTargetDb = 0.0;
    autoMode = 0;
    autoSeenValid = false;
    autoCountdown = 0;
    gainL = gainTargetL;
    gainR = gainTargetR;
    bypassMix = bypassTarget;
    deltaMix = deltaTarget;
    for (auto& p : peaks)
        p.store (0.0f, std::memory_order_relaxed);
}

void Engine::updateTargets (const ParamSnapshot& params, bool mono) noexcept
{
    const int quality = std::clamp (params.linQuality, 0, 4);
    if (params.phaseMode != phaseMode || (firMode && quality != linQuality))
    {
        // A different phase mode or kernel length changes latency. Neither is automatable,
        // so a short fade-in after the switch is enough.
        const bool wasFresh = fresh;
        configure (params.phaseMode, quality);
        modeFade = wasFresh ? 1.0 : 0.0;
    }
    if (firMode)
        postFirRequest (params);

    for (int i = 0; i < kMaxBands; ++i)
    {
        BandParams p = params.bands[static_cast<size_t> (i)];
        if (hasGain (p.shape))
        {
            p.gainDb = std::clamp (p.gainDb * params.gainScale, -60.0, 60.0);
            p.dyn.rangeDb = std::clamp (p.dyn.rangeDb * params.gainScale, -60.0, 60.0);
        }
        // A band realised in the linear-phase path is silent in the minimum-phase one.
        if (firMode && (phaseMode == 2 || p.linear))
            p.used = false;
        bands[static_cast<size_t> (i)].update (p, mono);
        dynGains[static_cast<size_t> (i)].store (static_cast<float> (bands[static_cast<size_t> (i)].dynamicGainDb()), std::memory_order_relaxed);
    }

    // Auto gain.
    if (params.autoGain != autoMode)
    {
        autoMode = params.autoGain;
        autoSeenValid = false;
        kDry.reset();
        kWet.reset();
        msDry = msWet = 0.0;
        if (autoMode == 0)
            autoTargetDb = 0.0;
    }
    if (autoMode == 1 && --autoCountdown <= 0)
    {
        // From the curve: recomputed when a band or the scale changes, at most every eighth
        // control block.
        autoCountdown = 8;
        const bool same = autoSeenValid && autoSeen.bands == params.bands && autoSeen.gainScale == params.gainScale;
        if (! same)
        {
            autoSeen = params;
            autoSeenValid = true;
            autoTargetDb = curveAutoGainDb (params);
        }
    }
    else if (autoMode == 2)
    {
        // Loudness matched: the difference between the K-weighted level of the dry and the
        // processed signal, frozen while the input is very quiet.
        const double dryDb = 10.0 * std::log10 (msDry + 1.0e-20) - 0.691, wetDb = 10.0 * std::log10 (msWet + 1.0e-20) - 0.691;
        if (dryDb > -60.0)
            autoTargetDb = std::clamp (dryDb - wetDb, -12.0, 12.0);
    }
    // Curve mode settles in 50 ms, loudness mode in about 2 s.
    const double tau = autoMode == 2 ? 2.0 : 0.05;
    autoDb += (autoTargetDb - autoDb) * (1.0 - std::exp (-(controlLen / fs) / tau));
    if (std::abs (autoTargetDb - autoDb) < 1.0e-4)
        autoDb = autoTargetDb;
    autoGainShown.store (static_cast<float> (autoDb), std::memory_order_relaxed);

    const double g = std::pow (10.0, (params.outGainDb + autoDb) / 20.0) * (params.invert ? -1.0 : 1.0);
    const double pan = std::clamp (params.pan, -1.0, 1.0);
    gainTargetL = g * (pan > 0.0 ? std::cos (pan * 0.5 * std::numbers::pi) : 1.0);
    gainTargetR = g * (pan < 0.0 ? std::cos (-pan * 0.5 * std::numbers::pi) : 1.0);
    if (mono)
        gainTargetL = gainTargetR = g;
    bypassTarget = params.bypass ? 1.0 : 0.0;
    deltaTarget = params.delta ? 1.0 : 0.0;
    updateSolo (params);

    if (fresh)   // first block after a reset: start settled
    {
        if (autoMode == 1)
        {
            autoDb = autoTargetDb;
            const double settled = std::pow (10.0, (params.outGainDb + autoDb) / 20.0) * (params.invert ? -1.0 : 1.0);
            gainTargetL *= settled / g;
            gainTargetR *= settled / g;
        }
        gainL = gainTargetL;
        gainR = gainTargetR;
        bypassMix = bypassTarget;
        deltaMix = deltaTarget;
        fresh = false;
    }
}

void Engine::updateSolo (const ParamSnapshot& params) noexcept
{
    const int request = soloRequest.load (std::memory_order_relaxed);
    if (request < 0 || request >= kMaxBands || ! params.bands[static_cast<size_t> (request)].used)
    {
        soloTarget = 0.0;
        return;
    }

    // The part of the spectrum the band works on: a band-pass for bells, notches and band
    // passes; for shelves and cuts, the side of the corner they change.
    const auto& b = params.bands[static_cast<size_t> (request)];
    const double w = 2.0 * std::numbers::pi * std::clamp (b.freq, 10.0, 0.45 * fs) / fs;
    const double cs = std::cos (w), sn = std::sin (w);
    const bool low = b.shape == Shape::LowShelf || b.shape == Shape::LowCut;
    const bool high = b.shape == Shape::HighShelf || b.shape == Shape::HighCut || b.shape == Shape::TiltShelf;
    const double q = (low || high) ? 0.7071 : std::clamp (b.q, 0.5, 12.0);
    const double alpha = sn / (2.0 * q), a0 = 1.0 + alpha;
    double c0, c1, c2;
    if (low)        { c0 = 0.5 * (1.0 - cs); c1 = 1.0 - cs; c2 = c0; }
    else if (high)  { c0 = 0.5 * (1.0 + cs); c1 = -(1.0 + cs); c2 = c0; }
    else            { c0 = alpha; c1 = 0.0; c2 = -alpha; }
    sb0 = c0 / a0;  sb1 = c1 / a0;  sb2 = c2 / a0;
    sa1 = -2.0 * cs / a0;  sa2 = (1.0 - alpha) / a0;
    soloPlace = b.place;
    soloTarget = 1.0;
}

void Engine::process (double* const* io, int numChannels, int numSamples, const ParamSnapshot& params,
                      const double* const* sidechain, int sidechainChannels) noexcept
{
    numChannels = std::min (numChannels, 2);
    if (numChannels <= 0)
        return;

    int pos = 0;
    while (pos < numSamples)
    {
        if (controlPhase == 0)
            updateTargets (params, numChannels == 1);

        const int n = std::min (controlLen - controlPhase, numSamples - pos);
        processSegment (io, numChannels, pos, n, sidechain, sidechainChannels);
        controlPhase = (controlPhase + n) % controlLen;
        pos += n;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        double pk = 0.0;
        for (int i = 0; i < numSamples; ++i)
            pk = std::max (pk, std::abs (io[ch][i]));
        auto& slot = peaks[static_cast<size_t> (ch)];
        slot.store (std::max (slot.load (std::memory_order_relaxed), static_cast<float> (pk)), std::memory_order_relaxed);
    }
}

bool Engine::runBands (double* left, double* right, int n) noexcept
{
    bool any = false;

    // Lane A: stereo, left, right.
    for (auto& b : bands)
        if (b.laneAActive())
        {
            b.processLaneA (left, right, n);
            any = true;
        }

    // Lane B: mid, side. Encoded only when something needs it, to keep the idle path exact.
    if (right != nullptr)
    {
        bool anyB = false;
        for (auto& b : bands)
            anyB = anyB || b.laneBActive();

        if (anyB)
        {
            any = true;
            for (int i = 0; i < n; ++i)
            {
                const double m = 0.5 * (left[i] + right[i]), s = 0.5 * (left[i] - right[i]);
                left[i] = m;
                right[i] = s;
            }
            for (auto& b : bands)
                if (b.laneBActive())
                    b.processLaneB (left, right, n);
            for (int i = 0; i < n; ++i)
            {
                const double m = left[i], s = right[i];
                left[i] = m + s;
                right[i] = m - s;
            }
        }
    }
    return any;
}

void Engine::processSegment (double* const* io, int numChannels, int offset, int n,
                             const double* const* sidechain, int sidechainChannels) noexcept
{
    double* left = io[0] + offset;
    double* right = numChannels > 1 ? io[1] + offset : nullptr;
    double* dryL = dry[0].data() + offset;
    double* dryR = dry[1].data() + offset;
    const int lat = latency.load (std::memory_order_relaxed);

    // Dynamic bands listen to the input, before any band has touched it.
    // A band set to External listens to the sidechain input instead, when there is one.
    const double* scLeft = sidechain != nullptr && sidechainChannels > 0 ? sidechain[0] + offset : nullptr;
    const double* scRight = scLeft == nullptr || right == nullptr ? nullptr
                          : sidechainChannels > 1 ? sidechain[1] + offset : scLeft;
    for (auto& b : bands)
        if (b.wantsDetection())
        {
            if (b.wantsExternal() && scLeft != nullptr)
                b.detect (scLeft, scRight, n);
            else
                b.detect (left, right, n);
        }

    // Dry signal, delayed by the latency: for bypass, delta, and the idle path.
    if (lat > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            const int w = (delayPos + i) & delayMask, r = (delayPos + i - lat) & delayMask;
            stages[0].delay[static_cast<size_t> (w)] = left[i];
            dryL[i] = stages[0].delay[static_cast<size_t> (r)];
            if (right != nullptr)
            {
                stages[1].delay[static_cast<size_t> (w)] = right[i];
                dryR[i] = stages[1].delay[static_cast<size_t> (r)];
            }
        }
        delayPos = (delayPos + n) & delayMask;
    }
    else
    {
        std::copy_n (left, n, dryL);
        if (right != nullptr)
            std::copy_n (right, n, dryR);
    }

    if (firMode)
    {
        // Minimum-phase bands first, then everything through the linear-phase lane, which
        // also supplies the latency when it has no bands of its own.
        const bool anyIir = runBands (left, right, n);
        fir.process (left, right, n);
        if (! anyIir && ! fir.shaping())
        {
            std::copy_n (dryL, n, left);   // nothing active: the delayed input, exactly
            if (right != nullptr)
                std::copy_n (dryR, n, right);
        }
    }
    else if (factor == 1)
    {
        runBands (left, right, n);
    }
    else
    {
        // Natural: run the bands at 2x or 4x so their response follows the analog prototype
        // through the top octave (docs/spec/03-dsp.md section 5).
        double* up[2] = { stages[0].work.data(), stages[1].work.data() };
        const int channels = right != nullptr ? 2 : 1;
        double* in[2] = { left, right };
        const int m = n * factor;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& st = stages[static_cast<size_t> (ch)];
            if (factor == 4)
            {
                st.first.up (in[ch], n, st.mid.data());
                st.second.up (st.mid.data(), 2 * n, up[ch]);
            }
            else
                st.first.up (in[ch], n, up[ch]);
        }

        const bool any = runBands (up[0], channels > 1 ? up[1] : nullptr, m);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& st = stages[static_cast<size_t> (ch)];
            if (factor == 4)
            {
                st.second.down (up[ch], 2 * n, st.mid.data());
                // One extra sample at 2x makes the round trip a whole number of samples.
                for (int i = 0; i < 2 * n; ++i)
                    std::swap (st.pad, st.mid[static_cast<size_t> (i)]);
                st.first.down (st.mid.data(), n, in[ch]);
            }
            else
                st.first.down (up[ch], n, in[ch]);
        }

        // With no band active the resampled signal is replaced by the delayed input, which
        // is exact; the resamplers keep running so a band can come in without a gap.
        if (! any)
        {
            std::copy_n (dryL, n, left);
            if (right != nullptr)
                std::copy_n (dryR, n, right);
        }
    }

    // Loudness-matched auto gain listens to the delayed input and to the processed signal.
    if (autoMode == 2)
    {
        for (int i = 0; i < n; ++i)
        {
            double d = kDry.tick (0, dryL[i]), w = kWet.tick (0, left[i]);
            double dd = d * d, ww = w * w;
            if (right != nullptr)
            {
                d = kDry.tick (1, dryR[i]);
                w = kWet.tick (1, right[i]);
                dd += d * d;
                ww += w * w;
            }
            msDry += (dd - msDry) * loudCoef;
            msWet += (ww - msWet) * loudCoef;
        }
    }

    // Fade in after a phase-mode switch.
    if (modeFade < 1.0)
    {
        const double step = 1.0 / rampLen;
        for (int i = 0; i < n; ++i)
        {
            modeFade = std::min (1.0, modeFade + step);
            left[i] *= modeFade;
            if (right != nullptr)
                right[i] *= modeFade;
        }
    }

    // Delta: what the EQ changed, before output gain.
    if (deltaMix != 0.0 || deltaTarget != 0.0)
    {
        const double step = 1.0 / rampLen;
        for (int i = 0; i < n; ++i)
        {
            deltaMix = deltaTarget > deltaMix ? std::min (deltaTarget, deltaMix + step) : std::max (deltaTarget, deltaMix - step);
            left[i] -= deltaMix * dryL[i];
            if (right != nullptr)
                right[i] -= deltaMix * dryR[i];
        }
    }

    // Output gain, balance, polarity.
    if (gainL != 1.0 || gainR != 1.0 || gainTargetL != 1.0 || gainTargetR != 1.0)
    {
        const double coef = 1.0 - std::exp (-1.0 / (0.005 * fs));
        for (int i = 0; i < n; ++i)
        {
            gainL += (gainTargetL - gainL) * coef;
            gainR += (gainTargetR - gainR) * coef;
            if (std::abs (gainTargetL - gainL) < 1.0e-9) gainL = gainTargetL;
            if (std::abs (gainTargetR - gainR) < 1.0e-9) gainR = gainTargetR;
            left[i] *= gainL;
            if (right != nullptr)
                right[i] *= gainR;
        }
    }

    // Solo: replace the output with the auditioned part of the (delayed) input.
    if (soloMix != 0.0 || soloTarget != 0.0)
    {
        const double step = 1.0 / rampLen;
        const auto tick = [this] (int ch, double x)
        {
            auto& z = soloState[ch];
            const double y = sb0 * x + z[0];
            z[0] = sb1 * x - sa1 * y + z[1];
            z[1] = sb2 * x - sa2 * y;
            return y;
        };
        for (int i = 0; i < n; ++i)
        {
            soloMix = soloTarget > soloMix ? std::min (soloTarget, soloMix + step) : std::max (soloTarget, soloMix - step);
            double a = dryL[i], b = right != nullptr ? dryR[i] : dryL[i];
            if (right != nullptr)
            {
                // Audition what the band's channel hears.
                if (soloPlace == Placement::Mid)        a = b = 0.5 * (dryL[i] + dryR[i]);
                else if (soloPlace == Placement::Side)  { a = 0.5 * (dryL[i] - dryR[i]); b = -a; }
                else if (soloPlace == Placement::Left)  b = 0.0;
                else if (soloPlace == Placement::Right) a = 0.0;
            }
            const double fa = tick (0, a), fb = tick (1, b);
            left[i] += soloMix * (fa - left[i]);
            if (right != nullptr)
                right[i] += soloMix * (fb - right[i]);
        }
        if (soloMix == 0.0 && soloTarget == 0.0)
            for (auto& z : soloState)
                z[0] = z[1] = 0.0;
    }

    // Bypass crossfade.
    if (bypassMix != 0.0 || bypassTarget != 0.0)
    {
        const double step = 1.0 / rampLen;
        for (int i = 0; i < n; ++i)
        {
            bypassMix = bypassTarget > bypassMix ? std::min (bypassTarget, bypassMix + step) : std::max (bypassTarget, bypassMix - step);
            left[i] += bypassMix * (dryL[i] - left[i]);
            if (right != nullptr)
                right[i] += bypassMix * (dryR[i] - right[i]);
        }
    }
}
} // namespace of
