#include "PluginEditor.h"
#include "Presets.h"

namespace of
{
namespace
{
constexpr int kTopBarHeight = 52, kDockHeight = 168, kOutputWidth = 76;

void applyTheme (juce::LookAndFeel_V4& look)
{
    using namespace juce;
    look.setColour (ComboBox::backgroundColourId, theme::ground);
    look.setColour (ComboBox::outlineColourId, theme::line);
    look.setColour (ComboBox::textColourId, theme::text);
    look.setColour (ComboBox::arrowColourId, theme::textMuted);
    look.setColour (ComboBox::focusedOutlineColourId, theme::accent);
    look.setColour (PopupMenu::backgroundColourId, theme::raised);
    look.setColour (PopupMenu::textColourId, theme::text);
    look.setColour (PopupMenu::highlightedBackgroundColourId, theme::lineStrong);
    look.setColour (PopupMenu::highlightedTextColourId, theme::text);
    look.setColour (TextButton::buttonColourId, theme::raised);
    look.setColour (TextButton::buttonOnColourId, theme::accent.withAlpha (0.25f));
    look.setColour (TextButton::textColourOffId, theme::text);
    look.setColour (TextButton::textColourOnId, theme::accent);
    look.setColour (ComboBox::buttonColourId, theme::line);
}
} // namespace

OpenFilterEditor::OpenFilterEditor (OpenFilterProcessor& p)
    : AudioProcessorEditor (p), processor (p), display (p), dock (p), table (p), matchPanel (p, display), instancePanel (p, display),
      bypassAttachment (p.apvts, "bypass", bypassButton),
      deltaAttachment (p.apvts, "delta", deltaButton)
{
    applyTheme (look);
    setLookAndFeel (&look);

    addAndMakeVisible (display);
    addAndMakeVisible (dock);

    outputDial.setBipolar (true);
    outputDial.setArcColour (theme::text);
    outputDial.attach (p.apvts.getParameter ("out_gain"), &p.undo);
    addAndMakeVisible (outputDial);

    // Gain scale multiplies every band's gain and dynamic range; negative values invert the curve.
    scaleDial.setBipolar (true);
    scaleDial.setArcColour (theme::accent);
    scaleDial.attach (p.apvts.getParameter ("gain_scale"), &p.undo);
    scaleDial.setTooltip ("Gain scale: multiplies the gain and dynamic range of every band. 0 % flattens the EQ, negative values invert it.");
    addAndMakeVisible (scaleDial);
    balanceDial.setBipolar (true);
    balanceDial.setArcColour (theme::text);
    balanceDial.attach (p.apvts.getParameter ("out_pan"), &p.undo);
    addAndMakeVisible (balanceDial);
    autoBox.addItemList ({ "Auto off", "Auto: curve", "Auto: loud" }, 1);
    autoBox.setTitle ("Auto gain");
    autoBox.setTooltip ("Auto gain keeps the level steady so you judge the EQ, not the loudness. Curve: estimated from the EQ curve, instant. Loud: measured loudness of input against output, settles over a couple of seconds.");
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "auto_gain", autoBox);
    addAndMakeVisible (autoBox);

    for (auto* d : { &outputDial, &scaleDial, &balanceDial })
        d->setMidiLearn (&p);

