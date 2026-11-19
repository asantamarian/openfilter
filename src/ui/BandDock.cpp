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

    dynBox.addItemList ({ "Static", "Dynamic", "Spectral" }, 1);
    dynBox.setTitle ("Dynamics mode");
    dynBox.setTooltip ("Dynamic: the band's gain moves by up to Range when the level around the band passes the threshold. "
                       "Spectral: the same, decided separately for every frequency inside the band, so one ringing note can be turned down without its neighbours. Spectral adds 2048 samples of delay.");

    resonanceButton.setClickingTogglesState (true);
    resonanceButton.setTitle ("Resonance mode");
    resonanceButton.setTooltip ("Spectral bands only. On: a frequency is treated when it sticks out of the spectrum around it, at any level; Thresh sets by how much. Off: a frequency is treated when it passes the threshold.");
    addChildComponent (resonanceButton);
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

    // Detector listen: solo, but playing what the band's detector hears (its filter, and the
    // sidechain when the band is set to it).
    listenButton.setTitle ("Listen to the detector");
    listenButton.setTooltip ("Hear what this band's detector hears: the track or the sidechain, through the detector's filter. For setting up a dynamic band.");
    listenButton.onClick = [this]
    {
        auto* listen = processor.apvts.getParameter ("sc_listen");
        const bool turningOn = ! (listen->getValue() > 0.5f && processor.soloBand() >= 0);
        listen->setValueNotifyingHost (turningOn ? 1.0f : 0.0f);
        processor.setSoloBand (turningOn ? band : -1);
    };
    addAndMakeVisible (listenButton);

    linearButton.setClickingTogglesState (true);
    linearButton.setTitle ("Linear phase for this band");
    linearButton.setTooltip ("In Mixed phase mode: realise this band with linear phase. A dynamic band still works when linear; it adds 2048 samples of delay and its gain moves in steps of about 10 ms.");
    addChildComponent (linearButton);

    pageButton.setTitle ("Switch between the filter and dynamics controls");
    pageButton.onClick = [this]
    {
        page = 1 - page;
        resized();
        refresh();
        repaint();
    };
    addChildComponent (pageButton);

    soloButton.setTooltip ("Hear only the part of the signal this band works on. Also: hold S in the display.");
    soloButton.onClick = [this]
    {
        processor.apvts.getParameter ("sc_listen")->setValueNotifyingHost (0.0f);   // plain solo, not the detector
        processor.setSoloBand (processor.soloBand() >= 0 || band < 0 ? -1 : band);
    };
    addAndMakeVisible (soloButton);

    gainDial.setBipolar (true);
    rangeDial.setBipolar (true);
    for (auto* d : { &freqDial, &gainDial, &qDial, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        addAndMakeVisible (*d);

    for (auto* d : { &freqDial, &gainDial, &qDial, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        d->setMidiLearn (&processor);

    // A multi-selection follows the primary band.
    const auto link = [this] (Dial& dial, const char* suffix, bool byRatio)
    {
        dial.onUserChange = [this, suffix, byRatio] (float before, float after) { applyToOthers (suffix, before, after, byRatio); };
    };
    link (freqDial, "freq", true);
    link (qDial, "q", true);
    link (ratioDial, "ratio", true);
    link (atkDial, "atk", true);
    link (relDial, "rel", true);
    link (gainDial, "gain", false);
    link (rangeDial, "range", false);
    link (thrDial, "thr", false);
    shapeBox.onChange = [this] { setOnOthers ("shape", static_cast<float> (shapeBox.getSelectedItemIndex())); };
    slopeBox.onChange = [this] { setOnOthers ("slope", static_cast<float> (slopeBox.getSelectedItemIndex())); };
    placeBox.onChange = [this] { setOnOthers ("place", static_cast<float> (placeBox.getSelectedItemIndex())); };
    dynBox.onChange = [this] { setOnOthers ("dyn", static_cast<float> (dynBox.getSelectedItemIndex())); };

    thrDial.textOverride = [this]
    {
        auto* p = band >= 0 ? processor.bandParam (band, "thr") : nullptr;
        if (p == nullptr)
            return juce::String();
        const float thr = p->convertFrom0to1 (p->getValue());
        if (current.dyn.spectral && current.dyn.resonance)   // how far a frequency must stick out
            return juce::String ((thr + 90.0f) / 90.0f * 24.0f, 1) + " dB out";
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

void BandDock::applyToOthers (const char* suffix, float before, float after, bool byRatio)
{
    if (! selection || band < 0 || (byRatio && std::abs (before) < 1.0e-9f))
        return;
    for (int other : selection())
    {
        if (other == band)
            continue;
        if (auto* p = processor.bandParam (other, suffix))
        {
            const auto& range = p->getNormalisableRange();
            const float now = p->convertFrom0to1 (p->getValue());
            processor.setBandValue (other, suffix, juce::jlimit (range.start, range.end, byRatio ? now * (after / before) : now + (after - before)));
        }
    }
}

void BandDock::setOnOthers (const char* suffix, float plainValue)
{
    // Lists fire when the dock is pointed at another band too; only a real pick counts.
    if (rebinding || ! selection || band < 0 || plainValue < 0.0f)
        return;
    const auto members = selection();
    if (members.size() < 2)
        return;
    for (int other : members)
        if (other != band)
            processor.setBandValue (other, suffix, plainValue);
}

BandDock::~BandDock()   // solo and detector listen never outlive the editor
{
    processor.setSoloBand (-1);
    processor.apvts.getParameter ("sc_listen")->setValueNotifyingHost (0.0f);
}

void BandDock::setBand (int newBand)
{
    band = newBand;
    if (processor.soloBand() >= 0)   // solo follows the selection
        processor.setSoloBand (band);
    rebinding = true;   // the lists are about to be pointed at another band: not a user pick
    shapeAttachment.reset();
    slopeAttachment.reset();
    placeAttachment.reset();
    dynAttachment.reset();
    autoAttachment.reset();
    linearAttachment.reset();
    resonanceAttachment.reset();

    const bool has = band >= 0;
    if (has)
    {
        shapeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "shape"), shapeBox);
        slopeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "slope"), slopeBox);
        placeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "place"), placeBox);
        dynAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "dyn"), dynBox);
        autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, params::bandId (band, "athr"), autoButton);
        linearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, params::bandId (band, "lin"), linearButton);
        resonanceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, params::bandId (band, "smode"), resonanceButton);
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

    rebinding = false;
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
        // Spectral bands swap the sidechain button for the resonance switch; in resonance
        // mode the threshold is relative by nature, so Auto does not apply.
        const bool spectralBand = dynOn && b.dyn.spectral;
        externalButton.setVisible (dynamicsShown && ! spectralBand);
        resonanceButton.setVisible (dynamicsShown && spectralBand);
        autoButton.setEnabled (dynOn && ! (spectralBand && b.dyn.resonance));
        externalButton.setEnabled (dynOn);
        externalButton.setToggleState (dynOn && b.dyn.external, juce::dontSendNotification);
        for (auto* d : { &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
            d->setDimmed (! dynOn);

        const float live = dynOn ? processor.dynamicGainDb (band) : 0.0f;   // for a spectral band: its strongest frequency
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
    const bool listening = processor.apvts.getRawParameterValue ("sc_listen")->load() > 0.5f;
    const bool soloed = band >= 0 && processor.soloBand() == band;
    soloButton.setToggleState (soloed && ! listening, juce::dontSendNotification);
    listenButton.setToggleState (soloed && listening, juce::dontSendNotification);
    listenButton.setEnabled (band >= 0 && current.dyn.on && canBeDynamic (current.shape));
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
    if (filterShown)
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

    if (filterShown)
    {
        theme::drawLabel (g, "Shape", shapeArea.withHeight (14));
        theme::drawLabel (g, "Filter", dialsArea.withHeight (14));
    }
    if (dynamicsShown)
    {
        theme::drawLabel (g, "Dynamics", dynArea.withHeight (14));
        if (! paged)
        {
            g.setColour (theme::line);
            g.fillRect (dynArea.getX() - 13, 14, 1, getHeight() - 28);
        }
    }

    // Live dynamic gain of the selected band.
    if (dynamicsShown && ! paged && band >= 0 && current.dyn.on && canBeDynamic (current.shape))   // dynamic or spectral
    {
        g.setColour (theme::band (band));
        g.setFont (theme::mono (11.0f));
        g.drawText (params::formatGain (liveDyn), dynArea.withHeight (14), juce::Justification::centredRight, false);
    }

    if (band >= 0)
    {
        g.setColour (theme::band (band));
        g.setFont (theme::ui (11.0f, true));
        g.drawText ("Band " + juce::String (band + 1), filterShown ? shapeArea.withHeight (14) : dynArea.withHeight (14).withTrimmedLeft (80).withWidth (90),
                    filterShown ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
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
    soloButton.setBounds (buttons.removeFromLeft (52));
    buttons.removeFromLeft (5);
    listenButton.setBounds (buttons.removeFromLeft (44));
    buttons.removeFromLeft (5);
    bypassButton.setBounds (buttons.removeFromLeft (70));
    buttons.removeFromLeft (5);
    deleteButton.setBounds (buttons.removeFromLeft (68));
    buttons.removeFromLeft (5);
    linearButton.setBounds (buttons.removeFromLeft (44));

    // With room for all eight dials everything shows at once. Otherwise the dock has two
    // pages: Filter (shape, slope, channel and three dials) and Dynamics (mode and five dials).
    const int needed = 3 * 56 + 24 + 96 + 8 + 5 * 56;
    paged = dialsArea.getWidth() < needed;
    const bool showFilter = ! paged || page == 0, showDynamics = ! paged || page == 1;

    pageButton.setVisible (paged);
    pageButton.setButtonText (page == 0 ? "Dynamics >" : "< Filter");
    pageButton.setBounds (getWidth() - 20 - 104, 8, 104, 22);

    for (auto* c : std::initializer_list<juce::Component*> { &shapeBox, &slopeBox, &placeBox, &freqDial, &gainDial, &qDial })
        c->setVisible (showFilter);
    for (auto* c : std::initializer_list<juce::Component*> { &dynBox, &autoButton, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        c->setVisible (showDynamics);
    dynamicsShown = showDynamics;
    filterShown = showFilter;

    // Shape, slope and channel stacked.
    auto s = shapeArea;
    s.removeFromTop (22);
    shapeBox.setBounds (s.removeFromTop (30));
    s.removeFromTop (8);
    slopeBox.setBounds (s.removeFromTop (30));
    s.removeFromTop (8);
    placeBox.setBounds (s.removeFromTop (30));

    auto d = dialsArea.withTrimmedTop (20);
    if (paged && page == 1)
        d = shapeArea.withRight (dialsArea.getRight()).withTrimmedTop (20);   // the Dynamics page takes the shape column too

    const int dialCount = paged ? (page == 0 ? 3 : 5) : 8;
    const int fixed = paged ? (page == 0 ? 0 : 96 + 8) : 24 + 96 + 8;
    const int dialW = juce::jlimit (50, 92, (d.getWidth() - fixed - 2 * dialCount) / dialCount);

    if (showFilter)
    {
        for (auto* dial : { &freqDial, &gainDial, &qDial })
        {
            dial->setBounds (d.removeFromLeft (dialW));
            d.removeFromLeft (2);
        }
        if (! paged)
            d.removeFromLeft (24);
    }
    if (showDynamics)
    {
        dynArea = d.withTop (dialsArea.getY());
        if (! paged)
            dialsArea.setRight (dynArea.getX() - 26);

        auto modeColumn = d.removeFromLeft (96);
        dynBox.setBounds (modeColumn.removeFromTop (30));
        modeColumn.removeFromTop (10);
        autoButton.setBounds (modeColumn.removeFromTop (28));
        modeColumn.removeFromTop (6);
        externalButton.setBounds (modeColumn.removeFromTop (28));
        resonanceButton.setBounds (externalButton.getBounds());
        d.removeFromLeft (8);
        for (auto* dial : { &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        {
            dial->setBounds (d.removeFromLeft (dialW));
            d.removeFromLeft (2);
        }
    }
    else
        dynArea = {};
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
