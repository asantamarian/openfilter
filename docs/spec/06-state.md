# 06 State, presets, undo

## 1. Session state

`getStateInformation` writes one `juce::ValueTree` as binary
(`ValueTree::writeToStream`), root type `OpenFilterEQ`:

```
OpenFilterEQ  version=1  pluginVersion="1.0.0"
  Params            APVTS state (copyState())
  Ui                width height scale displayRange autoRange piano theme
                    selection="4"  dockPage  panels
  Analyzer          pre post sidechain resolution speed tilt range view split
                    peakHold freeze
  Instance          uuid name colour hubVisible
  Slots             active="A"
    Slot id="A"     Params snapshot (same shape as Params)
    Slot id="B" ...
  MidiMap
    Map cc=74 channel=0 param="b04_freq" min=0 max=1
  Match             reference and source spectra (256 floats each, base64), bandCount
```

Rules:

- `setStateInformation` must accept: our binary tree, the XML form of the same
  tree (debugging), and garbage (ignored without crashing or asserting).
- Unknown children and properties are preserved on round trip.
- Loading replaces parameters through `apvts.replaceState` on the message thread;
  the engine picks the change up through its normal smoothing, so a session load
  during playback does not click.
- The undo history is cleared on load.
- `Instance.uuid` is regenerated if another live instance already has it
  (duplicated tracks).

## 2. Versioning and migration

`version` is an integer schema version. `of::migrate(ValueTree&)` runs a chain of
`migrate_N_to_N+1` functions before anything else reads the tree. A state from a
newer schema loads best-effort (known parameters applied, the rest kept) and the
status bar shows a notice. Every released schema version gets a fixture file in
`tests/fixtures/state/` and a test that loads it.

Parameter IDs never change (02). If a parameter's range must change, add a new
parameter and migrate the value.

## 3. Presets

- File: UTF-8 JSON, extension `.ofeq`.
- User location: `userApplicationDataDirectory/OpenFilter/EQ/Presets/` (folders
  are categories). Factory presets are embedded as binary data and shown in a
  read-only "Factory" folder.
- Content: band and global parameters in plain units, not normalised values.

```json
{
  "format": "openfilter.eq.preset",
  "version": 1,
  "name": "Lead vocal, air and control",
  "author": "",
  "tags": ["vocal"],
  "global": { "phase_mode": "Zero Latency", "gain_scale": 100, "auto_gain": "Off",
              "character": "Clean", "drive": 30 },
  "bands": [
    { "shape": "Low Cut", "slope": 24, "freq": 80, "q": 0.7071 },
    { "shape": "Bell", "freq": 3200, "gain": -1.5, "q": 2.2, "place": "Mid",
      "dyn": "Dynamic", "range": -6, "athr": true, "atk": 10, "rel": 120 }
  ]
}
```

- Omitted keys take the parameter default. Unknown keys are ignored.
- Bands are assigned to slots 1..n in file order; remaining slots are cleared.
- Not stored in presets: output gain, balance, bypass, delta, analyzer and UI
  state, instance identity, MIDI map. A setting "Presets recall output gain"
  (default off) changes the first.
- Loading a preset is one undo step. Preset I/O runs on the worker; the parsed
  result is applied on the message thread.
- "Default preset": `Presets/Default.ofeq` if present is applied to new instances.
- The clipboard format for copy/paste of bands is the `bands` array alone.

## 4. Undo and redo

- One `juce::UndoManager` per instance, attached to the APVTS.
- Transactions are opened by `BandController` and the dock attachments on gesture
  start and named ("Move band 4", "Add band", "Load preset"). Wheel edits coalesce
  when they arrive within 500 ms on the same parameter.
- Host automation playback and MIDI-mapped changes do not create undo steps.
- History depth 200 steps.
- Not undoable: analyzer and UI settings, solo, slot switching (it is its own
  toggle).

## 5. A/B/C/D slots

Each slot holds a full parameter snapshot. Switching stores the current parameters
into the active slot and applies the target slot (smoothly, through the engine's
normal ramps). A slot never used starts with every parameter at its default (no
bands). Each slot keeps its own preset name. "Copy to..." copies the active slot,
name included, to another. Slots are saved in
the session, not in presets. Switching slots is exposed to the host as nothing: it
changes many parameters at once and is reported with the usual per-parameter
notifications.

## 6. Themes

**Built:** three themes in code (Nocturne, High contrast, Daylight) and JSON theme
files as described below, chosen from View > Theme. The tokens are plain variables
that a theme overwrites; components that cache colours are told to re-read them.
The choice is user-wide, stored in `OpenFilter/EQ/settings.json`, and shared by
every instance in the process. "Save the current colours as a theme file" writes a
starting point to edit. **Not built:** the `spectrogramRamp` key, the contrast
check with its warning badge, and live reload when a file changes.

JSON, extension `.oftheme`, in `.../OpenFilter/Themes/`. Keys are the token names
from 05 §2 plus `spectrogramRamp` (array of 2 to 16 colour stops). Missing keys
fall back to Nocturne. A theme that fails the 4.5:1 text contrast check loads with
a warning badge in Settings.

## 7. MIDI learn

**Built:** right-click a dial, choose "Learn MIDI controller", move a hardware
control; the first controller received is mapped (any channel). The dial's caption
shows the controller number, and the same menu clears it. Mappings are saved in
the session state by parameter ID. Values are applied on the message thread by a
60 Hz timer, not as undo steps. **Not built:** a default map saved outside the
session, relative encoders, soft takeover, per-mapping ranges, and learn for
controls that are not dials.

- Enable "MIDI learn" in the status bar, touch a control, move a hardware control;
  the first CC received is mapped. Right-click a mapped control to clear.
- Mappings are per instance (saved in session state) with an option "Save as
  default map" (stored in the global settings file).
- CC values are applied through `setValueNotifyingHost` on the message thread
  (posted from the audio thread through a lock-free queue), scaled to the
  mapping's min/max. Relative encoders (two's complement and signed-bit) are
  detected per mapping during learn.
- Soft takeover: an absolute control only takes effect after it crosses the
  current parameter value.

## 8. Global settings

`userApplicationDataDirectory/OpenFilter/EQ/settings.json`: default window size and
scale, theme, renderer, frame-rate cap, shortcuts on/off, tuning reference,
analyzer defaults, default MIDI map, hub visibility default. Read once at editor
creation; written on change (debounced 500 ms, worker thread).
