#pragma once

#include "Dial.h"
#include "Segmented.h"
#include "plugin/PluginProcessor.h"

namespace of
{
/** The docked controls of the selected band (docs/spec/05-ui.md section 5), laid out as in
    the design mockup: band chips with solo, bypass and delete; an icon shape picker with
    slope and channel; the filter dials; and the dynamics mode with its dials. */
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
        dial or picking a shape, slope, channel or mode in the dock changes all of them:
        frequencies, Q, ratio and times by the same ratio; gain, range and threshold by the
        same amount; picks outright. */
    std::function<std::vector<int>()> selection;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** For tools and tests. */
    Dial& frequencyDial() noexcept { return freqDial; }
    Dial& gainDialForTests() noexcept { return gainDial; }
    void showPage (int newPage);

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** Chips shrink as bands are added, so all 32 always fit. */
    struct ChipLayout { int w, h, gap, perRow; bool dots; };
    ChipLayout chipLayout (int count) const noexcept;
    juce::Rectangle<int> chipBounds (int index, const ChipLayout&) const;
    int usedCount() const noexcept;

    void applyToOthers (const char* suffix, float before, float after, bool byRatio);
    void setOnOthers (const char* suffix, float plainValue);

    OpenFilterProcessor& processor;
    int band = -1;
    std::array<bool, kMaxBands> used {};
    BandParams current {};

    Segmented shapePicker, channelPicker, modePicker;
    juce::ComboBox slopeBox;
    std::unique_ptr<ComboAttachment> slopeAttachment;
    Dial freqDial { "Freq" }, gainDial { "Gain" }, qDial { "Q" };
    Dial thrDial { "Threshold" }, rangeDial { "Range" }, ratioDial { "Ratio" }, atkDial { "Attack" }, relDial { "Release" };
    juce::TextButton soloButton { "Solo" }, listenButton { "Key" }, bypassButton { "Bypass" };
    juce::TextButton autoButton { "Auto" }, externalButton { "Ext SC" }, resonanceButton { "Reson" }, linearButton { "Lin" }, pageButton;
    IconButton deleteButton { "Delete band", "M2 3.5h9M5 3.5V2h3v1.5M3.2 3.5l.5 7.5h5.6l.5-7.5" };
    std::unique_ptr<ButtonAttachment> autoAttachment, linearAttachment, resonanceAttachment;

    juce::Rectangle<int> chipsArea, shapeArea, filterArea, dynArea, addChip, liveArea;
    bool mixedMode = false, rebinding = false;
    bool paged = false, filterShown = true, dynamicsShown = true;
    int page = 0;
    float liveDyn = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandDock)
};
} // namespace of
