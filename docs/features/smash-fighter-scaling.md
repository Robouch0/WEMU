# Smash fighter-count scaling

The four- and eight-fighter probes in `scratchpad/scenarios/` enter 8-Player
Smash, select Mario, add three or seven level-3 CPU opponents, and start a
two-minute match on Big Battlefield. The CPUs fight while Player 1 remains
idle. Both probes use the same stage and menu route. These are rendering and
simulation stress cases, not tests of multiple physical controllers.

## Changes

Animated graphics draws continue to use the vertex-conversion cache after their
source data changes. The previous policy abandoned a mesh after four changes,
losing reuse between its later shadow/color passes. A stream-output write epoch
forces source validation if skinning replaces a mesh between draws in the same
frame. The existing 192 MiB conversion budget still bounds this cache.

Submitted vertex/index buffers use four independent hashing lanes instead of a
serial multiply per 32-bit word. The frame identity still includes every stored
word, partial tails and the buffer length. Snapshots retain their existing write
tracking, byte comparisons and ownership; the hash is not persisted.

Stream-output skinning retains converted vertex inputs independently of the
changing bone palettes. The cache compares fetch declarations, semantics,
bindings, the vertex range and all source bytes before reuse. It checks bytes
even within a frame, so an earlier stream-output write cannot hide changed
inputs. Conversion storage is bounded to 64 MiB and 512 entries. Palettes and
output buffers still update on every pass.

Vulkan stream-output buffers that the CPU reads now prefer host-cached,
coherent memory. Upload-only buffers keep their existing placement. Devices
without that memory type retain the coherent fallback.

Transient color and depth attachments reuse completed allocations through a
256 MiB pool. An image is available only when the pool is its sole owner:
in-flight command segments, readback tickets and texture consumers prevent
reuse. Reused images receive a new version and discard their previous active
pixels; each draw still uploads, clears or copies its requested initial state.
Older live versions and row padding remain intact.

Packed R4_G4 UNORM textures (`0x02`) now decode their two independent nibbles
and use the existing versioned guest-texture cache, including linear textures.
Smash's HUD binds these textures once per fighter. Previously they reached a
per-draw floating-point texel expansion and used the RGBA8 fallback despite
having only one byte per texel. This change corrects the texture interpretation
as well as removing repeated decoding and uploads. It does not change the
guest's fighter simulation, frame clock, resolution or frame-skipping policy.

## Local gameplay measurements

Release/x64 runs on an Intel Core i7-14650HX and NVIDIA RTX 4070 Laptop GPU.
The baseline executable was saved before these changes. Both versions used
the checked-in scripts, normal frame-clock pacing and no frame skipping.
Measurements cover delivered presents 3200–4200, after the match starts, with
profiling disabled. No builds, tests or other emulator sessions ran during
these measurement windows. Shader caches were warm.

| Fighters | Core FPS before → after | 95th-percentile interval before → after | 99th-percentile interval before → after |
| --- | ---: | ---: | ---: |
| 4 | 30.43 → 36.92 | 38.36 → 34.03 ms | 42.86 → 37.49 ms |
| 8 | 17.04 → 24.54 | 67.02 → 49.41 ms | 72.92 → 52.98 ms |

These pairs show 21.3% and 44.0% higher delivery rates, respectively. They are
single local comparisons; the eight-fighter result remains below real-time
Smash gameplay. An initial render-only pass measured 30.54 FPS with four fighters
and 21.88 FPS with eight; the changing-mesh admission and submitted-buffer hash
work produced the final figures above.

Captures at presents 3000, 3300, 3600, 3900 and 4200 preserve the stage, fighters
and timer in both pairs. All changed pixels lie in the HUD name strips, between
rows 650 and 683, where the R4_G4 correction restores the character names.
The entire image above row 560 is pixel-for-pixel identical in all ten pairs.
Local logs, frame CSVs, captures and `comparison.json` are in the ignored
`build/smash-scaling/` directory. `fighter-comparison.png` shows the paired
3600-present captures. The `final-4-before` / `final-8-before` directories hold
the baseline; `second-four` / `second-eight` hold the final optimized runs.

## Deferred skinning follow-up

The next optimization batches independent native stream-output passes. Each pass
owns its uploaded attributes, palette and output storage, so preparing the next
fighter no longer waits for the preceding dispatch. Replay resolves results in
order before dependent reads, graphics draws and the frame handoff. The queue
is bounded to 32 passes and 64 MiB of snapshots; backend mapped-buffer capacity
is bounded to 256 MiB. See [native stream output](gpu-stream-output.md) for the
dependency rules, ownership contract and diagnostic switches.

