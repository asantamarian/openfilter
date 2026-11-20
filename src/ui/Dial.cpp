#include "Dial.h"

namespace of
{
Dial::Dial (juce::String labelText) : label (std::move (labelText))
{
    setRepaintsOnMouseActivity (false);
    setWantsKeyboardFocus (true);   // reachable with Tab; arrows change it, Enter types a value
}

void Dial::attach (juce::RangedAudioParameter* parameter, juce::UndoManager* undo)
{
    valueEditor.reset();
    attachment.reset();
    param = parameter;
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }, undo);
        setTitle (param->getName (64));
    }
    repaint();
}

void Dial::paint (juce::Graphics& g)
{
    const float alpha = (dimmed || param == nullptr) ? 0.35f : 1.0f;
    auto area = getLocalBounds();
    auto labelArea = area.removeFromBottom (14);
    auto valueArea = area.removeFromBottom (18);

    const float size = static_cast<float> (juce::jmin (area.getWidth(), area.getHeight())) - 6.0f;
    const auto centre = area.toFloat().getCentre();
    const float radius = 0.5f * size - 2.0f;
    const float start = -0.75f * juce::MathConstants<float>::pi, end = 0.75f * juce::MathConstants<float>::pi;
    const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (theme::line.withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);

    if (param != nullptr)
    {
        const float v = param->getValue();
        const float from = bipolar ? start + (end - start) * juce::jmin (v, 0.5f) : start;
        const float to = bipolar ? start + (end - start) * juce::jmax (v, 0.5f) : start + (end - start) * v;
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, juce::jmax (to, from + 0.02f), true);
        theme::strokeGlow (g, value, colour.withMultipliedAlpha (alpha), 4.0f, alpha);

        g.setColour (theme::text.withMultipliedAlpha (alpha));
        g.setFont (theme::mono (12.0f));
        g.drawText (textOverride ? textOverride() : param->getCurrentValueAsText(), valueArea, juce::Justification::centred, false);
    }

    // MIDI learn: armed and waiting, or the controller it is mapped to.
    juce::String caption = label.toUpperCase();
    auto captionColour = theme::textMuted;
    if (midiLearn != nullptr && param != nullptr)
    {
        if (midiLearn->isLearning (*param))
        {
            caption = "MOVE A KNOB";
            captionColour = theme::warn;
        }
        else if (const int cc = midiLearn->mappedController (*param); cc >= 0)
            caption = label.toUpperCase() + " CC" + juce::String (cc);
    }
    g.setColour (captionColour.withMultipliedAlpha (alpha));
    g.setFont (theme::ui (10.0f, true).withExtraKerningFactor (caption.length() > 9 ? 0.0f : 0.08f));
    g.drawText (caption, labelArea.expanded (8, 0), juce::Justification::centred, false);

    if (hasKeyboardFocus (false))   // visible focus
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 1.5f);
    }
}

void Dial::setFromUser (float plainValue, bool partOfGesture)
{
    if (attachment == nullptr || param == nullptr)
        return;
    const float before = param->convertFrom0to1 (param->getValue());
    if (partOfGesture) attachment->setValueAsPartOfGesture (plainValue);
    else               attachment->setValueAsCompleteGesture (plainValue);
    const float after = param->convertFrom0to1 (param->getValue());
    if (onUserChange && ! juce::exactlyEqual (before, after))
        onUserChange (before, after);
}

void Dial::setNormalised (float value)
{
    if (attachment != nullptr && param != nullptr)
        setFromUser (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value)), true);
}

void Dial::showValueEditor()
{
    valueEditor = std::make_unique<juce::TextEditor>();
    valueEditor->setFont (theme::mono (12.0f));
    valueEditor->setJustification (juce::Justification::centred);
    valueEditor->setColour (juce::TextEditor::backgroundColourId, theme::ground);
    valueEditor->setColour (juce::TextEditor::textColourId, theme::text);
    valueEditor->setColour (juce::TextEditor::outlineColourId, theme::lineStrong);
    valueEditor->setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    valueEditor->setColour (juce::TextEditor::highlightColourId, theme::lineStrong);
    valueEditor->setText (param->getCurrentValueAsText(), false);
    valueEditor->setBounds (valueArea().expanded (0, 2));
    valueEditor->setTitle (param->getName (64));
    addAndMakeVisible (*valueEditor);
    valueEditor->selectAll();
    valueEditor->grabKeyboardFocus();

    // The editor must not be destroyed from inside its own callback.
    juce::Component::SafePointer<Dial> safe (this);
    const auto close = [safe] (bool apply)
    {
        juce::MessageManager::callAsync ([safe, apply] { if (safe != nullptr) safe->closeValueEditor (apply); });
    };
    valueEditor->onReturnKey = [close] { close (true); };
    valueEditor->onEscapeKey = [close] { close (false); };
    valueEditor->onFocusLost = [close] { close (true); };
}

void Dial::closeValueEditor (bool apply)
{
    if (valueEditor == nullptr)
        return;
    const auto text = valueEditor->getText();
    valueEditor.reset();
    if (apply && attachment != nullptr && param != nullptr && text.trim().isNotEmpty() && text != param->getCurrentValueAsText())
    {
        const auto& range = param->getNormalisableRange();
        const float plain = param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param->getValueForText (text)));
        setFromUser (juce::jlimit (range.start, range.end, plain), false);
    }
    repaint();
}

