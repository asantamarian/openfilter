// A minimal CLAP host: loads the built .clap, checks what it declares, and runs audio
// through it (docs/spec/07-build-test-release.md section 4).
//   of_clap_test "<path>/OpenFilter EQ.clap"
#include <clap/clap.h>

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("    FAIL  %s\n", what);
    }
}

// The host side: no extensions offered, requests ignored.
const void* hostExtension (const clap_host_t*, const char*) { return nullptr; }
void hostNoop (const clap_host_t*) {}

uint32_t noEvents (const clap_input_events_t*) { return 0; }
const clap_event_header_t* noEvent (const clap_input_events_t*, uint32_t) { return nullptr; }
bool acceptEvent (const clap_output_events_t*, const clap_event_header_t*) { return true; }

int64_t writeToVector (const clap_ostream_t* stream, const void* buffer, uint64_t size)
{
    auto* out = static_cast<std::vector<uint8_t>*> (stream->ctx);
    const auto* bytes = static_cast<const uint8_t*> (buffer);
    out->insert (out->end(), bytes, bytes + size);
    return static_cast<int64_t> (size);
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    if (argc < 2)
    {
        std::printf ("usage: of_clap_test <plugin.clap>\n");
        return 2;
    }
    std::printf ("%s\n", argv[1]);

    void* library = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    check (library != nullptr, "the file loads as a shared library");
    if (library == nullptr)
    {
        std::printf ("    %s\n", dlerror());
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*> (dlsym (library, "clap_entry"));
    check (entry != nullptr, "exports clap_entry");
    if (entry == nullptr || ! entry->init (argv[1]))
    {
        check (false, "entry initialises");
        return 1;
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    check (factory != nullptr && factory->get_plugin_count (factory) == 1, "one plugin in the factory");
    const auto* desc = factory != nullptr ? factory->get_plugin_descriptor (factory, 0) : nullptr;
    if (desc == nullptr)
        return 1;
    std::printf ("    %s by %s, id %s, version %s\n", desc->name, desc->vendor, desc->id, desc->version);
    check (std::string (desc->id) == "org.openfilter.eq", "plugin id");
    check (std::string (desc->name) == "OpenFilter EQ", "plugin name");
    bool isEq = false;
    for (auto f = desc->features; f != nullptr && *f != nullptr; ++f)
        isEq = isEq || std::string (*f) == CLAP_PLUGIN_FEATURE_EQUALIZER;
    check (isEq, "declares the equalizer feature");

    clap_host_t host {};
    host.clap_version = CLAP_VERSION;
    host.name = "of_clap_test";
    host.vendor = "OpenFilter";
    host.url = "";
    host.version = "1";
    host.get_extension = hostExtension;
    host.request_restart = hostNoop;
    host.request_process = hostNoop;
    host.request_callback = hostNoop;

    const auto* plugin = factory->create_plugin (factory, &host, desc->id);
    check (plugin != nullptr && plugin->init (plugin), "plugin creates and initialises");
    if (plugin == nullptr)
        return 1;

    // Parameters and ports.
    const auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    const uint32_t numParams = params != nullptr ? params->count (plugin) : 0;
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
    const uint32_t inPorts = ports != nullptr ? ports->count (plugin, true) : 0, outPorts = ports != nullptr ? ports->count (plugin, false) : 0;
    std::printf ("    %u parameters, %u input ports, %u output ports\n", numParams, inPorts, outPorts);
    check (numParams >= 781, "at least 781 parameters");
    check (inPorts >= 1 && outPorts == 1, "a main input (and optionally a sidechain) and one output");
    if (params != nullptr && numParams > 0)
    {
        bool foundBandFreq = false;
        for (uint32_t i = 0; i < numParams && ! foundBandFreq; ++i)
        {
            clap_param_info_t info {};
            if (params->get_info (plugin, i, &info))
                foundBandFreq = std::string (info.name) == "Band 1 Frequency";
        }
        check (foundBandFreq, "band parameters are exposed by name");
    }

    // Audio: with no bands in use the output must equal the input exactly.
    constexpr uint32_t frames = 512;
    check (plugin->activate (plugin, 48000.0, frames, frames), "activates at 48 kHz");
    check (plugin->start_processing (plugin), "starts processing");

    std::vector<std::vector<float>> inData (inPorts * 2, std::vector<float> (frames, 0.0f)), outData (outPorts * 2, std::vector<float> (frames, 0.0f));
    std::vector<std::vector<float*>> inPtrs (inPorts, std::vector<float*> (2)), outPtrs (outPorts, std::vector<float*> (2));
    std::vector<clap_audio_buffer_t> inBuffers (inPorts), outBuffers (outPorts);
    for (uint32_t p = 0; p < inPorts; ++p)
    {
        inPtrs[p] = { inData[p * 2].data(), inData[p * 2 + 1].data() };
        inBuffers[p] = {};
        inBuffers[p].data32 = inPtrs[p].data();
        inBuffers[p].channel_count = 2;
    }
    for (uint32_t p = 0; p < outPorts; ++p)
    {
        outPtrs[p] = { outData[p * 2].data(), outData[p * 2 + 1].data() };
        outBuffers[p] = {};
        outBuffers[p].data32 = outPtrs[p].data();
        outBuffers[p].channel_count = 2;
    }

    clap_input_events_t inEvents { nullptr, noEvents, noEvent };
    clap_output_events_t outEvents { nullptr, acceptEvent };
    clap_process_t process {};
    process.frames_count = frames;
    process.audio_inputs = inBuffers.data();
    process.audio_outputs = outBuffers.data();
    process.audio_inputs_count = inPorts;
    process.audio_outputs_count = outPorts;
    process.in_events = &inEvents;
    process.out_events = &outEvents;

    std::mt19937 rng (5);
    std::uniform_real_distribution<float> uni (-0.25f, 0.25f);
    bool identical = true, finite = true, processed = true;
    for (int block = 0; block < 50; ++block)
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            inData[0][i] = uni (rng);
            inData[1][i] = uni (rng);
        }
        process.steady_time = static_cast<int64_t> (block) * frames;
        processed = processed && plugin->process (plugin, &process) != CLAP_PROCESS_ERROR;
        for (uint32_t i = 0; i < frames; ++i)
        {
            finite = finite && std::isfinite (outData[0][i]) && std::isfinite (outData[1][i]);
            identical = identical && outData[0][i] == inData[0][i] && outData[1][i] == inData[1][i];
        }
    }
    check (processed, "processes without error");
    check (finite, "output is finite");
    check (identical, "idle plugin is bit-transparent");

    // State.
    const auto* state = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
    std::vector<uint8_t> saved;
    clap_ostream_t stream { &saved, writeToVector };
    check (state != nullptr && state->save (plugin, &stream) && saved.size() > 1000, "saves its state");
    std::printf ("    state is %zu bytes; latency extension %s\n", saved.size(),
                 plugin->get_extension (plugin, CLAP_EXT_LATENCY) != nullptr ? "present" : "missing");
    check (plugin->get_extension (plugin, CLAP_EXT_LATENCY) != nullptr, "reports latency");

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    entry->deinit();

    std::printf (failures == 0 ? "CLAP test passed\n" : "CLAP test FAILED: %d problem(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
