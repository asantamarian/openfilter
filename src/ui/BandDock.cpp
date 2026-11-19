#include "BandDock.h"

namespace of
{
namespace
{
constexpr int kChipW = 38, kChipH = 28, kChipGap = 6, kChipsPerRow = 8;
}

BandDock::BandDock (OpenFilterProcessor& p) : processor (p)
{
    shapeBox.addItemList (params::shapeNames(), 1);
    slopeBox.addItemList (params::slopeNames(), 1);
    placeBox.addItemList (params::placementNames(), 1);
    shapeBox.setTitle ("Shape");
    slopeBox.setTitle ("Slope");
    placeBox.setTitle ("Placement");
    for (auto* c : { &shapeBox, &slopeBox, &placeBox })
        addAndMakeVisible (*c);

    gainDial.setBipolar (true);
    for (auto* d : { &freqDial, &gainDial, &qDial })
        addAndMakeVisible (*d);

    bypassButton.setClickingTogglesState (false);
    bypassButton.onClick = [this]
    {
        if (band < 0)
            return;
        processor.undo.beginNewTransaction ("Toggle band");
        processor.setBandValue (band, "on", current.on ? 0.0f : 1.0f);
    };
    deleteButton.onClick = [this]
    {
        if (band >= 0)
            processor.removeBand (band);
    };
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (deleteButton);

    setBand (-1);
}

void BandDock::setBand (int newBand)
{
    band = newBand;
    shapeAttachment.reset();
    slopeAttachment.reset();
    placeAttachment.reset();

    const bool has = band >= 0;
    if (has)
    {
        shapeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "shape"), shapeBox);
        slopeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "slope"), slopeBox);
        placeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "place"), placeBox);
    }
    freqDial.attach (has ? processor.bandParam (band, "freq") : nullptr, &processor.undo);
    gainDial.attach (has ? processor.bandParam (band, "gain") : nullptr, &processor.undo);
    qDial.attach (has ? processor.bandParam (band, "q") : nullptr, &processor.undo);

    const auto colour = has ? theme::band (band) : theme::lineStrong;
    for (auto* d : { &freqDial, &gainDial, &qDial })
        d->setArcColour (colour);
    for (auto* c : std::initializer_list<juce::Component*> { &shapeBox, &slopeBox, &placeBox, &bypassButton, &deleteButton })
        c->setEnabled (has);

    refresh();
    repaint();
}

void BandDock::refresh()
{
    bool changed = false;
    for (int i = 0; i < kMaxBands; ++i)
    {
        const bool u = processor.readBand (i).used;
        changed = changed || u != used[static_cast<size_t> (i)];
        used[static_cast<size_t> (i)] = u;
    }

    if (band >= 0)
    {
        const auto b = processor.readBand (band);
        if (b.shape != current.shape || b.on != current.on || b.slope != current.slope)
            changed = true;
        current = b;
        gainDial.setDimmed (! hasGain (b.shape));
        qDial.setDimmed (b.shape == Shape::FlatTilt || b.slope == Slope::Brickwall
                         || (b.slope == Slope::S6 && (isCut (b.shape) || b.shape == Shape::LowShelf || b.shape == Shape::HighShelf
                                                      || b.shape == Shape::TiltShelf || b.shape == Shape::AllPass)));
        slopeBox.setEnabled (b.shape != Shape::FlatTilt);
        bypassButton.setButtonText (b.on ? "Bypass" : "Enable");
    }
    if (changed)
        repaint();
}

juce::Rectangle<int> BandDock::chipBounds (int index) const
{
    return { chipsArea.getX() + (index % kChipsPerRow) * (kChipW + kChipGap),
             chipsArea.getY() + 22 + (index / kChipsPerRow) * (kChipH + kChipGap), kChipW, kChipH };
}

void BandDock::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (shapeArea.getX() - 13, 14, 1, getHeight() - 28);
    g.fillRect (dialsArea.getX() - 13, 14, 1, getHeight() - 28);

    // Band chips, in slot order.
    int count = 0, index = 0;
    for (int i = 0; i < kMaxBands; ++i)
    {
        if (! used[static_cast<size_t> (i)])
            continue;
        ++count;
        const auto r = chipBounds (index++).toFloat();
        const auto c = theme::band (i);
        g.setColour (i == band ? theme::raised : theme::panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (i == band ? c : theme::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (c);
        g.fillEllipse (r.getX() + 7.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour (theme::text);
        g.setFont (theme::mono (11.0f));
        g.drawText (juce::String (i + 1), r.withTrimmedLeft (16.0f), juce::Justification::centred, false);
    }
    theme::drawLabel (g, "Bands  " + juce::String (count) + "/" + juce::String (kMaxBands), chipsArea.withHeight (14));
    if (count == 0)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::ui (12.0f));
        g.drawText ("No bands yet", chipsArea.withTrimmedTop (22).withHeight (kChipH), juce::Justification::centredLeft, false);
    }

    theme::drawLabel (g, "Shape", shapeArea.withHeight (14));
    theme::drawLabel (g, "Slope", juce::Rectangle<int> (slopeBox.getX(), slopeBox.getY() - 20, 100, 14));
    theme::drawLabel (g, "Channel", juce::Rectangle<int> (placeBox.getX(), placeBox.getY() - 20, 100, 14));
    theme::drawLabel (g, "Filter", dialsArea.withHeight (14));

    if (band >= 0)
    {
        g.setColour (theme::band (band));
        g.setFont (theme::ui (11.0f, true));
        g.drawText ("Band " + juce::String (band + 1), shapeArea.withHeight (14), juce::Justification::centredRight, false);
    }
}

void BandDock::resized()
{
    auto area = getLocalBounds().reduced (20, 14);

    chipsArea = area.removeFromLeft (kChipsPerRow * (kChipW + kChipGap) - kChipGap);
    area.removeFromLeft (26);
    shapeArea = area.removeFromLeft (300);
    area.removeFromLeft (26);
    dialsArea = area;

    auto buttons = chipsArea.withTop (chipsArea.getBottom() - 28);
    bypassButton.setBounds (buttons.removeFromLeft (84));
    buttons.removeFromLeft (6);
    deleteButton.setBounds (buttons.removeFromLeft (84));

    auto s = shapeArea;
    s.removeFromTop (22);
    shapeBox.setBounds (s.removeFromTop (30));
    auto lower = shapeArea.withTop (shapeArea.getBottom() - 30);
    slopeBox.setBounds (lower.removeFromLeft (144));
    lower.removeFromLeft (12);
    placeBox.setBounds (lower);

    auto d = dialsArea.withTrimmedTop (20);
    for (auto* dial : { &freqDial, &gainDial, &qDial })
    {
        dial->setBounds (d.removeFromLeft (96));
        d.removeFromLeft (4);
    }
}

void BandDock::mouseDown (const juce::MouseEvent& e)
{
    int index = 0;
    for (int i = 0; i < kMaxBands; ++i)
    {
        if (! used[static_cast<size_t> (i)])
            continue;
        if (chipBounds (index++).contains (e.getPosition()))
        {
            if (onSelectBand)
                onSelectBand (i);
            return;
        }
    }
}
} // namespace of
