# Native stream-output execution

Eligible vertex stream-output passes now run as Vulkan compute work. This removes
per-vertex shader interpretation from the replay thread and vertex workers. The
compute path uses the shared Latte arithmetic lowering and bounded uniform-buffer
windows; it does not identify titles, shaders by hash, or guest addresses.

The supported replay subset is non-indexed, single-instance point passes with at
least 64 vertices and at most 16 fetched attributes. Shader texture reads, execution
mask changes, branches, relative/burst stream exports, and unsupported arithmetic
retain reference execution. Uniform blocks must fit the existing native constant
budget. Devices without a suitable compute queue or buffer limits also fall back.

Direct STREAM0–3 writes support component masks and offsets within a vertex's
stride. An output snapshot starts with the previous bytes, so masked components
and padding survive. Attributes and constants are copied into reusable mapped
buffers, the kernel dispatch completes, and complete output snapshots are returned.
Replay validates all outputs before committing guest memory, advancing stream
offsets, and making the writes visible to later uniform-block bindings.

The backend retains mapped buffers and compute pipelines across passes. Compute
submissions preserve queue order with earlier graphics work and synchronize before
the CPU consumes their output. This is a synchronous implementation; it does not
keep stream-output buffers resident for subsequent GPU vertex fetches.

`WEMU_NATIVE_STREAMOUT=0` selects CPU reference execution for comparisons. Native
execution is enabled by default when a compatible raster backend is present.
`WEMU_STREAMOUT_VERIFY_EVERY=N` re-executes every Nth native pass on the reference
interpreter and compares the complete output byte-for-byte before committing.
It also checks which streams advance their offsets. Mismatch aborts replay with a diagnostic. Verification includes masked components,
padding and integer payloads, rather than accepting floating-point tolerances.

`WEMU_FRAME_PROFILE=1` adds a `native` field to `[STREAMOUT_PROFILE]` records.
Their wall time includes GPU synchronization or CPU worker completion. It is
already part of replay/frame wall time and must not be added to those totals.

Tests cover changed palettes, multiple output buffers, nonzero first vertices,
wrapped system indices, raw integer and exceptional float bits, partial workgroups,
masked writes and padding, repeated buffer resizing, malformed inputs, context
offset restoration and overflow rejection. Run them with Vulkan enabled:

```bash
WEMU_TEST_VULKAN=1 build/core/tests/wemu_tests \
  --gtest_filter='LatteStreamOutTest.*:Gx2CaptureTest.StreamOutWritesMaskedComponentsWithoutRasterizingAndRestoresOffsets'
```

For gameplay comparisons, use identical input scripts, capture intervals and
profiling settings. Measure after entering gameplay, with shader caches warm.
The session probe measures core frame delivery and excludes Qt uploads/display
synchronization; menu/boot averages do not establish fight performance.

## Combat probe measurement

A Release build on an i7-14650HX and RTX 4070 Laptop GPU was measured using the
x64 CPU backend and the checked-in `scratchpad/scenarios/smash-combat.json` input
sequence. It enters a 1v1 and performs movement, stick attacks and A-button
attacks. Measurements cover delivered presents 3000–4200, after the match starts.
Both paths used identical frame pacing, profiling and capture settings.

The repeated, warm-cache comparison measured:

| Metric | CPU stream output | Vulkan stream output |
| --- | ---: | ---: |
| Delivered presents/second | 37.52 | 51.71 |
| Mean frame interval | 26.65 ms | 19.34 ms |
| 95th percentile frame interval | 32.65 ms | 24.41 ms |
| 99th percentile frame interval | 36.12 ms | 29.98 ms |
| Stream-output wall time/frame | 9.59 ms | 3.66 ms |

This is a 37.8% increase in core delivery rate in that scene. All 18,142
stream-output passes in the measured window ran natively, and captures at
presents 3000, 3300, 3600, 3900 and 4200 matched the CPU path pixel-for-pixel.
A separate 4200-present run verified all 22,733 native passes, including boot and
menus, byte-for-byte against the reference interpreter with no mismatch. The
four focused stream-output tests passed on NVIDIA and Mesa lavapipe with Vulkan
validation enabled; the complete 1032-test core suite also passed with Vulkan.
An earlier paired run measured 32.54 versus 49.62 presents/second; the repeated
comparison reduces the influence of first-use compilation. These figures do not
establish 60 FPS, desktop display FPS, or performance with other stages, fighters,
player counts or hardware.

For a local dump, reproduce with the same command twice, adding
`--env WEMU_NATIVE_STREAMOUT=0` for the CPU comparison:

```bash
python3 scratchpad/session_probe.py /path/to/code/cross_f.rpx \
  --core build/core/wemu --cpu x64 --output /tmp/wemu-fight-native \
  --frames 4200 --seconds 240 --measure-from 3000 --capture-every 300 --profile \
  --input-script scratchpad/scenarios/smash-combat.json
```
