#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace of
{
/** What a control needs from the plugin to offer MIDI learn. */
struct MidiLearnTarget
{
    virtual ~MidiLearnTarget() = default;
    virtual void beginMidiLearn (juce::RangedAudioParameter&) = 0;
    virtual void cancelMidiLearn() = 0;
    virtual bool isLearning (const juce::RangedAudioParameter&) const = 0;
    /** The controller number mapped to the parameter, or -1. */
    virtual int mappedController (const juce::RangedAudioParameter&) const = 0;
    virtual void clearMidiMapping (juce::RangedAudioParameter&) = 0;
};

/** A 270 degree arc dial bound to one parameter (docs/spec/05-ui.md section 5).
    Drag vertically, scroll, double-click to reset. Shift = fine. */
class Dial final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Dial (juce::String labelText);

    /** Binds to a parameter, or to none (nullptr), which dims the dial. */
    void attach (juce::RangedAudioParameter* parameter, juce::UndoManager* undo);
    void setArcColour (juce::Colour c) { colour = c; repaint(); }
    void setBipolar (bool b) { bipolar = b; repaint(); }
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    /** Replaces the value text, for a parameter that is read differently in some mode. */
    std::function<juce::String()> textOverride;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** Enables "Learn MIDI controller" in the right-click menu. */
    void setMidiLearn (MidiLearnTarget* target) { midiLearn = target; }

private:
    void setNormalised (float value);
    juce::Rectangle<int> valueArea() const { return getLocalBounds().removeFromBottom (32).removeFromTop (18); }
    void showValueEditor();
    void closeValueEditor (bool apply);

    juce::String label;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::Colour colour { theme::accent };
    bool bipolar = false, dimmed = false, dragging = false;
    float dragStart = 0.0f;
    std::unique_ptr<juce::TextEditor> valueEditor;
    MidiLearnTarget* midiLearn = nullptr;
    void showContextMenu();
    void nudge (float normalisedStep);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dial)
};
} // namespace of
