# OpenFilter EQ

A dynamic, spectral, phase-flexible parametric equalizer plugin built with JUCE.
Feature target: parity with the leading commercial "pro" EQ (FabFilter Pro-Q is the
reference), plus the improvements listed in `docs/spec/00-product.md`.

The look, name, code and presets are original. Nothing is copied from the reference
product; it is used only as a feature benchmark.

## Status

Milestone M1 (static EQ, zero latency) is built and M2 (full UI) is under way. Build order is in
`docs/spec/07-build-test-release.md`.

| Part | State |
|---|---|
| Specs | Written; kept in step with what the tests show |
| DSP core (`src/dsp`): prototypes, matched design, filter kernel, engine | Built, unit tests pass |
| Plugin (`src/plugin`): 781 parameters, processor, session state | Built; VST3, CLAP, LV2 and Standalone on Linux |
| Editor (`src/ui`): response display with draggable bands, band dock, output meter | Built, first version |
| Spectrum analyzer (input and output), JSON presets, typed values with note names | Built (first part of M2) |
| Natural phase mode: filters at 4x, accurate to 20 kHz, 76 samples of latency at 44.1/48 kHz | Built (brought forward from M4) |
| Band table with editable cells, A/B/C/D slots, View menu (interface size, display range, analyzer on/off, undo history) | Built (M2) |
| Dynamic bands with the internal detector: threshold (manual or auto), range, ratio, knee, attack, release, trigger above or below (M3) | Built |
| External sidechain per band, band solo (button, or hold S in the display) | Built |
| Lookahead (2, 5 or 10 ms), detector listen (Key button), free detector filter | Built |
| Linear phase (five kernel lengths) and Mixed phase (per-band choice), designed on a worker thread (M4) | Built |
| Dynamics for bands in the linear-phase path (up to 8, adding 2048 samples of latency) | Built |
| Auto gain: estimated from the curve, or loudness matched (K-weighted, input against output) | Built (M5) |
| Spectral bands: a gain per frequency inside the band, with absolute and resonance modes | Built (M5) |
| Character: Tube and Iron saturation at 4x, with drive (first voicing, not yet tuned by ear) | Built (M5) |
| Nine factory presets; analyzer speed, tilt, range and freeze; piano strip; number keys to select bands; copy, paste and duplicate bands | Built |
| Multi-band selection (shift-click, rubber band, Ctrl/Cmd+A) with group drag, scroll, nudge, copy and delete; paged dock; minimum window 800 x 560 | Built |
| Advanced panel: character and drive, free detector filter, knee, trigger, spectral resolution; dock edits apply to every selected band | Built |
| Spectrogram view of the output (8 seconds, newest at the top); bundled fonts, so it looks the same on every machine | Built |
| Full-screen mode | Not yet |
| EQ match (reference from the sidechain or an audio file) and curve sketching, on a shared curve fitter; sidechain trace in the analyzer | Built (M6) |
| Instance hub: other instances in the same process drawn over the display, with collision strips | Built (M6) |
| EQ match with another instance as the reference | Built |
| Editing another instance from this window; instances in other processes | Not yet |
| Accessibility: dials are keyboard-operable sliders for screen readers, Tab steps through bands, selections are announced, focus is visible | Built (M7), not tested with a screen reader |
| MIDI learn: right-click a dial, move a controller; mappings are saved with the session | Built (M7) |
| CLAP build, with a minimal CLAP host test | Built (M7) |
| Themes: Nocturne, High contrast and Daylight built in, plus JSON theme files (M7) | Built |
| Installers (M7) | Not yet |

Known gaps:

- In Zero Latency mode the filters drift from the drawn curve in the top octave
  (about 1 dB around 9 kHz and 2 dB around 15 kHz at 44.1/48 kHz). Natural mode fixes
  this at the cost of latency and CPU. Figures: `docs/spec/03-dsp.md`, sections 3 and 5.
- Tested on Linux only. Windows and macOS builds have not been tried.
- Validated with our own host test and by hand in Reaper, not yet with pluginval.
- The DSP core is clean under address, undefined-behaviour and thread sanitizers; the
  plugin and editor code has not been run under one. CPU figures are in
  `docs/spec/07-build-test-release.md`, section 5.
- The LV2 editor has not been exercised (the headless host test cannot embed it).
- In LV2 the sidechain input is a required port (a workaround for a crash in JUCE's
  LV2 wrapper), so the plugin shows four inputs there.

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

`ctest` runs four things: the DSP unit tests, a host test that loads the built VST3
and LV2 the way a DAW does, a minimal CLAP host that does the same for the CLAP, and a
tool that renders the editor to `build/editor.png` and checks state, presets, slots,
EQ match, the instance hub and MIDI learn.

The plugins are in `build/OpenFilterEQ_artefacts/Release/`, and the build also copies
them to your user plugin folders (`~/.vst3/`, `~/.clap/` and `~/.lv2/` on Linux), so a rescan in
your DAW picks up the new version. Pass `-DOF_INSTALL_AFTER_BUILD=OFF` to turn that
off. The `Standalone/OpenFilter EQ` binary runs on its own.

Licence: AGPL-3.0-or-later (see `LICENSE`), as required by the open-source JUCE licence.
The bundled fonts, Inter and JetBrains Mono, are under the SIL Open Font License; their
licence texts are in `assets/fonts/`.

## Manual

`docs/manual.md` explains how to use the plugin.

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
