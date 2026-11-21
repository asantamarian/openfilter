# OpenFilter EQ: user manual

OpenFilter EQ is an equalizer plugin with up to 32 bands. Each band can be static,
dynamic or spectral, can sit on the whole stereo signal or on one of left, right,
mid and side, and can be minimum phase or linear phase.

This manual describes version 0.1.1, as built. Where something is limited or not
finished, it says so.

## Contents

1. Installing
2. The window
3. Working with bands
4. Shapes and slopes
5. Channel placement
6. Phase modes
7. Dynamic bands
8. Spectral bands
9. Output section
10. The analyzer
11. EQ match and sketch
12. Other instances
13. Presets, slots and undo
14. MIDI learn
15. Keyboard
16. Themes and interface size
17. Latency
18. CPU
19. Known limits

## 1. Installing

Build from source (see the README). On Linux the build copies the plugins to:

| Format | Folder |
|---|---|
| VST3 | `~/.vst3/` |
| CLAP | `~/.clap/` |
| LV2 | `~/.lv2/` |

Rescan plugins in your host afterwards. A standalone program is also built.

Only Linux has been built and tested so far.

## 2. The window

| Area | What is there |
|---|---|
| Top bar | Undo, redo, presets, A/B/C/D slots, auto gain, phase mode, View menu, Delta, Bypass |
| Display | The response curve, the bands, and the analyzer behind them |
| Output column | Level meter, output gain, gain scale, balance, polarity |
| Dock | The selected band's controls: band chips, shape picker, slope, channel, filter dials, dynamics |
| Status bar | MIDI learn, latency, a hint, and Range, Tilt and Scale, each of which a click steps through |

The window can be resized by its corner, down to 800 by 560. Below about 1180
pixels wide the dock splits into a **Filter** page and a **Dynamics** page (button
at its top right), and the top bar drops its readouts, then undo and redo, then the
slot buttons.

Most things that are not on the surface are in the **View** menu.

## 3. Working with bands

**Add** a band by double-clicking the display, or with the **+** chip in the dock. Where you click chooses the shape:
far left a low cut, left a low shelf, far right a high cut, right a high shelf,
anywhere else a bell.

**Select** a band by clicking it. The dock then shows its controls and the display
shades its contribution in the band's colour.

**Move** a band by dragging it: left and right for frequency, up and down for
gain. Hold Shift for fine movement.

**Change Q** by scrolling over a band.

**Other actions**

| Action | How |
|---|---|
| Bypass a band | Alt-click it, or the Bypass button in the dock |
| Delete | Delete key, or the Delete button |
| Reset gain to 0 dB | Double-click the band |
| Menu of shape, slope, channel | Right-click the band |
| Solo | Solo button, or hold S |
| Type a value | Click the number under a dial |

**Typed frequencies** accept `850`, `1.2k`, `1k2` and note names such as `A4`,
`C#3` or `Bb2+20` (cents).

**Several bands at once.** Shift-click to add or remove a band from the selection,
drag across empty space to draw a rubber band, or press Ctrl/Cmd+A for all.
Dragging one selected band moves the rest by the same frequency ratio and gain
difference. Scrolling, the arrow keys, copy, duplicate and delete act on all of
them. Dials in the dock change every selected band (frequencies, Q, ratio and times
by the same ratio; gain, range and threshold by the same amount), and picking a
shape, slope, channel or dynamics mode sets it on all.

**Band table** (View menu): every band as a row. Double-click a frequency, gain or
Q to type; click shape, slope or channel for a list; click On to toggle.

## 4. Shapes and slopes

| Shape | Use |
|---|---|
| Bell | Boost or cut around a frequency |
| Low Shelf, High Shelf | Raise or lower everything below or above a frequency |
| Low Cut (high-pass), High Cut (low-pass) | Remove everything below or above a frequency |
| Notch | Remove a narrow band completely |
| Band Pass | Keep only a band |
| Tilt Shelf | Tilt the spectrum around a frequency: one side up, the other down |
| Flat Tilt | A constant slope across the whole spectrum |
| All Pass | Change phase without changing level |

Slopes run from 6 to 96 dB per octave. **Brickwall** is available for the two cuts.
For bells, notches and band passes a steeper slope gives a flatter top and steeper
sides.

For cuts, Q sets the resonance at the corner: 0.71 is flat, higher values add a
bump.

## 5. Channel placement

The **channel** list in the dock places a band on Stereo, Left, Right, Mid or Side.
A small letter under the band shows a placement other than Stereo.

Left and right bands are applied first, then mid and side bands. Band numbers do
not affect the sound.

## 6. Phase modes

Chosen in the top bar. The number next to it is the latency in samples.