void Dial::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Type a value...");
    menu.addItem (2, "Reset to default");
    if (midiLearn != nullptr)
    {
        const int cc = midiLearn->mappedController (*param);
        menu.addSeparator();
        menu.addItem (3, midiLearn->isLearning (*param) ? "Cancel MIDI learn" : "Learn MIDI controller");
        menu.addItem (4, cc >= 0 ? "Clear MIDI mapping (CC " + juce::String (cc) + ")" : juce::String ("Clear MIDI mapping"), cc >= 0);
    }
    juce::Component::SafePointer<Dial> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withParentComponent (getTopLevelComponent()), [safe] (int result)
    {
        if (safe == nullptr || safe->param == nullptr || safe->attachment == nullptr)
            return;
        if (result == 1) safe->showValueEditor();
        else if (result == 2) safe->setFromUser (safe->param->convertFrom0to1 (safe->param->getDefaultValue()), false);
        else if (result == 3)
        {
            if (safe->midiLearn->isLearning (*safe->param)) safe->midiLearn->cancelMidiLearn();
            else safe->midiLearn->beginMidiLearn (*safe->param);
        }
        else if (result == 4) safe->midiLearn->clearMidiMapping (*safe->param);
        safe->repaint();
    });
}

void Dial::nudge (float normalisedStep)
{
    if (attachment != nullptr && param != nullptr && ! dimmed)
        setFromUser (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param->getValue() + normalisedStep)), false);
}

bool Dial::keyPressed (const juce::KeyPress& key)
{
    if (attachment == nullptr || dimmed)
        return false;
    const float step = key.getModifiers().isShiftDown() ? 0.002f : 0.02f;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)    { nudge (step); return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)   { nudge (-step); return true; }
    if (code == juce::KeyPress::pageUpKey)                                    { nudge (0.1f); return true; }
    if (code == juce::KeyPress::pageDownKey)                                  { nudge (-0.1f); return true; }
    if (code == juce::KeyPress::returnKey)                                    { showValueEditor(); return true; }
    if (code == juce::KeyPress::homeKey || code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        setFromUser (param->convertFrom0to1 (param->getDefaultValue()), false);
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> Dial::createAccessibilityHandler()
{
    // A slider to assistive technology: name from the title, value with its unit, settable.
    struct Value final : juce::AccessibilityValueInterface   // the general one: we supply the text, with units
    {
        explicit Value (Dial& d) : dial (d) {}
        bool isReadOnly() const override { return dial.param == nullptr || dial.attachment == nullptr || dial.dimmed; }
        double getCurrentValue() const override { return dial.param != nullptr ? dial.param->convertFrom0to1 (dial.param->getValue()) : 0.0; }
        juce::String getCurrentValueAsString() const override
        {
            if (dial.param == nullptr)
                return {};
            return dial.textOverride ? dial.textOverride() : dial.param->getCurrentValueAsText();
        }
        void setValue (double v) override
        {
            if (! isReadOnly())
            {
                const auto& r = dial.param->getNormalisableRange();
                dial.setFromUser (juce::jlimit (r.start, r.end, static_cast<float> (v)), false);
            }
        }
        void setValueAsString (const juce::String& text) override
        {
            if (! isReadOnly())
                dial.setFromUser (dial.param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, dial.param->getValueForText (text))), false);
        }
        AccessibleValueRange getRange() const override
        {
            if (dial.param == nullptr)
                return {};
            const auto& r = dial.param->getNormalisableRange();
            return { { static_cast<double> (r.start), static_cast<double> (r.end) }, static_cast<double> (r.end - r.start) / 100.0 };
        }
        Dial& dial;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

void Dial::mouseDown (const juce::MouseEvent& e)
{
    if (attachment == nullptr || dimmed)
        return;
    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }
    if (midiLearn != nullptr && midiLearn->midiLearnMode())   // MIDI learn mode: a click arms this dial
    {
        if (midiLearn->isLearning (*param)) midiLearn->cancelMidiLearn();
        else                                midiLearn->beginMidiLearn (*param);
        repaint();
        return;
    }
    if (valueArea().contains (e.getPosition()))   // click the value to type one
    {
        showValueEditor();
        return;
    }
    dragging = true;
    dragStart = param->getValue();
    attachment->beginGesture();
}

void Dial::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float sensitivity = e.mods.isShiftDown() ? 0.0005f : 0.005f;
    setNormalised (dragStart - static_cast<float> (e.getDistanceFromDragStartY()) * sensitivity);
}

void Dial::mouseUp (const juce::MouseEvent&)
{
    if (dragging && attachment != nullptr)
        attachment->endGesture();
    dragging = false;
}

void Dial::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (attachment != nullptr && ! dimmed && ! valueArea().contains (e.getPosition()))
        setFromUser (param->convertFrom0to1 (param->getDefaultValue()), false);
}

void Dial::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (attachment == nullptr || dimmed || dragging)
        return;
    const float step = (e.mods.isShiftDown() ? 0.02f : 0.1f) * wheel.deltaY;
    setFromUser (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param->getValue() + step)), false);
}
} // namespace of
