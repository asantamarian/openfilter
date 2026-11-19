#include "AdvancedPanel.h"

namespace of
{
AdvancedPanel::AdvancedPanel (OpenFilterProcessor& p, ResponseDisplay& d) : processor (p), display (d)
{
    setTitle ("Advanced settings");

    characterBox.addItemList ({ "Clean", "Tube", "Iron" }, 1);
    characterBox.setTitle ("Character");
    characterBox.setTooltip ("Saturation after the EQ. Tube adds even and odd harmonics; Iron adds odd ones and thickens the lows. Either adds a short delay.");
    characterAttachment = std::make_unique<ComboAttachment> (p.apvts, "character", characterBox);
    driveDial.attach (p.apvts.getParameter ("drive"), &p.undo);
    driveDial.setMidiLearn (&p);

    freeButton.setClickingTogglesState (true);
    freeButton.setTooltip ("Let this band's detector listen at its own frequency and Q instead of following the band. For example, a band at 3 kHz that ducks when the bass hits.");
    triggerBox.addItemList ({ "Above threshold", "Below threshold" }, 1);
    triggerBox.setTitle ("Trigger");
    triggerBox.setTooltip ("Whether the band acts when the level is above the threshold (the usual way) or below it.");
    resolutionDial.setTooltip ("Spectral bands: how finely the gain follows individual frequencies. Smaller is more surgical.");
    kneeDial.setTooltip ("How gradually the band starts to act around the threshold.");
    for (auto* dial : { &detFreqDial, &detQDial, &kneeDial, &resolutionDial })
        dial->setMidiLearn (&p);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    for (auto* c : std::initializer_list<juce::Component*> { &characterBox, &driveDial, &freeButton, &triggerBox, &detFreqDial, &detQDial, &kneeDial, &resolutionDial, &closeButton })
        addAndMakeVisible (*c);
    refresh();
}

void AdvancedPanel::bind (int newBand)
{
    band = newBand;
    freeAttachment.reset();
    triggerAttachment.reset();
    const bool has = band >= 0;
    if (has)
    {
        freeAttachment = std::make_unique<ButtonAttachment> (processor.apvts, params::bandId (band, "scfree"), freeButton);
        triggerAttachment = std::make_unique<ComboAttachment> (processor.apvts, params::bandId (band, "trig"), triggerBox);
    }
    const auto attach = [&] (Dial& d, const char* suffix)
    {
        d.attach (has ? processor.bandParam (band, suffix) : nullptr, &processor.undo);
        d.setArcColour (has ? theme::band (band) : theme::lineStrong);
    };
    attach (detFreqDial, "scfreq");
    attach (detQDial, "scq");
    attach (kneeDial, "knee");
    attach (resolutionDial, "sres");
    repaint();
}

void AdvancedPanel::refresh()
{
    if (display.selectedBand() != band)
        bind (display.selectedBand());

    driveDial.setDimmed (characterBox.getSelectedItemIndex() == 0);

    const bool has = band >= 0;
    const auto b = has ? processor.readBand (band) : BandParams {};
    const bool dynOn = has && b.dyn.on && canBeDynamic (b.shape);
    freeButton.setEnabled (dynOn && ! b.dyn.spectral);
    triggerBox.setEnabled (dynOn);
    detFreqDial.setDimmed (! (dynOn && ! b.dyn.spectral && b.dyn.freeFilter));
    detQDial.setDimmed (! (dynOn && ! b.dyn.spectral && b.dyn.freeFilter));
    kneeDial.setDimmed (! dynOn);
    resolutionDial.setDimmed (! (dynOn && b.dyn.spectral));
    if (b.dyn.on != current.dyn.on || b.dyn.spectral != current.dyn.spectral || b.shape != current.shape)
        repaint();
    current = b;
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (theme::panel.withAlpha (0.96f));
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (theme::lineStrong);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);

    theme::drawLabel (g, "Character", { 14, 10, 150, 14 });
    g.setColour (theme::line);
    g.fillRect (178, 12, 1, getHeight() - 24);

    theme::drawLabel (g, "Selected band: detector and dynamics", { 192, 10, 300, 14 });
    if (band >= 0)
    {
        g.setColour (theme::band (band));
        g.setFont (theme::ui (11.0f, true));
        g.drawText ("Band " + juce::String (band + 1), getLocalBounds().withTrimmedRight (86).withHeight (34), juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::ui (11.0f));
        g.drawText ("Select a band", getLocalBounds().withTrimmedRight (86).withHeight (34), juce::Justification::centredRight, false);
    }
}

void AdvancedPanel::resized()
{
    closeButton.setBounds (getWidth() - 74, 8, 64, 22);

    characterBox.setBounds (14, 34, 86, 28);
    driveDial.setBounds (104, 30, 70, getHeight() - 38);

    auto row = getLocalBounds().withTrimmedLeft (192).withTrimmedRight (12).withTrimmedTop (34).withTrimmedBottom (8);
    auto buttons = row.removeFromLeft (132);
    freeButton.setBounds (buttons.removeFromTop (28));
    buttons.removeFromTop (8);
    triggerBox.setBounds (buttons.removeFromTop (28));
    row.removeFromLeft (10);
    for (auto* dial : { &detFreqDial, &detQDial, &kneeDial, &resolutionDial })
    {
        dial->setBounds (row.removeFromLeft (72).withTrimmedTop (-4));
        row.removeFromLeft (2);
    }
}
} // namespace of
