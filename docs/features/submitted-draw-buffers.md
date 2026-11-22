# Submitted vertex and index buffers

The asynchronous renderer previously captured vertex/index addresses but read
their bytes later, while the guest was already preparing its next frame. A
submitted draw could therefore consume a mixture of two frames' data. Uniform
block snapshots alone did not protect these inputs.

This produced intermittent missing projector beams in the first second of
Mario Kart Stadium's course intro. The guest clock was running near normal
speed. Disabling asynchronous replay kept the beams visible, but also removed
CPU/GPU overlap.

## Buffer versions

`Gx2BufferSnapshots` captures CPU-provided attribute and index ranges before
handing the frame to replay. Each frame owns shared immutable byte versions.
Subsequent guest writes create a new version and preserve the submitted one.
Unchanged buffers retain the same allocation, including static course geometry.

The cache uses existing memory write generations when available. Backends that
write without tracking fall back to comparing bytes. Cached versions have a
128 MiB persistent budget; frame owners keep any evicted version alive until
replay no longer needs it. Overlapping input ranges share a snapshot.

Bindings persist between frames and are saved/restored with GX2 context state.
Input fetches preserve raw byte order and the existing vector-fetch allowance
past a declared stride. Cached converted vertices validate against the submitted
bytes as well, rather than the CPU's following frame.

Stream-output ranges stay live because replay produces them. The preceding
replay job completes before the next frame's inputs are captured. Attribute
fetch and index decode use the versioned bytes; stream-output writes continue
to update guest storage. Snapshot hashes contribute to captured-frame identity.
Texture image lifetime still uses the existing texture/resident-target paths.

The behavior is global and enabled by default. `WEMU_FRAME_BUFFER_SNAPSHOTS=0`
disables it for diagnosis. The Qt GUI and standalone core use the same path.
`WEMU_ASYNC_REPLAY=0` remains a diagnostic comparison rather than the fix.

## Validation

In the same 60-frame projector sample:

| Replay mode | Missing/dim beam frames |
| --- | ---: |
| Previous asynchronous input handling | 26/60 |
| Synchronous diagnostic | 0/60 |
| Asynchronous replay with submitted buffer versions | 0/60 |

The criterion uses the blue-contrast measurement documented in
[the clock investigation](emulation-timing.md#mk8-menu-and-projector-follow-up).
This establishes a concrete rendering correction in the reproduced scene;
it does not establish complete rendering accuracy for every particle effect.

The initial candidate kept menu throughput near 59.4 presents/s. Its first-second
intro sample delivered 58.6 presents/s versus 57.9 in the earlier run. These are
single local x64 runs with normal throttling and an immediate desktop-protocol
sink; they exclude Qt upload/vsync overhead and do not establish a throughput gain.

The Vulkan-enabled 1,046-test suite passes. Regressions cover immutable submitted
bytes, unchanged-version reuse, untracked stores, inherited/context-restored
bindings, stream-output exclusions and frame hashes. The stream-output replay
test overwrites a CPU vertex after capture and verifies that replay consumes the
submitted vertex while writing the correct output buffers.

Local captures are under `build/timing-review/mk8-intro-frozen-buffers`. The
prototype's explicit `WEMU_FRAME_BUFFER_SNAPSHOTS=1` is equivalent to the enabled
default. The corrected GUI core is built through the `appgui` target.

The final combined build repeats the same beam check with zero dropouts in 60
frames (`build/timing-review/beam-final-verification.json`). It also completes a
3,300-present Smash gameplay smoke and a 600-present Pikmin intro smoke. The
Pikmin sample runs at 29.86 presents/s and 0.995× guest/host clock speed with
interval 2. These smoke runs establish the exercised paths, rather than full
compatibility coverage. [Texture-layer reuse](texture-layer-reuse.md) documents
the subsequent Balloon Battle FPS improvement.
