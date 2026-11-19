#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Parameters.h"
#include "dsp/Engine.h"
#include "dsp/util/SpscFifo.h"

namespace of
{
class OpenFilterProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::AsyncUpdater
{
public:
    OpenFilterProcessor();
    ~OpenFilterProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OpenFilter EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // Message-thread helpers used by the editor. All band writes go through these.
    BandParams readBand (int band) const;          // gainDb is the raw parameter value
    double gainScale() const;
    juce::RangedAudioParameter* bandParam (int band, const char* suffix) const;

    /** Uses the lowest free slot. Returns its index, or -1 when all 32 are used. */
    int addBand (Shape shape, double freq, double gainDb);
    void removeBand (int band);
    void setBandValue (int band, const char* suffix, float plainValue);

    float takePeak (int channel) noexcept { return engine.takePeak (channel); }
    /** Band audition (docs/spec/05-ui.md section 6.1). -1 for none. Never saved. */
    void setSoloBand (int band) noexcept { engine.setSoloBand (band); }
    int soloBand() const noexcept { return engine.soloBand(); }
    /** True when the host has connected something to the sidechain input. */
    bool hasSidechain() const noexcept { return sidechainLive.load (std::memory_order_relaxed); }

    /** Live dynamic gain of a band in dB, for the display. */
    float dynamicGainDb (int band) const noexcept { return engine.dynamicGainDb (band); }

    // Analyzer taps (mono sum), written by the audio thread only while an editor is open.
    SpscFifo preFifo, postFifo;
    std::atomic<bool> analyzerActive { false };

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

    // Editor size, kept in the session state.
    int editorWidth = 1280, editorHeight = 800;
    juce::String presetName;   // empty until a preset is loaded or saved

    // View settings, kept in the session state.
    float uiScale = 1.0f;
    double displayRangeDb = 12.0;
    bool analyzerOn = true, tableOpen = false;

    // A/B/C/D: four full parameter snapshots (docs/spec/06-state.md section 5). Bypass and
    // delta are left alone when a slot is recalled.
    static constexpr int kNumSlots = 4;
    int activeSlot() const noexcept { return slot; }
    void switchSlot (int index);
    void copyActiveSlotTo (int index);

private:
    struct BandPtrs { std::atomic<float>* used, *on, *shape, *slope, *freq, *gain, *q, *place,
                                        * dyn, *range, *thr, *athr, *ratio, *knee, *atk, *rel, *trig, *sc; };

    void readSnapshot (ParamSnapshot&) const noexcept;
    // The phase mode changes latency; the host is told from the message thread.
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { updateLatency(); }
    void updateLatency();
    std::vector<float> captureValues() const;
    void applyValues (const std::vector<float>&);
    template <typename Sample> void process (juce::AudioBuffer<Sample>&);

    Engine engine;
    std::array<BandPtrs, kMaxBands> bandPtrs {};
    std::atomic<float>* gainScalePtr = nullptr, *outGainPtr = nullptr, *outPanPtr = nullptr,
                      * invertPtr = nullptr, *bypassPtr = nullptr, *deltaPtr = nullptr, *phaseModePtr = nullptr;
    juce::AudioProcessorParameter* bypassParam = nullptr;
    std::vector<double> work[2];
    std::vector<float> tap;
    std::vector<double> scWork[2];
    std::atomic<bool> sidechainLive { false };
    int maxBlock = 512;
    int slot = 0;
    std::array<std::vector<float>, kNumSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenFilterProcessor)
};
} // namespace of
