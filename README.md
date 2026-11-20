# INHA Grain

Windows x64 VST3 insert effect for captured granular textures. Development
version: **0.3.0**. Built on INHA Core 0.1.2 and the native INHA editor family.

## Getting started

Install the complete `INHA Grain.vst3` folder as explained in INSTALL.txt.
Generate a normal INHA key for product **`inha-grain`** and place
`inha-licence.key` in its `Contents/Resources` directory. No key is supplied or
committed. The production module uses the shared INHA licensing implementation.

Insert Grain after your instrument. Start with TRANSIENTS, adjust PICKUP., and
balance DRY/WET. In MIDI NOTES mode route notes to the effect using your DAW's
MIDI routing; AMOUNT sets the probability of accepting each note. Notes trigger
events without transposing their audio.

## Controls

Two values under a control are the lower/upper bounds of a seeded random range.

| Control | Behavior |
|---|---|
| TRIGGER | NONE, TRANSIENTS, MIDI NOTES, VOLUME |
| AMOUNT / PICKUP / THLD | MIDI acceptance probability, transient sensitivity, or volume threshold; inactive in NONE |
| MASS | 1×–8× parallel grain streams per event; higher values thicken the cloud without multiplying output gain |
| BUFFER | Duration of the source audio available for random grain starts |
| GRAIN | Requested grain-duration range; effective durations cannot exceed BUFFER or the source audio already captured |
| PAN | Seeded per-grain stereo balance range, −100 (left) to +100 (right); 0 preserves the original stereo balance |
| SYNC | Tempo-synced BUFFER, GRAIN, A, R and SWAY SPEED, from 1/16 note to two 4/4 bars |
| ENV | Grain-window range: 0% triangle, 50% sine, 100% rounded square |
| UNDULATION / SWAY tabs | Select event-shaped movement or continuous smooth random movement; each tab retains independent AMOUNT/SPEED settings |
| UNDULATION AMOUNT | Signed grain-size movement within the current duration domain: negative toward 5 ms, positive toward 10 seconds, zero steady |
| UNDULATION SPEED | Duration as a percentage of the event's chosen attack + release; 50% means half their sum, 200% twice it |
| SWAY AMOUNT | Depth range relative to the current event's base grain size, 0–100%; 20% permits approximately 80–120% of that size |
| SWAY SPEED | Time range between smooth random targets; independent of event attack/release |
| DUCK DRY | Attenuates the direct signal while granular events are active |
| DUCK PRV | Shortens previous events' tails when a new event arrives; disabled in NONE |
| A | Random linear event attack; also trigger lockout/minimum volume-event duration |
| R | Random curved event release |
| DRY / WET | Independent output levels; click a label to mute/restore, double-click to reset |
| SEED / refresh | Edit the seed or create a new one; stored with the project |
| Gear | Theme and 100%/150% editor size |

Drag vertically. Shift adjusts finely. For range controls Ctrl translates both
ends and Alt changes the width symmetrically. Double-click restores defaults.
Free-time duration controls span **40 ms–6 seconds** (two 4/4 bars at 80 BPM).
Times above 999 ms use seconds, e.g. `1.00 S`. SYNC uses standard note labels
`1/16`, `1/8`, `1/4`, `1/2`, `1 BAR` and `2 BARS`. Tempo follows the host
within 20–999 BPM; the last valid tempo is retained if the host omits it (initially 120 BPM).
UNDULATION SPEED remains a percentage of A + R.

GRAIN handles retain the requested values above BUFFER. The white range arc
and numeric values show the effective capped range; affected values turn pink.
Raising BUFFER restores the previously requested range automatically.

## Grain-size motion

**UNDULATION** keeps the original behavior: each event chooses a signed amount
and duration, then gradually moves its grain size in that direction.

