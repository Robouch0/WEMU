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

## Correctness checks

Tests cover all 256 R4_G4 values, linear and tiled textures, partial rows,
unchanged texture reuse and changed guest bytes. Stream-output tests verify
reused conversion against the reference shader and reject stale conversions
after source or fetch-layout changes. Attachment tests preserve unresolved older
color/depth versions while cycling pooled storage, including HDR and row padding.
Additional cases exercise ten changing poses with repeated draws, replacement
of a cached mesh by stream output within one frame, and snapshot hash changes
in every lane, partial tails and paired float sign bits.

The final Vulkan-enabled core suite passes all 1,075 tests; all six GUI test
targets pass. The seven focused snapshot, changing-mesh and attachment tests
also pass on Mesa lavapipe with Vulkan validation enabled and no validation
errors. The existing stream-output, packed-texture and attachment tests were
also validated on lavapipe earlier in this change.

The geometry-conversion and attachment-pool changes alone produced identical
eight-fighter captures at presents 3000, 3300, 3600 and 3900. The R4_G4 correction
intentionally changes HUD pixels; gameplay captures must be assessed with that
correction in mind.
