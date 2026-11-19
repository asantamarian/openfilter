#include "MatchPanel.h"

namespace of
{
MatchPanel::MatchPanel (OpenFilterProcessor& p, ResponseDisplay& d) : processor (p), display (d)
{
    setTitle ("EQ match");
    sourceButton.setClickingTogglesState (true);
    sourceButton.setTooltip ("Play the track and press this to start measuring it; press again to stop. A few seconds of typical material is enough.");
    sourceButton.onClick = [this]
    {
        if (sourceButton.getToggleState())
            display.startCapture (false);
        else
            haveSource = display.finishCapture (false, source);
        updateTarget();
    };
    sidechainButton.setClickingTogglesState (true);
    sidechainButton.setTooltip ("Route the reference to this plugin's sidechain input, play it, and press this to start measuring; press again to stop.");
    sidechainButton.onClick = [this]
    {
        if (sidechainButton.getToggleState())
            display.startCapture (true);
        else if ((haveReference = display.finishCapture (true, reference)))
            referenceName = "sidechain";
        updateTarget();
    };
    fileButton.setTooltip ("Use an audio file as the reference instead of the sidechain.");
    fileButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Reference audio file", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<MatchPanel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult() != juce::File())
                safe->loadReferenceFile (fc.getResult());
        });
    };
    for (int n : { 3, 5, 8, 12, 16, 24 })
        bandsBox.addItem (juce::String (n) + " bands", n);
    bandsBox.setSelectedId (8, juce::dontSendNotification);
    bandsBox.setTitle ("Number of bands for the match");
    bandsBox.onChange = [this] { updateTarget(); };
    applyButton.setTooltip ("Replace the current bands with the match. Undo brings them back.");
    applyButton.onClick = [this] { apply(); };
    closeButton.onClick = [this]
    {
        display.setTargetCurve (nullptr);
        if (onClose)
            onClose();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &sourceButton, &sidechainButton, &fileButton, &bandsBox, &applyButton, &closeButton })
        addAndMakeVisible (*c);
    updateTarget();
}

void MatchPanel::refresh()
{
    sidechainButton.setEnabled (processor.hasSidechain() || sidechainButton.getToggleState());
    repaint();
}

void MatchPanel::updateTarget()
{
    applyButton.setEnabled (haveSource && haveReference);
    lastError = -1.0;
    if (haveSource && haveReference)
    {
        const auto target = fit::matchTarget (source, reference);
        display.setTargetCurve (&target);
        lastError = fit::rmsError (target, fit::evaluate (fit::fitCurve (target, bandsBox.getSelectedId())));
    }
    else
        display.setTargetCurve (nullptr);
    repaint();
}

void MatchPanel::loadReferenceFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 8192)
    {
        status = "Could not read " + file.getFileName();
        repaint();
        return;
    }

    // Long-term average spectrum of up to two minutes, mono, 8192-point Hann frames.
    constexpr int order = 13, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> window (size), work (2 * size);
    std::vector<double> power (size / 2 + 1, 0.0);
    for (int i = 0; i < size; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / size);

    const auto total = juce::jmin (reader->lengthInSamples, static_cast<juce::int64> (reader->sampleRate * 120.0));
    juce::AudioBuffer<float> block (static_cast<int> (juce::jmax (1u, reader->numChannels)), size);
    int frames = 0;
    for (juce::int64 pos = 0; pos + size <= total; pos += size / 2)
    {
        reader->read (&block, 0, size, pos, true, true);
        for (int i = 0; i < size; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                mono += block.getSample (ch, i);
            work[static_cast<size_t> (i)] = mono / static_cast<float> (block.getNumChannels()) * window[static_cast<size_t> (i)];
        }
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        for (int k = 0; k <= size / 2; ++k)
            power[static_cast<size_t> (k)] += static_cast<double> (work[static_cast<size_t> (k)]) * work[static_cast<size_t> (k)];
        ++frames;
    }
    if (frames == 0)
        return;

    for (int i = 0; i < fit::kGridSize; ++i)
    {
        const double bin = juce::jlimit (1.0, size / 2.0 - 1.0, fit::gridFrequency (i) * size / reader->sampleRate);
        const auto lo = static_cast<size_t> (bin);
        const double t = bin - static_cast<double> (lo);
        reference[static_cast<size_t> (i)] = 10.0 * std::log10 ((power[lo] * (1.0 - t) + power[lo + 1] * t) / frames + 1.0e-20);
    }
    haveReference = true;
    referenceName = file.getFileName();
    status = {};
    updateTarget();
}

void MatchPanel::apply()
{
    if (! (haveSource && haveReference))
        return;
    const auto bands = fit::fitCurve (fit::matchTarget (source, reference), bandsBox.getSelectedId());
    processor.replaceBands (bands, "EQ match");
    display.refresh();
}

void MatchPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (theme::panel.withAlpha (0.96f));
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (theme::lineStrong);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);

    theme::drawLabel (g, "EQ match", getLocalBounds().reduced (14, 10).withHeight (14));

    juce::String text;
    if (status.isNotEmpty())                       text = status;
    else if (sourceButton.getToggleState())        text = "Measuring this track: " + juce::String (display.captureSeconds (false), 1) + " s";
    else if (sidechainButton.getToggleState())     text = "Measuring the sidechain: " + juce::String (display.captureSeconds (true), 1) + " s";
    else if (! haveSource)                         text = "1. Play the track and capture it.";
    else if (! haveReference)                      text = "2. Capture a reference from the sidechain, or load a file.";
    else                                           text = "Reference: " + referenceName + ". Fit error " + juce::String (lastError, 2) + " dB. The dashed line is the target.";
    g.setColour (theme::text);
    g.setFont (theme::ui (12.0f));
    g.drawFittedText (text, getLocalBounds().reduced (14, 0).withTop (getHeight() - 30).withHeight (22), juce::Justification::centredLeft, 1);
}

void MatchPanel::resized()
{
    auto row = getLocalBounds().reduced (14, 0).withTop (32).withHeight (28);
    sourceButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (6);
    sidechainButton.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (6);
    fileButton.setBounds (row.removeFromLeft (124));
    row.removeFromLeft (14);
    bandsBox.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (6);
    applyButton.setBounds (row.removeFromLeft (70));
    closeButton.setBounds (row.removeFromRight (64));
}
} // namespace of
