# OpenFilter EQ

A dynamic, spectral, phase-flexible parametric equalizer plugin built with JUCE.
Feature target: parity with the leading commercial "pro" EQ (FabFilter Pro-Q is the
reference), plus the improvements listed in `docs/spec/00-product.md`.

The look, name, code and presets are original. Nothing is copied from the reference
product; it is used only as a feature benchmark.

## Status

Milestone M1 (static EQ, zero latency) is built. Build order is in
`docs/spec/07-build-test-release.md`.

| Part | State |
|---|---|
| Specs | Written; kept in step with what the tests show |
| DSP core (`src/dsp`): prototypes, matched design, filter kernel, engine | Built, unit tests pass |
| Plugin (`src/plugin`): 781 parameters, processor, session state | Built; VST3, LV2 and Standalone on Linux |
| Editor (`src/ui`): response display with draggable bands, band dock, output meter | Built, first version |
| Analyzer, presets, dynamics, linear phase, CLAP, and the rest of M2 to M7 | Not yet |

Known gaps:

- Zero-latency filter accuracy near Nyquist and for high-order shapes is below the
  spec target. Figures and plan: `docs/spec/03-dsp.md`, section 3 and item R-6.
- Tested on Linux only. Windows and macOS builds have not been tried.
- Validated with our own host test, not yet with pluginval or in a DAW.
- The LV2 editor has not been exercised (the headless host test cannot embed it).

## Build and test

Linux packages (Arch names): `alsa-lib freetype2 fontconfig libx11 libxrandr
libxinerama libxcursor libxext mesa`, plus `cmake` and `ninja`.

```
git clone --recurse-submodules https://github.com/asantamarian/openfilter.git
cd openfilter
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest
```

`ctest` runs three things: the DSP unit tests, a host test that loads the built VST3
and LV2 the way a DAW does, and a tool that renders the editor to `build/editor.png`.

The plugins are in `build/OpenFilterEQ_artefacts/Release/`. To try one in a DAW,
copy `VST3/OpenFilter EQ.vst3` to `~/.vst3/` or `LV2/OpenFilter EQ.lv2` to `~/.lv2/`.
The `Standalone/OpenFilter EQ` binary runs on its own.

## Specs

| File | Contents |
|---|---|
| `docs/spec/00-product.md` | Scope, feature matrix against the reference, improvements, non-goals |
| `docs/spec/01-architecture.md` | Repo layout, modules, threads, signal flow, latency |
| `docs/spec/02-parameters.md` | Every automatable parameter: ID, range, mapping, default |
| `docs/spec/03-dsp.md` | Filter design, kernels, phase modes, dynamics, spectral, auto gain |
| `docs/spec/04-analysis.md` | Analyzer, instance hub, collision view, EQ match, sketch |
| `docs/spec/05-ui.md` | Tokens, layout, components, interaction, keyboard, accessibility |
| `docs/spec/06-state.md` | State format, presets, undo, A/B/C/D, MIDI learn, migration |
| `docs/spec/07-build-test-release.md` | CMake, formats, tests, CI, milestones |

## UI mockup

Interactive mockup (drag the nodes, pick shapes, switch a band to Dynamic):
https://claude.ai/artifact/Rgnk2WwKuTpHXttCVzhUoN

The mockup is the visual source of truth for `05-ui.md`. It is private to its owner
until shared from the page's Share menu.
