#include "InstancePanel.h"

namespace of
{
InstancePanel::InstancePanel (OpenFilterProcessor& p, ResponseDisplay& d) : processor (p), display (d)
{
    setTitle ("Instances");
    closeButton.onClick = [this]
    {
        shown.clear();
        display.setOverlays ({});
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton);
}

void InstancePanel::showAll()
{
    refresh();
    shown.clear();
    for (const auto& e : others)
        shown.push_back (e.serial);
    refresh();
}

void InstancePanel::refresh()
{
    others.clear();
    for (auto& e : processor.hub->entries())
    {
        if (e.processor == &processor)
            self = e;
        else
            others.push_back (e);
    }
    // Forget instances that have gone away.
    shown.erase (std::remove_if (shown.begin(), shown.end(), [this] (int serial)
    {
        return std::none_of (others.begin(), others.end(), [serial] (const auto& e) { return e.serial == serial; });
    }), shown.end());

    std::vector<ResponseDisplay::Overlay> overlays;
    for (const auto& e : others)
        if (e.hasSignal && std::find (shown.begin(), shown.end(), e.serial) != shown.end())   // a silent instance has nothing to draw
            overlays.push_back ({ e.colour, e.spectrum, InstanceHub::collision (self.spectrum, e.spectrum) });
    display.setOverlays (std::move (overlays));
    repaint();
}

void InstancePanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (theme::panel.withAlpha (0.96f));
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (theme::lineStrong);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);
    theme::drawLabel (g, "Instances", getLocalBounds().reduced (14, 10).withHeight (14));

    if (others.empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::ui (12.0f));
        g.drawFittedText ("No other OpenFilter EQ in this project yet. Instances appear here when the host runs them in the same process.",
                          getLocalBounds().reduced (14, 0).withTop (40).withHeight (60), juce::Justification::topLeft, 3);
        return;
    }

    for (int row = 0; row < static_cast<int> (others.size()); ++row)
    {
        const auto& e = others[static_cast<size_t> (row)];
        const auto r = rowBounds (row);
        if (r.getBottom() > getHeight() - 8)
            break;
        const bool on = std::find (shown.begin(), shown.end(), e.serial) != shown.end();
        g.setColour (on ? theme::raised : theme::ground.withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat(), 7.0f);
        g.setColour (on ? e.colour : theme::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.0f);

        g.setColour (e.colour);
        g.fillEllipse (static_cast<float> (r.getX() + 10), static_cast<float> (r.getY() + 9), 9.0f, 9.0f);
        g.setColour (theme::text);
        g.setFont (theme::ui (12.0f, true));
        g.drawText (e.name, r.withTrimmedLeft (26).withHeight (26), juce::Justification::centredLeft, true);
        g.setColour (on ? theme::accent : theme::textFaint);
        g.setFont (theme::ui (10.0f, true));
        g.drawText (on ? "SHOWN" : "CLICK TO SHOW", r.withTrimmedRight (10).withHeight (26), juce::Justification::centredRight, false);

        // Mini spectrum, -90..0 dB with a 4.5 dB/oct tilt like the main display.
        const auto plot = r.withTrimmedTop (26).reduced (10, 4).toFloat();
        juce::Path path;
        for (int i = 0; i < fit::kGridSize; i += 2)
        {
            const double tilted = e.spectrum[static_cast<size_t> (i)] + 4.5 * std::log2 (fit::gridFrequency (i) / 1000.0);
            const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / (fit::kGridSize - 1);
            const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, static_cast<float> ((tilted + 90.0) / 90.0));
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (e.colour.withAlpha (e.hasSignal ? 0.9f : 0.3f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
}

void InstancePanel::resized() { closeButton.setBounds (getWidth() - 74, 8, 64, 24); }

void InstancePanel::mouseDown (const juce::MouseEvent& e)
{
    for (int row = 0; row < static_cast<int> (others.size()); ++row)
        if (rowBounds (row).contains (e.getPosition()))
        {
            const int serial = others[static_cast<size_t> (row)].serial;
            const auto it = std::find (shown.begin(), shown.end(), serial);
            if (it != shown.end()) shown.erase (it); else shown.push_back (serial);
            refresh();
            return;
        }
}
} // namespace of
