#include "Dial.h"

namespace of
{
Dial::Dial (juce::String labelText) : label (std::move (labelText))
{
    setRepaintsOnMouseActivity (false);
    setWantsKeyboardFocus (false);
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

    g.setColour (theme::textMuted.withMultipliedAlpha (alpha));
    g.setFont (theme::ui (10.0f, true).withExtraKerningFactor (0.08f));
    g.drawText (label.toUpperCase(), labelArea, juce::Justification::centred, false);
}

void Dial::setNormalised (float value)
{
    if (attachment != nullptr && param != nullptr)
        attachment->setValueAsPartOfGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value)));
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
        attachment->setValueAsCompleteGesture (juce::jlimit (range.start, range.end, plain));
    }
    repaint();
}

void Dial::mouseDown (const juce::MouseEvent& e)
{
    if (attachment == nullptr || dimmed)
        return;
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
        attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void Dial::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (attachment == nullptr || dimmed || dragging)
        return;
    const float step = (e.mods.isShiftDown() ? 0.02f : 0.1f) * wheel.deltaY;
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param->getValue() + step)));
}
} // namespace of
