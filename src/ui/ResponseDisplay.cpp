#include "ResponseDisplay.h"

#include "dsp/design/Prototype.h"
#include "plugin/Presets.h"

namespace of
{
namespace
{
constexpr double kMinFreq = 16.0, kMaxFreq = 24000.0;
constexpr float kNodeRadius = 11.0f, kHitRadius = 15.0f;
constexpr int kAxisHeight = 22;

bool sameBand (const BandParams& a, const BandParams& b)
{
    return a.used == b.used && a.on == b.on && a.shape == b.shape && a.slope == b.slope && a.place == b.place
        && a.freq == b.freq && a.gainDb == b.gainDb && a.q == b.q && a.dyn == b.dyn;
}
} // namespace

ResponseDisplay::ResponseDisplay (OpenFilterProcessor& p)
    : processor (p), preAnalyzer (p.preFifo), postAnalyzer (p.postFifo), scAnalyzer (p.scFifo)
{
    processor.analyzerActive = true;
    setWantsKeyboardFocus (true);
    setTitle ("Equalizer response");
    setDescription ("Double-click to add a band. Drag a band to change frequency and gain, scroll to change Q.");
    refresh();
}

ResponseDisplay::~ResponseDisplay() { processor.analyzerActive = false; }

//==============================================================================
float ResponseDisplay::freqToX (double f) const noexcept
{
    return static_cast<float> (std::log (f / kMinFreq) / std::log (kMaxFreq / kMinFreq)) * static_cast<float> (getWidth());
}

double ResponseDisplay::xToFreq (float x) const noexcept
{
    return kMinFreq * std::pow (kMaxFreq / kMinFreq, static_cast<double> (x) / juce::jmax (1, getWidth()));
}

float ResponseDisplay::dbToY (double db) const noexcept
{
    const float plotH = static_cast<float> (getHeight() - kAxisHeight);
    const float centre = 0.5f * plotH;
    const float pxPerDb = (centre - 28.0f) / static_cast<float> (rangeDb);
    // Limit the position, not the gain: a curve that leaves the range must run off the plot
    // (where it is clipped), not flatten into a line inside it.
    return juce::jlimit (-24.0f, plotH + 24.0f, centre - static_cast<float> (db) * pxPerDb);
}

double ResponseDisplay::yToDb (float y) const noexcept
{
    const float plotH = static_cast<float> (getHeight() - kAxisHeight);
    const float centre = 0.5f * plotH;
    const float pxPerDb = (centre - 28.0f) / static_cast<float> (rangeDb);
    return static_cast<double> ((centre - y) / pxPerDb);
}

juce::Point<float> ResponseDisplay::nodePosition (const BandParams& b) const noexcept
{
    return { freqToX (juce::jlimit (kMinFreq, kMaxFreq, b.freq)), dbToY (hasGain (b.shape) ? b.gainDb * scale : 0.0) };
}

int ResponseDisplay::bandAt (juce::Point<float> p) const noexcept
{
    int best = -1;
    float bestDist = kHitRadius;
    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto& b = bands[static_cast<size_t> (i)];
        if (! b.used)
            continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestDist || (d <= kHitRadius && i == selected))
        {
            best = i;
            bestDist = i == selected ? 0.0f : d;
        }
    }
    return best;
}

//==============================================================================
void ResponseDisplay::refresh()
{
    bool changed = false;
    const double newScale = processor.gainScale();
    if (newScale != scale)
    {
        scale = newScale;
        changed = true;
    }
    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto b = processor.readBand (i);
        if (! sameBand (b, bands[static_cast<size_t> (i)]))
        {
            bands[static_cast<size_t> (i)] = b;
            changed = true;
        }
    }
    for (int i = 0; i < kMaxBands; ++i)   // deleted bands leave the selection
        if (! bands[static_cast<size_t> (i)].used)
            inSelection[static_cast<size_t> (i)] = false;
    if (selected >= 0 && ! bands[static_cast<size_t> (selected)].used)
    {
        int other = -1;
        for (int i = 0; i < kMaxBands; ++i)
            if (inSelection[static_cast<size_t> (i)])
                other = i;
        if (other >= 0) setPrimary (other); else selectBand (-1);
    }
    if (changed)
    {
        rebuildCurves();
        repaint();
    }

    // Live dynamic gain of each band.
    bool dynMoved = false;
    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto& lb = bands[static_cast<size_t> (i)];
        const float d = lb.dynamic() || lb.spectral() ? processor.dynamicGainDb (i) : 0.0f;
        dynMoved = dynMoved || std::abs (d - liveDyn[static_cast<size_t> (i)]) > 0.05f;
        liveDyn[static_cast<size_t> (i)] = d;
    }
    if (dynMoved)
        repaint();

    // Analyzer: pull what the audio thread queued and redraw while anything is showing.
    const auto now = juce::Time::getMillisecondCounter();
    const double elapsed = lastUpdateMs == 0 ? 0.0 : (now - lastUpdateMs) * 0.001;
    lastUpdateMs = now;
    preAnalyzer.setSampleRate (processor.getSampleRate());
    postAnalyzer.setSampleRate (processor.getSampleRate());
    // Frozen: the traces stay as they are (and whatever the audio thread queues is dropped).
    if (analyzerVisible && analyzerFrozen)
    {
        processor.preFifo.clear();
        processor.postFifo.clear();
        return;
    }
    scAnalyzer.setSampleRate (processor.getSampleRate());
    const bool scLive = analyzerVisible && scAnalyzer.update (elapsed) && processor.hasSidechain();
    if (scLive != ! scLine.isEmpty())
        repaint();
    const bool preLive = (analyzerVisible && preAnalyzer.update (elapsed)) || scLive, postLive = analyzerVisible && postAnalyzer.update (elapsed);
    if (preLive || postLive || spectraShown)
    {
        spectraShown = preLive || postLive;
        if (spectrogramView && spectraShown)
            writeSpectrogramRow();
        rebuildSpectra();
        repaint();
    }
}

