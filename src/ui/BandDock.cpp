#include "BandDock.h"

namespace of
{
namespace
{
// 11 chips per row and 3 rows hold all 32 bands.
constexpr int kChipW = 26, kChipH = 22, kChipGap = 3, kChipsPerRow = 11;
}

BandDock::BandDock (OpenFilterProcessor& p) : processor (p)
{
    shapeBox.addItemList (params::shapeLabels(), 1);
    slopeBox.addItemList (params::slopeNames(), 1);
    placeBox.addItemList (params::placementNames(), 1);
    shapeBox.setTitle ("Shape");
    slopeBox.setTitle ("Slope");
    placeBox.setTitle ("Placement");
    for (auto* c : { &shapeBox, &slopeBox, &placeBox })
        addAndMakeVisible (*c);

    // Spectral is in the parameter already but not built yet.
    dynBox.addItemList ({ "Static", "Dynamic", "Spectral (not yet)" }, 1);
    dynBox.setItemEnabled (3, false);
    dynBox.setTitle ("Dynamics mode");
    dynBox.setTooltip ("Dynamic: the band's gain moves by up to Range when the level around the band passes the threshold.");
    addAndMakeVisible (dynBox);

    autoButton.setClickingTogglesState (true);
    autoButton.setTooltip ("Auto threshold follows the long-term level around the band, so the band reacts the same on quiet and loud material. The Thresh dial then sets how far above or below that level it starts.");
    addAndMakeVisible (autoButton);

    externalButton.setTooltip ("Listen to the sidechain input instead of this track. Route a signal to the plugin's sidechain in your host first; without one the band keeps listening to this track.");
    externalButton.onClick = [this]
    {
        if (band < 0)
            return;
        processor.undo.beginNewTransaction ("Detector source");
        processor.setBandValue (band, "sc", current.dyn.external ? 0.0f : 1.0f);
    };
    addAndMakeVisible (externalButton);

    linearButton.setClickingTogglesState (true);
    linearButton.setTitle ("Linear phase for this band");
    linearButton.setTooltip ("In Mixed phase mode: realise this band with linear phase. Its dynamics do not run while it is linear.");
    addChildComponent (linearButton);

    soloButton.setTooltip ("Hear only the part of the signal this band works on. Also: hold S in the display.");
    soloButton.onClick = [this] { processor.setSoloBand (processor.soloBand() >= 0 || band < 0 ? -1 : band); };
    addAndMakeVisible (soloButton);

    gainDial.setBipolar (true);
    rangeDial.setBipolar (true);
    for (auto* d : { &freqDial, &gainDial, &qDial, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        addAndMakeVisible (*d);

    thrDial.textOverride = [this]
    {
        auto* p = band >= 0 ? processor.bandParam (band, "thr") : nullptr;
        if (p == nullptr)
            return juce::String();
        const float thr = p->convertFrom0to1 (p->getValue());
        if (band >= 0 && current.dyn.autoThreshold)   // read as an offset around the program level
            return params::formatGain ((thr + 90.0f) / 90.0f * 60.0f - 30.0f) + " rel";
        return p->getCurrentValueAsText();
    };

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

BandDock::~BandDock() { processor.setSoloBand (-1); }   // solo never outlives the editor

void BandDock::setBand (int newBand)
{
    band = newBand;
    if (processor.soloBand() >= 0)   // solo follows the selection
        processor.setSoloBand (band);
    shapeAttachment.reset();
    slopeAttachment.reset();
    placeAttachment.reset();
    dynAttachment.reset();
    autoAttachment.reset();
    linearAttachment.reset();

    const bool has = band >= 0;
    if (has)
    {
        shapeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "shape"), shapeBox);
        slopeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "slope"), slopeBox);
        placeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "place"), placeBox);
        dynAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "dyn"), dynBox);
        autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, params::bandId (band, "athr"), autoButton);
        linearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, params::bandId (band, "lin"), linearButton);
    }
    const auto bind = [&] (Dial& d, const char* suffix) { d.attach (has ? processor.bandParam (band, suffix) : nullptr, &processor.undo); };
    bind (thrDial, "thr");
    bind (rangeDial, "range");
    bind (ratioDial, "ratio");
    bind (atkDial, "atk");
    bind (relDial, "rel");
    freqDial.attach (has ? processor.bandParam (band, "freq") : nullptr, &processor.undo);
    gainDial.attach (has ? processor.bandParam (band, "gain") : nullptr, &processor.undo);
    qDial.attach (has ? processor.bandParam (band, "q") : nullptr, &processor.undo);

    const auto colour = has ? theme::band (band) : theme::lineStrong;
    for (auto* d : { &freqDial, &gainDial, &qDial, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        d->setArcColour (colour);
    for (auto* c : std::initializer_list<juce::Component*> { &shapeBox, &slopeBox, &placeBox, &dynBox, &autoButton, &externalButton, &soloButton, &bypassButton, &deleteButton })
        c->setEnabled (has);

    refresh();
    repaint();
}

void BandDock::refresh()
{
    bool changed = false;

    // The per-band Linear toggle only exists in Mixed phase mode.
    const bool mixed = juce::roundToInt (processor.apvts.getRawParameterValue ("phase_mode")->load()) == 3;
    if (mixed != mixedMode)
    {
        mixedMode = mixed;
        linearButton.setVisible (mixed);
        resized();
        changed = true;
    }
    linearButton.setEnabled (band >= 0);

    for (int i = 0; i < kMaxBands; ++i)
    {
        const bool u = processor.readBand (i).used;
        changed = changed || u != used[static_cast<size_t> (i)];
        used[static_cast<size_t> (i)] = u;
    }

    if (band >= 0)
    {
        const auto b = processor.readBand (band);
        if (b.shape != current.shape || b.on != current.on || b.slope != current.slope || ! (b.dyn == current.dyn))
            changed = true;
        current = b;

        // Dynamics controls only apply to shapes that can be dynamic, and only when switched on.
        const bool canDyn = canBeDynamic (b.shape);
        const bool dynOn = canDyn && b.dyn.on;
        dynBox.setEnabled (canDyn);
        autoButton.setEnabled (dynOn);
        externalButton.setEnabled (dynOn);
        externalButton.setToggleState (dynOn && b.dyn.external, juce::dontSendNotification);
        for (auto* d : { &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
            d->setDimmed (! dynOn);

        const float live = dynOn ? processor.dynamicGainDb (band) : 0.0f;
        if (std::abs (live - liveDyn) > 0.05f)
        {
            liveDyn = live;
            repaint (dynArea.withHeight (16));
        }
        gainDial.setDimmed (! hasGain (b.shape));
        qDial.setDimmed (b.shape == Shape::FlatTilt || b.slope == Slope::Brickwall
                         || (b.slope == Slope::S6 && (isCut (b.shape) || b.shape == Shape::LowShelf || b.shape == Shape::HighShelf
                                                      || b.shape == Shape::TiltShelf || b.shape == Shape::AllPass)));
        slopeBox.setEnabled (b.shape != Shape::FlatTilt);
        bypassButton.setButtonText (b.on ? "Bypass" : "Enable");
    }
    soloButton.setToggleState (band >= 0 && processor.soloBand() == band, juce::dontSendNotification);
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
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (i == band ? c : theme::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (i == band ? c : c.withAlpha (0.85f));
        g.setFont (theme::mono (11.0f));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
    }
    theme::drawLabel (g, "Bands  " + juce::String (count) + "/" + juce::String (kMaxBands), chipsArea.withHeight (14));
    if (count == 0)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::ui (12.0f));
        g.drawText ("No bands yet", chipsArea.withTrimmedTop (22).withHeight (kChipH), juce::Justification::centredLeft, false);
    }

    theme::drawLabel (g, "Shape", shapeArea.withHeight (14));
    theme::drawLabel (g, "Filter", dialsArea.withHeight (14));
    theme::drawLabel (g, "Dynamics", dynArea.withHeight (14));
    g.setColour (theme::line);
    g.fillRect (dynArea.getX() - 13, 14, 1, getHeight() - 28);

    // Live dynamic gain of the selected band.
    if (band >= 0 && current.dyn.on && canBeDynamic (current.shape))
    {
        g.setColour (theme::band (band));
        g.setFont (theme::mono (11.0f));
        g.drawText (params::formatGain (liveDyn), dynArea.withHeight (14), juce::Justification::centredRight, false);
    }

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
    shapeArea = area.removeFromLeft (190);
    area.removeFromLeft (26);
    dialsArea = area;

    auto buttons = chipsArea.withTop (chipsArea.getBottom() - 28);
    soloButton.setBounds (buttons.removeFromLeft (60));
    buttons.removeFromLeft (6);
    bypassButton.setBounds (buttons.removeFromLeft (76));
    buttons.removeFromLeft (6);
    deleteButton.setBounds (buttons.removeFromLeft (76));
    buttons.removeFromLeft (6);
    linearButton.setBounds (buttons.removeFromLeft (48));

    // Shape, slope and channel stacked.
    auto s = shapeArea;
    s.removeFromTop (22);
    shapeBox.setBounds (s.removeFromTop (30));
    s.removeFromTop (8);
    slopeBox.setBounds (s.removeFromTop (30));
    s.removeFromTop (8);
    placeBox.setBounds (s.removeFromTop (30));

    // Filter dials on the left, dynamics on the right; dials shrink a little when space is tight.
    const int dialW = juce::jlimit (54, 92, (dialsArea.getWidth() - 24 - 96 - 8 - 16) / 8);
    auto d = dialsArea.withTrimmedTop (20);
    for (auto* dial : { &freqDial, &gainDial, &qDial })
    {
        dial->setBounds (d.removeFromLeft (dialW));
        d.removeFromLeft (2);
    }
    d.removeFromLeft (24);
    dynArea = d.withTop (dialsArea.getY());
    dialsArea.setRight (dynArea.getX() - 26);

    auto modeColumn = d.removeFromLeft (96);
    dynBox.setBounds (modeColumn.removeFromTop (30));
    modeColumn.removeFromTop (10);
    autoButton.setBounds (modeColumn.removeFromTop (28));
    modeColumn.removeFromTop (6);
    externalButton.setBounds (modeColumn.removeFromTop (28));
    d.removeFromLeft (8);
    for (auto* dial : { &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
    {
        dial->setBounds (d.removeFromLeft (dialW));
        d.removeFromLeft (2);
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