    invertButton.setClickingTogglesState (true);
    invertButton.setTitle ("Invert polarity");
    invertButton.setTooltip ("Invert the polarity of the output.");
    invertAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "out_invert", invertButton);
    addAndMakeVisible (invertButton);

    bypassButton.setClickingTogglesState (true);
    deltaButton.setClickingTogglesState (true);
    undoButton.onClick = [this] { processor.undo.undo(); };
    redoButton.onClick = [this] { processor.undo.redo(); };
    phaseBox.addItemList ({ "Zero latency", "Natural", "Linear", "Mixed" }, 1);
    phaseBox.setTitle ("Phase mode");
    phaseBox.setTooltip ("Zero latency: no delay, less exact in the top octave. Natural: follows the ideal curve up to 20 kHz, with a short delay and more CPU. "
                         "Linear: no phase shift at all, with a long delay. Mixed: each band chooses, with the Lin button in the dock.");
    qualityBox.addItemList ({ "Low", "Medium", "High", "Very high", "Max" }, 1);
    qualityBox.setTitle ("Linear phase quality");
    qualityBox.setTooltip ("Length of the linear-phase filter. Longer is more exact at low frequencies and for narrow bands, and adds delay.");
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "lin_quality", qualityBox);
    addChildComponent (qualityBox);
    phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "phase_mode", phaseBox);
    addAndMakeVisible (phaseBox);

    presetButton.setTitle ("Preset");
    presetButton.onClick = [this] { showPresetMenu(); };
    for (auto* b : { &presetButton, &undoButton, &redoButton, &bypassButton, &deltaButton })
        addAndMakeVisible (*b);

    viewButton.onClick = [this] { showViewMenu(); };
    addAndMakeVisible (viewButton);
    for (int i = 0; i < OpenFilterProcessor::kNumSlots; ++i)
    {
        auto& b = slotButtons[static_cast<size_t> (i)];
        const auto letter = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i));
        b.setButtonText (letter);
        b.setTitle ("Slot " + letter);
        b.setTooltip ("Switch to slot " + letter + ". Copy the current settings to another slot from the View menu.");
        b.onClick = [this, i] { processor.switchSlot (i); };
        addAndMakeVisible (b);
    }

    addChildComponent (instancePanel);
    instancePanel.onClose = [this] { instancePanel.setVisible (false); };
    addChildComponent (matchPanel);
    matchPanel.onClose = [this] { matchPanel.setVisible (false); };
    addChildComponent (table);
    table.onSelectBand = [this] (int band) { display.selectBand (band); };
    display.onSelectionChanged = [this] (int band)
    {
        dock.setBand (band);
        table.setSelectedBand (band);
    };
    dock.onSelectBand = [this] (int band) { display.selectBand (band); };
    applyViewSettings();

    // Read the saved size first: setting the limits resizes the editor, which stores a size.
    const int width = p.editorWidth, height = p.editorHeight;
    setResizable (true, true);
    // Below about 1180 px the dock pages its dials and the top bar sheds its extras.
    setResizeLimits (800, 560, 8000, 8000);
    setSize (width, height);
    startTimerHz (30);
}

OpenFilterEditor::~OpenFilterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OpenFilterEditor::resized()
{
    processor.editorWidth = getWidth();
    processor.editorHeight = getHeight();

    auto area = getLocalBounds();
    topBar = area.removeFromTop (kTopBarHeight);
    dock.setBounds (area.removeFromBottom (kDockHeight));
    outputColumn = area.removeFromRight (kOutputWidth);
    display.setBounds (area);
    matchPanel.setBounds (area.withTrimmedLeft (16).withTrimmedRight (60).withY (area.getY() + 12).withHeight (94).withWidth (juce::jmin (760, area.getWidth() - 76)));
    instancePanel.setBounds (area.getRight() - 60 - 300, area.getY() + 12, 300, juce::jmin (area.getHeight() - 60, 40 + 6 * 58 + 8));
    // The band table covers the lower part of the display when open.
    table.setBounds (area.withTop (area.getBottom() - juce::jmin (area.getHeight() * 45 / 100, 26 * 12)));

    // Top bar. As the window narrows it gives up, in this order: the latency readout and
    // the auto gain readout, the undo and redo buttons (still in View > History and on
    // Ctrl/Cmd+Z in the host), then the slot buttons.
    const bool wide = getWidth() >= 1120, medium = getWidth() >= 960;
    auto right = topBar.reduced (16, 11);
    bypassButton.setBounds (right.removeFromRight (72));
    right.removeFromRight (8);
    deltaButton.setBounds (right.removeFromRight (64));
    right.removeFromRight (8);
    viewButton.setBounds (right.removeFromRight (56));
    right.removeFromRight (medium ? 16 : 8);
    phaseBox.setBounds (right.removeFromRight (medium ? 132 : 112));
    right.removeFromRight (6);
    // The quality selector only appears in the linear modes.
    if (qualityBox.isVisible())
    {
        qualityBox.setBounds (right.removeFromRight (medium ? 104 : 86));
        right.removeFromRight (6);
    }
    latencyArea = wide ? right.removeFromRight (78) : juce::Rectangle<int>();
    right.removeFromRight (wide ? 10 : 0);
    autoBox.setBounds (right.removeFromRight (medium ? 112 : 100));
    autoArea = wide ? right.removeFromRight (58) : juce::Rectangle<int>();

    auto left = topBar.reduced (16, 11).withTrimmedLeft (medium ? 170 : 150);
    undoButton.setVisible (medium);
    redoButton.setVisible (medium);
    if (medium)
    {
        undoButton.setBounds (left.removeFromLeft (60));
        left.removeFromLeft (6);
        redoButton.setBounds (left.removeFromLeft (60));
        left.removeFromLeft (16);
    }
    // The slot buttons give way when the bar runs out of room.
    const bool roomy = right.getRight() - left.getX() >= 90 + 16 + 4 * 30 + 3 * 2 + 12;
    const int slotsWidth = roomy ? 4 * 30 + 3 * 2 + 12 : 0;
    presetButton.setBounds (left.removeFromLeft (juce::jlimit (70, 240, right.getRight() - left.getX() - 10 - slotsWidth)));
    left.removeFromLeft (12);
    for (auto& b : slotButtons)
    {
        b.setVisible (roomy);
        b.setBounds (left.removeFromLeft (30));
        left.removeFromLeft (2);
    }

    auto col = outputColumn.reduced (0, 12);
    col.removeFromTop (36);
    // Output gain is always there; scale, balance and polarity appear when the column is tall
    // enough to keep a readable meter above them.
    const bool tall = col.getHeight() >= 470;
    scaleDial.setVisible (tall);
    balanceDial.setVisible (tall);
    invertButton.setVisible (tall);
    if (tall)
    {
        invertButton.setBounds (col.removeFromBottom (24).reduced (12, 0));
        col.removeFromBottom (8);
        balanceDial.setBounds (col.removeFromBottom (78).reduced (8, 0));
        scaleDial.setBounds (col.removeFromBottom (78).reduced (8, 0));
        outputDial.setBounds (col.removeFromBottom (78).reduced (8, 0));
    }
    else
        outputDial.setBounds (col.removeFromBottom (92).reduced (6, 0));
    meterArea = col.reduced (0, 8);
}

void OpenFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::ground);

    // Top bar.
    g.setColour (theme::panel);
    g.fillRect (topBar);
    // A lit edge under the top bar.
    g.setColour (theme::accent.withAlpha (0.06f));
    g.fillRect (topBar.withTop (topBar.getBottom() - 3));
    g.setColour (theme::accent.withAlpha (0.35f));
    g.fillRect (topBar.removeFromBottom (1));

    const auto logo = juce::Rectangle<float> (16.0f, 14.0f, 24.0f, 24.0f);
    juce::Path ring;
    ring.addEllipse (logo.reduced (1.5f));
    theme::strokeGlow (g, ring, theme::accent, 1.5f, 1.2f);
    juce::Path wave;
    wave.startNewSubPath (logo.getX() + 4.5f, logo.getY() + 14.5f);
    wave.cubicTo (logo.getX() + 8.0f, logo.getY() + 14.5f, logo.getX() + 8.5f, logo.getY() + 8.0f, logo.getX() + 12.0f, logo.getY() + 8.0f);
    wave.cubicTo (logo.getX() + 15.5f, logo.getY() + 8.0f, logo.getX() + 16.0f, logo.getY() + 14.5f, logo.getX() + 19.5f, logo.getY() + 14.5f);
    g.setColour (theme::text);
    g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (theme::ui (14.0f, true));
    g.drawText ("OpenFilter", 50, 0, 80, kTopBarHeight, juce::Justification::centredLeft, false);
    if (getWidth() >= 960)   // the "EQ" suffix is the first thing to go when space is short
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::ui (14.0f));
        g.drawText ("EQ", 128, 0, 30, kTopBarHeight, juce::Justification::centredLeft, false);
    }

    // Latency of the current phase mode.
    if (! latencyArea.isEmpty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::mono (11.0f));
        g.drawText (juce::String (processor.getLatencySamples()) + " smp", latencyArea, juce::Justification::centredRight, false);
    }

    // What auto gain is doing right now.
    if (autoBox.getSelectedItemIndex() > 0 && ! autoArea.isEmpty())
    {
        g.setColour (theme::accent);
        g.setFont (theme::mono (11.0f));
        g.drawText (params::formatGain (shownAuto), autoArea, juce::Justification::centredRight, false);
    }

    // Output column.
    g.setColour (theme::panel);
    g.fillRect (outputColumn);
    g.setColour (theme::line);
    g.fillRect (outputColumn.getX(), outputColumn.getY(), 1, outputColumn.getHeight());
    theme::drawLabel (g, "Out", outputColumn.withHeight (38).withTrimmedTop (12), juce::Justification::centredTop);
    paintMeter (g);
}

