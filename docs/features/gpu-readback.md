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

Each matching draw shape retains up to eight live versions before recycling its
oldest eligible bundle. Recycling materializes outstanding color/depth tickets
before any overwrite, preserving older pixels and row padding. Current target
and depth inputs require separate destination images; sampled aliases retain
their existing copy-before-write path. The limit is soft when every matching
bundle supplies an attachment input. Discarded or already materialized outputs
remain immediately reusable.

This bounds repeated allocation during passes that produce many small images,
while retaining a short chain of GPU-resident versions for later consumers.
The global cache remains bounded to 384 entries and an estimated 2560 MiB;
eviction materializes live tickets before releasing their resources.
`WEMU_NATIVE_LIVE_VERSIONS` changes the per-shape limit for diagnostics.
`WEMU_NATIVE_PRESERVE_IMAGES=0` disables live-version preservation for controlled
comparisons. Software fallback and foreign-context inputs keep their saved
snapshot behavior.

## Share immutable texture uploads across draws

Decoded guest textures and float mip/array snapshots carry an immutable owner
identity into the raster backend. Draws using the same snapshot share its Vulkan
sampling image, image view and staging allocation across pipelines and vertex
capacity variants. Different sampler states still use their own sampler objects.
Cache keys include the byte region, row pitch, dimensions, numeric format,
component mapping, image kind and complete mip layout. Changed source bytes or
rendered layers produce a new owner and upload; mutable callback inputs retain
the existing per-bundle upload path.

Shared immutable sampling images prefer device-local memory. Their uploads keep
host-visible staging buffers, and CPU consumers retain the source snapshot.
The allocator falls back to another compatible heap when the preferred memory
type is unavailable. Existing render-target placement follows its prior policy.

The cache retains at most 2048 entries and an estimated 1.5 GiB of image/staging
storage: a race frame in MK8 samples hundreds of distinct textures, and a 256-entry
bound re-uploaded most of them every frame. In-flight bundles own their sampled images independently of cache
eviction. Ordered submissions make an upload visible before subsequent draws;
duplicate bindings in its producing draw share the same recorded upload.
Deferred frame tickets retain their device until their final release. Shared
image/staging storage is counted once for its entire lifetime, including after
cache eviction while draws still retain it. The resource budget combines those
live bytes with private bundle storage and reserves a worst-case upload for the
incoming snapshots on both new allocations and bundle refreshes. This avoids
charging every shared texture again for every draw bundle.

Bounded weak lookup registries let CPU decoding and GPU upload caches recover
snapshots/images still owned by another live draw after strong-cache eviction.
These registries retain metadata without retaining pixels or GPU allocations.
This prevents alternating large textures from acquiring a new decode/upload
identity despite an existing live version. Signed decoded snapshots distinguish
view component mappings and source allocation sizes.

`shared_texture_uploads`, `shared_texture_hits` and `shared_texture_bytes` in
native timing logs expose upload/reuse counts and live storage. Regressions cover
duplicate bindings, sharing across
pipeline states, changed HDR layers and BC source bytes, cache eviction, older
frame pixels, weak recovery after CPU/GPU eviction, and device lifetime after
backend destruction.

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

Draw commands share a pooled completion fence in batches of up to eight command
buffers (`WEMU_NATIVE_BATCH_SUBMITS=N` sets the bound). In MK8's main menu, about
200 draws per frame, this cut per-submission driver work from 13.4 to 11.8 ms of
replay-thread time; larger bounds delayed the GPU enough to add host fence waits.
The Pikmin movie path did not gain FPS from delaying submissions. The small bound
lets the GPU begin work while replay continues; a CPU dependency, resource reuse or
cache drain flushes the current batch.
Each resource keeps its completion token and sampled-image owners until work
completes. Completed tokens are reused only after their last resource releases
them. Live color/depth tickets retain their existing per-version ownership.

Lazy color and depth copies use a separate command pool. They can be recorded
while their producer is queued or executing, then submitted on the same queue.
The copy's fence completes the producer and copy with one host wait. Draw command
pools and descriptor/staging data remain protected until completion; copy pools
are reset only after the previous synchronous copy finishes.

`WEMU_NATIVE_BATCH_SUBMITS=0` (or `1`) submits each command immediately for comparison;
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

## Dynamic viewport and scissor

Vulkan viewport and scissor values are now recorded per draw using core dynamic
state. Pipeline identity retains shaders, descriptors, blend/depth state, target
format and topology, but no longer includes target dimensions, scissor rectangles
or attachment load operations. Resource identity still includes dimensions and
load policy so backing images, framebuffers and uploads stay correct. Compatible
pipelines are shared across these variants; clipping and output pixels stay the
same. Pipelines are created through one context-level cache shared by both draw
paths and stay alive with the device, so no pipeline-limit drain is needed.

## Direct draws

Deferred draws with lazy readback (the GUI configuration) are recorded by a direct
renderer instead of a cached bundle per draw shape. A bundle owns target-sized
color/depth images and buffers (about 22 MiB for a 1280x720 HDR target with
depth), so MK8's race, with 2000-4500 draws per frame, overflowed the bundle
budget after about 120 entries; half of its draws built a new bundle (~20 ms each)
and frames took about 16 s. With direct draws they take about 0.5 s of native
work (RTX 3060 Laptop GPU).

Images the direct renderer creates are rendered in place: the replayer replaces
the previous version's ticket with the draw's result, as it does for adopted
bundle targets. Resident images from bundles, and targets kept for verification,
are first copied into a new direct image, which then becomes the surface's
version; a bundle that later adopts a direct image takes ownership of it. Depth
is handled the same way. Fixed-point (16/24-bit) depth writes store their
quantized value from the fragment shader, with the bundle quantization pass's
exact rounding, instead of three full-surface copies per draw; the depth test
then compares the quantized incoming value, as fixed-point hardware does, while
the reference compares the unquantized one. MK8's race writes a 1024x1024
16-bit shadow map about 500 times per frame. A read-only draw that had to upload
or copy its depth returns the new image as well, so later draws find the surface
resident instead of uploading 3.7 MB again.

Draws are recorded into segment command buffers of 48 draws
(`WEMU_NATIVE_SEGMENT_DRAWS=N`). Constants, vertices, indices, CPU target/depth
snapshots and private texels are suballocated from the segment's persistently
mapped upload buffer, and descriptor sets from its pools; both are reset when the
segment's fence completes. Immutable snapshots use the shared texture cache;
resident images are sampled directly, or through a scratch copy when the draw
also renders into them or when depth is sampled as R32. A segment is submitted
before any bundle work and before readbacks, so the queue order matches replay
order. `WEMU_NATIVE_DIRECT_DRAWS=0` restores the bundle path; GPU timing queries
also use it.

Tests verify different target sizes and padded/clipped output, repeated scissor
changes with a single pipeline, live older snapshots and depth comparisons under
synchronization validation. `pipelineCreations()` records graphics pipeline
creation for diagnostics. This follows the Vulkan rules for
[dynamic state](https://docs.vulkan.org/spec/latest/chapters/pipelines.html#pipelines-dynamic-state)
and [render-pass compatibility](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-compatibility).
