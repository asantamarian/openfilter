#include "Segmented.h"

namespace of
{
Segmented::Segmented()
{
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
}

void Segmented::setItems (const juce::StringArray& newLabels, const juce::StringArray& newNames)
{
    labels = newLabels;
    names = newNames.isEmpty() ? newLabels : newNames;
    icons.clear();
    enabledCells.assign (static_cast<size_t> (labels.size()), true);
    repaint();
}

void Segmented::setIcons (const juce::StringArray& svgPaths, const juce::StringArray& newNames)
{
    icons.clear();
    for (const auto& p : svgPaths)
        icons.push_back (juce::Drawable::parseSVGPath (p));
    names = newNames;
    labels.clear();
    for (int i = 0; i < svgPaths.size(); ++i)
        labels.add ({});
    enabledCells.assign (static_cast<size_t> (labels.size()), true);
    repaint();
}

void Segmented::setItemEnabled (int index, bool enabled)
{
    if (index >= 0 && index < static_cast<int> (enabledCells.size()) && enabledCells[static_cast<size_t> (index)] != enabled)
    {
        enabledCells[static_cast<size_t> (index)] = enabled;
        repaint();
    }
}

void Segmented::attach (juce::RangedAudioParameter* parameter, juce::UndoManager* undo)
{
    attachment.reset();
    param = parameter;
    if (param != nullptr)
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }, undo);
    repaint();
}

int Segmented::selectedIndex() const noexcept
{
    return param != nullptr ? juce::roundToInt (param->convertFrom0to1 (param->getValue())) : -1;
}

juce::Rectangle<float> Segmented::cellBounds (int index) const
{
    const auto inner = getLocalBounds().toFloat().reduced (3.0f);
    const float w = inner.getWidth() / static_cast<float> (juce::jmax (1, labels.size()));
    return { inner.getX() + w * static_cast<float> (index), inner.getY(), w, inner.getHeight() };
}

int Segmented::cellAt (juce::Point<int> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (cellBounds (i).contains (p.toFloat()))
            return i;
    return -1;
}

void Segmented::pick (int index)
{
    if (attachment == nullptr || ! isEnabled() || index < 0 || index >= labels.size() || ! enabledCells[static_cast<size_t> (index)])
        return;
    if (index != selectedIndex())
    {
        attachment->setValueAsCompleteGesture (static_cast<float> (index));
        if (onUserPick)
            onUserPick (index);
    }
    repaint();
}

void Segmented::paint (juce::Graphics& g)
{
    const float alpha = isEnabled() && param != nullptr ? 1.0f : 0.4f;
    const auto frame = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::ground);
    g.fillRoundedRectangle (frame, 8.0f);
    g.setColour (hasKeyboardFocus (false) ? theme::accent : theme::line);
    g.drawRoundedRectangle (frame, 8.0f, 1.0f);

    const int selected = selectedIndex();
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto cell = cellBounds (i).reduced (1.0f, 0.0f);
        const bool on = i == selected, usable = enabledCells[static_cast<size_t> (i)];
        if (on)
        {
            g.setColour (theme::line.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (cell, 6.0f);
        }
        else if (i == hovered && usable && alpha > 0.5f)
        {
            g.setColour (theme::line.withAlpha (0.45f));
            g.fillRoundedRectangle (cell, 6.0f);
        }

        const float cellAlpha = alpha * (usable ? 1.0f : 0.35f);
        if (static_cast<size_t> (i) < icons.size())
        {
            // The icon is defined in a 24 x 18 box; fit that box into the cell.
            const float scale = juce::jmin (cell.getWidth() / 28.0f, cell.getHeight() / 22.0f, 1.0f);
            const auto transform = juce::AffineTransform::scale (scale).translated (cell.getCentreX() - 12.0f * scale, cell.getCentreY() - 9.0f * scale);
            g.setColour ((on ? accent : theme::textMuted).withMultipliedAlpha (cellAlpha));
            g.strokePath (icons[static_cast<size_t> (i)], juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
        }
        else
        {
            g.setColour ((on ? theme::text : theme::textMuted).withMultipliedAlpha (cellAlpha));
            g.setFont (monospaced ? theme::mono (11.0f) : theme::ui (11.0f, on));
            g.drawText (labels[i], cell, juce::Justification::centred, false);
        }
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e) { pick (cellAt (e.getPosition())); }

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int h = cellAt (e.getPosition());
    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

bool Segmented::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey)
        return false;
    const int step = code == juce::KeyPress::rightKey ? 1 : -1;
    for (int i = selectedIndex() + step; i >= 0 && i < labels.size(); i += step)
        if (enabledCells[static_cast<size_t> (i)])
        {
            pick (i);
            break;
        }
    return true;
}

juce::String Segmented::getTooltip()
{
    // The cell under the pointer by name, then whatever was set for the whole control.
    const auto base = juce::SettableTooltipClient::getTooltip();
    if (hovered >= 0 && hovered < names.size() && static_cast<size_t> (hovered) < icons.size())
        return names[hovered];
    return base;
}

//==============================================================================
int TextLink::preferredWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::ui (11.0f), caption)
                           + juce::GlyphArrangement::getStringWidth (theme::mono (11.0f), value)) + 14;
}

void TextLink::paintButton (juce::Graphics& g, bool over, bool)
{
    auto area = getLocalBounds();
    const auto labelFont = theme::ui (11.0f), valueFont = theme::mono (11.0f);
    const int labelW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (labelFont, caption)) + 5;
    g.setColour (theme::textMuted);
    g.setFont (labelFont);
    g.drawText (caption, area.removeFromLeft (labelW), juce::Justification::centredLeft, false);
    g.setColour (over || hasKeyboardFocus (false) ? theme::accent : theme::text);
    g.setFont (valueFont);
    g.drawText (value, area, juce::Justification::centredLeft, false);
}

//==============================================================================
IconButton::IconButton (const juce::String& name, const juce::String& svgPath, float boxSize)
    : juce::Button (name), icon (juce::Drawable::parseSVGPath (svgPath)), box (boxSize)
{
    setTitle (name);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    g.setColour ((down ? theme::lineStrong : over ? theme::raised.brighter (0.08f) : theme::raised).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (area, 7.0f);
    g.setColour ((hasKeyboardFocus (false) ? theme::accent : over ? theme::lineStrong : theme::line).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (area, 7.0f, 1.0f);
    g.setColour ((over ? theme::text : theme::textMuted).withMultipliedAlpha (alpha));
    g.strokePath (icon, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (area.getCentreX() - 0.5f * box, area.getCentreY() - 0.5f * box));
}
} // namespace of
