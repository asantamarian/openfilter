#include "BandDock.h"

namespace of
{
namespace
{
constexpr int kPadX = 20, kPadY = 14, kColumnGap = 24;
constexpr int kChipsWidth = 200, kShapeWidth = 332;

juce::String middleDot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
} // namespace

BandDock::BandDock (OpenFilterProcessor& p) : processor (p)
{
    // Shape: one icon per shape, in parameter order.
    shapePicker.setIcons ({ "M1 13H7C10 13 10 4 12 4C14 4 14 13 17 13H23",                     // bell
                            "M1 5H8C12 5 12 13 16 13H23",                                       // low shelf
                            "M1 13H8C12 13 12 5 16 5H23",                                       // high shelf
                            "M3 16C5 7 7 6 11 6H23",                                            // low cut
                            "M1 6H13C17 6 19 7 21 16",                                          // high cut
                            "M1 5H8C11 5 11 15 12 15C13 15 13 5 16 5H23",                       // notch
                            "M3 16C8 16 9 4 12 4C15 4 16 16 21 16",                             // band pass
                            "M1 13H6L18 5H23",                                                  // tilt shelf
                            "M1 14L23 4",                                                       // flat tilt
                            "M1 9H8M16 9H23M8.5 9a3.5 3.5 0 1 0 7 0a3.5 3.5 0 1 0 -7 0" },      // all pass
                          params::shapeLabels());
    shapePicker.setTitle ("Shape");
    shapePicker.onUserPick = [this] (int index) { setOnOthers ("shape", static_cast<float> (index)); };

    channelPicker.setItems ({ "ST", "L", "R", "M", "S" }, params::placementNames());
    channelPicker.setMonospaced (true);
    channelPicker.setTitle ("Channel");
    channelPicker.setTooltip ("Where the band acts: the whole stereo signal, left, right, mid (what both sides share) or side (what differs).");
    channelPicker.onUserPick = [this] (int index) { setOnOthers ("place", static_cast<float> (index)); };

    modePicker.setItems ({ "Static", "Dynamic", "Spectral" });
    modePicker.setTitle ("Dynamics mode");
    modePicker.setTooltip ("Dynamic: the band's gain moves by up to Range when the level around the band passes the threshold. "
                           "Spectral: the same, decided separately for every frequency inside the band, so one ringing note can be turned down without its neighbours. Spectral adds 2048 samples of delay.");
    modePicker.onUserPick = [this] (int index) { setOnOthers ("dyn", static_cast<float> (index)); };

    slopeBox.addItemList (params::slopeNames(), 1);
    slopeBox.setTitle ("Slope");
    slopeBox.onChange = [this] { setOnOthers ("slope", static_cast<float> (slopeBox.getSelectedItemIndex())); };

    for (auto* c : std::initializer_list<juce::Component*> { &shapePicker, &channelPicker, &modePicker, &slopeBox })
        addAndMakeVisible (*c);

    autoButton.setClickingTogglesState (true);
    autoButton.setTooltip ("Auto threshold follows the long-term level around the band, so the band reacts the same on quiet and loud material. The Threshold dial then sets how far above or below that level it starts.");
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

    resonanceButton.setClickingTogglesState (true);
    resonanceButton.setTitle ("Resonance mode");
    resonanceButton.setTooltip ("Spectral bands only. On: a frequency is treated when it sticks out of the spectrum around it, at any level; Threshold sets by how much. Off: a frequency is treated when it passes the threshold.");
    addChildComponent (resonanceButton);

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
    pageButton.onClick = [this] { showPage (1 - page); };
    addChildComponent (pageButton);

    soloButton.setTooltip ("Hear only the part of the signal this band works on. Also: hold S in the display.");
    soloButton.onClick = [this]
    {
        processor.apvts.getParameter ("sc_listen")->setValueNotifyingHost (0.0f);   // plain solo, not the detector
        processor.setSoloBand (processor.soloBand() >= 0 || band < 0 ? -1 : band);
    };
    addAndMakeVisible (soloButton);

    bypassButton.onClick = [this]
    {
        if (band < 0)
            return;
        processor.undo.beginNewTransaction ("Toggle band");
        processor.setBandValue (band, "on", current.on ? 0.0f : 1.0f);
    };
    deleteButton.setTooltip ("Delete this band.");
    deleteButton.onClick = [this]
    {
        if (band >= 0)
            processor.removeBand (band);
    };
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (deleteButton);

    gainDial.setBipolar (true);
    rangeDial.setBipolar (true);
    for (auto* d : { &freqDial, &gainDial, &qDial, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
    {
        addAndMakeVisible (*d);
        d->setMidiLearn (&processor);
    }

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

    thrDial.textOverride = [this]
    {
        auto* param = band >= 0 ? processor.bandParam (band, "thr") : nullptr;
        if (param == nullptr)
            return juce::String();
        const float thr = param->convertFrom0to1 (param->getValue());
        if (current.dyn.spectral && current.dyn.resonance)   // how far a frequency must stick out
            return juce::String ((thr + 90.0f) / 90.0f * 24.0f, 1) + " dB out";
        if (current.dyn.autoThreshold)   // read as an offset around the programme level
        {
            const float offset = (thr + 90.0f) / 90.0f * 60.0f - 30.0f;
            return std::abs (offset - 10.0f) < 0.05f ? juce::String ("Auto") : "Auto " + params::formatGain (offset).upToFirstOccurrenceOf (" ", false, false);
        }
        return param->getCurrentValueAsText();
    };

    setBand (-1);
}

BandDock::~BandDock()   // solo and detector listen never outlive the editor
{
    processor.setSoloBand (-1);
    processor.apvts.getParameter ("sc_listen")->setValueNotifyingHost (0.0f);
}

void BandDock::applyToOthers (const char* suffix, float before, float after, bool byRatio)
{
    if (! selection || band < 0 || (byRatio && std::abs (before) < 1.0e-9f))
        return;
    for (int other : selection())
    {
        if (other == band)
            continue;
        if (auto* param = processor.bandParam (other, suffix))
        {
            const auto& range = param->getNormalisableRange();
            const float now = param->convertFrom0to1 (param->getValue());
            processor.setBandValue (other, suffix, juce::jlimit (range.start, range.end, byRatio ? now * (after / before) : now + (after - before)));
        }
    }
}

void BandDock::setOnOthers (const char* suffix, float plainValue)
{
    // The slope list also fires when the dock is pointed at another band; only a real pick counts.
    if (rebinding || ! selection || band < 0 || plainValue < 0.0f)
        return;
    const auto members = selection();
    if (members.size() < 2)
        return;
    for (int other : members)
        if (other != band)
            processor.setBandValue (other, suffix, plainValue);
}

void BandDock::showPage (int newPage)
{
    page = juce::jlimit (0, 1, newPage);
    resized();
    refresh();
    repaint();
}

void BandDock::setBand (int newBand)
{
    band = newBand;
    if (processor.soloBand() >= 0)   // solo follows the selection
        processor.setSoloBand (band);

    rebinding = true;   // controls are about to be pointed at another band: not a user pick
    slopeAttachment.reset();
    autoAttachment.reset();
    linearAttachment.reset();
    resonanceAttachment.reset();

    const bool has = band >= 0;
    const auto param = [&] (const char* suffix) { return has ? processor.bandParam (band, suffix) : nullptr; };
    if (has)
    {
        slopeAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "slope"), slopeBox);
        autoAttachment = std::make_unique<ButtonAttachment> (processor.apvts, params::bandId (band, "athr"), autoButton);
        linearAttachment = std::make_unique<ButtonAttachment> (processor.apvts, params::bandId (band, "lin"), linearButton);
        resonanceAttachment = std::make_unique<ButtonAttachment> (processor.apvts, params::bandId (band, "smode"), resonanceButton);
    }
    shapePicker.attach (param ("shape"), &processor.undo);
    channelPicker.attach (param ("place"), &processor.undo);
    modePicker.attach (param ("dyn"), &processor.undo);
    freqDial.attach (param ("freq"), &processor.undo);
    gainDial.attach (param ("gain"), &processor.undo);
    qDial.attach (param ("q"), &processor.undo);
    thrDial.attach (param ("thr"), &processor.undo);
    rangeDial.attach (param ("range"), &processor.undo);
    ratioDial.attach (param ("ratio"), &processor.undo);
    atkDial.attach (param ("atk"), &processor.undo);
    relDial.attach (param ("rel"), &processor.undo);

    const auto colour = has ? theme::band (band) : theme::lineStrong;
    for (auto* d : { &freqDial, &gainDial, &qDial, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        d->setArcColour (colour);
    shapePicker.setAccent (colour);
    for (auto* c : std::initializer_list<juce::Component*> { &shapePicker, &channelPicker, &modePicker, &slopeBox, &autoButton, &externalButton,
                                                             &soloButton, &bypassButton, &deleteButton })
        c->setEnabled (has);

    rebinding = false;
    refresh();
    repaint();
}

int BandDock::usedCount() const noexcept { return static_cast<int> (std::count (used.begin(), used.end(), true)); }

void BandDock::refresh()
{
    bool changed = false;

    // The per-band Linear toggle only exists in Mixed phase mode.
    const bool mixed = juce::roundToInt (processor.apvts.getRawParameterValue ("phase_mode")->load()) == 3;
    if (mixed != mixedMode)
    {
        mixedMode = mixed;
        resized();
        changed = true;
    }
    linearButton.setEnabled (band >= 0);

    const int before = usedCount();
    for (int i = 0; i < kMaxBands; ++i)
    {
        const bool u = processor.readBand (i).used;
        changed = changed || u != used[static_cast<size_t> (i)];
        used[static_cast<size_t> (i)] = u;
    }
    if (usedCount() != before)
        resized();   // the chips may change size

    if (band >= 0)
    {
        const auto b = processor.readBand (band);
        if (b.shape != current.shape || b.on != current.on || b.slope != current.slope || b.place != current.place || ! (b.dyn == current.dyn))
            changed = true;
        current = b;

        // Dynamics controls only apply to shapes that can be dynamic, and only when switched on.
        const bool canDyn = canBeDynamic (b.shape);
        const bool dynOn = canDyn && b.dyn.on;
        modePicker.setEnabled (canDyn);
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
            repaint (liveArea);
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

//==============================================================================
BandDock::ChipLayout BandDock::chipLayout (int count) const noexcept
{
    // The add chip takes one place while there is a free slot.
    const int cells = count + (count < kMaxBands ? 1 : 0);
    if (cells <= 8)   return { 38, 32, 6, 4, true };     // as in the mockup: number and colour dot
    if (cells <= 18)  return { 28, 24, 4, 6, false };
    return { 22, 18, 3, 8, false };                       // four rows of eight hold all 32
}

juce::Rectangle<int> BandDock::chipBounds (int index, const ChipLayout& l) const
{
    return { chipsArea.getX() + (index % l.perRow) * (l.w + l.gap), chipsArea.getY() + 24 + (index / l.perRow) * (l.h + l.gap), l.w, l.h };
}

void BandDock::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.fillRect (0, 0, getWidth(), 1);
    const auto divider = [&] (int x) { g.fillRect (x, kPadY, 1, getHeight() - 2 * kPadY); };
    divider (chipsArea.getRight() + kColumnGap);
    if (filterShown && ! filterArea.isEmpty())
        divider (shapeArea.getRight() + kColumnGap);

    // Band chips, in slot order, then the add chip.
    const int count = usedCount();
    const auto layout = chipLayout (count);
    int index = 0;
    for (int i = 0; i < kMaxBands; ++i)
    {
        if (! used[static_cast<size_t> (i)])
            continue;
        const auto r = chipBounds (index++, layout).toFloat();
        const auto c = theme::band (i);
        const float radius = layout.dots ? 7.0f : 5.0f;
        g.setColour (i == band ? theme::raised : theme::panel);
        g.fillRoundedRectangle (r, radius);
        g.setColour (i == band ? c : theme::line);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        if (layout.dots)
        {
            g.setColour (c);
            g.fillEllipse (r.getX() + 8.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
            g.setColour (theme::text);
            g.setFont (theme::mono (11.0f));
            g.drawText (juce::String (i + 1), r.withTrimmedLeft (17.0f), juce::Justification::centred, false);
        }
        else
        {
            g.setColour (i == band ? c : c.withAlpha (0.85f));
            g.setFont (theme::mono (layout.h < 20 ? 10.0f : 11.0f));
            g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
        }
    }
    if (! addChip.isEmpty())
    {
        const auto r = addChip.toFloat().reduced (0.5f);
        juce::Path outline;
        outline.addRoundedRectangle (r, layout.dots ? 7.0f : 5.0f);
        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (theme::lineStrong);
        g.fillPath (dashed);
        g.setColour (theme::textMuted);
        const float cx = r.getCentreX(), cy = r.getCentreY(), arm = layout.dots ? 4.0f : 3.0f;
        g.fillRect (cx - arm, cy - 0.75f, 2.0f * arm, 1.5f);
        g.fillRect (cx - 0.75f, cy - arm, 1.5f, 2.0f * arm);
    }

    theme::drawLabel (g, "Bands", chipsArea.withHeight (14));
    g.setColour (theme::textMuted);
    g.setFont (theme::mono (10.5f));
    g.drawText (juce::String (count) + "/" + juce::String (kMaxBands), chipsArea.withHeight (14).withTrimmedLeft (46), juce::Justification::centredLeft, false);

    if (filterShown)
    {
        theme::drawLabel (g, "Shape", shapeArea.withHeight (14));
        if (band >= 0)
        {
            g.setColour (theme::band (band));
            g.setFont (theme::ui (11.0f, true));
            g.drawText ("Band " + juce::String (band + 1) + middleDot() + params::shapeNames()[static_cast<int> (current.shape)],
                        shapeArea.withHeight (14), juce::Justification::centredRight, false);
        }
        theme::drawLabel (g, "Slope", juce::Rectangle<int> (slopeBox.getX(), slopeBox.getY() - 20, 100, 14));
        theme::drawLabel (g, "Channel", juce::Rectangle<int> (channelPicker.getX(), channelPicker.getY() - 20, 100, 14));
        if (! filterArea.isEmpty())
            theme::drawLabel (g, "Filter", filterArea.withHeight (26));
    }
    if (dynamicsShown)
    {
        theme::drawLabel (g, "Dynamics", dynArea.withHeight (26));
        if (paged && band >= 0)   // the shape column is hidden on this page: say which band this is
        {
            g.setColour (theme::band (band));
            g.setFont (theme::ui (11.0f, true));
            g.drawText ("Band " + juce::String (band + 1), dynArea.withHeight (26).withTrimmedRight (pageButton.getWidth() + 12), juce::Justification::centredRight, false);
        }
        // Live dynamic gain of the selected band, where the header has room for it (the
        // display shows it on the band as well).
        else if (band >= 0 && current.dyn.on && canBeDynamic (current.shape) && liveArea.getWidth() >= 52)
        {
            g.setColour (theme::band (band));
            g.setFont (theme::mono (10.5f));
            g.drawText (params::formatGain (liveDyn), liveArea, juce::Justification::centredRight, false);
        }
    }
}

void BandDock::resized()
{
    auto area = getLocalBounds().reduced (kPadX, kPadY);

    // Column 1: chips, and under them solo, key, bypass, delete.
    chipsArea = area.removeFromLeft (kChipsWidth);
    area.removeFromLeft (2 * kColumnGap + 1);
    {
        auto buttons = chipsArea.withTop (chipsArea.getBottom() - 28);
        soloButton.setBounds (buttons.removeFromLeft (50));
        buttons.removeFromLeft (6);
        listenButton.setBounds (buttons.removeFromLeft (40));
        buttons.removeFromLeft (6);
        bypassButton.setBounds (buttons.removeFromLeft (62));
        buttons.removeFromLeft (6);
        deleteButton.setBounds (buttons.removeFromLeft (30));

        const int count = usedCount();
        addChip = count < kMaxBands ? chipBounds (count, chipLayout (count)) : juce::Rectangle<int>();
    }

    // With room for everything the dock shows it all, as in the mockup. Otherwise it has two
    // pages: Filter (shape, slope, channel and three dials) and Dynamics (mode and five dials).
    const int afterChips = area.getWidth();
    paged = afterChips - kShapeWidth - (2 * kColumnGap + 1) < 600;
    const bool showFilter = ! paged || page == 0, showDynamics = ! paged || page == 1;
    filterShown = showFilter;
    dynamicsShown = showDynamics;

    pageButton.setVisible (paged);
    pageButton.setButtonText (page == 0 ? "Dynamics >" : "< Filter");
    pageButton.setBounds (getWidth() - kPadX - 100, kPadY - 4, 100, 24);

    for (auto* c : std::initializer_list<juce::Component*> { &shapePicker, &slopeBox, &channelPicker, &freqDial, &gainDial, &qDial })
        c->setVisible (showFilter);
    for (auto* c : std::initializer_list<juce::Component*> { &modePicker, &autoButton, &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        c->setVisible (showDynamics);
    linearButton.setVisible (showFilter && mixedMode);

    // Column 2: shape picker on top, slope and channel underneath.
    const int shapeWidth = paged ? juce::jlimit (240, kShapeWidth, afterChips - (2 * kColumnGap + 1) - 3 * 66) : kShapeWidth;
    shapeArea = showFilter ? area.removeFromLeft (shapeWidth) : juce::Rectangle<int>();
    if (showFilter)
    {
        area.removeFromLeft (2 * kColumnGap + 1);
        shapePicker.setBounds (shapeArea.getX(), shapeArea.getY() + 22, shapeArea.getWidth(), 36);
        auto lower = shapeArea.withTop (shapeArea.getBottom() - 30);
        slopeBox.setBounds (lower.removeFromLeft (108));
        lower.removeFromLeft (12);
        if (mixedMode)
        {
            linearButton.setBounds (lower.removeFromRight (40));
            lower.removeFromRight (6);
        }
        channelPicker.setBounds (lower);
    }

    // Column 3: three filter dials, then the dynamics header and its five dials.
    const int dials = (showFilter ? 3 : 0) + (showDynamics ? 5 : 0);
    const int betweenGroups = showFilter && showDynamics ? 16 : 0;
    const int dialW = juce::jlimit (50, 80, (area.getWidth() - betweenGroups - 4 * (dials - 1)) / juce::jmax (1, dials));
    auto row = area.withTrimmedTop (30);

    filterArea = {};
    if (showFilter)
    {
        filterArea = area.withWidth (3 * dialW + 8);
        for (auto* dial : { &freqDial, &gainDial, &qDial })
        {
            dial->setBounds (row.removeFromLeft (dialW));
            row.removeFromLeft (4);
        }
        row.removeFromLeft (betweenGroups - 4);
    }

    dynArea = {};
    if (showDynamics)
    {
        dynArea = area.withLeft (row.getX());
        // Header: label, mode switch, then Auto and the sidechain or resonance switch.
        auto header = dynArea.withHeight (26).withTrimmedLeft (72);
        if (paged)
            header.removeFromRight (pageButton.getWidth() + 70);
        modePicker.setBounds (header.removeFromLeft (juce::jmin (180, header.getWidth() - 110)));
        header.removeFromLeft (6);
        autoButton.setBounds (header.removeFromLeft (44).reduced (0, 1));
        header.removeFromLeft (4);
        externalButton.setBounds (header.removeFromLeft (54).reduced (0, 1));
        resonanceButton.setBounds (externalButton.getBounds());
        liveArea = header.withTrimmedLeft (6);

        for (auto* dial : { &thrDial, &rangeDial, &ratioDial, &atkDial, &relDial })
        {
            dial->setBounds (row.removeFromLeft (dialW));
            row.removeFromLeft (4);
        }
    }
}

void BandDock::mouseDown (const juce::MouseEvent& e)
{
    if (addChip.contains (e.getPosition()))
    {
        const int added = processor.addBand (Shape::Bell, 1000.0, 0.0);
        if (added >= 0 && onSelectBand)
            onSelectBand (added);
        return;
    }

    const auto layout = chipLayout (usedCount());
    int index = 0;
    for (int i = 0; i < kMaxBands; ++i)
    {
        if (! used[static_cast<size_t> (i)])
            continue;
        if (chipBounds (index++, layout).contains (e.getPosition()))
        {
            if (onSelectBand)
                onSelectBand (i);
            return;
        }
    }
}
} // namespace of
