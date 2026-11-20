# INHA Grain

Windows x64 VST3 insert effect for captured granular textures. Development
version: **0.2.0**. Built on INHA Core 0.1.2 and the native INHA editor family.

## Getting started

Install the complete `INHA Grain.vst3` folder as explained in INSTALL.txt.
Generate a normal INHA key for product **`inha-grain`** and place
`inha-licence.key` in its `Contents/Resources` directory. No key is supplied or
committed. The production module uses the shared INHA licensing implementation.

Insert Grain after your instrument. Start with TRANSIENTS, adjust SENS., and
balance DRY/WET. In MIDI NOTES mode route notes to the effect using your DAW's
MIDI routing; AMOUNT sets the probability of accepting each note. Notes trigger
events without transposing their audio.

## Controls

Two values under a control are the lower/upper bounds of a seeded random range.

| Control | Behavior |
|---|---|
| TRIGGER | NONE, TRANSIENTS, MIDI NOTES, VOLUME |
| AMOUNT / SENS. / THLD | MIDI acceptance probability, transient sensitivity, or volume threshold; inactive in NONE |
| SCATTER | Duration of the source audio available for random grain starts, up to 20 seconds |
| GRAIN | Grain-duration range, 5 ms–10 seconds |
| ENV | Grain-window range: 0% triangle, 50% sine, 100% rounded square |
| UNDULATION / SWAY tabs | Select event-shaped movement or continuous smooth random movement; each tab retains independent AMOUNT/SPEED settings |
| UNDULATION AMOUNT | Signed grain-size movement within the full 5 ms–10 second knob domain: negative toward 5 ms, positive toward 10 seconds, zero steady |
| UNDULATION SPEED | Duration as a percentage of the event's chosen attack + release; 50% means half their sum, 200% twice it |
| SWAY AMOUNT | Depth range relative to the current event's base grain size, 0–100%; 20% permits approximately 80–120% of that size |
| SWAY SPEED | Time range between smooth random targets, 25 ms–20 seconds; independent of event attack/release |
| DUCK DRY | Attenuates the direct signal while granular events are active |
| DUCK PRV | Shortens previous events' tails when a new event arrives |
| A | Random linear event attack, up to 10 seconds; also trigger lockout/minimum volume-event duration |
| R | Random curved event release, up to 30 seconds |
| DRY / WET | Independent output levels; click a label to mute/restore, double-click to reset |
| SEED / refresh | Edit the seed or create a new one; stored with the project |
| Gear | Theme and 100%/150% editor size |

Drag vertically. Shift adjusts finely. For range controls Ctrl translates both
ends and Alt changes the width symmetrically. Double-click restores defaults.
Times above 999 ms use seconds, e.g. `1.00 S`.

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
5 ms–10 second limits. Both motion modes update newly launched grains while
active grains finish smoothly, so very long grains respond more slowly.

The tabs retain independent settings and the selection is automatable. Old
0.1.0 states load in UNDULATION mode with their original parameter values.
The new controls are appended to preserve existing automation IDs.

## Capture and texture

- **NONE:** rolling live capture; no event trigger is needed.
- **TRANSIENTS:** an onset starts a capture at that sample. Detection is locked
  for the chosen attack duration. The capture fills progressively for SCATTER's
  duration, then stays frozen for that event's remaining tail.
- **MIDI NOTES:** accepted note-ons use the same trigger-forward capture.
- **VOLUME:** the effect remains held while the detected input level is above
  THLD, then releases. Attack is the minimum hold duration/retrigger lockout.

Capture is causal: the complete post-trigger buffer cannot exist immediately.
Early grains use the already recorded portion, and the available capture grows
as input arrives. Each event owns its capture; later notes do not replace an
older event's source audio.

Grains choose seeded random positions. Grain streams use 50% overlap and window
normalization. Active grains keep their duration and envelope: automation and
grain-size motion change subsequent grains with smooth transitions. Even square-like
windows retain short edge ramps, and sample loops use smoothed seams.

Six event slots bound simultaneous work. Older events are retired more quickly
when the budget is under pressure; incoming floods cannot allocate unbounded
voices or overwrite a still-audible capture. Output level control includes
overlap normalization and peak protection.

## Repeatability

Random choices derive from seed and audio timeline, rather than the clock.
Repeat renders use the same input, automation, MIDI, sample rate and playback
start/history. Audio captures and detector histories are live DSP state: seeking
into the middle of a recording cannot reconstruct audio the plug-in has never
received. Discontinuous transport resets that history. Use the same preroll for
identical renders. Refreshing SEED intentionally changes the result.

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
