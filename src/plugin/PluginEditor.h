#pragma once

#include "PluginProcessor.h"
#include "ui/AdvancedPanel.h"
#include "ui/BandDock.h"
#include "ui/BandTable.h"
#include "ui/InstancePanel.h"
#include "ui/MatchPanel.h"
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
    void showMatchPanel (bool show) { matchPanel.setVisible (show); }
    void showAdvancedPanel (bool show) { advancedPanel.setVisible (show); }
    BandDock& bandDock() noexcept { return dock; }
    void showInstancePanel (bool show) { instancePanel.setVisible (show); if (show) instancePanel.showAll(); }

private:
    void timerCallback() override;
    void paintMeter (juce::Graphics&);
    void showPresetMenu();
    void showViewMenu();
    void applyViewSettings();
public:
    /** Re-reads the theme after it changed. */
    void themeChanged();
private:

    OpenFilterProcessor& processor;
    theme::NeonLook look;
    ResponseDisplay display;
    BandDock dock;
    Dial outputDial { "Out" }, scaleDial { "Scale" }, balanceDial { "Bal" };
    juce::TextButton invertButton { "Inv" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> invertAttachment;
    juce::TextButton presetButton, viewButton { "View" };
    std::array<juce::TextButton, OpenFilterProcessor::kNumSlots> slotButtons;
    BandTable table;
    MatchPanel matchPanel;
    InstancePanel instancePanel;
    AdvancedPanel advancedPanel;
    juce::TooltipWindow tooltips { this };
    juce::ComboBox phaseBox, qualityBox, autoBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> autoAttachment;
    juce::Rectangle<int> autoArea;
    float shownAuto = 0.0f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseAttachment, qualityAttachment;
    juce::Rectangle<int> latencyArea;
    int shownLatency = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, bypassButton { "Bypass" }, deltaButton { "Delta" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment, deltaAttachment;
    juce::Rectangle<int> topBar, outputColumn, meterArea;
    float meterDb[2] { -100.0f, -100.0f }, heldDb = -100.0f;
    int holdTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenFilterEditor)
};
} // namespace of
