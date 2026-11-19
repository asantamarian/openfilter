# 04 Analysis, instances, match

## 1. Spectrum analyzer

### 1.1 Taps

| Tap | Signal |
|---|---|
| Pre | Plugin input |
| Post | Plugin output (before delta/bypass mix, after output gain) |
| Sidechain | External sidechain bus, when enabled |

Each tap is a `SpscFifo<float>` per channel, capacity 65536 samples, written on the
audio thread with a non-blocking push (drop on overflow). Taps are only written
while an editor is open or the instance is published to the hub.

### 1.2 Transform

Runs on the worker thread, triggered by the UI frame timer.

- Window: Hann. FFT size by resolution setting, at 44.1/48 kHz: Low 2048,
  Medium 4096, High 8192, Maximum 16384 (scaled with sample rate).
- Overlap: new frame every `max(N/8, samples per UI frame)` samples.
- Channel view: Stereo sum (power average), Left/Right, or Mid/Side split.
- Magnitude to dB, then a display tilt of 0, 3 or 4.5 dB/oct around 1 kHz
  (default 4.5).
- **Display resampling**: bins are mapped to `P` log-spaced display points (one
  per horizontal pixel, capped at 1024). Where several bins fall in one point take
  the maximum; where one bin spans several points use Catmull-Rom interpolation in
  dB. Then fractional-octave smoothing (off, 1/12, 1/6, 1/3 octave).
- **Ballistics** per display point: instant attack, exponential release by speed
  setting: Slow 6, Medium 12, Fast 24, Very fast 48 dB/s.
- **Peak hold** trace (optional): decays at 3 dB/s after a 2 s hold.
- **Freeze**: stops ballistics; the trace keeps its values.
- Range: bottom of the display maps to -60, -90 or -120 dBFS.

The result is a `SpectrumFrame { float db[1024]; int count; double fMin, fMax; }`
handed to the UI through an `AtomicSnapshot`.

### 1.3 Spectrogram view

**Built** as described below, for the output spectrum: 240 rows (8 s at 30 updates
a second) in a ring image half the plot's width, one row written per analyzer
update and drawn in two pieces so the newest is at the top. The colour ramp runs
from nothing through indigo, the accent and amber to white and is fixed in code
for now (the theme key arrives with theme files).

Alternative to the trace: a scrolling image, time on the vertical axis (newest at
the top), same log frequency axis as the display, 8 s history. Each new frame
writes one pixel row into a ring `juce::Image`; colour is a perceptually uniform
ramp defined in the theme (`spectrogramRamp`, 256 entries). The EQ curve and nodes
draw on top as usual.

### 1.4 Spectrum grab

When the pointer rests over the analyzer for 600 ms without a button down:

1. Freeze a copy of the post trace.
2. Find local maxima at least 6 dB above the 1-octave smoothed trace; keep the 8
   largest.
3. Draw a small handle on each. Dragging a handle creates a Bell at that frequency
   with `Q` estimated from the peak's -3 dB width (clamped 1 to 12) and follows the
   pointer for gain.

Leaving the display or pressing Esc cancels.

## 2. Instance hub

**Built** (`src/hub/InstanceHub`, `src/ui/InstancePanel`): the in-process registry
(2.1), the panel's list with names, colours and mini spectra, per-instance overlays
in the display, and the collision view (2.4). Each instance feeds a copy of its
output to a small FIFO only while more than one instance is registered; the hub
computes the spectra on the message thread at 10 Hz. Names and colours come from
the host through `updateTrackProperties`, with "EQ n" and a palette colour as
fallbacks. A silent instance is listed but not drawn.

**Not built:** the cross-process fallback (2.2), opening another instance inline
to edit it and its bypass toggle (2.3), and using another instance as the EQ match
reference. Verified by the snapshot tool with two instances in one process.

Purpose: show other instances' spectra, highlight masking, and edit other
instances from one window.

### 2.1 In-process registry

`of::InstanceHub` is a `juce::SharedResourcePointer` singleton per process.

```cpp
struct InstanceInfo {
  juce::Uuid id;
  juce::String name;         // host track name via updateTrackProperties, else user name
  juce::Colour colour;       // host track colour, else assigned from the band palette
  AtomicSnapshot<SpectrumFrame> post;   // 1/6-octave smoothed, 200 ms release
  std::atomic<bool> visible;
  juce::WeakReference<PluginProcessor> processor;   // message thread only
};
```

