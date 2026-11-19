#pragma once

#include "Dial.h"
#include "plugin/PluginProcessor.h"

namespace of
{
/** The docked controls of the selected band (docs/spec/05-ui.md section 5): band chips,
    shape, slope, placement and the filter dials. Dynamics controls arrive with M3. */
class BandDock final : public juce::Component
{
public:
    explicit BandDock (OpenFilterProcessor&);

    /** Rebinds every control to the given band; -1 for no selection. */
    void setBand (int band);
    /** Called from the editor's timer to keep chips and dimming in step with the parameters. */
    void refresh();

    std::function<void (int)> onSelectBand;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::Rectangle<int> chipBounds (int index) const;

    OpenFilterProcessor& processor;
    int band = -1;
    std::array<bool, kMaxBands> used {};
    BandParams current {};

    juce::ComboBox shapeBox, slopeBox, placeBox;
    std::unique_ptr<ComboAttachment> shapeAttachment, slopeAttachment, placeAttachment;
    Dial freqDial { "Freq" }, gainDial { "Gain" }, qDial { "Q" };
    juce::TextButton bypassButton { "Bypass" }, deleteButton { "Delete" };
    juce::Rectangle<int> chipsArea, shapeArea, dialsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandDock)
};
} // namespace of
