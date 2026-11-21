#pragma once

#include "SpectrumAnalyzer.h"
#include "Theme.h"
#include "match/CurveFitter.h"
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

    /** The primary selected band (the one the dock shows), or -1. */
    int selectedBand() const noexcept { return selected; }
    /** Selects one band, or none; replaces any multi-selection. */
    void selectBand (int band);
    /** How many bands are selected. Shift-click, a rubber band or Ctrl/Cmd+A select several;
        dragging, scrolling, nudging, copying and deleting then act on all of them. */
    int selectionCount() const noexcept;
    bool isSelected (int band) const noexcept { return inSelection[static_cast<size_t> (band)]; }
    std::vector<int> selectedBands() const
    {
        std::vector<int> out;
        for (int i = 0; i < kMaxBands; ++i)
            if (inSelection[static_cast<size_t> (i)])
                out.push_back (i);
        return out;
    }
    /** For tools: add a band to the selection, as a shift-click would. */
    void addToSelection (int band) { inSelection[static_cast<size_t> (band)] = true; repaint(); }
    std::function<void (int)> onSelectionChanged;

    /** Vertical range of the gain axis: 3, 6, 12 or 30 dB each way. */
    void setRangeDb (double range);
    void setAnalyzerVisible (bool visible);
    /** speed in dB per second of release; tilt in dB per octave; range in dB below 0 dBFS. */
    void setAnalyzerOptions (float releasePerSecond, float tiltPerOctave, int rangeDb, bool frozen);
    void setPianoStrip (bool show);
    /** The analyzer's dBFS labels down the left edge. */
    void setAnalyzerScale (bool show) { if (show != analyzerScale) { analyzerScale = show; repaint(); } }
    /** Height at the bottom that another panel covers; the piano strip moves above it. */
    void setCoveredBottom (int pixels);
    /** Spectrogram view: the output spectrum over the last 8 seconds, newest at the top, in
        place of the two traces. */
    void setSpectrogramView (bool show);

    // EQ match: long-term spectra of this track (the plugin input) or the sidechain.
    void startCapture (bool sidechain);
    /** Stops and writes the spectrum to the grid. False if under a second was captured. */
    bool finishCapture (bool sidechain, fit::Curve& out);
    double captureSeconds (bool sidechain) const noexcept;
    /** A dashed target curve over the display; nullptr removes it. */
    void setTargetCurve (const fit::Curve* target);

    /** Other instances drawn over this display, with where each collides with this one. */
    struct Overlay
    {
        juce::Colour colour;
        fit::Curve spectrum;    // dB, no tilt
        fit::Curve collision;   // 0..1
    };
    void setOverlays (std::vector<Overlay> overlays);

    /** Curve sketching: the next drag draws a curve, which is fitted with this many bands. */
    void beginSketch (int numBands);
    bool sketching() const noexcept { return sketchBands > 0; }

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
    /** Picks up band and scale changes. The analyzer advances only in refresh(), on the
        editor's timer, so pointer events do not make it run faster. */
    void syncBands();
    void rebuildCurves();
    void rebuildSpectra();
    void showBandMenu (int band);
    void endDrag();
    void setPrimary (int band);
    void announceSelection();

    OpenFilterProcessor& processor;
    std::array<BandParams, kMaxBands> bands {};
    double scale = 1.0;
    double rangeDb = 12.0;
    int selected = -1, hovered = -1, dragging = -1;
    bool dragHasGain = false, soloHeld = false;
    juce::Point<float> dragOffset;
    std::array<bool, kMaxBands> inSelection {};
    std::array<BandParams, kMaxBands> dragStart {};
    juce::Point<float> rubberStart;
    juce::Rectangle<float> rubber;
    bool rubbering = false;
    juce::Path compositePath, selectedPath, selectedFill, rangePath;
    std::array<float, kMaxBands> liveDyn {};

    SpectrumAnalyzer preAnalyzer, postAnalyzer, scAnalyzer;
    juce::Path scLine, targetPath, sketchPath;
    fit::Curve targetCurve {};
    bool hasTarget = false;
    int sketchBands = 0;
    std::vector<juce::Point<float>> sketchPoints;
    std::vector<Overlay> overlays;

    // Spectrogram: a ring of rows, one written per analyzer update.
    void writeSpectrogramRow();
    static juce::Colour spectrogramColour (float level) noexcept;
    juce::Image spectrogram;
    int spectrogramRow = 0;
    bool spectrogramView = false;
    float overlayTilt = 0.0f;
    juce::Path preFill, postLine;
    std::vector<float> spectrumPoints, spectrumSmooth;
    juce::uint32 lastUpdateMs = 0;
    bool spectraShown = false, analyzerVisible = true, analyzerFrozen = false, piano = false;
    float analyzerRange = 90.0f;
    int coveredBottom = 0;
    bool analyzerScale = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseDisplay)
};
} // namespace of