void ResponseDisplay::rebuildSpectra()
{
    preFill.clear();
    postLine.clear();
    scLine.clear();
    const int w = getWidth();
    if (w <= 0 || ! spectraShown)
        return;

    const int step = 2, count = w / step + 1;
    const float plotBottom = static_cast<float> (getHeight() - kAxisHeight);
    // Analyzer scale: the chosen range at the bottom of the plot, 0 dBFS at the top.
    const float shown = analyzerRange;
    const auto toY = [plotBottom, shown] (float db) { return plotBottom * (1.0f - juce::jlimit (0.0f, 1.0f, (db + shown) / shown)); };
    // 1/6-octave smoothing: display points are evenly spaced in log frequency.
    const int half = juce::jmax (1, juce::roundToInt (count / std::log2 (kMaxFreq / kMinFreq) / 12.0));

    const auto trace = [&] (const SpectrumAnalyzer& analyzer)
    {
        spectrumPoints.resize (static_cast<size_t> (count));
        spectrumSmooth.resize (static_cast<size_t> (count));
        for (int i = 0; i < count; ++i)
            spectrumPoints[static_cast<size_t> (i)] = analyzer.peakBetween (xToFreq (static_cast<float> (i * step - 1)), xToFreq (static_cast<float> (i * step + 1)));
        for (int i = 0; i < count; ++i)
        {
            float sum = 0.0f;
            int n = 0;
            for (int j = juce::jmax (0, i - half); j <= juce::jmin (count - 1, i + half); ++j, ++n)
                sum += spectrumPoints[static_cast<size_t> (j)];
            spectrumSmooth[static_cast<size_t> (i)] = sum / static_cast<float> (n);
        }
    };

    trace (preAnalyzer);
    preFill.startNewSubPath (0.0f, plotBottom);
    for (int i = 0; i < count; ++i)
        preFill.lineTo (static_cast<float> (i * step), toY (spectrumSmooth[static_cast<size_t> (i)]));
    preFill.lineTo (static_cast<float> (w), plotBottom);
    preFill.closeSubPath();

    trace (postAnalyzer);
    for (int i = 0; i < count; ++i)
    {
        const float y = toY (spectrumSmooth[static_cast<size_t> (i)]);
        if (i == 0) postLine.startNewSubPath (0.0f, y); else postLine.lineTo (static_cast<float> (i * step), y);
    }

    if (processor.hasSidechain())
    {
        trace (scAnalyzer);
        for (int i = 0; i < count; ++i)
        {
            const float y = toY (spectrumSmooth[static_cast<size_t> (i)]);
            if (i == 0) scLine.startNewSubPath (0.0f, y); else scLine.lineTo (static_cast<float> (i * step), y);
        }
    }
}

int ResponseDisplay::selectionCount() const noexcept
{
    return static_cast<int> (std::count (inSelection.begin(), inSelection.end(), true));
}

void ResponseDisplay::setPrimary (int band)
{
    if (band == selected)
        return;
    selected = band;
    if (band >= 0)
        inSelection[static_cast<size_t> (band)] = true;
    rebuildCurves();
    repaint();
    announceSelection();
    if (onSelectionChanged)
        onSelectionChanged (selected);
}

