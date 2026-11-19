# 02 Parameters

All automatable state is an `AudioProcessorParameter` created in
`createParameterLayout()`. The list is fixed at 32 bands so the host sees a stable
set. Every parameter uses `ParameterID { id, 1 }` (version hint 1). **IDs are
permanent**: never rename or reuse one; add new parameters with a higher version
hint.

## 1. Conventions

- Band parameters are named `bNN_<suffix>` with `NN` = `01`..`32`.
- Display names are `Band N <Name>`, for example `Band 4 Frequency`.
- Mappings:
  - **log**: `value = min * (max/min)^norm`.
  - **lin**: `value = min + norm * (max - min)`.
  - **sym**: linear, centre at 0, double-click resets to 0.
- Text entry is parsed by `of::ValueParser` (see 05 §7): accepts `1.2k`, `850hz`,
  `A4`, `-3`, `+2.5db`, `15ms`, `0.2s`, `2:1`, `inf`.
- Discrete parameters are `AudioParameterChoice` or `AudioParameterBool` and are
  flagged non-smoothed. Continuous parameters are smoothed in the engine (03 §11),
  never in the parameter object.

## 2. Per-band parameters (x32)

| Suffix | Name | Type | Range / choices | Default | Mapping | Notes |
|---|---|---|---|---|---|---|
| `used` | Used | bool | | off | | Band exists. Off = slot is free |
| `on` | Enabled | bool | | on | | Off = bypassed but kept |
| `shape` | Shape | choice | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass | Bell | | |
| `slope` | Slope | choice | 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct, Brickwall | 12 | | Ignored by Flat Tilt. Brickwall only for cuts |
| `freq` | Frequency | float | 10 to 30000 Hz | 1000 | log | |
| `gain` | Gain | float | -30 to +30 dB | 0 | sym | Ignored by cuts, notch, band pass, all pass |
| `q` | Q | float | 0.025 to 40 | 1.0 | log | Cuts: resonance, 0.7071 = Butterworth. Shelves: 1.0 = maximally flat |
| `place` | Placement | choice | Stereo, Left, Right, Mid, Side | Stereo | | |
| `lin` | Linear Phase | bool | | off | | Only read in Mixed phase mode |
| `dyn` | Dynamics Mode | choice | Off, Dynamic, Spectral | Off | | Only for gain-type shapes |
| `range` | Dynamic Range | float | -30 to +30 dB | 0 | sym | Gain added at full detection |
| `thr` | Threshold | float | -90 to 0 dBFS | -30 | lin | Offset (-30..+30 dB) when auto is on |
| `athr` | Auto Threshold | bool | | on | | |
| `ratio` | Ratio | float | 1 to 100 | 3 | log | 100 displays as `inf:1` |
| `knee` | Knee | float | 0 to 24 dB | 6 | lin | |
| `atk` | Attack | float | 0.05 to 500 ms | 10 | log | |
| `rel` | Release | float | 5 to 5000 ms | 120 | log | |
| `trig` | Trigger | choice | Above, Below | Above | | Below = acts when level is under threshold |
| `sc` | Detector Source | choice | Internal, External | Internal | | External falls back to Internal if the bus is disabled |
| `scfree` | Free Detector Filter | bool | | off | | Off = detector follows the band |
| `scfreq` | Detector Frequency | float | 10 to 30000 Hz | 1000 | log | Read when `scfree` is on |
| `scq` | Detector Q | float | 0.1 to 20 | 1.0 | log | Read when `scfree` is on |
| `smode` | Spectral Mode | choice | Absolute, Resonance | Resonance | | Read when `dyn` = Spectral |
| `sres` | Spectral Resolution | float | 1/24 to 1 octave | 1/6 | log | Smoothing width of the per-bin gains |

23 parameters per band, 736 in total.

When the threshold is in auto mode the same `thr` parameter is reinterpreted as an
offset: `offsetDb = jmap(thr, -90, 0, -30, +30)`. The UI shows the right unit. This
avoids a second parameter that would be dead half the time.

## 3. Global parameters

| ID | Name | Type | Range / choices | Default | Mapping |
|---|---|---|---|---|---|
| `phase_mode` | Phase Mode | choice | Zero Latency, Natural, Linear, Mixed | Zero Latency | |
| `lin_quality` | Linear Quality | choice | Low, Medium, High, Very High, Max | Medium | |
| `gain_scale` | Gain Scale | float | -200 to +200 % | 100 | lin |
| `out_gain` | Output Gain | float | -36 to +36 dB | 0 | sym |
| `out_pan` | Output Balance | float | -100 to +100 | 0 | sym |
| `out_invert` | Invert Polarity | bool | | off | |
| `auto_gain` | Auto Gain | choice | Off, Curve, Loudness | Off | |
| `bypass` | Bypass | bool | | off | |
| `delta` | Delta | bool | | off | |
| `character` | Character | choice | Clean, Tube, Iron | Clean | |
| `drive` | Drive | float | 0 to 100 % | 30 | lin |
| `lookahead` | Dynamics Lookahead | choice | Off, 2 ms, 5 ms, 10 ms | Off | |
| `sc_listen` | Sidechain Listen | bool | | off | |

`bypass` is returned from `getBypassParameter()` so the host's bypass and ours are
the same control. Bypass is a latency-compensated crossfade (10 ms), never a hard
switch.

## 4. Parameter behaviour rules

1. **Gain scale** multiplies `gain` and `range` of every gain-type band before
   design. It does not touch cuts, notches or Q.
2. **Adding a band** from the UI sets `used` = on and writes shape, freq, gain, q
   inside one undo transaction and one `beginChangeGesture/endChangeGesture` group
   per parameter. The lowest free slot is used.
3. **Deleting a band** sets `used` = off and resets the slot's other parameters to
   defaults (so automation lanes of a reused slot start clean).
4. **Shape change** keeps freq, gain and q, except: switching to a cut sets `q` to
   0.7071 if it was the bell default 1.0; switching away from a cut restores 1.0 if
   it was 0.7071.
5. Parameters that have no effect in the current configuration (for example `thr`
   when `dyn` = Off) still hold and report their value. The UI dims them; the
   engine ignores them.
6. `phase_mode`, `lin_quality`, `lookahead`, `character` and any change that alters
   latency are marked `isMetaParameter() = false`, non-automatable in the
   attributes (`withAutomatable(false)`), because hosts cannot follow latency
   changes mid-playback. Everything else is automatable.
7. Solo is **not** a parameter (it must never be saved or automated). See 05 §6.

## 5. Non-parameter state

Saved with the session but invisible to automation. Full schema in 06.

- UI: window size, scale, display range (±3/6/12/30 dB), selected bands, open
  panels, theme, piano strip on/off.
- Analyzer: sources shown, resolution, speed, tilt, range, freeze, view
  (spectrum / spectrogram), mid/side split.
- Instance: display name, colour, hub visibility.
- A/B/C/D slots, MIDI mappings, EQ-match captures.
