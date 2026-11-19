#include "InstanceHub.h"

#include "plugin/PluginProcessor.h"
#include "ui/Theme.h"

namespace of
{
InstanceHub::InstanceHub() { startTimerHz (10); }
InstanceHub::~InstanceHub() { stopTimer(); }

void InstanceHub::add (OpenFilterProcessor& processor)
{
    const juce::ScopedLock sl (lock);
    auto slot = std::make_unique<Slot>();
    slot->entry.processor = &processor;
    slot->entry.serial = nextSerial++;
    slot->entry.colour = theme::band (slot->entry.serial - 1);
    slot->entry.spectrum.fill (-140.0);
    slot->entry.average.fill (-140.0);
    slot->analyzer = std::make_unique<SpectrumAnalyzer> (processor.hubFifo);
    slot->analyzer->setTiltPerOctave (0.0f);
    slot->analyzer->setReleasePerSecond (20.0f);   // steady enough that differing track latencies do not matter
    slot->analyzer->startAveraging();
    slots.push_back (std::move (slot));
    for (auto& s : slots)   // feeding only costs anything when there is someone to show it to
        s->entry.processor->hubFeeding.store (slots.size() > 1, std::memory_order_relaxed);
}

void InstanceHub::remove (OpenFilterProcessor& processor)
{
    const juce::ScopedLock sl (lock);
    slots.erase (std::remove_if (slots.begin(), slots.end(), [&] (const auto& s) { return s->entry.processor == &processor; }), slots.end());
    for (auto& s : slots)
        s->entry.processor->hubFeeding.store (slots.size() > 1, std::memory_order_relaxed);
}

int InstanceHub::count() const
{
    const juce::ScopedLock sl (lock);
    return static_cast<int> (slots.size());
}

std::vector<InstanceHub::Entry> InstanceHub::entries() const
{
    const juce::ScopedLock sl (lock);
    std::vector<Entry> out;
    for (const auto& s : slots)
        out.push_back (s->entry);
    return out;
}

void InstanceHub::timerCallback()
{
    const juce::ScopedLock sl (lock);
    if (slots.size() < 2)
        return;
    for (auto& s : slots)
    {
        auto& e = s->entry;
        e.name = e.processor->instanceName.isNotEmpty() ? e.processor->instanceName : "EQ " + juce::String (e.serial);
        if (! e.processor->instanceColour.isTransparent())
            e.colour = e.processor->instanceColour;

        s->analyzer->setSampleRate (e.processor->getSampleRate());
        if (! s->analyzer->isAveraging())   // a sample-rate change resets the analyzer
            s->analyzer->startAveraging();
        e.hasSignal = s->analyzer->update (0.1);

        fit::Curve raw {}, avg {};
        for (int i = 0; i < fit::kGridSize; ++i)
        {
            raw[static_cast<size_t> (i)] = s->analyzer->levelAt (fit::gridFrequency (i));
            avg[static_cast<size_t> (i)] = s->analyzer->averageDb (fit::gridFrequency (i));
        }
        e.spectrum = fit::smooth (raw, 1.0 / 6.0);
        e.average = avg;
    }
}

fit::Curve InstanceHub::collision (const fit::Curve& mine, const fit::Curve& other) noexcept
{
    double top = -200.0;
    for (int i = 0; i < fit::kGridSize; ++i)
        top = std::max ({ top, mine[static_cast<size_t> (i)], other[static_cast<size_t> (i)] });
    const double floor = top - 40.0;

    fit::Curve out {};
    for (int i = 0; i < fit::kGridSize; ++i)
    {
        const double p = mine[static_cast<size_t> (i)], o = other[static_cast<size_t> (i)];
        if (p > floor && o > floor && std::abs (p - o) < 9.0 && top > -100.0)
            out[static_cast<size_t> (i)] = 1.0 - std::abs (p - o) / 9.0;
    }
    return out;
}
} // namespace of
