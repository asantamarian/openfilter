#include "SpectrumAnalyzer.h"

namespace of
{
SpectrumAnalyzer::SpectrumAnalyzer (SpscFifo& source) : fifo (source)
{
    scratch.resize (4096);
    setSampleRate (48000.0);
}

void SpectrumAnalyzer::setSampleRate (double sampleRate)
{
    sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const int newOrder = sampleRate > 140000.0 ? 14 : sampleRate > 70000.0 ? 13 : 12;
    if (juce::exactlyEqual (sampleRate, fs) && newOrder == order && fft != nullptr)
        return;

    fs = sampleRate;
    order = newOrder;
    size = 1 << order;
    bins = size / 2 + 1;
    hop = size / 4;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize (static_cast<size_t> (size));
    for (int i = 0; i < size; ++i)   // Hann
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (size));
    ring.assign (static_cast<size_t> (size), 0.0f);
    work.assign (static_cast<size_t> (2 * size), 0.0f);
    levels.assign (static_cast<size_t> (bins), kFloorDb);
    ringPos = sinceFrame = 0;
    fifo.clear();
}

void SpectrumAnalyzer::clear()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (levels.begin(), levels.end(), kFloorDb);
    ringPos = sinceFrame = 0;
    fifo.clear();
}

bool SpectrumAnalyzer::update (double elapsedSeconds)
{
    // Release first, so a frame analysed in this call shows at full height.
    const float drop = release * static_cast<float> (juce::jlimit (0.0, 1.0, elapsedSeconds));
    for (auto& l : levels)
        l = juce::jmax (kFloorDb, l - drop);

    for (;;)
    {
        const auto n = fifo.pop (scratch.data(), scratch.size());
        if (n == 0)
            break;
        for (size_t i = 0; i < n; ++i)
        {
            ring[static_cast<size_t> (ringPos)] = scratch[i];
            ringPos = (ringPos + 1) & (size - 1);
            if (++sinceFrame >= hop)
            {
                sinceFrame = 0;
                analyseFrame();
            }
        }
    }

    for (int k = 1; k < bins; ++k)
        if (levels[static_cast<size_t> (k)] > -100.0f)
            return true;
    return false;
}

void SpectrumAnalyzer::analyseFrame()
{
    for (int i = 0; i < size; ++i)
        work[static_cast<size_t> (i)] = ring[static_cast<size_t> ((ringPos + i) & (size - 1))] * window[static_cast<size_t> (i)];
    std::fill (work.begin() + size, work.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (work.data(), true);

    // A full-scale sine reads 0 dB: Hann has a coherent gain of 0.5.
    const float norm = 4.0f / static_cast<float> (size);
    for (int k = 1; k < bins; ++k)
    {
        const float db = juce::Decibels::gainToDecibels (work[static_cast<size_t> (k)] * norm, kFloorDb);
        auto& level = levels[static_cast<size_t> (k)];
        level = juce::jmax (level, db);   // instant attack
    }
}

float SpectrumAnalyzer::levelAt (double freqHz) const noexcept
{
    const double pos = freqHz * size / fs;
    const int i = static_cast<int> (pos);
    const float t = static_cast<float> (pos - i);
    const float p0 = binDb (i - 1), p1 = binDb (i), p2 = binDb (i + 1), p3 = binDb (i + 2);
    const float v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    return v + tilt * static_cast<float> (std::log2 (juce::jmax (1.0, freqHz) / 1000.0));
}

float SpectrumAnalyzer::peakBetween (double lowHz, double highHz) const noexcept
{
    const int lo = static_cast<int> (std::ceil (lowHz * size / fs)), hi = static_cast<int> (std::floor (highHz * size / fs));
    if (hi < lo)
        return levelAt (0.5 * (lowHz + highHz));
    float peak = kFloorDb;
    for (int k = lo; k <= hi; ++k)
        peak = juce::jmax (peak, binDb (k));
    return peak + tilt * static_cast<float> (std::log2 (juce::jmax (1.0, 0.5 * (lowHz + highHz)) / 1000.0));
}
} // namespace of