void ResponseDisplay::announceSelection()
{
    // Screen readers cannot see the nodes, so say which band is now selected.
    if (selected < 0 || ! bands[static_cast<size_t> (selected)].used)
        return;
    const auto& b = bands[static_cast<size_t> (selected)];
    juce::String text = "Band " + juce::String (selected + 1) + ", " + params::shapeLabels()[static_cast<int> (b.shape)] + ", " + params::formatFrequency (b.freq);
    if (hasGain (b.shape))
        text << ", " << params::formatGain (b.gainDb);
    if (! b.on)
        text << ", bypassed";
    if (b.dynamic())        text << ", dynamic";
    else if (b.spectral())  text << ", spectral";
    juce::AccessibilityHandler::postAnnouncement (text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}

void ResponseDisplay::selectBand (int band)
{
    // Selecting one band (or none) replaces any multi-selection.
    inSelection.fill (false);
    if (band >= 0)
        inSelection[static_cast<size_t> (band)] = true;
    if (band == selected)
    {
        repaint();
        return;
    }
    selected = band;
    rebuildCurves();
    repaint();
    announceSelection();
    if (onSelectionChanged)
        onSelectionChanged (selected);
}

void ResponseDisplay::resized() { rebuildCurves(); }

void ResponseDisplay::setRangeDb (double range)
{
    if (juce::exactlyEqual (range, rangeDb))
        return;
    rangeDb = range;
    rebuildCurves();
    repaint();
}

void ResponseDisplay::setAnalyzerOptions (float releasePerSecond, float tiltPerOctave, int rangeDb, bool frozen)
{
    for (auto* a : { &preAnalyzer, &postAnalyzer, &scAnalyzer })
    {
        a->setReleasePerSecond (releasePerSecond);
        a->setTiltPerOctave (tiltPerOctave);
    }
    analyzerRange = static_cast<float> (rangeDb);
    overlayTilt = tiltPerOctave;
    if (analyzerFrozen && ! frozen)   // drop what queued up while frozen
    {
        processor.preFifo.clear();
        processor.postFifo.clear();
    }
    analyzerFrozen = frozen;
    rebuildSpectra();
    repaint();
}

void ResponseDisplay::startCapture (bool sidechain) { (sidechain ? scAnalyzer : preAnalyzer).startAveraging(); }

double ResponseDisplay::captureSeconds (bool sidechain) const noexcept { return (sidechain ? scAnalyzer : preAnalyzer).averagedSeconds(); }

bool ResponseDisplay::finishCapture (bool sidechain, fit::Curve& out)
{
    auto& analyzer = sidechain ? scAnalyzer : preAnalyzer;
    analyzer.stopAveraging();
    if (analyzer.averagedSeconds() < 1.0)
        return false;
    for (int i = 0; i < fit::kGridSize; ++i)
        out[static_cast<size_t> (i)] = analyzer.averageDb (fit::gridFrequency (i));
    return true;
}

void ResponseDisplay::setTargetCurve (const fit::Curve* target)
{
    hasTarget = target != nullptr;
    if (hasTarget)
        targetCurve = *target;
    rebuildCurves();
    repaint();
}

void ResponseDisplay::setOverlays (std::vector<Overlay> newOverlays)
{
    if (overlays.empty() && newOverlays.empty())
        return;
    overlays = std::move (newOverlays);
    repaint();
}

void ResponseDisplay::beginSketch (int numBands)
{
    sketchBands = numBands;
    sketchPoints.clear();
    sketchPath.clear();
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    repaint();
}

void ResponseDisplay::setSpectrogramView (bool show)
{
    if (show == spectrogramView)
        return;
    spectrogramView = show;
    spectrogram = {};   // start with a clean history
    repaint();
}

juce::Colour ResponseDisplay::spectrogramColour (float level) noexcept
{
    // Quiet to loud: nothing, indigo, the accent, amber, white.
    static const juce::Colour stops[] = { juce::Colour (0x00161A40), juce::Colour (0xff343CA8), juce::Colour (0xff6EE7C5),
                                          juce::Colour (0xffFFB454), juce::Colour (0xffFFFFFF) };
    static const float at[] = { 0.0f, 0.38f, 0.64f, 0.84f, 1.0f };
    level = juce::jlimit (0.0f, 1.0f, level);
    for (int i = 0; i < 4; ++i)
        if (level <= at[i + 1])
            return stops[i].interpolatedWith (stops[i + 1], (level - at[i]) / (at[i + 1] - at[i]));
    return stops[4];
}

void ResponseDisplay::writeSpectrogramRow()
{
    constexpr int rows = 240;   // 8 seconds at 30 updates a second
    const int columns = juce::jmax (64, getWidth() / 2);
    if (spectrogram.getWidth() != columns || spectrogram.getHeight() != rows)
    {
        spectrogram = juce::Image (juce::Image::ARGB, columns, rows, true);
        spectrogramRow = 0;
    }
    spectrogramRow = (spectrogramRow + rows - 1) % rows;   // the newest row moves up the ring
    juce::Image::BitmapData data (spectrogram, 0, spectrogramRow, columns, 1, juce::Image::BitmapData::writeOnly);
    const float step = static_cast<float> (getWidth()) / static_cast<float> (columns);
    for (int c = 0; c < columns; ++c)
    {
        const float db = postAnalyzer.peakBetween (xToFreq (static_cast<float> (c) * step), xToFreq (static_cast<float> (c + 1) * step));
        data.setPixelColour (c, 0, spectrogramColour ((db + analyzerRange) / analyzerRange));
    }
}

void ResponseDisplay::setPianoStrip (bool show)
{
    piano = show;
    repaint();
}

void ResponseDisplay::setAnalyzerVisible (bool visible)
{
    analyzerVisible = visible;
    processor.analyzerActive = visible;   // no taps on the audio thread while hidden
    if (! visible)
    {
        preAnalyzer.clear();
        postAnalyzer.clear();
    }
}

void ResponseDisplay::rebuildCurves()
{
    compositePath.clear();
    selectedPath.clear();
    selectedFill.clear();
    rangePath.clear();
    targetPath.clear();
    if (hasTarget)
        for (int i = 0; i < fit::kGridSize; ++i)
        {
            const float x = freqToX (fit::gridFrequency (i)), y = dbToY (targetCurve[static_cast<size_t> (i)]);
            if (i == 0) targetPath.startNewSubPath (x, y); else targetPath.lineTo (x, y);
        }
    const int w = getWidth();
    if (w <= 0)
        return;

    // The drawn curve is the analog prototype, the same function the filters are designed
    // to match (docs/spec/03-dsp.md section 1).
    std::array<Prototype, kMaxBands> protos;
    std::array<bool, kMaxBands> audible {};
    for (int i = 0; i < kMaxBands; ++i)
    {
        auto b = bands[static_cast<size_t> (i)];
        if (hasGain (b.shape))
            b.gainDb *= scale;
        audible[static_cast<size_t> (i)] = b.audible();
        if (b.audible() || i == selected)
            protos[static_cast<size_t> (i)] = makePrototype (b);
    }

    const float zeroY = dbToY (0.0);
    const bool hasSelection = selected >= 0 && bands[static_cast<size_t> (selected)].used;

    // A dynamic band also shows where it ends up at full detection.
    Prototype rangeProto;
    const bool showRange = hasSelection && (bands[static_cast<size_t> (selected)].dynamic() || bands[static_cast<size_t> (selected)].spectral());
    if (showRange)
    {
        auto b = bands[static_cast<size_t> (selected)];
        b.gainDb = (b.gainDb + b.dyn.rangeDb) * scale;
        rangeProto = makePrototype (b);
    }
    if (hasSelection)
        selectedFill.startNewSubPath (0.0f, zeroY);

    for (int x = 0; x <= w; x += 2)
    {
        const double f = xToFreq (static_cast<float> (x));
        double sum = 0.0;
        for (int i = 0; i < kMaxBands; ++i)
            if (audible[static_cast<size_t> (i)])
                sum += protos[static_cast<size_t> (i)].magnitudeDb (f);

        const float y = dbToY (sum);
        if (x == 0) compositePath.startNewSubPath (0.0f, y); else compositePath.lineTo (static_cast<float> (x), y);

        if (hasSelection)
        {
            const float sy = dbToY (protos[static_cast<size_t> (selected)].magnitudeDb (f));
            if (x == 0) selectedPath.startNewSubPath (0.0f, sy); else selectedPath.lineTo (static_cast<float> (x), sy);
            selectedFill.lineTo (static_cast<float> (x), sy);
        }
        if (showRange)
        {
            const float ry = dbToY (rangeProto.magnitudeDb (f));
            if (x == 0) rangePath.startNewSubPath (0.0f, ry); else rangePath.lineTo (static_cast<float> (x), ry);
        }
    }
    if (hasSelection)
    {
        selectedFill.lineTo (static_cast<float> (w), zeroY);
        selectedFill.closeSubPath();
    }
}

//==============================================================================
void ResponseDisplay::paint (juce::Graphics& g)
{
    g.fillAll (theme::ground);
    const float w = static_cast<float> (getWidth());
    const float plotBottom = static_cast<float> (getHeight() - kAxisHeight);

    // Grid.
    static const double majors[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static const double minors[] = { 30, 40, 60, 70, 80, 90, 300, 400, 600, 700, 800, 900, 3000, 4000, 6000, 7000, 8000, 9000 };
    g.setColour (theme::gridMinor);
    for (double f : minors)
        g.fillRect (std::round (freqToX (f)), 0.0f, 1.0f, plotBottom);
    g.setColour (theme::gridMajor);
    for (double f : majors)
        g.fillRect (std::round (freqToX (f)), 0.0f, 1.0f, plotBottom);

    const double stepDb = rangeDb / 4.0;
    g.setColour (theme::gridH);
    for (int i = -4; i <= 4; ++i)
        if (i != 0)
            g.fillRect (0.0f, std::round (dbToY (i * stepDb)), w, 1.0f);
    // The 0 dB line is lit.
    const float zeroLine = std::round (dbToY (0.0));
    g.setColour (theme::accent.withAlpha (0.07f));
    g.fillRect (0.0f, zeroLine - 2.0f, w, 5.0f);
    g.setColour (theme::accent.withAlpha (0.45f));
    g.fillRect (0.0f, zeroLine, w, 1.0f);

    // Axis labels.
    g.setFont (theme::mono (10.0f));
    g.setColour (theme::textFaint);
    for (double f : majors)
        g.drawText (f >= 1000.0 ? juce::String (juce::roundToInt (f / 1000.0)) + "k" : juce::String (juce::roundToInt (f)),
                    juce::Rectangle<float> (freqToX (f) - 20.0f, plotBottom + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    for (int i = -2; i <= 2; ++i)
    {
        const double db = i * rangeDb / 2.0;
        g.drawText ((db > 0 ? "+" : "") + juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (w - 44.0f, dbToY (db) - 7.0f, 36.0f, 14.0f), juce::Justification::centredRight, false);
    }

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (0, 0, getWidth(), getHeight() - kAxisHeight);

        // Analyzer: a scrolling spectrogram of the output, or the input as a quiet fill with
        // the output as an accent line.
        if (spectrogramView && spectrogram.isValid())
        {
            // The ring is drawn in two pieces so the newest row is always at the top.
            const int rows = spectrogram.getHeight(), cols = spectrogram.getWidth();
            const int firstPiece = rows - spectrogramRow;
            const int splitY = juce::roundToInt (plotBottom * static_cast<float> (firstPiece) / static_cast<float> (rows));
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.setOpacity (0.9f);
            g.drawImage (spectrogram, 0, 0, getWidth(), splitY, 0, spectrogramRow, cols, firstPiece);
            if (spectrogramRow > 0)
                g.drawImage (spectrogram, 0, splitY, getWidth(), juce::roundToInt (plotBottom) - splitY, 0, 0, cols, spectrogramRow);
            g.setOpacity (1.0f);
        }
        else if (! preFill.isEmpty())
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6C7BFF).withAlpha (0.26f), 0.0f, 0.0f,
                                                     juce::Colour (0xff6C7BFF).withAlpha (0.04f), 0.0f, plotBottom, false));
            g.fillPath (preFill);
            g.setColour (theme::accent.withAlpha (0.10f));
            g.strokePath (postLine, juce::PathStrokeType (5.0f));
            g.setColour (theme::accent.withAlpha (0.75f));
            g.strokePath (postLine, juce::PathStrokeType (1.2f));
            if (! scLine.isEmpty())   // the sidechain, in the warning colour
            {
                g.setColour (theme::warn.withAlpha (0.6f));
                g.strokePath (scLine, juce::PathStrokeType (1.0f));
            }
        }

        // Selected band: fill and outline in its colour.
        if (! selectedPath.isEmpty())
        {
            const auto c = theme::band (selected);
            g.setColour (c.withAlpha (0.18f));
            g.fillPath (selectedFill);
            theme::strokeGlow (g, selectedPath, c, 1.4f, 0.8f);
            if (! rangePath.isEmpty())
            {
                juce::Path dashed;
                const float dashes[] = { 3.0f, 5.0f };
                juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createDashedStroke (dashed, rangePath, dashes, 2);
                g.setColour (c.withAlpha (0.85f));
                g.fillPath (dashed);
            }
        }

        // Other instances: their spectra in their own colours, and a strip along the bottom
        // wherever one of them sits in the same place as this instance.
        const float plotH = static_cast<float> (getHeight() - kAxisHeight);
        for (size_t o = 0; o < overlays.size(); ++o)
        {
            const auto& overlay = overlays[o];
            juce::Path line;
            for (int i = 0; i < fit::kGridSize; ++i)
            {
                const double f = fit::gridFrequency (i);
                const float db = static_cast<float> (overlay.spectrum[static_cast<size_t> (i)] + overlayTilt * std::log2 (f / 1000.0));
                const float y = plotH * (1.0f - juce::jlimit (0.0f, 1.0f, (db + analyzerRange) / analyzerRange));
                if (i == 0) line.startNewSubPath (freqToX (f), y); else line.lineTo (freqToX (f), y);
            }
            g.setColour (overlay.colour.withAlpha (0.12f));
            g.strokePath (line, juce::PathStrokeType (5.0f));
            g.setColour (overlay.colour.withAlpha (0.85f));
            g.strokePath (line, juce::PathStrokeType (1.2f));

            const float stripY = plotH - 8.0f - 7.0f * static_cast<float> (o);
            for (int i = 0; i + 1 < fit::kGridSize; ++i)
            {
                const double strength = overlay.collision[static_cast<size_t> (i)];
                if (strength <= 0.0)
                    continue;
                const float x0 = freqToX (fit::gridFrequency (i)), x1 = freqToX (fit::gridFrequency (i + 1));
                g.setColour (overlay.colour.withAlpha (static_cast<float> (0.25 + 0.65 * strength)));
                g.fillRect (x0, stripY, x1 - x0 + 0.5f, 5.0f);
            }
        }

        // EQ match target and the curve being sketched.
        if (! targetPath.isEmpty())
        {
            juce::Path dashed;
            const float dashes[] = { 6.0f, 5.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, targetPath, dashes, 2);
            g.setColour (theme::warn);
            g.fillPath (dashed);
        }
        if (! sketchPath.isEmpty())
            theme::strokeGlow (g, sketchPath, theme::warn, 2.0f);

        // Composite curve: a white-hot core inside an accent halo.
        theme::strokeGlow (g, compositePath, theme::accent, 2.6f, 1.4f);
        g.setColour (theme::curve);
        g.strokePath (compositePath, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Faint scanlines over the plot.
        g.setColour (juce::Colours::black.withAlpha (theme::isLight() ? 0.035f : 0.13f));
        for (int y = 0; y < getHeight() - kAxisHeight; y += 3)
            g.fillRect (0, y, getWidth(), 1);
    }

    // Piano strip: one key per semitone along the bottom of the plot, C notes labelled.
    if (piano)
    {
        const float top = plotBottom - 14.0f;
        g.setFont (theme::mono (9.0f));
        for (int note = 12; note <= 135; ++note)
        {
            const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const float x0 = freqToX (f * std::pow (2.0, -1.0 / 24.0)), x1 = freqToX (f * std::pow (2.0, 1.0 / 24.0));
            if (x1 < 0.0f || x0 > w)
                continue;
            const int pitch = note % 12;
            const bool black = pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
            g.setColour (black ? theme::ground : theme::lineStrong.withAlpha (0.75f));
            g.fillRect (x0 + 0.5f, top, juce::jmax (1.0f, x1 - x0 - 1.0f), 14.0f);
            if (pitch == 0 && x1 - x0 > 5.0f)
            {
                g.setColour (theme::text);
                g.drawText ("C" + juce::String (note / 12 - 1), juce::Rectangle<float> (x0 + 2.0f, top, 24.0f, 14.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    // Nodes.
    g.setFont (theme::mono (11.0f));
    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto& b = bands[static_cast<size_t> (i)];
        if (! b.used)
            continue;
        const auto p = nodePosition (b);
        const auto c = theme::band (i);
        const auto circle = juce::Rectangle<float> (2.0f * kNodeRadius, 2.0f * kNodeRadius).withCentre (p);

        // A dynamic band shows its live gain as a marker on a stem from the node.
        if (b.dynamic() || b.spectral())
        {
            const float liveY = dbToY ((b.gainDb + liveDyn[static_cast<size_t> (i)]) * scale);
            g.setColour (c.withAlpha (0.7f));
            g.fillRect (p.x - 0.75f, juce::jmin (p.y, liveY), 1.5f, std::abs (liveY - p.y));
            theme::fillGlowEllipse (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ p.x, liveY }), c, 0.9f);
            g.setColour (c.withAlpha (0.6f));
            g.drawEllipse (circle.expanded (6.5f), 1.0f);   // second ring marks the band as dynamic
        }

        if (i == selected)
        {
            g.setColour (c);
            g.drawEllipse (circle.expanded (3.5f), 2.0f);
        }
        else if (inSelection[static_cast<size_t> (i)])   // selected along with the primary band
        {
            g.setColour (c.withAlpha (0.75f));
            g.drawEllipse (circle.expanded (3.5f), 1.2f);
        }
        else if (i == hovered)
        {
            g.setColour (c.withAlpha (0.4f));
            g.drawEllipse (circle.expanded (2.5f), 2.0f);
        }

        if (b.on)
        {
            theme::fillGlowEllipse (g, circle, c, i == selected ? 1.5f : 1.0f);
            g.setColour (theme::ground);
        }
        else   // bypassed: outline only
        {
            g.setColour (theme::ground);
            g.fillEllipse (circle);
            g.setColour (c);
            g.drawEllipse (circle.reduced (0.75f), 1.5f);
        }
        g.drawText (juce::String (i + 1), circle, juce::Justification::centred, false);

        if (b.place != Placement::Stereo)
        {
            static const char* tags[] = { "", "L", "R", "M", "S" };
            g.setColour (c);
            g.setFont (theme::mono (10.0f));
            g.drawText (tags[static_cast<int> (b.place)], circle.translated (0.0f, kNodeRadius + 10.0f), juce::Justification::centred, false);
            g.setFont (theme::mono (11.0f));
        }
    }

    if (rubbering && ! rubber.isEmpty())
    {
        g.setColour (theme::accent.withAlpha (0.08f));
        g.fillRect (rubber);
        g.setColour (theme::accent.withAlpha (0.7f));
        g.drawRect (rubber, 1.0f);
    }

    // Value tag next to the selected band.
    if (selected >= 0 && bands[static_cast<size_t> (selected)].used)
    {
        const auto& b = bands[static_cast<size_t> (selected)];
        const auto p = nodePosition (b);
        const auto freqText = params::formatFrequency (b.freq);
        const auto gainText = hasGain (b.shape) ? params::formatGain (b.gainDb) : params::shapeNames()[static_cast<int> (b.shape)];
        const auto font = theme::mono (11.0f);
        const float fw = juce::GlyphArrangement::getStringWidth (font, freqText), gw = juce::GlyphArrangement::getStringWidth (font, gainText);
        juce::Rectangle<float> tag (p.x + 20.0f, p.y - 42.0f, fw + gw + 26.0f, 24.0f);
        if (tag.getRight() > w - 50.0f) tag.setX (p.x - 20.0f - tag.getWidth());
        if (tag.getY() < 6.0f) tag.setY (p.y + 18.0f);

        g.setColour (theme::raised);
        g.fillRoundedRectangle (tag, 6.0f);
        g.setColour (theme::lineStrong);
        g.drawRoundedRectangle (tag.reduced (0.5f), 6.0f, 1.0f);
        g.setFont (font);
        g.setColour (theme::text);
        g.drawText (freqText, tag.withTrimmedLeft (8.0f).withWidth (fw + 2.0f), juce::Justification::centredLeft, false);
        g.setColour (theme::band (selected));
        g.drawText (gainText, tag.withTrimmedRight (8.0f), juce::Justification::centredRight, false);
    }

    if (std::none_of (bands.begin(), bands.end(), [] (const BandParams& b) { return b.used; }))
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::ui (13.0f));
        g.drawText ("Double-click to add a band", getLocalBounds().withTrimmedBottom (kAxisHeight).translated (0, 40), juce::Justification::centred, false);
    }
}

