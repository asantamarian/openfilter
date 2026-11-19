#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

namespace of
{
/** The main display: grid, composite response curve and band nodes
    (docs/spec/05-ui.md section 4). Analyzer layers arrive with milestone M2. */
class ResponseDisplay final : public juce::Component
{
public:
    explicit ResponseDisplay (OpenFilterProcessor&);

    /** Called from the editor's timer: re-reads the bands and repaints if they changed. */
    void refresh();

    int selectedBand() const noexcept { return selected; }
    void selectBand (int band);
    std::function<void (int)> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // Coordinate mapping: 16 Hz to 24 kHz, log; gain linear in dB around the 0 dB line.
    float freqToX (double f) const noexcept;
    double xToFreq (float x) const noexcept;
    float dbToY (double db) const noexcept;
    double yToDb (float y) const noexcept;
    juce::Point<float> nodePosition (const BandParams&) const noexcept;
    int bandAt (juce::Point<float>) const noexcept;
    void rebuildCurves();
    void showBandMenu (int band);
    void endDrag();

    OpenFilterProcessor& processor;
    std::array<BandParams, kMaxBands> bands {};
    double scale = 1.0;
    double rangeDb = 12.0;
    int selected = -1, hovered = -1, dragging = -1;
    bool dragHasGain = false;
    juce::Point<float> dragOffset;
    juce::Path compositePath, selectedPath, selectedFill;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseDisplay)
};
} // namespace of