| Mode | What it does | Cost |
|---|---|---|
| Zero latency | No delay. The response drifts from the drawn curve in the top octave at 44.1/48 kHz (about 1 dB around 9 kHz, 2 dB around 15 kHz) | Least CPU |
| Natural | Follows the drawn curve up to 20 kHz | 76 samples at 44.1/48 kHz, 13 at 88.2/96 kHz; about five times the CPU |
| Linear | No phase shift at all | Long delay, set by Quality |
| Mixed | Each band chooses: a **Lin** button appears in the dock | As Linear |

**Linear quality** (appears in Linear and Mixed) sets the filter length: Low to Max.
Longer is more exact at low frequencies and for narrow bands. Latency at 48 kHz is
2560, 4608, 8704, 16896 and 33280 samples.

Linear phase smears a little energy *before* transients (pre-ringing), most
noticeably with steep or narrow low-frequency bands. That is inherent to the
technique. Mixed mode lets you keep such bands minimum phase.

All Pass bands do nothing in the linear-phase path.

## 7. Dynamic bands

A dynamic band moves its gain when the level around it passes a threshold. Bells,
both shelves and the tilt shelf can be dynamic.

Set the **Dynamics** list in the dock to **Dynamic**.

| Control | Meaning |
|---|---|
| Range | How far the gain moves at most. Negative ducks the band, positive lifts it |
| Thresh | The level where it starts |
| Auto | The threshold follows the long-term level around the band, so the band reacts the same on quiet and loud material. Thresh then reads as an offset ("+10 dB rel") |
| Ratio | How much of the excess over the threshold is acted on |
| Attack, Release | How fast it engages and lets go |
| Ext SC | Listen to the plugin's sidechain input instead of this track |
| Key | Hear what the band's detector hears |

In the display, a dashed curve shows where the selected band ends up at full
range, and a small marker on each dynamic band moves with its live gain.

The **Advanced** panel (View menu) adds, for the selected band: **Knee**,
**Trigger** (act above or below the threshold), and a **Free detector** that
listens at its own frequency and Q. A free detector lets a band at one frequency
react to another, for example a band at 3 kHz that ducks when the bass hits.

**Lookahead** (View menu: 2, 5 or 10 ms) delays the audio so the gain is already
moving when a transient arrives. It adds that much latency.

The detector always listens to the plugin's input (or the sidechain), never to
another band, so bands cannot influence each other's detection.

In the linear-phase path a dynamic band still works. It adds 2048 samples of
latency, its gain moves in steps of about 10 ms, and at most eight bands can be
dynamic there.

## 8. Spectral bands

Set the Dynamics list to **Spectral**. The band then decides its gain separately
for every frequency inside its shape. One ringing note can be turned down while its
neighbours are left alone, which an ordinary dynamic band cannot do.

| Control | Meaning |
|---|---|
| Reson (on) | A frequency is treated when it sticks out of the spectrum around it, at any level. Thresh then reads "dB out": how far it must stick out |
| Reson (off) | A frequency is treated when it passes the threshold |
| Resolution (Advanced panel) | How finely the gain follows individual frequencies. Smaller is more surgical |

Range, ratio, attack and release work as for dynamic bands. The marker in the
display shows the strongest frequency's gain.

Spectral processing adds 2048 samples of latency while any band is spectral.

## 9. Output section

| Control | Meaning |
|---|---|
| Out | Output gain |
| Scale | Multiplies the gain and dynamic range of every band. 0 % flattens the EQ, 200 % doubles it, negative values invert it |
| Bal | Balance |
| Inv | Invert polarity |
| Auto gain: curve | Compensates for the loudness change the curve is estimated to cause. Instant |
| Auto gain: loud | Measures the loudness of input against output and matches them. Settles over about two seconds |
| Delta | Hear only what the EQ changes |
| Bypass | Latency-compensated, with a short crossfade |

**Character** (Advanced panel) adds saturation after the EQ: **Tube** (even and odd
harmonics) or **Iron** (odd harmonics, thicker lows), with a **Drive** dial. It adds
76 samples of latency at 44.1/48 kHz. Loud signals come out quieter; use loudness
auto gain to compensate.

## 10. The analyzer

Behind the curve: the input as a soft fill, the output as a line, and the
sidechain as an orange line when one is connected.

View menu, Spectrum analyzer:

| Option | Choices |
|---|---|
| Show, Freeze | |
| View | Input and output traces, or a spectrogram of the output (8 seconds, newest at the top) |
| Speed | Slow to very fast |
| Tilt | None, 1.5, 3, 4.5 or 6 dB per octave, or any value from 0 to 6 with "other" |
| Range | 60, 90 or 120 dB |

**Piano strip** (View menu) shows note positions along the bottom of the display.
With the band table open it sits just above the table.

The analyzer has its own level scale, labelled in dBFS down the left edge: 0 at the
top and the chosen range at the bottom. With a tilt the labels hold at 1 kHz. Turn
the tilt off to compare slopes with an analyzer that has none, such as the ones in
most hosts.

**Display range** (View menu, or Range in the status bar) sets the gain axis on the
right to ±3, 6, 12 or 30 dB. It scales the EQ curve and the nodes, not the analyzer.

