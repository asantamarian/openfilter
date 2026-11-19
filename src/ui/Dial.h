#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace of
{
/** A 270 degree arc dial bound to one parameter (docs/spec/05-ui.md section 5).
    Drag vertically, scroll, double-click to reset. Shift = fine. */
class Dial final : public juce::Component
{
public:
    explicit Dial (juce::String labelText);

    /** Binds to a parameter, or to none (nullptr), which dims the dial. */
    void attach (juce::RangedAudioParameter* parameter, juce::UndoManager* undo);
    void setArcColour (juce::Colour c) { colour = c; repaint(); }
    void setBipolar (bool b) { bipolar = b; repaint(); }
    void setDimmed (bool d) { dimmed = d; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void setNormalised (float value);

    juce::String label;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::Colour colour { theme::accent };
    bool bipolar = false, dimmed = false, dragging = false;
    float dragStart = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dial)
};
} // namespace of
