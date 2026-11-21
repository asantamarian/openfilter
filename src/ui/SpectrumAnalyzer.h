#pragma once

#include <juce_dsp/juce_dsp.h>

#include "dsp/util/SpscFifo.h"

namespace of
{
/** Turns one analyzer tap into a smoothed spectrum (docs/spec/04-analysis.md section 1).
    Runs on the thread that calls update(): the message thread for now, the worker later. */
class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer (SpscFifo& source);

    /** Chooses the FFT size for the sample rate (4096 at 44.1/48 kHz) and clears history. */
    void setSampleRate (double sampleRate);

    /** Pulls whatever the audio thread queued, analyses complete frames and applies release
        ballistics for the elapsed time. Returns true while anything is above the floor. */
    bool update (double elapsedSeconds);

    /** Level in dB at a frequency, display tilt included. Catmull-Rom between bins. */
    float levelAt (double freqHz) const noexcept;

    /** Loudest level in dB between two frequencies (for display points that span bins). */
    float peakBetween (double lowHz, double highHz) const noexcept;

    void setTiltPerOctave (float db) noexcept { tilt = db; }
    void setReleasePerSecond (float db) noexcept { release = db; }
    void clear();

    /** Long-term average, for EQ match: starts collecting, and reads it back in dB (no tilt). */
    void startAveraging();
    void stopAveraging() noexcept { averaging = false; }
    bool isAveraging() const noexcept { return averaging; }
    double averagedSeconds() const noexcept { return fs > 0.0 ? averagedFrames * static_cast<double> (hop) / fs : 0.0; }
    double averageDb (double freqHz) const noexcept;

    static constexpr float kFloorDb = -140.0f;

private:
    void analyseFrame();
    float binDb (int bin) const noexcept { return levels[static_cast<size_t> (juce::jlimit (1, bins - 1, bin))]; }

    SpscFifo& fifo;
    double fs = 48000.0;
    int order = 12, size = 4096, bins = 2049, hop = 1024;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, ring, work, levels, scratch;
    int ringPos = 0, sinceFrame = 0;
    float tilt = 0.0f, release = 12.0f;
    std::vector<double> averagePower;
    int averagedFrames = 0;
    bool averaging = false;
};
} // namespace of
