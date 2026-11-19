# 01 Architecture

## 1. Toolchain

| Item | Choice |
|---|---|
| Framework | JUCE 8.x, pinned by git tag as a submodule in `extern/JUCE` |
| Language | C++20, no exceptions or RTTI on the audio path |
| Build | CMake 3.22+, Ninja; presets in `CMakePresets.json` |
| CLAP | `clap-juce-extensions` submodule (replace with JUCE's native CLAP target if the pinned JUCE provides one) |
| FFT | `pffft` (BSD) wrapped in `of::Fft`; do not depend on `juce::dsp::FFT` backend selection |
| Tests | Catch2 v3, pluginval, clap-validator, auval, Steinberg VST3 validator |
| Fonts | Inter and JetBrains Mono (OFL), embedded through `juce_add_binary_data` |

JUCE modules: `juce_audio_processors`, `juce_audio_utils`, `juce_dsp`,
`juce_gui_basics`, `juce_gui_extra`, `juce_opengl` (optional renderer).

## 2. Repository layout

```
CMakeLists.txt
CMakePresets.json
extern/                JUCE, clap-juce-extensions, pffft, Catch2
assets/                fonts, factory presets, themes, icon
src/
  plugin/              PluginProcessor, PluginEditor, parameter layout, state
  dsp/                 no JUCE GUI includes; depends on juce_core/juce_dsp only
    design/            analog prototypes, matched biquad design, Butterworth, shelves
    kernel/            SvfSection, BiquadSection, BandProcessor
    lanes/             IirLane, FirLane, SpectralLane, CharacterStage
    dynamics/          Detector, GainComputer, AutoThreshold
    analysis/          SpectrumTap, LoudnessMeter, ResponseEvaluator
    util/              SmoothedValue, SpscFifo, AtomicSnapshot, Delay
  hub/                 InstanceHub, shared-memory spectra
  match/               CurveFitter (EQ match and sketch)
  ui/
    theme/             Tokens, Theme, LookAndFeel
    display/           ResponseDisplay, AnalyzerLayer, NodeLayer, GridLayer
    dock/              BandDock, Dial, ShapePicker, Segmented
    panels/            PresetBrowser, InstancePanel, BandTable, Settings, History
tests/
  unit/  integration/  bench/
docs/spec/
```

Rule: `src/dsp` compiles and is unit-tested without any GUI module. The UI never
includes a kernel header; it talks to the processor through parameters, the
`ResponseEvaluator` and lock-free snapshots.

## 3. Threads and what each may do

| Thread | Owns | Forbidden |
|---|---|---|
| Audio (`processBlock`) | Lanes, kernels, detectors, meters | Allocation, locks, logging, file or message-thread calls |
| Message (UI) | Editor, parameter attachments, undo manager, preset I/O dispatch | Blocking on the worker for more than a frame |
| Worker (`of::Worker`, one per instance, low priority) | FIR kernel design, EQ match fitting, preset file I/O, analyzer FFTs | Touching components |
| Hub timer (shared, message thread) | Instance registry housekeeping | Audio data other than published snapshots |

Cross-thread channels:

- **Parameters to audio:** `AudioProcessorValueTreeState` raw atomics, read once per
  block into a `ParamSnapshot` struct.
- **Audio to UI:** `SpscFifo<float>` per analyzer tap (pre, post, sidechain, per
  channel pair); `AtomicSnapshot<BandMeters>` for live dynamic gain per band
  (written once per block, read at frame rate).
- **Worker to audio:** new FIR kernels are handed over as an owning pointer through a
  single-slot lock-free mailbox; the audio thread swaps and crossfades, and returns
  the old pointer through a second mailbox for the worker to free.

## 4. Signal flow

```
            +--------------------------- dry delay (total latency) ---------------+
            |                                                                     v
in --+------+--> IIR lane A --> IIR lane B --> Spectral --> FIR --> Character --> gain --> delta/bypass --> out
     |          (ST, L, R)     (M, S)         lane        lane     stage       auto, out,   mix
     |                                                                         pan, invert
     +--> detector bus (pre-EQ copy) --+--> band detectors (dynamic and spectral)
ext sidechain -------------------------+
```

Rules:

1. Lane A processes bands placed on Stereo, Left, Right, in band-index order.
   Lane B encodes to M/S (`M = (L+R)/2`, `S = (L-R)/2`), processes Mid and Side
   bands, and decodes. Mono layouts have only Stereo placement; M/S/L/R bands are
   treated as Stereo.
2. Detectors read the detector bus, which is the plugin input before any EQ, or the
   external sidechain. A band never listens to another band's output. This makes
   band order irrelevant to the sound for static bands and for the detection path.
3. In **Linear** mode every band is realised in the FIR lane and the IIR lanes are
   empty. In **Mixed** mode bands flagged linear go to the FIR lane, the rest to the
   IIR lanes. In **Zero latency** and **Natural** the FIR lane is empty.
4. A band whose gain is exactly 0 dB with dynamics off (for gain-type shapes), or
   that is unused or bypassed, is not processed at all. With every band inactive
   and Character on Clean, the output equals the input bit for bit (after latency).
5. Delta output is `wet - delayedDry` after output gain is removed from the wet
   path (delta shows what the EQ changed, not the level trim).

## 5. Latency

Reported with `setLatencySamples` whenever it changes. All figures at 44.1/48 kHz;
multiply by 2 at 88.2/96 and by 4 at 176.4/192.

| Component | Latency (samples) |
|---|---|
| Zero latency IIR | 0 |
| Natural (oversampled IIR) | 76 at 44.1/48 kHz (4x); 13 at 88.2/96 kHz (2x); 0 above. These are absolute, not scaled |
| Linear, quality Low / Medium / High / Very high / Max | 2048 / 4096 / 8192 / 16384 / 32768 |
| Dynamic band in the FIR lane (any number) | +2048 |
| Spectral lane (any number of spectral bands) | +2048 |
| Dynamics lookahead | 0, 2, 5 or 10 ms as selected |
| Character stage oversampling | Measured, about 16 when Tube or Iron is active |

Total latency is the sum of active components. Latency changes only on a user edit
that enables or disables a component, never during automation of continuous
parameters. A latency change is applied at a block boundary with a 10 ms output
fade to avoid a click.

## 6. Core classes

```cpp
namespace of {

struct BandParams {            // plain values, already smoothed for this control block
  bool   used, on, linear;
  Shape  shape;  Slope slope;  Placement place;  DynMode dyn;
  double freq, gainDb, q;
  DynParams dynamics;          // threshold, range, ratio, knee, attack, release, ...
};

class BandProcessor {          // one band in an IIR lane
public:
  void prepare (double sampleRate, int maxBlock);
  void setTarget (const BandParams&);              // control rate
  void process (double* const* io, int numCh, int n, const float* detector);
  float currentDynamicGainDb() const noexcept;     // for the UI
private:
  std::array<SvfSection, 16> sections;             // worst case: order-8 notch or band pass
  Detector detector;  GainComputer gain;
};

class Engine {                 // everything processBlock needs, no JUCE GUI types
public:
  void prepare (const Spec&);
  void process (AudioBlock in, AudioBlock sidechain, const ParamSnapshot&);
  int  latencySamples() const noexcept;
};

class ResponseEvaluator {      // UI and FIR designer share this; pure functions
public:
  static double magnitudeDb (const BandParams&, double freqHz, double gainOverrideDb);
  static void   composite (std::span<const BandParams>, Placement view,
                           std::span<const double> freqs, std::span<double> outDb);
};

} // namespace of
```

`ResponseEvaluator` evaluates the **analog prototype** magnitude, which is also the
design target for the digital filters, so the drawn curve and the audio agree by
construction and the UI needs no access to coefficients.

## 7. Bus layout

- Main in/out: mono, stereo. Mono-in/stereo-out is not offered.
- Sidechain input bus: mono or stereo. Optional and off by default in VST3, AU and
  the other formats; **required in LV2**. With JUCE 8.0.9 the LV2 wrapper reads
  every declared input port, so an unconnected optional port is a null dereference;
  declaring the bus enabled-by-default when the plugin is loaded as LV2 makes its
  ports mandatory and hosts always connect them (03 section 12, R-7). The check must
  use `PluginHostType::getPluginLoadedAs()`: the processor's own `wrapperType` is not
  set yet while its base class is being constructed.
- MIDI input: enabled from M7, used only for MIDI learn.
- `supportsDoublePrecisionProcessing()` returns true; the engine is `double`
  internally and converts at the boundary for float hosts.

## 8. Real-time rules (enforced)

- `juce::ScopedNoDenormals` at the top of `processBlock`.
- All buffers sized in `prepareToPlay` for `maxBlock` and the worst-case lane set.
- No `std::function`, `std::shared_ptr` copies, `String`, or container growth on the
  audio thread.
- Debug and CI builds compile the audio path with Clang's RealtimeSanitizer
  (`[[clang::nonblocking]]` on `Engine::process`) and run the integration suite
  under it.
- Host calls with `numSamples == 0`, varying block sizes and block sizes larger
  than announced are handled (split into chunks of at most `maxBlock`).
