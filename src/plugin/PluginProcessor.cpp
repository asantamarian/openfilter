#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace of
{
OpenFilterProcessor::OpenFilterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // The sidechain is optional, except in LV2: JUCE's LV2 wrapper reads
                          // every declared input port and crashes on an unconnected optional
                          // one, so there the ports are required and hosts always connect them.
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(),
                                      juce::PluginHostType::getPluginLoadedAs() == wrapperType_LV2)),
      apvts (*this, &undo, "Params", params::createLayout())
{
    const auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto id = [b] (const char* suffix) { return params::bandId (b, suffix); };
        bandPtrs[static_cast<size_t> (b)] = { raw (id ("used")), raw (id ("on")), raw (id ("shape")), raw (id ("slope")),
                                              raw (id ("freq")), raw (id ("gain")), raw (id ("q")), raw (id ("place")),
                                              raw (id ("dyn")), raw (id ("range")), raw (id ("thr")), raw (id ("athr")), raw (id ("ratio")),
                                              raw (id ("knee")), raw (id ("atk")), raw (id ("rel")), raw (id ("trig")), raw (id ("sc")) };
    }
    gainScalePtr = raw ("gain_scale");
    outGainPtr = raw ("out_gain");
    outPanPtr = raw ("out_pan");
    invertPtr = raw ("out_invert");
    bypassPtr = raw ("bypass");
    deltaPtr = raw ("delta");
    phaseModePtr = raw ("phase_mode");
    bypassParam = apvts.getParameter ("bypass");
    apvts.addParameterListener ("phase_mode", this);
}

OpenFilterProcessor::~OpenFilterProcessor()
{
    apvts.removeParameterListener ("phase_mode", this);
    cancelPendingUpdate();
}

void OpenFilterProcessor::updateLatency()
{
    const int mode = juce::roundToInt (phaseModePtr->load());
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    setLatencySamples (Engine::latencyFor (mode, rate));
}

//==============================================================================
void OpenFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (16, samplesPerBlock);
    for (auto& w : work)
        w.assign (static_cast<size_t> (maxBlock), 0.0);
    tap.assign (static_cast<size_t> (maxBlock), 0.0f);
    for (auto& w : scWork)
        w.assign (static_cast<size_t> (maxBlock), 0.0);
    engine.prepare (sampleRate, maxBlock, getMainBusNumOutputChannels());
    updateLatency();
}

