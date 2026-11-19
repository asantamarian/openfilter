#pragma once

#include "Dial.h"
#include "ResponseDisplay.h"

namespace of
{
/** The settings that have no room in the dock: the character stage's drive, and for the
    selected band the detector's own filter, the knee, the trigger direction and the
    resolution of a spectral band. */
class AdvancedPanel final : public juce::Component
{
public:
    AdvancedPanel (OpenFilterProcessor&, ResponseDisplay&);

    /** Called from the editor's timer: follows the selected band. */
    void refresh();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void bind (int band);

    OpenFilterProcessor& processor;
    ResponseDisplay& display;
    int band = -2;   // -2: not bound yet
    BandParams current {};

    juce::ComboBox characterBox, triggerBox;
    std::unique_ptr<ComboAttachment> characterAttachment, triggerAttachment;
    juce::TextButton freeButton { "Free detector" }, closeButton { "Close" };
    std::unique_ptr<ButtonAttachment> freeAttachment;
    Dial driveDial { "Drive" }, detFreqDial { "Det freq" }, detQDial { "Det Q" }, kneeDial { "Knee" }, resolutionDial { "Resol." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedPanel)
};
} // namespace of
