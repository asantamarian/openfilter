#include "Engine.h"

#include <algorithm>
#include <cmath>
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

    const bool routing = starting || target.place != last.place || mono != lastMono;

    if (redesign || routing)
    {
        BandParams p = target;
        p.freq = std::exp (logFreq);
        p.gainDb = gainDb;
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
void Engine::prepare (double sampleRate, int maxBlock, int)
{
    fs = sampleRate;
    controlLen = controlBlockFor (sampleRate);
    rampLen = std::max (1, static_cast<int> (0.010 * sampleRate));
    for (auto& d : dry)
        d.assign (static_cast<size_t> (std::max (1, maxBlock)), 0.0);
    for (auto& b : bands)
        b.prepare (sampleRate, controlLen);
    reset();
}

void Engine::reset() noexcept
{
    for (auto& b : bands)
        b.reset();
    controlPhase = 0;
    fresh = true;
    gainL = gainTargetL;
    gainR = gainTargetR;
    bypassMix = bypassTarget;
    deltaMix = deltaTarget;
    for (auto& p : peaks)
        p.store (0.0f, std::memory_order_relaxed);
}

void Engine::updateTargets (const ParamSnapshot& params, bool mono) noexcept
{
    for (int i = 0; i < kMaxBands; ++i)
    {
        BandParams p = params.bands[static_cast<size_t> (i)];
        if (hasGain (p.shape))
            p.gainDb = std::clamp (p.gainDb * params.gainScale, -60.0, 60.0);
        bands[static_cast<size_t> (i)].update (p, mono);
    }

    const double g = std::pow (10.0, params.outGainDb / 20.0) * (params.invert ? -1.0 : 1.0);
    const double pan = std::clamp (params.pan, -1.0, 1.0);
    gainTargetL = g * (pan > 0.0 ? std::cos (pan * 0.5 * std::numbers::pi) : 1.0);
    gainTargetR = g * (pan < 0.0 ? std::cos (-pan * 0.5 * std::numbers::pi) : 1.0);
    if (mono)
        gainTargetL = gainTargetR = g;
    bypassTarget = params.bypass ? 1.0 : 0.0;
    deltaTarget = params.delta ? 1.0 : 0.0;

    if (fresh)   // first block after a reset: start settled
    {
        gainL = gainTargetL;
        gainR = gainTargetR;
        bypassMix = bypassTarget;
        deltaMix = deltaTarget;
        fresh = false;
    }
}

void Engine::process (double* const* io, int numChannels, int numSamples, const ParamSnapshot& params) noexcept
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
        processSegment (io, numChannels, pos, n);
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

void Engine::processSegment (double* const* io, int numChannels, int offset, int n) noexcept
{
    double* left = io[0] + offset;
    double* right = numChannels > 1 ? io[1] + offset : nullptr;

    const bool needDry = bypassMix != 0.0 || bypassTarget != 0.0 || deltaMix != 0.0 || deltaTarget != 0.0;
    if (needDry)
    {
        std::copy_n (left, n, dry[0].data() + offset);
        if (right != nullptr)
            std::copy_n (right, n, dry[1].data() + offset);
    }

    // Lane A: stereo, left, right.
    for (auto& b : bands)
        if (b.laneAActive())
            b.processLaneA (left, right, n);

    // Lane B: mid, side. Encoded only when something needs it, to keep the idle path exact.
    if (right != nullptr)
    {
        bool anyB = false;
        for (auto& b : bands)
            anyB = anyB || b.laneBActive();

        if (anyB)
        {
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

    // Delta: what the EQ changed, before output gain.
    if (deltaMix != 0.0 || deltaTarget != 0.0)
    {
        const double step = 1.0 / rampLen;
        for (int i = 0; i < n; ++i)
        {
            deltaMix = deltaTarget > deltaMix ? std::min (deltaTarget, deltaMix + step) : std::max (deltaTarget, deltaMix - step);
            left[i] -= deltaMix * dry[0][static_cast<size_t> (offset + i)];
            if (right != nullptr)
                right[i] -= deltaMix * dry[1][static_cast<size_t> (offset + i)];
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

    // Bypass crossfade.
    if (bypassMix != 0.0 || bypassTarget != 0.0)
    {
        const double step = 1.0 / rampLen;
        for (int i = 0; i < n; ++i)
        {
            bypassMix = bypassTarget > bypassMix ? std::min (bypassTarget, bypassMix + step) : std::max (bypassTarget, bypassMix - step);
            left[i] += bypassMix * (dry[0][static_cast<size_t> (offset + i)] - left[i]);
            if (right != nullptr)
                right[i] += bypassMix * (dry[1][static_cast<size_t> (offset + i)] - right[i]);
        }
    }
}
} // namespace of
