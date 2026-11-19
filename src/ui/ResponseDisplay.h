#pragma once

#include "SpectrumAnalyzer.h"
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
    ~ResponseDisplay() override;

    /** Called from the editor's timer: re-reads the bands and repaints if they changed. */
    void refresh();

    int selectedBand() const noexcept { return selected; }
    void selectBand (int band);
    std::function<void (int)> onSelectionChanged;

    /** Vertical range of the gain axis: 3, 6, 12 or 30 dB each way. */
    void setRangeDb (double range);
    void setAnalyzerVisible (bool visible);
    /** speed in dB per second of release; tilt in dB per octave; range in dB below 0 dBFS. */
    void setAnalyzerOptions (float releasePerSecond, float tiltPerOctave, int rangeDb, bool frozen);
    void setPianoStrip (bool show);

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
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    // Coordinate mapping: 16 Hz to 24 kHz, log; gain linear in dB around the 0 dB line.
    float freqToX (double f) const noexcept;
    double xToFreq (float x) const noexcept;
    float dbToY (double db) const noexcept;
    double yToDb (float y) const noexcept;
    juce::Point<float> nodePosition (const BandParams&) const noexcept;
    int bandAt (juce::Point<float>) const noexcept;
    void rebuildCurves();
    void rebuildSpectra();
    void showBandMenu (int band);
    void endDrag();

    OpenFilterProcessor& processor;
    std::array<BandParams, kMaxBands> bands {};
    double scale = 1.0;
    double rangeDb = 12.0;
    int selected = -1, hovered = -1, dragging = -1;
    bool dragHasGain = false, soloHeld = false;
    juce::Point<float> dragOffset;
    juce::Path compositePath, selectedPath, selectedFill, rangePath;
    std::array<float, kMaxBands> liveDyn {};

    SpectrumAnalyzer preAnalyzer, postAnalyzer;
    juce::Path preFill, postLine;
    std::vector<float> spectrumPoints, spectrumSmooth;
    juce::uint32 lastUpdateMs = 0;
    bool spectraShown = false, analyzerVisible = true, analyzerFrozen = false, piano = false;
    float analyzerRange = 90.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseDisplay)
};
} // namespace of