**SWAY** is smooth random modulation. Its clock follows the DAW timeline and
continues across notes, transients and gaps; new events do not restart it.
Targets occur at seeded random intervals within the SPEED range and are joined
smoothly. There is no fixed rising/falling direction. Movement can fall anywhere
inside the chosen depth; the lower AMOUNT is a lower bound on target depth,
not a requirement to stay away from the center.

SWAY is centered on each event's current base GRAIN size. Moving or automating
GRAIN therefore moves the center of the modulation too. At 20% depth, a 200 ms
base can move roughly between 160 and 240 ms. Grain sizes remain within the
current timing domain and available BUFFER. Both motion modes update newly
launched grains while
active grains finish smoothly, so very long grains respond more slowly.

The tabs retain independent settings and the selection is automatable. NONE
uses continuous SWAY and disables the UNDULATION tab and DUCK PRV. Switching
back to a triggered mode restores the stored motion selection. New controls
are appended to preserve existing automation IDs. Saved 0.1/0.2 states migrate
stored durations in milliseconds into the new 40 ms–6 second bounds.

## Capture and texture

- **NONE:** rolling live capture; no event trigger is needed.
- **TRANSIENTS:** an onset starts a capture at that sample. Detection is locked
  for the chosen attack duration. The capture fills progressively for BUFFER's
  duration, then stays frozen for that event's remaining tail.
- **MIDI NOTES:** accepted note-ons use the same trigger-forward capture.
- **VOLUME:** the effect remains held while the detected input level is above
  THLD, then releases. Attack is the minimum hold duration/retrigger lockout.

Capture is causal: the complete post-trigger buffer cannot exist immediately.
Grains wait until at least the current minimum duration has been recorded,
then read a complete, already-written source segment once. The available
capture grows as input arrives. Long requested grains initially use shorter
safe segments until enough source has been captured. A short capture is never
looped inside a longer grain; this prevents the former pitched startup tones.
Each event owns its capture; later notes do not replace an older event's source audio.

Grains choose seeded random positions. Each stream uses 50% overlap and window
normalization; MASS streams choose independent seeded positions and PAN values.
Active grains keep their duration and envelope: automation and
grain-size motion change subsequent grains with smooth transitions. Even square-like
windows retain short edge ramps. Every source read is inside captured audio.

Six event slots bound simultaneous work. Older events are retired more quickly
when the budget is under pressure; incoming floods cannot allocate unbounded
voices or overwrite a still-audible capture. Output level control includes
overlap normalization and peak protection.

## Repeatability

Random choices derive from seed and audio timeline, rather than the clock.
Repeat renders use the same input, automation, MIDI, sample rate and playback
start/history. Audio captures and detector histories are live DSP state: seeking
into the middle of a recording cannot reconstruct audio the plug-in has never
received. Discontinuous transport and playback restarts reset that history. Use the same preroll for
identical renders. Refreshing SEED intentionally changes the result.

## Changes in 0.3.0

- Removed short inner capture loops responsible for artificial pitched tones at triggers.
- Added MASS, PAN, tempo SYNC and BUFFER-capped grain durations.
- Reorganized the editor to six top knobs and bottom DRY/WET sliders.
- Renamed SCATTER to BUFFER and SENS. to PICKUP; shortened selected trigger labels.
- NONE uses SWAY and disables controls that need previous triggered events.

## Build and validation

The Windows GitHub Actions workflow fetches the pinned Steinberg SDK, builds
Release x64, runs standalone engine and VST3 module/editor tests, captures editor
previews, and packages the production bundle using INHA Core's package script.
The test-only editor fixture uses a separate licensing dependency and remains
outside the release bundle.

```sh
cmake -S . -B build -DINHA_PLUGIN_BUILD_VST3=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

For Windows, fetch SDK tag `v3.7.14_build_55` with submodules into
`Dependencies/VST3_SDK`, configure with Visual Studio 2022 x64 and
`-DINHA_PLUGIN_BUILD_VST3=ON`, then build/test Release. See
[docs/PROVENANCE.md](docs/PROVENANCE.md) for foundations and STREAM research.