void OpenFilterEditor::paintMeter (juce::Graphics& g)
{
    g.setColour (heldDb > -0.1f ? theme::warn : theme::text);
    g.setFont (theme::mono (11.0f));
    g.drawText (heldDb < -90.0f ? "-inf" : juce::String (heldDb, 1), outputColumn.withHeight (50).withTrimmedTop (30), juce::Justification::centredTop, false);

    // LED ladder: one lit segment per 1.5 dB from -60 to 0 dBFS, amber above -6, coral at the top.
    const int barW = 11, gap = 4, segH = 4, segGap = 2;
    const int segments = juce::jmax (1, meterArea.getHeight() / (segH + segGap));
    int x = meterArea.getCentreX() - barW - gap / 2;
    for (float db : meterDb)
    {
        const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * static_cast<float> (segments));
        for (int s = 0; s < segments; ++s)
        {
            const juce::Rectangle<float> seg (static_cast<float> (x), static_cast<float> (meterArea.getBottom() - (s + 1) * (segH + segGap) + segGap),
                                              static_cast<float> (barW), static_cast<float> (segH));
            const float level = static_cast<float> (s + 1) / static_cast<float> (segments);   // 0..1 up the ladder
            const auto colour = level > 0.98f ? theme::band (0) : level > 0.9f ? theme::warn : theme::accent;
            if (s < lit)
            {
                g.setColour (colour.withAlpha (0.18f));
                g.fillRoundedRectangle (seg.expanded (2.0f), 2.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (seg, 1.0f);
            }
            else
            {
                g.setColour (colour.withAlpha (0.10f));
                g.fillRoundedRectangle (seg, 1.0f);
            }
        }
        x += barW + gap;
    }
}

void OpenFilterEditor::applyViewSettings()
{
    display.setRangeDb (processor.displayRangeDb);
    display.setAnalyzerVisible (processor.analyzerOn);
    constexpr float speeds[] = { 6.0f, 12.0f, 24.0f, 48.0f };
    display.setAnalyzerOptions (speeds[juce::jlimit (0, 3, processor.analyzerSpeed)], processor.analyzerTilt, processor.analyzerRange, processor.analyzerFrozen);
    display.setPianoStrip (processor.pianoStrip);
    table.setVisible (processor.tableOpen);
    if (! juce::approximatelyEqual (processor.uiScale, static_cast<float> (getTransform().getScaleFactor())))
        setScaleFactor (processor.uiScale);
}

