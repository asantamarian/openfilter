#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace of
{
/** A row of mutually exclusive cells, as text or icons, bound to a choice parameter. Used for
    the shape picker, the channel placement and the dynamics mode. Left and right arrows move
    the selection when it has keyboard focus. */
class Segmented final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented();

    /** Text cells. names are what a screen reader and the tooltip say for each cell. */
    void setItems (const juce::StringArray& labels, const juce::StringArray& names = {});
    /** Icon cells: one SVG path per cell, drawn in a 24 x 18 box. */
    void setIcons (const juce::StringArray& svgPaths, const juce::StringArray& names);
    void setMonospaced (bool mono) { monospaced = mono; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setItemEnabled (int index, bool enabled);

    /** Binds to a choice parameter, or to none. */
    void attach (juce::RangedAudioParameter* parameter, juce::UndoManager* undo);
    int selectedIndex() const noexcept;

    /** Called when the user picks a cell (not when the parameter changes by itself). */
    std::function<void (int)> onUserPick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    juce::String getTooltip() override;

private:
    juce::Rectangle<float> cellBounds (int index) const;
    int cellAt (juce::Point<int>) const;
    void pick (int index);

    juce::StringArray labels, names;
    std::vector<juce::Path> icons;
    std::vector<bool> enabledCells;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::Colour accent { theme::text };
    bool monospaced = false;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
};

/** A flat "Label value" button for the status bar: the value is what a click changes. */
class TextLink final : public juce::Button
{
public:
    explicit TextLink (const juce::String& label) : juce::Button (label), caption (label) { setTitle (label); }
    void setValueText (const juce::String& text) { if (text != value) { value = text; repaint(); } }
    int preferredWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String caption, value;
};

/** A small button that shows an icon instead of text. */
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, const juce::String& svgPath, float boxSize = 13.0f);
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::Path icon;
    float box;
};
} // namespace of