## 11. EQ match and sketch

**EQ match** (View menu) makes this track's tone follow a reference.

1. Play the track and press **Capture this track**; press again to stop. A few
   seconds of typical material is enough.
2. Take a reference: **Capture sidechain** (route the reference to the plugin's
   sidechain), **Reference file** (wav, aiff, flac, ogg), or **From instance**
   (another OpenFilter EQ in the project).
3. Choose how many bands to use and press **Apply**.

The orange dashed line is the target, and the panel shows how closely the chosen
number of bands can follow it. Overall level is removed, so the match changes tone
and not loudness. Apply replaces all bands; undo brings them back.

**Sketch** (View menu): choose a number of bands, then draw a curve in the display.
On release it becomes that many bands. It also replaces all bands.

## 12. Other instances

**Instances** (View menu) lists the other OpenFilter EQs in the project. Click one
to draw its spectrum over your display in its colour. Strips along the bottom mark
where it and this track are both loud and close in level: likely masking.

This only works for instances the host runs in the same process. You can view
other instances but not edit them from here.

## 13. Presets, slots and undo

**Presets** (top bar): nine factory presets, your own saved ones, and Save as /
Load from file. Presets are plain JSON files with the extension `.ofeq`, stored in
your user data folder under `OpenFilter/EQ/Presets`. They hold the bands and phase,
scale, auto gain and character settings, not output gain or view settings.

**A/B/C/D**: four complete snapshots for comparing. Switching stores the current
settings in the slot you leave. View menu, Slots, copies the current one to
another. Bypass and Delta are not part of a slot.

**Undo and redo** in the top bar; View menu, History, lists recent steps.

**Copy and paste** bands with Ctrl/Cmd+C and V. The clipboard holds text, so bands
can be pasted into another instance.

## 14. MIDI learn

Turn on **MIDI learn** in the status bar, click a dial, and move a control on your
hardware; turn it off when you are done. Or right-click a dial and choose **Learn
MIDI controller**. The dial's caption then shows the controller number. The same menu clears
it. Mappings are saved with the session.

One controller drives one parameter, with absolute values. Only dials can learn.

## 15. Keyboard

When the display has focus:

| Key | Action |
|---|---|
| 1 to 9, 0 | Select band 1 to 10 |
| Tab, Shift+Tab | Next or previous band by frequency |
| Left, Right | Frequency down or up a semitone (Shift: a tenth) |
| Up, Down | Gain up or down 0.5 dB (Shift: 0.1) |
| Delete | Delete the selection |
| S (hold) | Solo |
| Ctrl/Cmd+A | Select all |
| Ctrl/Cmd+C, V, D | Copy, paste, duplicate |

When a dial has focus: arrows change it (Shift fine, Page Up/Down coarse), Enter
types a value, Home resets it.

Some hosts take keys before the plugin sees them.

## 16. Themes and interface size

**Theme** (View menu): Nocturne (the default), High contrast and Daylight. You can
write the current colours to a file, edit it, and it appears in the list. Theme
files are JSON with the extension `.oftheme`.

**Interface size** (View menu): 50 to 200 %.

## 17. Latency

The plugin reports its latency to the host, which compensates for it. At 48 kHz:

| What is on | Samples |
|---|---|
| Zero latency, nothing else | 0 |
| Natural | 76 |
| Linear or Mixed, Low to Max | 2560, 4608, 8704, 16896, 33280 |
| A dynamic band in the linear-phase path | +2048 |
| Any spectral band | +2048 |
| Lookahead | +96, 240 or 480 |
| Character | +76 |

Changing any of these changes the latency. The output fades in over 10 ms when
that happens, so make such changes while stopped or between sections. None of them
can be automated.

## 18. CPU

Measured for the processing alone at 48 kHz with 512-sample blocks, as a share of
one core of a desktop processor:

| Situation | CPU |
|---|---|
| 8 static bells | 0.35 % |
| 24 mixed bands | 0.9 % |
| 24 bands, Natural | 4.3 % |
| 24 bands, Linear, Medium | 1.0 % |
| 24 bands, Linear, Max | 3.6 % |
| 8 dynamic bands | 0.7 % |
| 4 spectral bands | 2.3 % |

Bands that do nothing (0 dB, bypassed) cost nothing. Linear phase costs about the
same for one band as for many. The editor's drawing cost is separate and has not
been measured.

## 19. Known limits

- Zero latency drifts from the drawn curve in the top octave at 44.1/48 kHz. Use
  Natural or Linear when that matters.
- Only Linear, Mixed and Natural modes follow the curve exactly near 20 kHz.
- The saturation voicing and the dynamics defaults have not been tuned by ear.
- Not tested: Windows, macOS, a screen reader, hardware MIDI controllers, and the
  LV2 editor in a real host.
- Other instances are visible only when the host runs them in the same process.
- In LV2 the sidechain is a required input, so the plugin shows four inputs.
- No full-screen mode and no surround support.
