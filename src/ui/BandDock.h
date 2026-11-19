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
    ~BandDock() override;

    /** Rebinds every control to the given band; -1 for no selection. */
    void setBand (int band);
    /** Called from the editor's timer to keep chips and dimming in step with the parameters. */
    void refresh();

    std::function<void (int)> onSelectBand;
    /** Returns the bands currently selected in the display. When there are several, turning a
        dial or picking from a list in the dock changes all of them: frequencies, Q, ratio and
        times by the same ratio; gain, range and threshold by the same amount; lists outright. */
    std::function<std::vector<int>()> selection;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** For tools and tests. */
    Dial& frequencyDial() noexcept { return freqDial; }
    Dial& gainDialForTests() noexcept { return gainDial; }

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::Rectangle<int> chipBounds (int index) const;

    OpenFilterProcessor& processor;
    int band = -1;
    std::array<bool, kMaxBands> used {};
    BandParams current {};

    juce::ComboBox shapeBox, slopeBox, placeBox, dynBox;
    std::unique_ptr<ComboAttachment> shapeAttachment, slopeAttachment, placeAttachment, dynAttachment;
    Dial freqDial { "Freq" }, gainDial { "Gain" }, qDial { "Q" };
    Dial thrDial { "Thresh" }, rangeDial { "Range" }, ratioDial { "Ratio" }, atkDial { "Attack" }, relDial { "Release" };
    juce::TextButton autoButton { "Auto" }, externalButton { "Ext SC" }, soloButton { "Solo" }, linearButton { "Lin" }, resonanceButton { "Reson" }, listenButton { "Key" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> resonanceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linearAttachment;
    bool mixedMode = false, rebinding = false;
    void applyToOthers (const char* suffix, float before, float after, bool byRatio);
    void setOnOthers (const char* suffix, float plainValue);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;
    juce::Rectangle<int> dynArea;
    float liveDyn = 0.0f;
    juce::TextButton pageButton;
    bool paged = false, filterShown = true, dynamicsShown = true;
    int page = 0;
    juce::TextButton bypassButton { "Bypass" }, deleteButton { "Delete" };
    juce::Rectangle<int> chipsArea, shapeArea, dialsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandDock)
};
} // namespace of
