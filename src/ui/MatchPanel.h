#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "ResponseDisplay.h"
#include "match/CurveFitter.h"

namespace of
{
/** EQ match (docs/spec/04-analysis.md section 3): capture this track, capture or load a
    reference, and fit bands that take one to the other. */
class MatchPanel final : public juce::Component
{
public:
    MatchPanel (OpenFilterProcessor&, ResponseDisplay&);

    /** Called from the editor's timer. */
    void refresh();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateTarget();
    void loadReferenceFile (const juce::File&);
    void apply();

    OpenFilterProcessor& processor;
    ResponseDisplay& display;
    juce::TextButton sourceButton { "Capture this track" }, sidechainButton { "Capture sidechain" },
                     fileButton { "Reference file..." }, applyButton { "Apply" }, closeButton { "Close" };
    juce::ComboBox bandsBox;
    std::unique_ptr<juce::FileChooser> chooser;
    fit::Curve source {}, reference {};
    bool haveSource = false, haveReference = false;
    juce::String referenceName, status;
    double lastError = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchPanel)
};
} // namespace of