bool OpenFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto main = layouts.getMainOutputChannelSet();
    if (main != layouts.getMainInputChannelSet())
        return false;
    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;
    const auto sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
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
        // Dynamics mode 1 is Dynamic; 2 (Spectral) is not built yet and behaves as Off.
        out.dyn.on = juce::roundToInt (f (p.dyn)) == 1;
        out.dyn.rangeDb = f (p.range);
        out.dyn.thresholdDb = f (p.thr);
        out.dyn.autoThreshold = f (p.athr) > 0.5f;
        out.dyn.ratio = f (p.ratio);
        out.dyn.kneeDb = f (p.knee);
        out.dyn.attackMs = f (p.atk);
        out.dyn.releaseMs = f (p.rel);
        out.dyn.below = f (p.trig) > 0.5f;
        out.dyn.external = f (p.sc) > 0.5f;
    }
    s.phaseMode = juce::roundToInt (f (phaseModePtr));
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

    // Sidechain input, when the host has enabled that bus.
    const auto* scBus = getBus (true, 1);
    const int scChannels = scBus != nullptr && scBus->isEnabled() ? juce::jmin (2, scBus->getNumberOfChannels()) : 0;
    const int scFirst = scChannels > 0 ? scBus->getChannelIndexInProcessBlockBuffer (0) : 0;
    const bool scUsable = scChannels > 0 && scFirst + scChannels <= buffer.getNumChannels();
    sidechainLive.store (scUsable, std::memory_order_relaxed);

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

        const double* sc[2] = { scWork[0].data(), scWork[1].data() };
        if (scUsable)
            for (int ch = 0; ch < scChannels; ++ch)
            {
                const Sample* src = buffer.getReadPointer (scFirst + ch, pos);
                for (int i = 0; i < n; ++i)
                    scWork[ch][static_cast<size_t> (i)] = static_cast<double> (src[i]);
            }

        if (analyse)
            pushTap (preFifo);
        engine.process (io, channels, n, snapshot, scUsable ? sc : nullptr, scUsable ? scChannels : 0);
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
    ui.setProperty ("preset", presetName, nullptr);
    ui.setProperty ("scale", uiScale, nullptr);
    ui.setProperty ("displayRange", displayRangeDb, nullptr);
    ui.setProperty ("analyzer", analyzerOn, nullptr);
    ui.setProperty ("table", tableOpen, nullptr);
    root.appendChild (ui, nullptr);

    juce::ValueTree slotsTree ("Slots");
    slotsTree.setProperty ("active", slot, nullptr);
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& values = slots[static_cast<size_t> (i)];
        if (i == slot || values.empty())
            continue;   // the active slot is the live parameters
        juce::ValueTree one ("Slot");
        one.setProperty ("index", i, nullptr);
        one.setProperty ("values", juce::MemoryBlock (values.data(), values.size() * sizeof (float)).toBase64Encoding(), nullptr);
        slotsTree.appendChild (one, nullptr);
    }
    root.appendChild (slotsTree, nullptr);

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
        updateLatency();
    }

    const auto ui = root.getChildWithName ("Ui");
    if (ui.isValid())
    {
        editorWidth = juce::jlimit (1180, 8000, static_cast<int> (ui.getProperty ("width", 1280)));
        editorHeight = juce::jlimit (560, 8000, static_cast<int> (ui.getProperty ("height", 800)));
        presetName = ui.getProperty ("preset", "").toString();
        uiScale = juce::jlimit (0.5f, 2.0f, static_cast<float> (ui.getProperty ("scale", 1.0)));
        const double range = ui.getProperty ("displayRange", 12.0);
        displayRangeDb = range <= 3.0 ? 3.0 : range <= 6.0 ? 6.0 : range <= 12.0 ? 12.0 : 30.0;
        analyzerOn = ui.getProperty ("analyzer", true);
        tableOpen = ui.getProperty ("table", false);
    }

    for (auto& values : slots)
        values.clear();
    slot = 0;
    const auto slotsTree = root.getChildWithName ("Slots");
    if (slotsTree.isValid())
    {
        slot = juce::jlimit (0, kNumSlots - 1, static_cast<int> (slotsTree.getProperty ("active", 0)));
        for (const auto& one : slotsTree)
        {
            const int index = one.getProperty ("index", -1);
            juce::MemoryBlock block;
            if (index >= 0 && index < kNumSlots && block.fromBase64Encoding (one.getProperty ("values").toString())
                && block.getSize() == static_cast<size_t> (getParameters().size()) * sizeof (float))
            {
                auto& values = slots[static_cast<size_t> (index)];
                values.resize (static_cast<size_t> (getParameters().size()));
                std::memcpy (values.data(), block.getData(), block.getSize());
            }
        }
    }
}

//==============================================================================
std::vector<float> OpenFilterProcessor::captureValues() const
{
    std::vector<float> values;
    values.reserve (static_cast<size_t> (getParameters().size()));
    for (auto* p : getParameters())
        values.push_back (p->getValue());
    return values;
}

void OpenFilterProcessor::applyValues (const std::vector<float>& values)
{
    const auto& all = getParameters();
    if (values.size() != static_cast<size_t> (all.size()))
        return;
    auto* delta = apvts.getParameter ("delta");
    for (int i = 0; i < all.size(); ++i)
    {
        auto* p = all[i];
        if (p == bypassParam || p == delta)
            continue;
        if (! juce::approximatelyEqual (p->getValue(), values[static_cast<size_t> (i)]))
            p->setValueNotifyingHost (values[static_cast<size_t> (i)]);
    }
}

void OpenFilterProcessor::switchSlot (int index)
{
    index = juce::jlimit (0, kNumSlots - 1, index);
    if (index == slot)
        return;
    slots[static_cast<size_t> (slot)] = captureValues();
    slot = index;
    // An unused slot starts as a copy of where you came from.
    if (! slots[static_cast<size_t> (slot)].empty())
    {
        undo.beginNewTransaction ("Switch to slot " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + slot)));
        applyValues (slots[static_cast<size_t> (slot)]);
    }
}

void OpenFilterProcessor::copyActiveSlotTo (int index)
{
    index = juce::jlimit (0, kNumSlots - 1, index);
    if (index != slot)
        slots[static_cast<size_t> (index)] = captureValues();
}

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
    for (const char* suffix : { "on", "shape", "slope", "freq", "gain", "q", "place", "lin", "dyn", "range", "thr", "athr",
                                "ratio", "knee", "atk", "rel", "trig", "sc", "scfree", "scfreq", "scq", "smode", "sres" })
        if (auto* p = bandParam (band, suffix))
            p->setValueNotifyingHost (p->getDefaultValue());
}
} // namespace of

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new of::OpenFilterProcessor(); }