void OpenFilterEditor::showViewMenu()
{
    juce::PopupMenu menu, scale, range, copy, history, look;
    const int lookNow = juce::roundToInt (processor.apvts.getRawParameterValue ("lookahead")->load());
    const char* lookNames[] = { "Off", "2 ms", "5 ms", "10 ms" };
    for (int i = 0; i < 4; ++i)
        look.addItem (5000 + i, lookNames[i], true, i == lookNow);
    for (int percent : { 50, 75, 100, 125, 150, 200 })
        scale.addItem (1000 + percent, juce::String (percent) + " %", true, juce::roundToInt (processor.uiScale * 100.0f) == percent);
    for (int db : { 3, 6, 12, 30 })
        range.addItem (2000 + db, juce::String::fromUTF8 ("\xc2\xb1") + juce::String (db) + " dB", true, juce::roundToInt (processor.displayRangeDb) == db);
    const auto letter = [] (int i) { return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)); };
    for (int i = 0; i < OpenFilterProcessor::kNumSlots; ++i)
        if (i != processor.activeSlot())
            copy.addItem (3000 + i, "Copy " + letter (processor.activeSlot()) + " to " + letter (i));

    const auto steps = processor.undo.getUndoDescriptions();
    for (int i = 0; i < juce::jmin (15, steps.size()); ++i)
        history.addItem (4000 + i, "Undo back to before: " + (steps[i].isEmpty() ? juce::String ("edit") : steps[i]));
    if (steps.isEmpty())
        history.addItem (3999, "Nothing to undo", false);

    juce::PopupMenu analyzer;
    analyzer.addItem (2, "Show", true, processor.analyzerOn);
    analyzer.addItem (4, "Freeze", processor.analyzerOn, processor.analyzerFrozen);
    analyzer.addSeparator();
    const char* speedNames[] = { "Speed: slow", "Speed: medium", "Speed: fast", "Speed: very fast" };
    for (int i = 0; i < 4; ++i)
        analyzer.addItem (7000 + i, speedNames[i], true, processor.analyzerSpeed == i);
    analyzer.addSeparator();
    analyzer.addItem (7100, "Tilt: none", true, processor.analyzerTilt < 0.1f);
    analyzer.addItem (7103, "Tilt: 3 dB/oct", true, std::abs (processor.analyzerTilt - 3.0f) < 0.1f);
    analyzer.addItem (7145, "Tilt: 4.5 dB/oct", true, std::abs (processor.analyzerTilt - 4.5f) < 0.1f);
    analyzer.addSeparator();
    for (int db : { 60, 90, 120 })
        analyzer.addItem (7200 + db, "Range: " + juce::String (db) + " dB", true, processor.analyzerRange == db);

    juce::PopupMenu sketch;
    for (int n : { 3, 5, 8, 12 })
        sketch.addItem (8000 + n, "Draw a curve, fit " + juce::String (n) + " bands");
    menu.addItem (6, "Instances (" + juce::String (processor.hub->count() - 1) + " other)...", true, instancePanel.isVisible());
    menu.addItem (5, "EQ match...", true, matchPanel.isVisible());
    menu.addSubMenu ("Sketch", sketch);
    menu.addSeparator();
    menu.addItem (1, "Band table", true, processor.tableOpen);
    menu.addItem (3, "Piano strip", true, processor.pianoStrip);
    menu.addSubMenu ("Spectrum analyzer", analyzer);
    menu.addSeparator();
    menu.addSubMenu ("Display range", range);
    menu.addSubMenu ("Interface size", scale);
    menu.addSubMenu ("Dynamics lookahead", look);

    // Character: saturation after the EQ. Drive is also a host parameter.
    juce::PopupMenu character;
    const int charNow = juce::roundToInt (processor.apvts.getRawParameterValue ("character")->load());
    const int driveNow = juce::roundToInt (processor.apvts.getRawParameterValue ("drive")->load());
    const char* charNames[] = { "Clean", "Tube (even and odd harmonics)", "Iron (odd harmonics, thicker lows)" };
    for (int i = 0; i < 3; ++i)
        character.addItem (6000 + i, charNames[i], true, i == charNow);
    character.addSeparator();
    for (int percent : { 10, 20, 30, 50, 70, 100 })
        character.addItem (6100 + percent, "Drive " + juce::String (percent) + " %", charNow != 0, driveNow == percent);
    menu.addSubMenu ("Character", character);
    menu.addSeparator();
    menu.addSubMenu ("Slots", copy);
    menu.addSubMenu ("History", history);

    juce::Component::SafePointer<OpenFilterEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (viewButton).withParentComponent (this), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& proc = safe->processor;
        if (result == 1) proc.tableOpen = ! proc.tableOpen;
        else if (result == 2) proc.analyzerOn = ! proc.analyzerOn;
        else if (result == 3) proc.pianoStrip = ! proc.pianoStrip;
        else if (result == 5) safe->matchPanel.setVisible (! safe->matchPanel.isVisible());
        else if (result == 6)
        {
            safe->instancePanel.setVisible (! safe->instancePanel.isVisible());
            if (! safe->instancePanel.isVisible())
                safe->display.setOverlays ({});
        }
        else if (result >= 8000) safe->display.beginSketch (result - 8000);
        else if (result == 4) proc.analyzerFrozen = ! proc.analyzerFrozen;
        else if (result >= 7200) proc.analyzerRange = result - 7200;
        else if (result >= 7100) proc.analyzerTilt = static_cast<float> (result - 7100) / 10.0f;
        else if (result >= 7000) proc.analyzerSpeed = result - 7000;
        else if (result >= 6100)
        {
            if (auto* p = proc.apvts.getParameter ("drive"))
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (result - 6100)));
        }
        else if (result >= 6000)
        {
            if (auto* p = proc.apvts.getParameter ("character"))
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (result - 6000)));
        }
        else if (result >= 5000)
        {
            if (auto* p = proc.apvts.getParameter ("lookahead"))
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (result - 5000)));
        }
        else if (result >= 4000) { for (int i = 0; i <= result - 4000; ++i) proc.undo.undo(); }
        else if (result >= 3000) proc.copyActiveSlotTo (result - 3000);
        else if (result >= 2000) proc.displayRangeDb = result - 2000;
        else if (result >= 1000) proc.uiScale = static_cast<float> (result - 1000) / 100.0f;
        safe->applyViewSettings();
    });
}