A separate eight-fighter profile against commit `0e1e64b` measured replay wall
time falling from 38.56 to 30.96 ms/frame. Skinning drains averaged 14.2 passes,
instead of waiting after each dispatch. A preliminary eight-pass queue measured
32.19 ms/frame. These diagnostic runs include profiling overhead and are separate
from the gameplay FPS comparisons. All four captures at presents 3000, 3300,
3600 and 3900 match the committed baseline pixel-for-pixel.

The final comparisons with profiling disabled, against the same committed
baseline, measured:

| Fighters | Core FPS before → after | 95th-percentile interval before → after | 99th-percentile interval before → after |
| --- | ---: | ---: | ---: |
| 4 | 37.61 → 40.65 | 32.76 → 30.75 ms | 54.97 → 32.70 ms |
| 8 | 25.50 → 27.43 | 47.99 → 43.96 ms | 52.13 → 49.03 ms |

These single local pairs show 8.1% and 7.6% higher delivery rates. All ten
captures at presents 3000, 3300, 3600, 3900 and 4200 are pixel-for-pixel identical,
including the HUD. The larger percentage reduction in the diagnostic replay
timings does not describe the gain with profiling disabled. Eight-fighter
gameplay remains below 60 FPS.

Local artifacts for this follow-up are in `build/smash-throughput/`:
`wemu-baseline` preserves the committed executable, `baseline-*` and `optimized-*`
hold the gameplay pairs, `comparison.json` records intervals and image checks,
and `profile-comparison.json` records the separate diagnostic timings.
`fighter-comparison.png` shows both pairs at present 3600. Profiling is disabled
in the gameplay pairs, using the same host, scripts and 3200–4200-present window
as above, with no concurrent builds or tests during measurement.

## Reproduction

Use a local game dump and a Release build with the x64 CPU backend:

```bash
python3 scratchpad/session_probe.py /path/to/code/cross_f.rpx \
  --core build/core/wemu --cpu x64 --output /tmp/wemu-eight-fighters \
  --input-script scratchpad/scenarios/smash-8-fighters.json \
  --frames 4200 --seconds 300 --measure-from 3200 \
  --capture-every 300 --capture-from 3000
```

Use `smash-4-fighters.json` for four fighters. The scripts expect the English
menus and the roster layout in the local base-game dump; inspect captures before
using another dump or save state for a comparison. CPU fighter selection is
random, so corresponding before/after captures must show the same scene.

Measure equal gameplay windows with profiling disabled and no concurrent
builds, tests or other game sessions. The probe acknowledges desktop-protocol
frames immediately: its figures measure core delivery and exclude Qt texture
uploads and display synchronization. They do not establish sustained 60 FPS
or results on every stage, fighter combination or host.

For a separate diagnosis pass, add `--profile --clock-profile` and
`--env WEMU_NATIVE_DIRECT_PROFILE=1`. Backend input preparation now separates
depth, target, textures and constants. Stream-output records include
`inputs_reused`. These are CPU wall timings, not GPU timestamps.

### Full desktop app

The core probe above acknowledges frames immediately. To include the normal
library/session UI, input polling, texture upload, window presentation and frame
acknowledgements, the app also accepts `QT_WEMU_SESSION_PROBE=/path/to/probe.json`.
The JSON file specifies absolute paths:

```json
{
  "rpx": "/path/to/code/cross_f.rpx",
  "script": "/path/to/WEMU/scratchpad/scenarios/smash-4-fighters.json",
  "output": "/tmp/wemu-full-desktop",
  "frames": 4200,
  "measureFrom": 3200
}
```

```bash
QT_WEMU_SESSION_PROBE=/tmp/probe.json ./build/desktop/gui/appgui
```

This runs the full app with its saved settings and visible FPS overlay, injects
the scripted inputs, and stops after the requested number of displayed frames.
Scripted input is sent before the frame acknowledgement to avoid the normal
analog-input coalescing timer changing menu selections between different cores.
Ordinary input polling and the normal presentation path stay active.
It writes `frames.csv` with receive/presentation times and acknowledgement delay,
`summary.json`, and captures from present 3000 onward. Without the environment
variable, the measurement hooks are disabled. The app logs its canonical core
executable path as `[SESSION_CORE]` on every launch.

Preserve an older executable before rebuilding a launch path. For an explicit
comparison, `QT_WEMU_PROBE_CORE=/path/to/saved/wemu` selects that executable only
while the desktop probe is enabled. All other launcher settings remain the same.
Do not compare a newly rebuilt `build/core/wemu` with an older independent
`build/desktop/core/wemu` as if they were the same version. Build the `appgui`
target in the directory used for normal play; it also rebuilds its matching core.