//==============================================================================
void ResponseDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = bandAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ResponseDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

void ResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (sketching())
    {
        sketchPoints.assign (1, e.position);
        sketchPath.clear();
        sketchPath.startNewSubPath (e.position);
        return;
    }
    const int band = bandAt (e.position);

    if (band < 0)
    {
        // Empty space: a drag from here is a rubber band; a plain click deselects.
        rubberStart = e.position;
        rubber = {};
        rubbering = true;
        return;
    }

    if (e.mods.isShiftDown() && ! e.mods.isPopupMenu())
    {
        // Shift-click adds a band to the selection or takes it out.
        auto& member = inSelection[static_cast<size_t> (band)];
        member = ! member;
        if (member)
            setPrimary (band);
        else if (band == selected)
        {
            int other = -1;
            for (int i = 0; i < kMaxBands; ++i)
                if (inSelection[static_cast<size_t> (i)])
                    other = i;
            setPrimary (other);
        }
        repaint();
        return;
    }

    if (! inSelection[static_cast<size_t> (band)])
        selectBand (band);   // clicking outside the selection replaces it
    else
        setPrimary (band);

    if (e.mods.isPopupMenu())
    {
        showBandMenu (band);
        return;
    }
    if (e.mods.isAltDown())   // toggle bypass
    {
        processor.undo.beginNewTransaction ("Toggle band");
        processor.setBandValue (band, "on", bands[static_cast<size_t> (band)].on ? 0.0f : 1.0f);
        return;
    }

    // Drag: the grabbed band follows the pointer; the rest of the selection moves with it,
    // by the same frequency ratio and the same gain difference.
    dragging = band;
    dragHasGain = hasGain (bands[static_cast<size_t> (band)].shape);
    dragOffset = nodePosition (bands[static_cast<size_t> (band)]) - e.position;
    dragStart = bands;
    processor.undo.beginNewTransaction (selectionCount() > 1 ? juce::String ("Move bands") : "Move band " + juce::String (band + 1));
    for (int i = 0; i < kMaxBands; ++i)
        if (inSelection[static_cast<size_t> (i)])
        {
            processor.bandParam (i, "freq")->beginChangeGesture();
            processor.bandParam (i, "gain")->beginChangeGesture();
        }
}

void ResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (sketching())
    {
        if (! sketchPoints.empty())
        {
            sketchPoints.push_back (e.position);
            sketchPath.lineTo (e.position);
            repaint();
        }
        return;
    }
    if (rubbering)
    {
        rubber = juce::Rectangle<float> (rubberStart, e.position);
        for (int i = 0; i < kMaxBands; ++i)
            inSelection[static_cast<size_t> (i)] = bands[static_cast<size_t> (i)].used && rubber.contains (nodePosition (bands[static_cast<size_t> (i)]));
        repaint();
        return;
    }
    if (dragging < 0)
        return;
    auto p = e.position + dragOffset;
    if (e.mods.isShiftDown())   // fine: a tenth of the pointer travel
        p = e.mouseDownPosition + dragOffset + (e.position - e.mouseDownPosition) * 0.1f;

    const auto& grabbed = dragStart[static_cast<size_t> (dragging)];
    const double s = std::abs (scale) > 1.0e-6 ? scale : 1.0;
    const double ratio = juce::jlimit (10.0, 30000.0, xToFreq (p.x)) / grabbed.freq;
    const double gainDelta = dragHasGain ? juce::jlimit (-30.0, 30.0, yToDb (p.y) / s) - grabbed.gainDb : 0.0;

    for (int i = 0; i < kMaxBands; ++i)
    {
        if (! inSelection[static_cast<size_t> (i)])
            continue;
        const auto& from = dragStart[static_cast<size_t> (i)];
        processor.setBandValue (i, "freq", static_cast<float> (juce::jlimit (10.0, 30000.0, from.freq * ratio)));
        if (hasGain (from.shape))
            processor.setBandValue (i, "gain", static_cast<float> (juce::jlimit (-30.0, 30.0, from.gainDb + gainDelta)));
    }
    refresh();
}

