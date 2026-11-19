// Loads the built plugin binaries the way a host does and exercises them
// (docs/spec/07-build-test-release.md section 4).
//   of_host_test "<path>/OpenFilter EQ.vst3" ["<path>/OpenFilter EQ.lv2" ...]
#include <juce_audio_utils/juce_audio_utils.h>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("    FAIL  %s\n", what.toRawUTF8());
    }
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName (128) == name)
            return p;
    return nullptr;
}

void set (juce::AudioPluginInstance& plugin, const juce::String& name, float normalised)
{
    if (auto* p = findParam (plugin, name))
        p->setValueNotifyingHost (normalised);
    else
        check (false, "parameter not found: " + name);
}

bool finite (const juce::AudioBuffer<float>& b, int channels, int n)
{
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

void fillNoise (juce::AudioBuffer<float>& b, juce::Random& random)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);
}

void testPlugin (juce::AudioPluginFormatManager& formats, const juce::String& path)
{
    std::printf ("%s\n", path.toRawUTF8());

    // LV2 plugins are found by scanning a folder for bundles, VST3 by file; scanning the
    // containing folder works for both.
    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    const juce::String wantedFormat = file.hasFileExtension ("lv2") ? "LV2" : "VST3";
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
    {
        if (format->getName() != wantedFormat)
            continue;
        for (const auto& id : format->searchPathsForPlugins (juce::FileSearchPath (file.getParentDirectory().getFullPathName()), false, false))
            format->findAllTypesForFile (found, id);
    }
    // An LV2 scan also returns whatever else is installed on the machine: keep only ours.
    for (int i = found.size(); --i >= 0;)
        if (found[i]->name != "OpenFilter EQ")
            found.remove (i);
    check (found.size() == 1, "the folder should hold exactly one " + wantedFormat + " plugin, found " + juce::String (found.size()));
    if (found.isEmpty())
        return;

    const auto& desc = *found[0];
    std::printf ("    %s by %s, format %s, version %s\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(),
                 desc.pluginFormatName.toRawUTF8(), desc.version.toRawUTF8());
    check (desc.name == "OpenFilter EQ", "plugin name");
    check (! desc.isInstrument, "must be an effect");

    juce::String error;
    auto plugin = formats.createPluginInstance (desc, 48000.0, 512, error);
    check (plugin != nullptr, "instantiation: " + error);
    if (plugin == nullptr)
        return;

    const int numParams = plugin->getParameters().size();
    std::printf ("    %d parameters, %d in / %d out channels, latency %d\n", numParams,
                 plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(), plugin->getLatencySamples());
    check (numParams >= 781, "expected at least 781 parameters");
    check (plugin->getLatencySamples() == 0, "zero latency");
    auto* freqParam = findParam (*plugin, "Band 1 Frequency");
    check (freqParam != nullptr && findParam (*plugin, "Band 32 Spectral Resolution") != nullptr, "band parameters are exposed by name");
    if (freqParam == nullptr)
    {
        for (int i = 0; i < juce::jmin (20, numParams); ++i)
            std::printf ("      parameter %d is called \"%s\"\n", i, plugin->getParameters()[i]->getName (128).toRawUTF8());
        return;
    }

    juce::MidiBuffer midi;
    juce::Random random (42);
    // Wide enough for the sidechain whether or not it is enabled.
    const int channels = juce::jmax (4, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());

    // 1. With no bands in use the plugin must pass audio through unchanged.
    plugin->prepareToPlay (48000.0, 512);
    {
        juce::AudioBuffer<float> buffer (channels, 512), reference;
        bool identical = true;
        for (int block = 0; block < 20; ++block)
        {
            fillNoise (buffer, random);
            reference.makeCopyOf (buffer);
            plugin->processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    identical = identical && juce::exactlyEqual (buffer.getSample (ch, i), reference.getSample (ch, i));
        }
        check (identical, "idle plugin is bit-transparent");
    }

    // 2. A +12 dB bell at 1 kHz must raise a 1 kHz sine by 12 dB.
    set (*plugin, "Band 1 Frequency", freqParam->getValueForText ("1000"));
    set (*plugin, "Band 1 Gain", 0.7f);   // -30..+30 dB: 0.7 is +12 dB
    set (*plugin, "Band 1 Used", 1.0f);
    {
        juce::AudioBuffer<float> buffer (channels, 512);
        double in = 0.0, out = 0.0;
        for (int block = 0; block < 100; ++block)
        {
            buffer.clear();
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.1f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * static_cast<float> (block * 512 + i) / 48000.0f);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            const double before = buffer.getRMSLevel (0, 0, 512);
            plugin->processBlock (buffer, midi);
            if (block >= 50)
            {
                in += before;
                out += buffer.getRMSLevel (0, 0, 512);
            }
        }
        const double gainDb = juce::Decibels::gainToDecibels (out / in);
        std::printf ("    1 kHz bell set to +12 dB measures %+.2f dB\n", gainDb);
        check (std::abs (gainDb - 12.0) < 0.1, "bell gain at its centre frequency");
    }

    // 2b. Natural phase mode: the host must be told the new latency, and the bell must still
    //     measure +12 dB once that latency is accounted for.
    if (auto* phase = findParam (*plugin, "Phase Mode"))
    {
        phase->setValueNotifyingHost (phase->getValueForText ("Natural"));
        juce::AudioBuffer<float> buffer (channels, 512);
        double in = 0.0, out = 0.0;
        for (int block = 0; block < 100; ++block)
        {
            buffer.clear();
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.1f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * static_cast<float> (block * 512 + i) / 48000.0f);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            const double before = buffer.getRMSLevel (0, 0, 512);
            plugin->processBlock (buffer, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            if (block >= 50)
            {
                in += before;
                out += buffer.getRMSLevel (0, 0, 512);
            }
        }
        std::printf ("    Natural mode: host sees %d samples of latency, bell measures %+.2f dB\n",
                     plugin->getLatencySamples(), juce::Decibels::gainToDecibels (out / in));
        check (plugin->getLatencySamples() == 76, "Natural mode at 48 kHz reports 76 samples of latency");
        check (std::abs (juce::Decibels::gainToDecibels (out / in) - 12.0) < 0.1, "bell gain in Natural mode");
        // Linear phase, Medium: 8192 taps, so 4096 + one 512-sample partition.
        phase->setValueNotifyingHost (phase->getValueForText ("Linear"));
        in = out = 0.0;
        for (int block = 0; block < 300; ++block)
        {
            buffer.clear();
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.1f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * static_cast<float> (block * 512 + i) / 48000.0f);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            const double before = buffer.getRMSLevel (0, 0, 512);
            plugin->processBlock (buffer, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            if (block >= 200)
            {
                in += before;
                out += buffer.getRMSLevel (0, 0, 512);
            }
        }
        std::printf ("    Linear mode: host sees %d samples of latency, bell measures %+.2f dB\n",
                     plugin->getLatencySamples(), juce::Decibels::gainToDecibels (out / in));
        check (plugin->getLatencySamples() == 4608, "Linear mode, Medium, at 48 kHz reports 4608 samples of latency");
        check (std::abs (juce::Decibels::gainToDecibels (out / in) - 12.0) < 0.1, "bell gain in Linear mode");

        phase->setValueNotifyingHost (0.0f);
        for (int block = 0; block < 6; ++block)   // LV2 reports latency only while running
        {
            buffer.clear();
            plugin->processBlock (buffer, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
        check (plugin->getLatencySamples() == 0, "back to zero latency");
    }
    else
        check (false, "parameter not found: Phase Mode");

    // 2bb. A spectral band adds one 2048-sample frame of latency, and removing it takes it away.
    if (auto* dyn = findParam (*plugin, "Band 1 Dynamics Mode"))
    {
        juce::AudioBuffer<float> buffer (channels, 512);
        const auto settleLatency = [&]
        {
            for (int block = 0; block < 6; ++block)
            {
                buffer.clear();
                plugin->processBlock (buffer, midi);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            }
        };
        dyn->setValueNotifyingHost (dyn->getValueForText ("Spectral"));
        settleLatency();
        std::printf ("    with a spectral band the host sees %d samples of latency\n", plugin->getLatencySamples());
        check (plugin->getLatencySamples() == 2048, "a spectral band reports 2048 samples of latency at 48 kHz");
        dyn->setValueNotifyingHost (0.0f);
        settleLatency();
        check (plugin->getLatencySamples() == 0, "latency returns to zero without spectral bands");
    }
    else
        check (false, "parameter not found: Band 1 Dynamics Mode");

    // 2c. Sidechain: optional in VST3 (off until the host enables it), always present in LV2.
    std::printf ("    input buses: %d, total input channels: %d\n", plugin->getBusCount (true), plugin->getTotalNumInputChannels());
    check (plugin->getBusCount (true) == 2, "main input plus a sidechain input bus");
    if (desc.pluginFormatName == "VST3")
    {
        check (plugin->getTotalNumInputChannels() == 2, "VST3 sidechain is off by default");
        plugin->releaseResources();
        const bool enabled = plugin->enableAllBuses();
        check (enabled && plugin->getTotalNumInputChannels() == 4, "VST3 sidechain can be enabled");
        plugin->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> wide (4, 512);
        bool ok = true;
        for (int block = 0; block < 20; ++block)
        {
            fillNoise (wide, random);
            plugin->processBlock (wide, midi);
            ok = ok && finite (wide, 2, 512);
        }
        check (ok, "processing with the sidechain connected");
    }

    // 3. Random block sizes, sample rates and parameter storms: never NaN or Inf.
    {
        bool allFinite = true;
        for (double rate : { 44100.0, 96000.0, 192000.0 })
        {
            plugin->releaseResources();
            plugin->prepareToPlay (rate, 1024);
            for (int block = 0; block < 300; ++block)
            {
                if (block % 4 == 0)
                    for (int i = 0; i < 40; ++i)
                        plugin->getParameters()[random.nextInt (juce::jmin (numParams, 13 + 8 * 24))]->setValueNotifyingHost (random.nextFloat());
                const int n = 1 + random.nextInt (1024);
                juce::AudioBuffer<float> buffer (channels, n);
                fillNoise (buffer, random);
                plugin->processBlock (buffer, midi);
                allFinite = allFinite && finite (buffer, 2, n);
            }
        }
        check (allFinite, "output stays finite under random parameters, block sizes and sample rates");
    }

    // 4. Double precision, if offered.
    if (plugin->supportsDoublePrecisionProcessing())
    {
        plugin->releaseResources();
        plugin->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        plugin->prepareToPlay (48000.0, 256);
        juce::AudioBuffer<double> buffer (channels, 256);
        bool ok = true;
        for (int block = 0; block < 50; ++block)
        {
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < 256; ++i)
                    buffer.setSample (ch, i, random.nextDouble() * 0.5 - 0.25);
            plugin->processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i)
                ok = ok && std::isfinite (buffer.getSample (0, i));
        }
        check (ok, "double-precision processing");
        plugin->releaseResources();
        plugin->setProcessingPrecision (juce::AudioProcessor::singlePrecision);
        plugin->prepareToPlay (48000.0, 512);
    }

    // 5. State: what is saved must come back, also into a second instance, and garbage must
    //    be ignored.
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        // Host-side parameter objects are updated from the plugin asynchronously, so let the
        // message loop run before reading them back. Discrete parameters are compared by
        // their text: the host may hold an unsnapped 0..1 value for them.
        // LV2 plugins only report their values to the host while they are running, so a few
        // blocks are processed as well.
        const auto settle = [&] (juce::AudioPluginInstance& instance)
        {
            juce::AudioBuffer<float> silence (channels, 512);
            for (int i = 0; i < 4; ++i)
            {
                silence.clear();
                instance.processBlock (silence, midi);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            }
        };
        const auto read = [&] (juce::AudioPluginInstance& instance)
        {
            juce::StringArray values;
            for (auto* p : instance.getParameters())
                values.add (p->isDiscrete() ? p->getText (p->getValue(), 64) : juce::String (p->getValue(), 4));
            return values;
        };
        const auto countDiffering = [&] (const juce::StringArray& a, const juce::StringArray& b)
        {
            int n = 0;
            for (int i = 0; i < a.size(); ++i)
                if (a[i] != b[i] && std::abs (a[i].getFloatValue() - b[i].getFloatValue()) > 2.0e-4f)
                {
                    if (++n <= 3)
                        std::printf ("      %s: \"%s\" became \"%s\"\n", plugin->getParameters()[i]->getName (64).toRawUTF8(), a[i].toRawUTF8(), b[i].toRawUTF8());
                }
                else if (a[i] != b[i] && (a[i].containsAnyOf ("abcdefghijklmnopqrstuvwxyz") || b[i].containsAnyOf ("abcdefghijklmnopqrstuvwxyz")))
                {
                    if (++n <= 3)
                        std::printf ("      %s: \"%s\" became \"%s\"\n", plugin->getParameters()[i]->getName (64).toRawUTF8(), a[i].toRawUTF8(), b[i].toRawUTF8());
                }
            return n;
        };
        settle (*plugin);
        const auto saved = read (*plugin);

        auto second = formats.createPluginInstance (desc, 48000.0, 512, error);
        check (second != nullptr, "second instance");
        if (second != nullptr)
        {
            second->prepareToPlay (48000.0, 512);
            second->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            settle (*second);
            const auto loaded = read (*second);
            const int differing = countDiffering (saved, loaded);
            check (differing == 0, juce::String (differing) + " parameters differ after loading the state into a new instance");

            const char garbage[] = "this is not a plugin state";
            std::printf ("    (a state-parsing error from the host on the next line is expected)\n");
            second->setStateInformation (garbage, sizeof (garbage));
            settle (*second);
            check (countDiffering (loaded, read (*second)) == 0, "garbage state must be ignored");
        }
        std::printf ("    state is %d bytes\n", static_cast<int> (state.getSize()));
    }

    // 6. Mono.
    {
        plugin->releaseResources();
        auto layout = plugin->getBusesLayout();
        for (auto& set : layout.inputBuses)  set = juce::AudioChannelSet::disabled();
        if (layout.inputBuses.size() > 0)  layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        if (layout.outputBuses.size() > 0) layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        if (plugin->setBusesLayout (layout))
        {
            plugin->prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buffer (juce::jmax (1, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), 512);
            bool ok = true;
            for (int block = 0; block < 20; ++block)
            {
                fillNoise (buffer, random);
                plugin->processBlock (buffer, midi);
                ok = ok && finite (buffer, 1, 512);
            }
            check (ok, "mono processing");
            std::printf ("    mono layout accepted\n");
        }
        else
            std::printf ("    mono layout not offered through this format wrapper (skipped)\n");
    }

    // 7. Editor: open and close repeatedly. An LV2 editor has to be embedded in a host window,
    //    which this headless test does not have, so it is only checked for VST3.
    if (desc.pluginFormatName == "LV2")
        std::printf ("    editor not exercised for LV2 (needs a host window)\n");
    else if (plugin->hasEditor())
    {
        int opened = 0;
        for (int i = 0; i < 10; ++i)
            if (std::unique_ptr<juce::AudioProcessorEditor> editor { plugin->createEditor() })
            {
                check (editor->getWidth() >= 1180 && editor->getHeight() >= 560, "editor size");
                ++opened;
            }
        check (opened == 10, "editor opens and closes 10 times");
    }
    else
        check (false, "plugin reports no editor");

    plugin->releaseResources();
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // keep output even if a plugin crashes
    // LV2 hosts find bundles through LV2_PATH: point it at the folder of the bundle under test.
    for (int i = 1; i < argc; ++i)
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[i]));
        if (file.hasFileExtension ("lv2"))
            ::setenv ("LV2_PATH", file.getParentDirectory().getFullPathName().toRawUTF8(), 1);
    }

    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    if (argc < 2)
    {
        std::printf ("usage: of_host_test <plugin path> [...]\n");
        return 2;
    }
    for (int i = 1; i < argc; ++i)
        testPlugin (formats, juce::String::fromUTF8 (argv[i]));

    std::printf (failures == 0 ? "host test passed\n" : "host test FAILED: %d problem(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