The scripted stage route can land on different stages in full-app runs because
analog inputs pass through the desktop input timer. Initial language-5 runs
selected Mario Circuit with the saved core and Big Battlefield with the updated
core. Those frame rates are not a matched performance comparison. Inspect
captures and compare matched scenes before interpreting a frame-rate difference.

A desktop audit found that the local launch command used
`build/desktop/gui/appgui`, while the earlier optimization builds updated
`build/gui/appgui`. The older desktop core lacked both input reuse and deferred
stream output. Both build paths have now been rebuilt. The following full-app
runs compare the saved older desktop core with the accumulated optimizations;
they do not isolate the deferred-skinning follow-up alone.

These Release/x64 measurements use the same host, saved language 5, the exposed
normal app window and presents 3200–3900. Profiling is disabled, and no builds or
tests run during the measured combat window. FPS below is calculated from
acknowledged window presentations, including desktop rendering and backpressure.

| Four-fighter stage | Older desktop core | Rebuilt desktop core | 95th-percentile interval before → after |
| --- | ---: | ---: | ---: |
| Big Battlefield | 21.29 FPS | 29.28 FPS | 55.76 → 41.92 ms |
| Mario Circuit (indicative real-play check) | 11.81 FPS | 23.28 FPS | 97.47 → 55.55 ms |

The controlled Big Battlefield pair shows 37.5% higher presentation throughput.
The stage-selection captures at presents 2600, 2700 and 2800 match exactly, and
the gameplay captures at 3000, 3300, 3600 and 3900 differ only in the corrected
HUD name strips. All gameplay pixels above row 560 match exactly.

The Mario Circuit check reproduces the approximately 10–12 FPS slowdown in the
older desktop core. Stage, fighters and measurement window match, and the intro
matches outside the corrected HUD. Later AI poses differ, so this is an
indicative real-play comparison. The checked-in
`smash-4-fighters-mario-circuit.json` script selects this heavier stage with the
desktop probe. These results remain below sustained real-time gameplay and
do not establish a minimum FPS for other stages, effects or fighter combinations.

Logs, captures, presentation CSVs, executable hashes and
`desktop-comparison.json` are in `build/smash-desktop-check/`.
`matched-before` / `matched-after` hold the controlled Big Battlefield pair;
`app-before` / `mario-circuit-after` hold the Mario Circuit check.
`desktop-fight-comparison.png` shows both pairs. The initial
`app-before` / `app-after` pair selected different stages and is excluded as a
paired speedup measurement.

## Correctness checks

Tests cover all 256 R4_G4 values, linear and tiled textures, partial rows,
unchanged texture reuse and changed guest bytes. Stream-output tests verify
reused conversion against the reference shader and reject stale conversions
after source or fetch-layout changes. Attachment tests preserve unresolved older
color/depth versions while cycling pooled storage, including HDR and row padding.
Additional cases exercise ten changing poses with repeated draws, replacement
of a cached mesh by stream output within one frame, and snapshot hash changes
in every lane, partial tails and paired float sign bits.

The Vulkan-enabled core suite now passes all 1,076 tests; all six GUI test
targets pass. The seven focused snapshot, changing-mesh and attachment tests
also pass on Mesa lavapipe with Vulkan validation enabled and no validation
errors. The existing stream-output, packed-texture and attachment tests were
also validated on lavapipe earlier in this change.

Deferred stream-output tests also exercise 36 appended passes, output reused as
the next pass's attributes, and 24 backend jobs with changing palettes, reverse
resolution, slot reuse and unresolved work at teardown. The six focused tests
pass on lavapipe with Vulkan validation and deferred raster readback enabled;
five stream-output tests also pass with synchronous execution and single-command
submissions. The full build, formatting checks and clang-tidy checks pass.

An extra full run with `WEMU_NATIVE_DEFER_READBACK=1` forced globally passes
1,075/1,076 tests: the packed-texture test expects two live cached allocations
but observes one in that mode. It fails the same allocation-count assertion in
isolation and passes under default settings. The new stream-output tests pass
in both configurations; the failed assertion does not compare rendered pixels.

The geometry-conversion and attachment-pool changes alone produced identical
eight-fighter captures at presents 3000, 3300, 3600 and 3900. The R4_G4 correction
intentionally changes HUD pixels; gameplay captures must be assessed with that
correction in mind.
