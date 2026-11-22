# Reuse of rendered texture-array layers

An isolated Mario Kart 8 Balloon Battle profile showed a large cost in the open
Moo Moo Meadows view: roughly 850 native draws per frame, with most time on the
host CPU rather than waiting for GPU completion. The same run became much faster
after the camera faced a nearby fence. This does not isolate the cost of balloons
from the rest of the course.

## Cause and correction

Arrays can contain both GPU-rendered layers and CPU-backed layers, including
cleared depth layers, software-rendered colour layers and decoded guest images.
Previously, repeated bindings could copy CPU layers into new readbacks and
reassemble/upload the same array for every draw. GPU-only arrays already reused
their assembled texture, but the mixed case could not establish stable identity.

The replayer now retains immutable CPU layer versions. Depth writes invalidate
their snapshot; colour writes use existing surface write counters; decoded guest
layers retain the decoded object's version. CPU colour/guest layer versions have
a 128 MiB cache budget. Readers hold their own version while later writes create
another one.

The Vulkan backend keys assembled arrays by GPU image/version pairs and CPU
readback identities. Weak source owners prevent a recycled address from matching
an expired version. An unchanged mixed array is assembled once and reused;
changing either kind of layer rebuilds it. Existing attachment/sampler state and
draw submission order stay intact. Native depth/stencil images also reuse their
existing row-padding snapshot when the actual storage format matches.

This is shared renderer behavior, with no game identification or title patches.

## Measured gameplay

Local Release/x64 runs on an Intel Core i7-14650HX and NVIDIA RTX 4070 Laptop GPU,
with normal presentation pacing and identical scripted Battle inputs. The
baseline already includes the [submitted-buffer correction](submitted-draw-buffers.md),
so this comparison isolates layer reuse. The input route selects single-player
Battle, default character/kart/options and Moo Moo Meadows, then holds accelerate.
The later window faces a fence; it is not a complete survey of battle situations.

The final comparison disabled all profiling in both runs. No builds, tests or
other emulator probes ran during either measurement.
The immediate desktop-protocol sink excludes Qt texture upload and desktop vsync.

| Guest presents | View | Before FPS | With layer reuse FPS |
| --- | --- | ---: | ---: |
| 4000–4200 | Open course | 11.74 | 46.87 |
| 4200–4500 | Moving toward fence | 31.86 | 51.24 |
| 4500–4800 | Fence view | 54.86 | 55.66 |
| 4800–5100 | Fence view | 53.38 | 54.63 |
| 4000–5100 | Full measured window | 29.30 | 52.37 |

Across the full window, 95th-percentile frame interval fell from 88.84 to 23.08 ms
and 99th percentile from 92.27 to 28.00 ms.

A separate matched pass enabled frame and JIT profiling in both versions to
identify CPU work; its absolute FPS differs from the uninstrumented runs.
Mean replay-thread wall time fell
from 36.97 to 16.72 ms. In the slow open-course window specifically, replay fell
from 97.05 to 19.50 ms and native backend CPU time from 61.03 to 8.04 ms.
These exclusive CPU stage values must not be added to the emulation thread's
overlapped wait measurements.

All 43 corresponding guest-clock samples in the instrumented pair match exactly.
Captures in both comparisons at
presents 3900, 4200, 4500, 4800 and 5100 are pixel-for-pixel identical (zero
different pixels), including the balloons and course lighting.

Measurements and comparisons are in the local ignored `build/timing-review/`
directory: `battle-final-comparison.json`, `battle-profile-comparison.json`, the
`mk8-battle-perf-before-profile` and `mk8-battle-perf-cached-layers-profile` logs,
and final `mk8-battle-final-before` / `mk8-battle-final-after` data, plus
`balloon-battle-frame-times.png`. The input script is `mk8-battle-input.json`.

## Validation and profiling

The Vulkan-enabled 1,046-test suite passes. The mixed-array regression samples
both layers repeatedly, verifies one assembly and three cache hits, then changes
the CPU layer and verifies the new colour and a second assembly. Existing tests
cover GPU layer replacement, mip levels, HDR arrays, depth/stencil and row pitch.
The two array-version tests also pass with Vulkan validation enabled and no
validation errors.

`WEMU_NATIVE_DIRECT_PROFILE=1` adds opt-in CPU diagnostics for draws taking at
least 100 microseconds. `[NATIVE_BACKEND_PROFILE]` separates module lookup,
context setup, input preparation and direct rendering. `[NATIVE_DIRECT_PROFILE]`
separates segment opening, uploads, textures, attachments, pipeline/descriptors,
commands and readback-ticket preparation. These are host CPU durations, not GPU
timestamp measurements. They complement the existing frame/replay/draw profiles.
