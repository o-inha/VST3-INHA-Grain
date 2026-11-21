# INHA Grate

Windows x64 VST3 insert effect for captured granular textures. Development
version: **0.5.1**. Built on INHA Core 0.1.2 and the native INHA editor family.

## Getting started

Install the complete `INHA Grate.vst3` folder as explained in INSTALL.txt.
Generate a normal INHA key for product **`inha-grate`** and place
`inha-licence.key` in its `Contents/Resources` directory. No key is supplied or
committed. The production module uses the shared INHA licensing implementation.

Insert Grate after your instrument. Start with TRANSIENTS, adjust PICKUP., and
balance DRY/WET. In MIDI NOTES mode route notes to the effect using your DAW's
MIDI routing; AMOUNT sets the probability of accepting each note. Notes trigger
events without transposing their audio.

## Controls

Two values under a control usually bound a seeded random range. EQ is the
exception: its two values are fixed low-cut and high-cut frequencies.

| Control | Behavior |
|---|---|
| TRIGGER | NONE, TRANSIENTS, MIDI NOTES, VOLUME |
| AMOUNT / PICKUP / THLD | MIDI acceptance probability, transient sensitivity, or volume threshold; inactive in NONE |
| MASS | 1×–8× wet layers. The main layer follows the visible motion controls; added layers use independent seeded slow SWAY and progressively lower levels |
| BUFFER | Duration of the source audio available for random grain starts |
| GRAIN | Requested grain-duration range; effective durations cannot exceed BUFFER or the source audio already captured |
| PAN | Seeded per-grain stereo balance range, −100 (left) to +100 (right); 0 preserves the original stereo balance |
| SYNC | Tempo-synced BUFFER, GRAIN, A, R and SWAY SPEED, from 1/16 note to two 4/4 bars |
| T beside BUFFER / GRAIN | Independently include triplet divisions alongside straight values in that control's synced pool; disabled in free time while retaining each toggle setting |
| ENV | Grain-window range: 0% triangle, 50% sine, 100% rounded square |
| UNDULATION / SWAY tabs | Select event-shaped movement or continuous smooth random movement; each tab retains independent AMOUNT/SPEED settings |
| UNDULATION AMOUNT | Signed grain-size movement: negative toward the selected GRAIN minimum, positive toward the current duration upper limit, zero steady; BUFFER caps the effective size |
| UNDULATION SPEED | Duration as a percentage of the event's chosen attack + release; 50% means half their sum, 200% twice it |
| SWAY AMOUNT | Depth range relative to the current event's base grain size, 0–100%; 20% permits approximately 80–120% of that size |
| SWAY SPEED | Time range between smooth random targets; independent of event attack/release |
| DUCK WET | Actual dry-input audio ducks the wet effect; 0% off, 100% nearly mutes wet on loud input; default 0% |
| DUCK PRV | Shortens previous events' tails when a new event arrives; disabled in NONE; default 50% |
| EQ | Fixed wet-only low/high cutoffs, 20 Hz–20 kHz, 12 dB/octave; full range bypasses both filters |
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
The separate **T** buttons beside BUFFER and GRAIN add triplet choices such as
`1/8T`, `1/4T` and `1/2T` to that control's value pool. They use the compact
outlined/filled INHA Stutter style and are available while SYNC is enabled.
Both default off; their settings are saved and automatable. Turning T off
retains the requested position and resolves triplet positions to straight
values; turning it back on restores those triplet positions.
UNDULATION SPEED remains a percentage of A + R.

GRAIN handles retain the requested values above BUFFER. The white range arc
and numeric values show the effective capped range; affected values turn pink.
Raising BUFFER restores the previously requested range automatically.

## MASS layers

MASS 1× plays the main wet layer. Each higher setting adds another parallel wet
layer sharing the event trigger and captured audio. The direct DRY signal is
mixed once.

The main layer follows the selected UNDULATION/SWAY controls. Added layers
ignore UNDULATION and the visible SWAY settings. Each gets a separate,
repeatable random identity derived from the main SEED and its layer index,
including its own source positions, grain-size choices, PAN and grain window.
Its built-in slow SWAY selects a depth in **5–25%**, with movement over
**3–6 seconds** in free time or **1–2 bars (4/4)** with SYNC enabled. In SYNC, an added layer starts from a
valid GRAIN division and then drifts smoothly around it; its hidden SWAY is
continuous so weak movement is not flattened by note-length snapping. The main
layer continues to follow the selected SYNC divisions.

Layer gains follow **1, 0.85, 0.7225, 0.6141…**. Every layer normalizes its own
overlapping windows, then the layers add without overall MASS normalization.
The primary layer keeps its level. Eight identical layers can sum to about
**+13.7 dB** before WET adjustment and final peak protection; lower WET to taste.
Peak protection uses one linked gain for both channels, with a short hold and
smooth recovery. It reduces overloaded peaks without flattening individual
waveform crests. Below the ceiling, with no preceding overload, it is exact
unity. MASS adds no automatic layer-count normalization.
Adding/removing layers fades smoothly. Existing layer sequences and the main
layer's timing/source choices stay independent of the MASS setting.
Parallel layers can still replay source transients at different times; MASS
adds an evolving cloud rather than guaranteeing unchanged transient rhythm.