void ResponseDisplay::endDrag()
{
    if (dragging < 0)
        return;
    for (int i = 0; i < kMaxBands; ++i)
        if (inSelection[static_cast<size_t> (i)])
        {
            processor.bandParam (i, "freq")->endChangeGesture();
            processor.bandParam (i, "gain")->endChangeGesture();
        }
    dragging = -1;
}

void ResponseDisplay::mouseUp (const juce::MouseEvent&)
{
    if (sketching())
    {
        const int numBands = sketchBands;
        sketchBands = 0;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (sketchPoints.size() >= 4)
        {
            // Left to right, then onto the grid: between drawn points interpolate, outside them
            // hold the nearest end.
            std::sort (sketchPoints.begin(), sketchPoints.end(), [] (auto a, auto b) { return a.x < b.x; });
            fit::Curve drawn {};
            size_t at = 0;
            for (int i = 0; i < fit::kGridSize; ++i)
            {
                const float x = freqToX (fit::gridFrequency (i));
                while (at + 1 < sketchPoints.size() && sketchPoints[at + 1].x < x)
                    ++at;
                float y = sketchPoints[at].y;
                if (x <= sketchPoints.front().x)       y = sketchPoints.front().y;
                else if (x >= sketchPoints.back().x)   y = sketchPoints.back().y;
                else if (at + 1 < sketchPoints.size() && sketchPoints[at + 1].x > sketchPoints[at].x)
                    y = juce::jmap (x, sketchPoints[at].x, sketchPoints[at + 1].x, sketchPoints[at].y, sketchPoints[at + 1].y);
                const double s = std::abs (scale) > 1.0e-6 ? scale : 1.0;
                drawn[static_cast<size_t> (i)] = juce::jlimit (-24.0, 24.0, yToDb (y) / s);
            }
            processor.replaceBands (fit::fitCurve (fit::smooth (drawn, 1.0 / 6.0), numBands), "Sketch curve");
        }
        sketchPoints.clear();
        sketchPath.clear();
        refresh();
        repaint();
        return;
    }
    if (rubbering)
    {
        rubbering = false;
        if (rubber.getWidth() < 3.0f && rubber.getHeight() < 3.0f)
            selectBand (-1);   // a click on empty space
        else
        {
            int first = -1;
            for (int i = kMaxBands; --i >= 0;)
                if (inSelection[static_cast<size_t> (i)])
                    first = i;
            setPrimary (first);
        }
        rubber = {};
        repaint();
        return;
    }
    endDrag();
}

void ResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (sketching())
        return;
    const int band = bandAt (e.position);
    if (band >= 0)
    {
        if (e.mods.isCommandDown())   // reset gain
        {
            processor.undo.beginNewTransaction ("Reset gain");
            processor.setBandValue (band, "gain", 0.0f);
        }
        return;
    }
    if (e.position.y > static_cast<float> (getHeight() - kAxisHeight))
        return;

    // Shape by horizontal position (docs/spec/05-ui.md section 6.1).
    const float rel = e.position.x / static_cast<float> (juce::jmax (1, getWidth()));
    const Shape shape = rel < 0.06f ? Shape::LowCut : rel < 0.16f ? Shape::LowShelf
                      : rel > 0.94f ? Shape::HighCut : rel > 0.84f ? Shape::HighShelf : Shape::Bell;
    const double s = std::abs (scale) > 1.0e-6 ? scale : 1.0;
    const int added = processor.addBand (shape, xToFreq (e.position.x), juce::jlimit (-30.0, 30.0, yToDb (e.position.y) / s));
    refresh();
    if (added >= 0)
        selectBand (added);
}

void ResponseDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = bandAt (e.position) >= 0 ? bandAt (e.position) : selected;
    if (band < 0 || dragging >= 0)
        return;
    const double factor = std::pow (e.mods.isShiftDown() ? 1.05 : 1.5, static_cast<double> (wheel.deltaY) * 4.0);
    // Over a selected band the whole selection changes Q by the same ratio.
    for (int i = 0; i < kMaxBands; ++i)
        if (i == band || (inSelection[static_cast<size_t> (band)] && inSelection[static_cast<size_t> (i)]))
            processor.setBandValue (i, "q", static_cast<float> (juce::jlimit (0.025, 40.0, bands[static_cast<size_t> (i)].q * factor)));
    refresh();
}

bool ResponseDisplay::keyStateChanged (bool)
{
    // Hold S to audition the selected band; release to return.
    const bool down = juce::KeyPress::isKeyCurrentlyDown ('s') && selected >= 0;
    if (down != soloHeld)
    {
        soloHeld = down;
        processor.setSoloBand (down ? selected : -1);
        return true;
    }
    return false;
}

void ResponseDisplay::focusLost (FocusChangeType)
{
    if (soloHeld)
    {
        soloHeld = false;
        processor.setSoloBand (-1);
    }
}