- Instances register in the processor constructor and deregister in the
  destructor, both on the message thread, guarded by a mutex that the audio thread
  never takes.
- A 10 Hz hub timer prunes dead entries and notifies listeners (instance panels).
- Hub spectra are heavily time-smoothed, so differing latencies between tracks do
  not matter.

### 2.2 Cross-process fallback

Some hosts run each plugin in its own process. On registration each instance also
publishes `{id, name, colour, spectrum}` into a memory-mapped file
`<temp>/openfilter-hub-<session>/<uuid>.bin` (fixed 8 kB layout, sequence counter
for tear-free reads, rewritten at 10 Hz). Instances scan that directory at 1 Hz.
Entries not updated for 5 s are ignored; an instance deletes its own file on
destruction. Cross-process entries are **read-only**: spectra and collision view
work, remote editing does not, and the panel says so.

### 2.3 Instance panel

Lists all instances with name, colour swatch, mini response curve and mini
spectrum. Actions per in-process instance:

- Show its spectrum as an overlay in this display (toggle).
- Open it inline: the panel swaps this window's display and dock to edit the other
  instance's parameters (through that processor's APVTS on the message thread). A
  banner shows whose bands are being edited.
- Bypass toggle.

### 2.4 Collision view

For each overlay instance, compare its spectrum `O` with our post spectrum `P` on a
1/6-octave grid:

```
collide(f) = P(f) > floor  and  O(f) > floor  and  |P(f) - O(f)| < 9 dB
floor = max(P.max, O.max) - 40 dB
strength = 1 - |P - O| / 9
```

Draw a strip along the bottom of the display in the other instance's colour with
opacity proportional to `strength`. The instance panel shows the same strip per
row. Constants are *(tune)*.

## 3. EQ match and sketch

**Built** (`src/match/CurveFitter`, `src/ui/MatchPanel`). Differences from the text
below: the fitter runs on the message thread (it takes a few milliseconds), the
band count is a drop-down of 3 to 24 rather than a slider, the fitted curve is not
previewed live (the panel shows the target and the fit error), and a reference
cannot yet be taken from another instance. Applying a match or a sketch replaces
all bands in one undo step. Verified by `tests/unit/fit_tests.cpp` (a six-band
curve recovered to 0.01 dB RMS) and by the snapshot tool, which captures a
spectrum through the analyzer path and recovers a known difference to 0.02 dB.

Both produce a **target curve** `D(f)` in dB on a 256-point log grid from 20 Hz to
20 kHz and hand it to `of::CurveFitter`.

### 3.1 Targets

- **Match**: `D = smooth(Ref) - smooth(Src)`, both long-term average spectra
  (power average, minimum 3 s of signal above -60 dBFS), 1/3-octave smoothed.
  `Src` is our input. `Ref` is captured from the sidechain, from another
  instance's hub spectrum, or from an audio file dropped on the panel (decoded on
  the worker with `juce::AudioFormatManager`, analysed offline). `D` is clamped to
  ±18 dB and its mean between 200 Hz and 5 kHz is removed.
- **Sketch**: the user draws a freehand curve in the display with the pencil tool;
  the polyline is resampled to the grid and lightly smoothed (1/6 octave).

### 3.2 Fitter

Input: `D`, band budget `K` (1 to 24, user slider). Output: `K` bands.

1. **Ends**: if the mean of `D` below 80 Hz or above 8 kHz deviates more than 2 dB
   from the mid-band mean, seed a shelf there.
2. **Greedy seed**: repeat until `K` bands: find the frequency of the largest
   absolute residual; seed a Bell with that frequency and gain, `Q` from the
   residual's half-gain width; subtract its prototype response from the residual.
3. **Refine**: Levenberg-Marquardt on all parameters jointly (log f, dB gain,
   log Q), cost = sum of squared dB error on the grid, at most 30 iterations,
   parameters clamped to their ranges, `Q` clamped 0.3 to 8.
4. **Prune**: drop bands whose gain is under 0.3 dB and re-run step 3 once.

Runs on the worker; the UI shows the target as a dashed line and the fitted curve
live as the slider moves (results for each `K` are cached). Applying writes the
bands in one undo transaction. Acceptance: for a target generated from 6 known
bands, the fitter with `K = 6` reproduces the curve within 0.5 dB RMS.
