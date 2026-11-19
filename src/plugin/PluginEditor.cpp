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
    : AudioProcessorEditor (p), processor (p), display (p), dock (p), table (p),
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

    bypassButton.setClickingTogglesState (true);
    deltaButton.setClickingTogglesState (true);
    undoButton.onClick = [this] { processor.undo.undo(); };
    redoButton.onClick = [this] { processor.undo.redo(); };
    // Linear and Mixed are in the parameter already but not built yet.
    phaseBox.addItemList ({ "Zero latency", "Natural", "Linear (not yet)", "Mixed (not yet)" }, 1);
    phaseBox.setItemEnabled (3, false);
    phaseBox.setItemEnabled (4, false);
    phaseBox.setTitle ("Phase mode");
    phaseBox.setTooltip ("Zero latency: no delay, less exact in the top octave. Natural: follows the ideal curve up to 20 kHz, with a short delay and more CPU.");
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
    setResizeLimits (800, 500, 8000, 8000);
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
    // The band table covers the lower part of the display when open.
    table.setBounds (area.withTop (area.getBottom() - juce::jmin (area.getHeight() * 45 / 100, 26 * 12)));

    auto right = topBar.reduced (16, 11);
    bypassButton.setBounds (right.removeFromRight (72));
    right.removeFromRight (8);
    deltaButton.setBounds (right.removeFromRight (64));
    right.removeFromRight (8);
    viewButton.setBounds (right.removeFromRight (56));
    right.removeFromRight (16);
    phaseBox.setBounds (right.removeFromRight (132));
    right.removeFromRight (8);
    latencyArea = right.removeFromRight (84);
    auto left = topBar.reduced (16, 11).withTrimmedLeft (170);
    undoButton.setBounds (left.removeFromLeft (60));
    left.removeFromLeft (6);
    redoButton.setBounds (left.removeFromLeft (60));
    left.removeFromLeft (16);
    // The slot buttons and the latency readout give way first when the window is narrow.
    const bool roomy = getWidth() >= 1040;
    const int slotsWidth = roomy ? 4 * 30 + 3 * 2 + 12 : 0;
    presetButton.setBounds (left.removeFromLeft (juce::jlimit (90, 240, right.getRight() - left.getX() - 16 - slotsWidth)));
    left.removeFromLeft (12);
    for (auto& b : slotButtons)
    {
        b.setVisible (roomy);
        b.setBounds (left.removeFromLeft (30));
        left.removeFromLeft (2);
    }

    auto col = outputColumn.reduced (0, 12);
    col.removeFromTop (36);
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
    g.setColour (theme::textMuted);
    g.setFont (theme::ui (14.0f));
    g.drawText ("EQ", 128, 0, 30, kTopBarHeight, juce::Justification::centredLeft, false);

    // Latency of the current phase mode.
    g.setColour (theme::textMuted);
    g.setFont (theme::mono (11.0f));
    g.drawText (juce::String (processor.getLatencySamples()) + " smp", latencyArea, juce::Justification::centredRight, false);

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
    table.setVisible (processor.tableOpen);
    if (! juce::approximatelyEqual (processor.uiScale, static_cast<float> (getTransform().getScaleFactor())))
        setScaleFactor (processor.uiScale);
}

void OpenFilterEditor::showViewMenu()
{
    juce::PopupMenu menu, scale, range, copy, history;
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

    menu.addItem (1, "Band table", true, processor.tableOpen);
    menu.addItem (2, "Spectrum analyzer", true, processor.analyzerOn);
    menu.addSeparator();
    menu.addSubMenu ("Display range", range);
    menu.addSubMenu ("Interface size", scale);
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

        if (result >= 100 && result - 100 < files.size())
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
