#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "match/CurveFitter.h"
#include "ui/SpectrumAnalyzer.h"

namespace of
{
class OpenFilterProcessor;

/** Lets the instances in one process see each other (docs/spec/04-analysis.md section 2).

    Every processor registers here. While more than one is registered, each feeds a copy of
    its output to a small FIFO, and the hub turns those into smoothed spectra ten times a
    second on the message thread. Editors read the entries to draw other instances over
    their own display and to mark where two instances occupy the same frequencies.

    One hub per process (a SharedResourcePointer); it disappears with the last instance. */
class InstanceHub final : private juce::Timer
{
public:
    struct Entry
    {
        OpenFilterProcessor* processor = nullptr;
        int serial = 0;                 // unique within the process, for stable identity in the UI
        juce::String name;
        juce::Colour colour;
        fit::Curve spectrum {};         // dB on the fit grid, 1/6 octave smoothed, slow release
        fit::Curve average {};          // long-term average since the instance appeared
        bool hasSignal = false;
    };

    InstanceHub();
    ~InstanceHub() override;

    void add (OpenFilterProcessor&);
    void remove (OpenFilterProcessor&);

    /** A copy of the current entries. Message thread. */
    std::vector<Entry> entries() const;
    int count() const;

    /** Runs one update now (what the 10 Hz timer does). For tools and tests. */
    void refreshNow() { timerCallback(); }

    /** How strongly two spectra collide at each grid point, 0..1: both well above the floor
        and within 9 dB of each other. */
    static fit::Curve collision (const fit::Curve& mine, const fit::Curve& other) noexcept;

private:
    struct Slot
    {
        Entry entry;
        std::unique_ptr<SpectrumAnalyzer> analyzer;
    };

    void timerCallback() override;

    mutable juce::CriticalSection lock;
    std::vector<std::unique_ptr<Slot>> slots;
    int nextSerial = 1;
};
} // namespace of
