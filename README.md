# OpenFilter EQ

A dynamic, spectral, phase-flexible parametric equalizer plugin built with JUCE.
Feature target: parity with the leading commercial "pro" EQ (FabFilter Pro-Q is the
reference), plus the improvements listed in `docs/spec/00-product.md`.

The look, name, code and presets are original. Nothing is copied from the reference
product; it is used only as a feature benchmark.

## Status

In progress, milestone M1 (static EQ, zero latency). Build order is in
`docs/spec/07-build-test-release.md`.

| Part | State |
|---|---|
| Specs | Written; kept in step with what the tests show |
| DSP core (`src/dsp`): prototypes, matched design, filter kernel, engine | Built, unit tests pass |
| Plugin and editor (`src/plugin`, `src/ui`) | Not yet |

Known gap: zero-latency filter accuracy near Nyquist and for high-order shapes is
below the spec target. Measured figures and the plan are in `docs/spec/03-dsp.md`
(section 3 and research item R-6).

## Build and test

```
git clone --recurse-submodules https://github.com/asantamarian/openfilter.git
cd openfilter
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/of_tests
```

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
