#pragma once

#include "PluginProcessor.h"
#include "ui/BandDock.h"
#include "ui/ResponseDisplay.h"

namespace of
{
class OpenFilterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OpenFilterEditor (OpenFilterProcessor&);
    ~OpenFilterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For tools and tests. */
    ResponseDisplay& responseDisplay() noexcept { return display; }
    void updateNow() { timerCallback(); }

private:
    void timerCallback() override;
    void paintMeter (juce::Graphics&);

    OpenFilterProcessor& processor;
    juce::LookAndFeel_V4 look;
    ResponseDisplay display;
    BandDock dock;
    Dial outputDial { "Out" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, bypassButton { "Bypass" }, deltaButton { "Delta" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment, deltaAttachment;
    juce::Rectangle<int> topBar, outputColumn, meterArea;
    float meterDb[2] { -100.0f, -100.0f }, heldDb = -100.0f;
    int holdTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenFilterEditor)
};
} // namespace of
