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
