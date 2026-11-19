# 00 Product

## 1. What it is

OpenFilter EQ is a mixing and mastering equalizer: up to 32 bands, each of which can
be static, dynamic or spectral, placed on stereo/left/right/mid/side, and processed
in minimum phase or linear phase. It has a real-time analyzer, cross-instance
visibility, EQ matching and a resizable vector UI.

Formats: VST3, AU, CLAP, LV2, AAX, Standalone. Platforms: Windows 10+ (x64, arm64),
macOS 11+ (universal), Linux (x64, arm64; X11 and XWayland).

## 2. Feature matrix against the reference

"Reference" is FabFilter Pro-Q 4 as understood at the time of writing. Re-check the
reference column against the current release before making public comparison claims.

| Area | Reference | OpenFilter EQ v1 |
|---|---|---|
| Bands | 24 | 32 |
| Shapes | Bell, low/high shelf, low/high cut, notch, band pass, tilt shelf, flat tilt, all pass | Same ten |
| Slopes | 6 to 96 dB/oct, brickwall | 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct, brickwall; applies to every shape that has an order |
| Decramped response | Yes | Yes, matched analog magnitude at every sample rate |
| Phase modes | Zero latency, natural phase, linear phase (several lengths) | Zero latency, natural, linear (5 lengths), **mixed (per-band choice)** |
| Stereo placement | Per band L/R/M/S/stereo | Same |
| Dynamic EQ | Per band, threshold with auto, attack/release, external sidechain | Per band, plus ratio, knee, trigger direction, free detector filter, **level-independent auto threshold** |
| Spectral dynamics | Per band | Per band, absolute and **resonance (relative to envelope)** modes, resolution control |
| Character / saturation | Clean, plus two coloured modes | Clean, Tube, Iron with drive amount |
| Analyzer | Pre, post, sidechain; freeze; spectrum grab; tilt; resolution | Same, plus **spectrogram view** and **mid/side split view** |
| Cross-instance | Other instances' spectra, collision highlight, remote instance list | Same in-process; spectra only when the host sandboxes plugins per process |
| EQ match | Yes | Yes, plus **reference from an audio file** |
| Curve sketch | Yes | Yes |
| Auto gain | Curve-based estimate | Curve-based **and loudness-matched (LUFS) mode** |
| Gain scale | 0 to 200 % | -200 to +200 % (negative inverts the curve) |
| Delta monitor | Not as a global switch (verify) | **Global delta listen**, latency aligned |
| Band solo | Yes | Yes |
| A/B | A/B | A/B/C/D |
| Undo/redo | Yes | Yes, with a visible history list |
| MIDI learn | Yes | Yes |
| Piano display | Yes | Yes, with note-name entry (`A4`, `C#3+20`) |
| Resizable UI, full screen | Yes | Yes, free aspect ratio, 50 to 200 % scale |
| Surround | Up to 9.1.6 | Not in v1; engine is channel-agnostic so it can follow |
| Accessibility | Limited (verify) | **Screen-reader labels, full keyboard operation, band table view** |
| Linux | No native build | **Native** |
| LV2 | No | **Yes** |
| Source and preset format | Closed | **Open source, documented JSON presets** |

## 3. Where we intend to be better

Each item is a requirement, not a slogan. The spec section that implements it is in
brackets.

1. **Runs everywhere.** Linux native, LV2 and CLAP as first-class targets. [07]
2. **Mixed phase.** In Mixed mode each band chooses minimum or linear phase, so a
   mastering low shelf can be linear while a surgical notch stays zero-pre-ring.
   [03 §6]
3. **Dynamics that survive gain staging.** Auto threshold follows the long-term band
   level, so a dynamic band behaves the same on a quiet and a loud take. Ratio, knee
   and trigger direction are exposed instead of hidden. [03 §7]
4. **Resonance mode.** Spectral bands can act on bins that stick out of the local
   spectral envelope, not only on bins above an absolute level. [03 §8]
5. **Honest level matching.** Loudness-matched auto gain and a global delta switch
   make "is it better or just louder" answerable. [03 §10]
6. **Band order never matters.** Every detector taps the plugin input (or the
   external sidechain), never the output of another band. Reordering or renumbering
   bands cannot change the sound. [01 §4]
7. **Everything is visible and typeable.** A band table lists all bands with
   editable cells; every value accepts typed input with units and note names. [05]
8. **Accessible.** Every control has an accessibility handler; the plugin is fully
   usable from the keyboard. [05 §9]
9. **Open formats.** Presets and themes are documented JSON. [06]
10. **Predictable CPU.** No allocation, lock or system call on the audio thread;
    idle bands cost zero; budgets are enforced by benchmarks in CI. [07 §5]

## 4. Non-goals for v1

- Surround and immersive layouts (planned for v1.x; do not hard-code "2 channels").
- Continuous (fractional) slopes. Tracked as research item R-3 in [03 §12].
- Remote editing of instances that live in a different process.
- Copy protection, licensing, online accounts.
- A built-in preset store or cloud sync.

## 5. Naming and legal

- Product name: OpenFilter EQ. Manufacturer code `Opfl`, plugin code `OfEq`.
- Licence: AGPL-3.0-or-later, which is what the open-source JUCE licence requires.
  A closed-source build needs a commercial JUCE licence.
- AAX builds need the Avid AAX SDK and PACE signing; the AAX target is optional and
  off by default.
- Do not use the reference product's name, logo, preset names, colour scheme or
  screenshots in the UI, presets, website or store listing.

## 6. Definitions

| Term | Meaning |
|---|---|
| Band | One filter with its own shape, placement, phase and dynamics settings |
| Lane | A group of bands that share a processing path (IIR, FIR, spectral) |
| Control block | 32 samples at 44.1/48 kHz, 64 at 88.2/96, 128 at 176.4/192 |
| Section | One first- or second-order filter stage inside a band |
| Instance hub | Process-wide registry that lets instances see each other |