bool ResponseDisplay::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
        return true;   // handled by keyStateChanged; do not pass it to the host

    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    // Tab and Shift+Tab step through the bands in order of frequency; past the last band Tab
    // moves on to the next control as usual.
    if (code == juce::KeyPress::tabKey)
    {
        std::vector<int> order;
        for (int i = 0; i < kMaxBands; ++i)
            if (bands[static_cast<size_t> (i)].used)
                order.push_back (i);
        std::sort (order.begin(), order.end(), [this] (int a, int b) { return bands[static_cast<size_t> (a)].freq < bands[static_cast<size_t> (b)].freq; });
        const auto it = std::find (order.begin(), order.end(), selected);
        const bool back = mods.isShiftDown();
        if (order.empty() || (it != order.end() && (back ? it == order.begin() : it + 1 == order.end())))
            return false;   // off the end: let focus move on
        selectBand (it == order.end() ? (back ? order.back() : order.front()) : *(back ? it - 1 : it + 1));
        return true;
    }

    // 1..9 and 0 select bands 1..10.
    if (! mods.isAnyModifierKeyDown() && code >= '0' && code <= '9')
    {
        const int band = code == '0' ? 9 : code - '1';
        if (bands[static_cast<size_t> (band)].used)
            selectBand (band);
        return true;
    }
    // Paste: bands from the clipboard (the same JSON a preset's "bands" array holds).
    if (mods.isCommandDown() && (code == 'v' || code == 'V'))
    {
        juce::var parsed;
        if (juce::JSON::parse (juce::SystemClipboard::getTextFromClipboard(), parsed).wasOk() && parsed.isArray())
        {
            processor.undo.beginNewTransaction ("Paste bands");
            int last = -1;
            for (const auto& item : *parsed.getArray())
                last = juce::jmax (last, presets::addBandFromVar (processor, item));
            refresh();
            if (last >= 0)
                selectBand (last);
        }
        return true;
    }
    // Select every band.
    if (mods.isCommandDown() && (code == 'a' || code == 'A'))
    {
        int first = -1;
        for (int i = kMaxBands; --i >= 0;)
        {
            inSelection[static_cast<size_t> (i)] = bands[static_cast<size_t> (i)].used;
            if (bands[static_cast<size_t> (i)].used)
                first = i;
        }
        if (selected < 0 || ! bands[static_cast<size_t> (selected)].used)
            setPrimary (first);
        repaint();
        return true;
    }
    if (selected < 0)
        return false;

    // Copy and duplicate the selection.
    if (mods.isCommandDown() && (code == 'c' || code == 'C'))
    {
        juce::Array<juce::var> list;
        for (int i = 0; i < kMaxBands; ++i)
            if (inSelection[static_cast<size_t> (i)])
                list.add (presets::bandToVar (processor, i));
        juce::SystemClipboard::copyTextToClipboard (juce::JSON::toString (juce::var (list)));
        return true;
    }
    if (mods.isCommandDown() && (code == 'd' || code == 'D'))
    {
        processor.undo.beginNewTransaction ("Duplicate bands");
        const auto members = inSelection;
        int copy = -1;
        for (int i = 0; i < kMaxBands; ++i)
            if (members[static_cast<size_t> (i)])
                copy = juce::jmax (copy, presets::addBandFromVar (processor, presets::bandToVar (processor, i)));
        refresh();
        if (copy >= 0)
            selectBand (copy);
        return true;
    }
    const auto& b = bands[static_cast<size_t> (selected)];

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        const auto members = inSelection;
        for (int i = 0; i < kMaxBands; ++i)
            if (members[static_cast<size_t> (i)])
                processor.removeBand (i);
        refresh();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        const double semis = (key.getModifiers().isShiftDown() ? 0.1 : 1.0) * (key.getKeyCode() == juce::KeyPress::leftKey ? -1.0 : 1.0);
        processor.undo.beginNewTransaction ("Nudge frequency");
        for (int i = 0; i < kMaxBands; ++i)
            if (inSelection[static_cast<size_t> (i)])
                processor.setBandValue (i, "freq", static_cast<float> (juce::jlimit (10.0, 30000.0, bands[static_cast<size_t> (i)].freq * std::pow (2.0, semis / 12.0))));
        refresh();
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey) && hasGain (b.shape))
    {
        const double step = (key.getModifiers().isShiftDown() ? 0.1 : 0.5) * (key.getKeyCode() == juce::KeyPress::upKey ? 1.0 : -1.0);
        processor.undo.beginNewTransaction ("Nudge gain");
        for (int i = 0; i < kMaxBands; ++i)
            if (inSelection[static_cast<size_t> (i)] && hasGain (bands[static_cast<size_t> (i)].shape))
                processor.setBandValue (i, "gain", static_cast<float> (juce::jlimit (-30.0, 30.0, bands[static_cast<size_t> (i)].gainDb + step)));
        refresh();
        return true;
    }
    return false;
}

void ResponseDisplay::showBandMenu (int band)
{
    juce::PopupMenu menu, shapes, slopes, places;
    const auto& b = bands[static_cast<size_t> (band)];
    for (int i = 0; i < params::shapeNames().size(); ++i)
        shapes.addItem (100 + i, params::shapeLabels()[i], true, static_cast<int> (b.shape) == i);
    for (int i = 0; i < params::slopeNames().size(); ++i)
        slopes.addItem (200 + i, params::slopeNames()[i], true, static_cast<int> (b.slope) == i);
    for (int i = 0; i < params::placementNames().size(); ++i)
        places.addItem (300 + i, params::placementNames()[i], true, static_cast<int> (b.place) == i);
    menu.addSubMenu ("Shape", shapes);
    menu.addSubMenu ("Slope", slopes);
    menu.addSubMenu ("Placement", places);
    menu.addSeparator();
    menu.addItem (1, b.on ? "Bypass" : "Enable");
    menu.addItem (2, "Invert gain", hasGain (b.shape));
    menu.addItem (3, "Delete");

    juce::Component::SafePointer<ResponseDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (getTopLevelComponent()), [safe, band, b] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& proc = safe->processor;
        proc.undo.beginNewTransaction ("Edit band " + juce::String (band + 1));
        if (result == 1) proc.setBandValue (band, "on", b.on ? 0.0f : 1.0f);
        else if (result == 2) proc.setBandValue (band, "gain", static_cast<float> (-b.gainDb));
        else if (result == 3) proc.removeBand (band);
        else if (result >= 300) proc.setBandValue (band, "place", static_cast<float> (result - 300));
        else if (result >= 200) proc.setBandValue (band, "slope", static_cast<float> (result - 200));
        else if (result >= 100) proc.setBandValue (band, "shape", static_cast<float> (result - 100));
        safe->refresh();
    });
}
} // namespace of
