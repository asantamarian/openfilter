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
    : AudioProcessorEditor (p), processor (p), display (p), dock (p),
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
    presetButton.setTitle ("Preset");
    presetButton.onClick = [this] { showPresetMenu(); };
    for (auto* b : { &presetButton, &undoButton, &redoButton, &bypassButton, &deltaButton })
        addAndMakeVisible (*b);

    display.onSelectionChanged = [this] (int band) { dock.setBand (band); };
    dock.onSelectBand = [this] (int band) { display.selectBand (band); };

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

    auto right = topBar.reduced (16, 11);
    bypassButton.setBounds (right.removeFromRight (72));
    right.removeFromRight (8);
    deltaButton.setBounds (right.removeFromRight (64));
    auto left = topBar.reduced (16, 11).withTrimmedLeft (170);
    undoButton.setBounds (left.removeFromLeft (60));
    left.removeFromLeft (6);
    redoButton.setBounds (left.removeFromLeft (60));
    left.removeFromLeft (16);
    presetButton.setBounds (left.removeFromLeft (juce::jmin (260, juce::jmax (120, right.getRight() - left.getX() - 24))));

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
    g.setColour (theme::line);
    g.fillRect (topBar.removeFromBottom (1));

    const auto logo = juce::Rectangle<float> (16.0f, 14.0f, 24.0f, 24.0f);
    g.setColour (theme::accent);
    g.drawEllipse (logo.reduced (1.5f), 1.5f);
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

    const int barW = 10, gap = 4;
    int x = meterArea.getCentreX() - barW - gap / 2;
    for (float db : meterDb)
    {
        const juce::Rectangle<float> bar (static_cast<float> (x), static_cast<float> (meterArea.getY()), static_cast<float> (barW), static_cast<float> (meterArea.getHeight()));
        g.setColour (theme::ground);
        g.fillRoundedRectangle (bar, 3.0f);
        const float level = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);   // -60..0 dBFS
        if (level > 0.0f)
        {
            g.setColour (db > -0.1f ? theme::warn : theme::accent);
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * level), 3.0f);
        }
        x += barW + gap;
    }
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
    const auto presetText = processor.presetName.isEmpty() ? juce::String ("Presets") : processor.presetName;
    if (presetButton.getButtonText() != presetText)
        presetButton.setButtonText (presetText);

    display.refresh();
    dock.refresh();
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
