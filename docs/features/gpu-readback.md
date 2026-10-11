# GPU readback and software fallback

Native Vulkan rasterization can keep rendered images on the GPU until the CPU
needs their pixels. Deferred readback tickets own each image version and
materialize a complete CPU snapshot when required by presentation, sampling or
software rendering.

Software fallback now checks the existing triangle-area and scissor bounds
before preparing CPU pixel inputs. Degenerate triangles and triangles outside
the drawable bounds leave pending images on the GPU. Vertex execution and the
draw's existing coverage rules are retained.

For ordinary 2D inputs, a covered software draw resolves its destination and
bound textures instead of every pending render target. This preparation runs
on the replay thread before software workers start. Feedback snapshots preserve
the pre-draw image, and a later native draw can still consume an unrelated GPU
image directly. Texture arrays retain conservative resolution of all pending
color images because their slices are selected dynamically.

Native/reference verification explicitly prepares the pre-draw CPU snapshot
before comparing the complete target, including pixels that clipped geometry
leaves untouched. This diagnostic readback is separate from normal rendering.

This is a game-independent change to synchronization and data transfer. Shader
translation, emulated clocks, movie delivery and frame pacing are unchanged.
The desktop launch path already enables deferred/lazy readback and resident
textures/targets. It also forces full frame replay: the older command-stream
hash omits live texture and vertex bytes, so it cannot safely establish that
the rendered image is unchanged. GPU resource caches still reuse verified data.
No new build dependency or user setting is required.

The GX2 capture regression matrix covers degenerate and clipped fallback draws,
software writes to an unrelated target, native continuation, feedback snapshots,
and final presentation. Enable `WEMU_NATIVE_DEFER_READBACK=1`,
`WEMU_NATIVE_RESIDENT_TEXTURES=1` and `WEMU_NATIVE_RESIDENT_TARGETS=1` when running
the deferred-readback regression cases directly.

## Direct resident color sampling

Compatible RGBA8 and RGBA32 float render targets can be sampled through cached
Vulkan image views, without copying their pixels into an intermediate sampling
image. Component mappings remain part of each view. Vertex and fragment stages
share this path; duplicate bindings of the same image share one layout
transition. Source images remain alive until the submission completes.

Target/depth input copies occur before sampling transitions. Each source returns
to its canonical transfer-source layout after the draw, preserving later GPU
consumers and deferred CPU readbacks. A draw sampling its own output keeps the
snapshot-copy path. Stale or foreign images still resolve their saved CPU
version. Depth retains the existing D32-to-R32 transfer path.

CPU uploads and copied textures remain available when direct sampling cannot be
used, including when a reused bundle changes from GPU inputs to CPU snapshots.
`WEMU_NATIVE_DIRECT_TEXTURES=0` selects the copy path for controlled comparisons.
Native timing logs distinguish actual resident texture copies from direct
bindings and the corresponding avoided-copy bytes. Presentation still reads
pixels back to the CPU and uploads them to Qt; cross-process GPU sharing is
outside this change.

## Complete target overwrites

The shader translator marks unconditional color exports only when execution
masks, jumps and depth exports cannot suppress a color write. The backend combines
that proof with exact full-target scissor/geometry checks: two triangles must
cover a rectangle's four corners and share its diagonal, with unit reciprocal W.
Blending, partial channel masks, depth testing and GPU vertex stages exclude this
path. Bounding boxes alone do not establish coverage.

For those draws, Vulkan uses a discard load operation and skips uploading or
copying the previous active target pixels. Padding and older readback versions
remain preserved. Other draws retain their existing load/transfer behavior.
`WEMU_NATIVE_TARGET_OVERWRITE=0` disables this optimization independently for
comparisons. Timing counters record target uploads, target copies and proven
full-overwrite draws.

## Preserve live GPU versions when reusing resources

With lazy readback enabled, a bundle whose color or depth ticket still needs a
CPU transfer stays intact. The cache selects another free bundle with the same
pipeline and texture shape. It searches existing slots, including across holes
left by eviction, before allocating another bundle. Discarded or materialized
tickets allow normal reuse.

This keeps offscreen surfaces available to later GPU consumers instead of
forcing them through GPU readback and CPU reupload merely to recycle a bundle.
It can trade additional cached GPU storage and image copies for fewer CPU/GPU
round trips. The existing 64-entry/768 MiB estimated cache limits remain;
eviction materializes live tickets before releasing their resources. Older
color/depth versions, row padding, software fallback and foreign-context inputs
retain their snapshot behavior. `WEMU_NATIVE_PRESERVE_IMAGES=0` disables this
selection policy for controlled comparisons.

## Reduce scanout and desktop frame copies

CPU scanout clears only pixels outside the rectangle its next copy overwrites.
Smaller sources clear their uncovered margins each time, and invalid sources
still produce a complete clear frame. RGBA8 and float-to-RGBA8 scanout retain
their existing pixel values and dimensions.

The isolated core writes frame metadata and its pixel span separately to the
local socket, avoiding full-frame body and packet temporaries. The receiver can
transfer a complete packet buffer into the decoder; the Qt image then keeps
immutable packet storage alive through a cleanup callback, including references
held by the render thread. Writable QImage copies detach normally. Protocol
version, bounds checks, ordered acknowledgements and the two-frame queue remain
unchanged. GPU readback, socket transport and Qt texture upload still occur;
this does not add cross-process GPU texture sharing.

## Batched submissions and lazy readback copies

`WEMU_NATIVE_BATCH_SUBMITS=1` lets draw commands share a pooled completion fence
in batches of up to four command buffers. Immediate submission remains the default
because the measured Pikmin movie path did not gain FPS from delaying
submissions. The small bound lets the GPU begin work while guest execution continues;
a CPU dependency, resource reuse or cache drain flushes the current batch.
Each resource keeps its completion token and sampled-image owners until work
completes. Completed tokens are reused only after their last resource releases
them. Live color/depth tickets retain their existing per-version ownership.

Lazy color and depth copies use a separate command pool. They can be recorded
while their producer is queued or executing, then submitted on the same queue.
The copy's fence completes the producer and copy with one host wait. Draw command
pools and descriptor/staging data remain protected until completion; copy pools
are reset only after the previous synchronous copy finishes.

`WEMU_NATIVE_BATCH_SUBMITS=0` submits each command immediately for comparison;
it retains the independent readback pool. Timing counters expose actual queue
submissions, submitted command buffers and host fence waits. GPU execution and
presentation still use the existing emulated ordering and CPU IPC path.

## Host session teardown

The GX2 host replayer is released through session-scope cleanup before renderer
and guest memory destruction. Workers, cached images and deferred ticket owners
are dropped while Vulkan and driver thread state are still available. The same
cleanup applies on normal Stop and exception exits; creating another host
replayer initializes its backend independently. Destructors drain pending work
without propagating submission exceptions.
