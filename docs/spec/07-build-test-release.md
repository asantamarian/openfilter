# 07 Build, test, release

## 1. CMake

Top-level `CMakeLists.txt` (skeleton; paths as in 01 §2):

```cmake
cmake_minimum_required(VERSION 3.22)
project(OpenFilterEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

option(OF_BUILD_AAX   "Build AAX (needs AAX SDK)"        OFF)
option(OF_BUILD_CLAP  "Build CLAP"                       ON)
option(OF_BUILD_TESTS "Build unit tests and benchmarks"  ON)
option(OF_RTSAN       "Enable RealtimeSanitizer (Clang)" OFF)

add_subdirectory(extern/JUCE)

set(OF_FORMATS VST3 LV2 Standalone)
if(APPLE)
  list(APPEND OF_FORMATS AU)
endif()
if(OF_BUILD_AAX)
  juce_set_aax_sdk_path("${AAX_SDK_PATH}")
  list(APPEND OF_FORMATS AAX)
endif()

juce_add_plugin(OpenFilterEQ
  PRODUCT_NAME                "OpenFilter EQ"
  COMPANY_NAME                "OpenFilter"
  BUNDLE_ID                   "org.openfilter.eq"
  PLUGIN_MANUFACTURER_CODE    Opfl
  PLUGIN_CODE                 OfEq
  FORMATS                     ${OF_FORMATS}
  IS_SYNTH                    FALSE
  NEEDS_MIDI_INPUT            TRUE
  NEEDS_MIDI_OUTPUT           FALSE
  IS_MIDI_EFFECT              FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES             Fx EQ
  AU_MAIN_TYPE                kAudioUnitType_Effect
  AAX_CATEGORY                AAX_ePlugInCategory_EQ
  LV2URI                      "https://github.com/OWNER/openfilter/eq"   # set before first release; permanent
  COPY_PLUGIN_AFTER_BUILD     TRUE)

juce_add_binary_data(OpenFilterAssets SOURCES
  assets/fonts/Inter-Variable.ttf
  assets/fonts/JetBrainsMono-Variable.ttf)   # plus factory presets and themes

add_library(of_dsp STATIC)                    # sources from src/dsp, src/match
target_link_libraries(of_dsp PUBLIC juce::juce_dsp juce::juce_audio_basics pffft)

target_sources(OpenFilterEQ PRIVATE)          # sources from src/plugin, src/ui, src/hub
target_compile_definitions(OpenFilterEQ PUBLIC
  JUCE_WEB_BROWSER=0  JUCE_USE_CURL=0  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(OpenFilterEQ
  PRIVATE of_dsp OpenFilterAssets
          juce::juce_audio_utils juce::juce_gui_extra juce::juce_opengl
  PUBLIC  juce::juce_recommended_config_flags
          juce::juce_recommended_lto_flags
          juce::juce_recommended_warning_flags)

if(OF_BUILD_CLAP)
  add_subdirectory(extern/clap-juce-extensions EXCLUDE_FROM_ALL)
  clap_juce_extensions_plugin(TARGET OpenFilterEQ
    CLAP_ID "org.openfilter.eq"
    CLAP_FEATURES audio-effect equalizer stereo mono)
endif()

if(OF_BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

Notes:

- The `LV2URI` is a placeholder. Set it to a URL you control before the first
  release; hosts identify the plugin by it, so it can never change afterwards.
- Warnings are errors for `src/` (`-Wall -Wextra -Wconversion -Wshadow`, `/W4`).
- `CMakePresets.json` provides `dev` (Debug, tests, ASan+UBSan), `rtsan` (Clang,
  `OF_RTSAN=ON`), `release` (RelWithDebInfo, LTO).
- Linux build dependencies (Arch names): `alsa-lib jack2 freetype2 fontconfig
  libx11 libxrandr libxinerama libxcursor libxext mesa`.

Build on this machine:

```
git init && git submodule add https://github.com/juce-framework/JUCE extern/JUCE
cmake --preset dev && cmake --build --preset dev
```

## 2. Formats and host coverage

| Format | Hosts it reaches | Notes |
|---|---|---|
| VST3 | Cubase, Nuendo, Ableton Live, FL Studio, Studio One, Reaper, Bitwig, Cakewalk, Ardour, others | Primary target |
| AU | Logic Pro, GarageBand, MainStage | macOS only; must pass `auval -v aufx OfEq Opfl` |
| CLAP | Bitwig, Reaper, FL Studio, others | Sample-accurate automation |
| LV2 | Ardour, Mixbus, Qtractor, Carla | Main Linux-native format |
| AAX | Pro Tools | Optional; needs Avid SDK and PACE signing account |
| Standalone | none | For development and demos |

## 3. Unit tests (`tests/unit`, Catch2)

| Suite | Checks |
|---|---|
| `design_matched` | Regression guard at the measured accuracy in 03 §3 (tighten as R-6 progresses) |
| `kernel_equivalence` | SVF vs direct form, 10 000 random sets (03 §4) |
| `prototype_symmetry` | Boost then equal cut of the same band is flat within 0.01 dB |
| `butterworth` | -3 dB at `f0`, slope within 0.5 dB/oct of nominal one octave into the stop band |
| `transparent` | All bands idle: output equals input bit for bit, every phase mode (after latency) |
| `latency` | Impulse in, peak position equals `getLatencySamples()` for every mode and quality |
| `linear_phase` | FIR lane output impulse is symmetric within -100 dB |
| `mixed_phase` | Mixed with no linear bands equals Zero Latency plus delay |
| `band_order` | Permuting band slots leaves the output unchanged within -120 dB |
| `dynamics_static` | Gain computer curve against a table; range clamp; trigger Below |
| `dynamics_timing` | Step response reaches 63 % in attack/release time ±1 control block |
| `auto_threshold` | Same dynamic gain for a signal and its copy at -18 dB (within 0.5 dB after 4 s) |
| `spectral_cola` | Spectral lane with zero range is transparent within -100 dB |
| `smoothing` | Sweeping freq, gain, Q at maximum automation speed produces no sample-to-sample jump above the input's own, and no NaN/Inf |
| `autogain_curve` | Pink noise in: output loudness within 1 dB of input for 50 random curves |
| `value_parser` | 05 §7 table |
| `fitter` | 04 §3.2 acceptance |
| `state_roundtrip` | Save, load, save: identical trees; fixtures of every schema version load |
| `preset_json` | Round trip; unknown keys ignored; defaults applied |

## 4. Integration and validation

- **pluginval** at strictness 10 on VST3 (all platforms) and AU (macOS).
- **clap-validator**, **Steinberg VST3 validator**, **auval**, **lv2lint**.
- Headless host test (`tests/integration`): loads the built VST3 with
  `juce::AudioPluginFormatManager`, runs random block sizes (1 to 4096), sample
  rates 44.1 to 192 kHz, mono/stereo/sidechain layouts, random parameter storms,
  state save/load mid-stream, editor open/close 100 times. Asserts no NaN, no
  allocation on the audio thread (RTSan build), no leaks (ASan).
- Thread sanitizer run of the same test with the editor open.
- Manual host matrix before each release: Reaper, Bitwig, Ardour (Linux);
  Ableton Live, Cubase, FL Studio, Studio One (Windows); Logic Pro, Ableton Live,
  Pro Tools if AAX (macOS). Checklist: insert, automate, save/reopen session,
  offline bounce equals real-time bounce, latency compensation against a dry
  copy, sidechain routing, resize, HiDPI, duplicate track, freeze track.

## 5. Benchmarks (`tests/bench`)

Targets on the CI reference machine, 48 kHz, 512-sample blocks, stereo, as a share
of one core. They are budgets to validate in M1 and then lock; a regression of
more than 15 % fails CI.

| Scenario | Budget |
|---|---|
| Idle (no active bands) | under 0.05 % |
| 8 static bells, Zero Latency | under 0.4 % |
| 24 static bands, mixed shapes, Zero Latency | under 1.5 % |
| 8 dynamic bands | under 1.5 % |
| 24 bands, Linear, Medium | under 3 % |
| 4 spectral bands | under 4 % |
| Editor open, analyzer on, 1280 x 800, 60 fps | under 5 % of one core on the message thread |

## 6. CI

GitHub Actions, on every push and pull request:

| Job | Runner | Steps |
|---|---|---|
| `linux` | ubuntu-24.04, Clang | Build all formats, unit tests, RTSan integration, pluginval (xvfb), lv2lint, clap-validator |
| `windows` | windows-latest, MSVC | Build VST3/CLAP/Standalone, unit tests, pluginval |
| `macos` | macos-14 | Universal build, unit tests, pluginval, auval |
| `sanitize` | ubuntu-24.04 | ASan+UBSan and TSan runs |
| `bench` | self-hosted or fixed-size runner | Benchmarks against stored baselines |

Tagged builds additionally package: `.pkg` (macOS, signed and notarised when
credentials are present), Inno Setup installer (Windows), `.tar.gz` plus an AUR
`PKGBUILD` (Linux).

## 7. Milestones

Durations are rough estimates for one experienced JUCE developer and should be
re-planned after M1.

| ID | Deliverable | Exit criteria | Est. |
|---|---|---|---|
| M0 | Skeleton: CMake, formats, empty processor and editor, CI | pluginval 10 passes on three platforms | 1 wk |
| M1 | Static EQ, Zero Latency: parameters, prototypes, matched design, SVF kernel, smoothing; basic display with nodes | Suites `design_matched` to `band_order`, `smoothing`; R-1 answered | 3 wk |
| M2 | Full UI: theme, dock, output column, value entry, band table, presets, undo, A/B/C/D, resize and scale, analyzer | 05 complete except panels of later milestones; UI budget met | 4 wk |
| M3 | Dynamic bands, sidechain bus, lookahead, solo | `dynamics_*`, `auto_threshold`; R-2 answered | 2 wk |
| M4 | Natural, Linear, Mixed modes | `latency`, `linear_phase`, `mixed_phase`; R-5 answered | 3 wk |
| M5 | Spectral bands, auto gain (both), delta, character | `spectral_cola`, `autogain_curve`; R-4 answered | 3 wk |
| M6 | Instance hub, collision view, EQ match, sketch, spectrum grab | 04 complete; `fitter` | 3 wk |
| M7 | Accessibility pass, MIDI learn, themes, installers, manual, host matrix | Release checklist | 3 wk |

Build strictly in this order: each milestone is shippable and every later feature
depends on the prototype/evaluator/kernel foundation from M1.

## 8. Definition of done (every change)

1. Unit and integration tests pass on all CI jobs; new behaviour has a test.
2. No new allocation, lock or system call reachable from `Engine::process`.
3. No parameter ID changed or removed.
4. State written by the previous release still loads.
5. Spec updated in the same change when behaviour differs from it.
