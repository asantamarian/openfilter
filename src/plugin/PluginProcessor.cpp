#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace of
{
OpenFilterProcessor::OpenFilterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "Params", params::createLayout())
{
    const auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto id = [b] (const char* suffix) { return params::bandId (b, suffix); };
        bandPtrs[static_cast<size_t> (b)] = { raw (id ("used")), raw (id ("on")), raw (id ("shape")), raw (id ("slope")),
                                              raw (id ("freq")), raw (id ("gain")), raw (id ("q")), raw (id ("place")) };
    }
    gainScalePtr = raw ("gain_scale");
    outGainPtr = raw ("out_gain");
    outPanPtr = raw ("out_pan");
    invertPtr = raw ("out_invert");
    bypassPtr = raw ("bypass");
    deltaPtr = raw ("delta");
    bypassParam = apvts.getParameter ("bypass");
}

//==============================================================================
void OpenFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (16, samplesPerBlock);
    for (auto& w : work)
        w.assign (static_cast<size_t> (maxBlock), 0.0);
    tap.assign (static_cast<size_t> (maxBlock), 0.0f);
    engine.prepare (sampleRate, maxBlock, getMainBusNumOutputChannels());
    setLatencySamples (engine.latencySamples());
}

bool OpenFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto main = layouts.getMainOutputChannelSet();
    if (main != layouts.getMainInputChannelSet())
        return false;
    // The sidechain input bus arrives with the dynamics milestone (M3); see research item
    // R-7 in docs/spec/03-dsp.md before adding it.
    return main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo();
}

void OpenFilterProcessor::readSnapshot (ParamSnapshot& s) const noexcept
{
    const auto f = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& p = bandPtrs[static_cast<size_t> (b)];
        auto& out = s.bands[static_cast<size_t> (b)];
        out.used = f (p.used) > 0.5f;
        out.on = f (p.on) > 0.5f;
        out.shape = static_cast<Shape> (juce::jlimit (0, kNumShapes - 1, juce::roundToInt (f (p.shape))));
        out.slope = static_cast<Slope> (juce::jlimit (0, kNumSlopes - 1, juce::roundToInt (f (p.slope))));
        out.place = static_cast<Placement> (juce::jlimit (0, 4, juce::roundToInt (f (p.place))));
        out.freq = f (p.freq);
        out.gainDb = f (p.gain);
        out.q = f (p.q);
    }
    s.gainScale = f (gainScalePtr) * 0.01;
    s.outGainDb = f (outGainPtr);
    s.pan = f (outPanPtr) * 0.01;
    s.invert = f (invertPtr) > 0.5f;
    s.bypass = f (bypassPtr) > 0.5f;
    s.delta = f (deltaPtr) > 0.5f;
}

template <typename Sample>
void OpenFilterProcessor::process (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin (2, getMainBusNumOutputChannels(), buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    if (channels <= 0 || total <= 0)
        return;

    ParamSnapshot snapshot;
    readSnapshot (snapshot);

    // Hosts may send more samples than they announced: work in chunks of maxBlock.
    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - pos);
        double* io[2] = { work[0].data(), work[1].data() };
        for (int ch = 0; ch < channels; ++ch)
        {
            const Sample* src = buffer.getReadPointer (ch, pos);
            for (int i = 0; i < n; ++i)
                io[ch][i] = static_cast<double> (src[i]);
        }

        const bool analyse = analyzerActive.load (std::memory_order_relaxed);
        const auto pushTap = [&] (SpscFifo& fifo)
        {
            for (int i = 0; i < n; ++i)
                tap[static_cast<size_t> (i)] = static_cast<float> (channels > 1 ? 0.5 * (io[0][i] + io[1][i]) : io[0][i]);
            fifo.push (tap.data(), static_cast<size_t> (n));
        };

        if (analyse)
            pushTap (preFifo);
        engine.process (io, channels, n, snapshot);
        if (analyse)
            pushTap (postFifo);

        for (int ch = 0; ch < channels; ++ch)
        {
            Sample* dst = buffer.getWritePointer (ch, pos);
            for (int i = 0; i < n; ++i)
                dst[i] = static_cast<Sample> (io[ch][i]);
        }
    }
}

void OpenFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer); }
void OpenFilterProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { process (buffer); }

//==============================================================================
juce::AudioProcessorEditor* OpenFilterProcessor::createEditor() { return new OpenFilterEditor (*this); }

void OpenFilterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // docs/spec/06-state.md section 1.
    juce::ValueTree root ("OpenFilterEQ");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree ui ("Ui");
    ui.setProperty ("width", editorWidth, nullptr);
    ui.setProperty ("height", editorHeight, nullptr);
    root.appendChild (ui, nullptr);

    juce::MemoryOutputStream stream (dest, false);
    root.writeToStream (stream);
}

void OpenFilterProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0)
        return;

    auto root = juce::ValueTree::readFromData (data, static_cast<size_t> (size));
    if (! root.isValid())
        if (auto xml = juce::parseXML (juce::String::fromUTF8 (static_cast<const char*> (data), size)))
            root = juce::ValueTree::fromXml (*xml);

    if (! root.hasType ("OpenFilterEQ"))
        return;   // not ours: ignore

    const auto state = root.getChildWithName (apvts.state.getType());
    if (state.isValid())
    {
        apvts.replaceState (state.createCopy());
        undo.clearUndoHistory();
    }

    const auto ui = root.getChildWithName ("Ui");
    if (ui.isValid())
    {
        editorWidth = juce::jlimit (800, 8000, static_cast<int> (ui.getProperty ("width", 1280)));
        editorHeight = juce::jlimit (500, 8000, static_cast<int> (ui.getProperty ("height", 800)));
    }
}

//==============================================================================
juce::RangedAudioParameter* OpenFilterProcessor::bandParam (int band, const char* suffix) const
{
    return apvts.getParameter (params::bandId (band, suffix));
}

BandParams OpenFilterProcessor::readBand (int band) const
{
    ParamSnapshot s;   // small enough; keeps one code path for reading
    readSnapshot (s);
    return s.bands[static_cast<size_t> (juce::jlimit (0, kMaxBands - 1, band))];
}

double OpenFilterProcessor::gainScale() const { return gainScalePtr->load() * 0.01; }

void OpenFilterProcessor::setBandValue (int band, const char* suffix, float plainValue)
{
    if (auto* p = bandParam (band, suffix))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

int OpenFilterProcessor::addBand (Shape shape, double freq, double gainDb)
{
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (bandPtrs[static_cast<size_t> (b)].used->load() > 0.5f)
            continue;

        undo.beginNewTransaction ("Add band");
        setBandValue (b, "shape", static_cast<float> (shape));
        setBandValue (b, "slope", static_cast<float> (Slope::S12));
        setBandValue (b, "freq", static_cast<float> (freq));
        setBandValue (b, "gain", hasGain (shape) ? static_cast<float> (gainDb) : 0.0f);
        setBandValue (b, "q", isCut (shape) ? 0.7071f : 1.0f);
        setBandValue (b, "place", 0.0f);
        setBandValue (b, "on", 1.0f);
        setBandValue (b, "used", 1.0f);
        return b;
    }
    return -1;
}

void OpenFilterProcessor::removeBand (int band)
{
    undo.beginNewTransaction ("Delete band");
    setBandValue (band, "used", 0.0f);
    // Reset the slot so a later band (and its automation lanes) starts clean.
    for (const char* suffix : { "on", "shape", "slope", "freq", "gain", "q", "place" })
        if (auto* p = bandParam (band, suffix))
            p->setValueNotifyingHost (p->getDefaultValue());
}
} // namespace of

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new of::OpenFilterProcessor(); }