void OpenFilterEditor::showPresetMenu()
{
    const auto files = presets::list();
    juce::PopupMenu menu;
    const auto folder = presets::userFolder();
    for (int i = 0; i < files.size(); ++i)
        menu.addItem (100 + i, files[i].getRelativePathFrom (folder).upToLastOccurrenceOf (presets::kExtension, false, true),
                      true, files[i].getFileNameWithoutExtension() == processor.presetName);
    if (files.isEmpty())
        menu.addItem (99, "No saved presets", false);
    juce::PopupMenu factory;
    const auto factoryNames = presets::factoryNames();
    for (int i = 0; i < factoryNames.size(); ++i)
        factory.addItem (500 + i, factoryNames[i], true, factoryNames[i] == processor.presetName);
    menu.addSeparator();
    menu.addSubMenu ("Factory", factory);
    menu.addSeparator();
    menu.addItem (1, "Save as...");
    menu.addItem (2, "Load from file...");
    menu.addItem (3, "Show presets folder");

    juce::Component::SafePointer<OpenFilterEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton).withParentComponent (this), [safe, files, folder] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& proc = safe->processor;
        juce::String error;

        if (result >= 500)
        {
            if (presets::loadFactory (proc, result - 500, error))
                proc.presetName = presets::factoryNames()[result - 500];
        }
        else if (result >= 100 && result - 100 < files.size())
        {
            if (presets::load (proc, files[result - 100], error))
                proc.presetName = files[result - 100].getFileNameWithoutExtension();
        }
        else if (result == 3)
            folder.revealToUser();
        else if (result == 1 || result == 2)
        {
            const bool saving = result == 1;
            safe->chooser = std::make_unique<juce::FileChooser> (saving ? "Save preset" : "Load preset", folder, juce::String ("*") + presets::kExtension);
            const int flags = saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting
                                     : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
            safe->chooser->launchAsync (flags, [safe, saving] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (safe == nullptr || file == juce::File())
                    return;
                juce::String err;
                if (saving)
                    file = file.withFileExtension (presets::kExtension);
                if (saving ? presets::save (safe->processor, file, err) : presets::load (safe->processor, file, err))
                    safe->processor.presetName = file.getFileNameWithoutExtension();
                if (err.isNotEmpty())
                    juce::NativeMessageBox::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                           .withTitle ("Preset").withMessage (err).withButton ("OK").withAssociatedComponent (safe.getComponent()), nullptr);
            });
        }

        if (error.isNotEmpty())
            juce::NativeMessageBox::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                   .withTitle ("Preset").withMessage (error).withButton ("OK").withAssociatedComponent (safe.getComponent()), nullptr);
    });
}

void OpenFilterEditor::timerCallback()
{
    const float autoNow = processor.autoGainDb();
    if (std::abs (autoNow - shownAuto) > 0.05f)
    {
        shownAuto = autoNow;
        repaint (autoArea);
    }

    const int phase = phaseBox.getSelectedItemIndex();
    if (qualityBox.isVisible() != (phase >= 2))
    {
        qualityBox.setVisible (phase >= 2);
        resized();
        repaint (topBar);
    }
    if (processor.getLatencySamples() != shownLatency)
    {
        shownLatency = processor.getLatencySamples();
        repaint (latencyArea);
    }

    const auto presetText = processor.presetName.isEmpty() ? juce::String ("Presets") : processor.presetName;
    if (presetButton.getButtonText() != presetText)
        presetButton.setButtonText (presetText);

    display.refresh();
    dock.refresh();
    if (table.isVisible())
        table.refresh();
    if (matchPanel.isVisible())
        matchPanel.refresh();
    if (instancePanel.isVisible())
        instancePanel.refresh();
    for (int i = 0; i < OpenFilterProcessor::kNumSlots; ++i)
        slotButtons[static_cast<size_t> (i)].setToggleState (i == processor.activeSlot(), juce::dontSendNotification);
    undoButton.setEnabled (processor.undo.canUndo());
    redoButton.setEnabled (processor.undo.canRedo());

    // Meters: instant attack, 24 dB/s release, 1.5 s peak hold.
    float top = -100.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = juce::Decibels::gainToDecibels (processor.takePeak (ch), -100.0f);
        meterDb[ch] = juce::jmax (db, meterDb[ch] - 0.8f);
        top = juce::jmax (top, db);
    }
    if (top >= heldDb)
    {
        heldDb = top;
        holdTicks = 45;
    }
    else if (--holdTicks <= 0)
        heldDb = juce::jmax (top, heldDb - 0.8f);

    repaint (outputColumn);
}
} // namespace of