## Grain-size motion

**UNDULATION** keeps the original behavior: each event chooses a signed amount
and duration, then gradually moves its grain size in that direction. Negative
amounts approach the lower of the two GRAIN values, instead of the global
minimum duration. The selected minimum follows GRAIN automation and tempo
sync. Positive amounts continue toward the upper duration limit. BUFFER and
the audio already captured always bound the effective grain size.

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

## Wet ducking and EQ

DUCK WET listens to the original stereo input before the DRY output fader.
It uses one linked detector for both channels, preserving the stereo balance.
Quiet input below roughly -36 dBFS causes no reduction; by -12 dBFS the chosen
duck depth is fully applied. At 100%, loud dry input nearly mutes the wet
effect. A fast 3 ms gain attack and 120 ms recovery let the effect return as
the source becomes quiet. This follows actual audio, independently of event
envelopes and the WET output level. DRY is not attenuated by this control.

EQ applies only to the summed wet signal. Raise the lower handle to remove
low frequencies; lower the upper handle to remove high frequencies. The fixed
cutoffs use 12 dB/octave Butterworth slopes and smooth parameter changes.
At the default 20 Hz / 20 kHz endpoints both filters are exactly bypassed.
Neither EQ cutoff is randomized. EQ and ducking work in NONE as well as
triggered modes. Existing saved duck values are retained when loading older
states; the new 0% WET / 50% PRV defaults apply to new instances and resets.

## Capture and texture

- **NONE:** rolling live capture; no event trigger is needed.
- **TRANSIENTS:** an onset starts an event. Detection is locked for the chosen
  attack duration. The event uses rolling pre-trigger history and keeps
  recording for a seeded **50–95% of BUFFER** after the trigger, then freezes.
- **MIDI NOTES:** accepted note-ons use the same seeded capture behavior.
- **VOLUME:** the effect remains held while the detected input level is above
  THLD, then releases. Attack is the minimum hold duration/retrigger lockout.

The remaining 5–50% of the final BUFFER window comes from audio preceding the
trigger. The hidden capture fraction derives from the main seed and event
timeline; it adds no user control. Capture continues in the background for
future events. After its chosen recording interval, an event reads only its
own frozen source for the rest of its lifetime. Missing prehistory at a fresh
transport start is known silence, never stale audio. Existing source history
lets grains start without waiting for an entire BUFFER to fill. Long grains
still require a complete contiguous source slice; a short slice is never
looped inside a longer grain, preventing the former pitched startup tones.
Later triggers cannot overwrite an older event's source. BUFFER changes do
not restart or extend an already chosen capture interval.

Grains choose seeded random positions. Each layer uses 50% overlap and independent window
normalization; MASS layers choose independent seeded positions and PAN values.
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

## Changes in 0.5.1

- Replaced the instantaneous output clipping curve with stereo-linked peak
  gain control. Additive MASS can raise the dry-plus-wet mix above the output
  ceiling; the previous curve generated robotic harmonic tones in that case.
- Preserved additive MASS gains, grain/capture timing, seed choices, and exact
  below-ceiling audio. Full bypass continues to preserve the original input.
- Added overloaded-sine harmonic, stereo-link and recovery regressions.

## Changes in 0.5.0

- Seeded 50–95% post-trigger capture with rolling prehistory, then a frozen
  BUFFER source for the full event.
- Additive MASS preserves the primary layer's volume and the 15% layer taper.
- Audio-driven DUCK WET replaces event-driven dry attenuation; new defaults
  are WET 0% and PRV 50%.
- Added fixed wet EQ cutoffs with neutral full-range defaults and 12 dB/octave
  slopes. Widened the editor and evenly redistributed the top-row spacing.
- State v5 imports v1–v4 parameters with neutral EQ defaults.

## Changes in 0.4.0

- Renamed the product, bundle, VST3 identities, licensing ID, preferences and
  resources to INHA Grate / inha-grate, with the new supplied logo.
- MASS now adds quieter independent layers with forced slow 5–25% SWAY.
- Main and existing layer random sequences stay stable when MASS changes.
- Older saved parameter payloads can be imported manually; the DAW sees the
  renamed product as a separate VST3 identity.

## Changes in 0.3.2

- SYNC uses a background highlight with a constant text color and no underline.
- Compact adjacent TRIGGER/SYNC buttons align to the header's right edge.
- The trigger dropdown uses the compact width and padding from INHA Stutter.
- Independent T buttons beside BUFFER and GRAIN add triplet divisions to each
  synced value pool, with Stutter's visual styling and label spacing.
- State v4 preserves existing v1/v2/v3 projects, with both T buttons off for
  older states.

## Changes in 0.3.1

- UNDULATION now uses the selected GRAIN minimum as its lower endpoint and
  lower bound, including when that minimum is automated or tempo-synced.
- Shorter BUFFER/captured source still takes precedence for safe audio reads.

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
