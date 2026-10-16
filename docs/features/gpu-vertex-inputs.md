# Vertex inputs and shader resource queries

GX2 vertex shader declarations can map 32 semantic IDs to R1 through R32.
Replay now retains the complete mapping, decodes the corresponding fetch inputs,
and passes the declared input count to the reference shader interpreter. Context
capture and reload preserve both the mapping and the count. This supports wider
declarations used for model positions, normals, weights and bone indices.

The reference path retains all 32 PARAM exports and pixel input registers,
including semantic mappings across all eight packed output-ID words. Context
capture and reload preserve the pixel count and both semantic tables. Software
interpolation covers the actual mapped parameter range.

The Vulkan payload transports 16 attributes and 16 interpolated pixel inputs.
Translated vertex shaders can write all 32 internal PARAM slots and map the
needed exports to those pixel inputs. Wider declarations use reference execution.
Native triangle vertex execution remains opt-in for ordinary shaders. Eligible
point, vertex-texture and indexed uniform-buffer shaders can execute natively
by default. Devices must
support the required attribute, varying and push-constant limits. These changes
do not establish complete model rendering: other shader instructions, integer render targets, depth consistency and render
states still need additional implementation. See [cube maps and mip levels](cube-mip-sampling.md)
for the supported texture and independent-attachment paths.

Vertex uniform-buffer fetches now support direct `VTX_FETCH` in TEX, VTX and
VTX_TC clauses: resource IDs 128–143 select GX2 uniform banks, a raw integer GPR
component selects a float4 row, and an aligned byte offset can select a later row.
Masked destination channels and selector reordering are preserved. Relative
operands, format overrides and unsupported fetch sizes retain fallback. The native
path reserves the remaining slots in its existing 512-vec4 constant block for
these reads and checks that each complete bound block fits before dispatch.
Larger blocks use reference execution; palettes are never silently truncated.
Readonly palette fetches are also eligible for the per-draw vertex result cache,
whose lifetime does not cross uniform updates.

`MULLO_INT` and `MULLO_UINT` compute the low 32 bits with wrapping arithmetic.
The shared decoder places both in the scalar T slot even when their destination
channel has a free vector slot. This distinction matters when the following ALU
group reads PS to calculate a bone-matrix row. Tests exercise that dependency in
reference and native vertex execution, including all three supported clause kinds,
changed palette contents, masked writes and unsupported fetch flags.

GX2 polygon-control setters and register init/get/set APIs now preserve front/back
culling and winding, including PPC stack arguments. Replay retains that state
through context save/restore. Software triangle coverage and Vulkan pipelines use
the same winding convention; both-face culling preserves the previous target even
for a nominal fullscreen overwrite. Fully culled triangle lists and quads also
bypass vertex execution and attachment preparation; point draws still execute. Rasterizer clip-control APIs retain their
flags, and a disabled rasterizer suppresses draws. Triangle polygon offset now applies guest front/back slope, constant and clamp
values in both execution paths. Different visible front/back offsets retain
reference rendering. Polygon fill modes, point/line offset and Z-clip/half-Z
behavior still need implementation. See [depth-only shadows](depth-only-shadows.md).

When existing vertex summary and framebuffer tracing are enabled together,
`vs_inputs_drawNNN.txt` records vertex-zero attributes, uniform registers and all
uniform blocks, along with the first submitted index. These diagnostic snapshots
allow offline comparisons of depth and color shaders using identical guest data.

The shared shader decoder names `SQRT_IEEE` (opcode `0x6A`). Reference execution
uses the floating-point square root, and Vulkan lowering emits the same operation
with explicit exceptional-value handling. Negative values produce NaN; signed
zero, NaN and positive infinity retain their classifications. Tests compare
uniform operands with the reference to avoid interpolation of exceptional values.

`CUBE` reduction accepts a validated four-slot layout with operand pairs
`(z,y), (z,x), (x,z), (y,z)`. Axes can reside in different registers, PV or PS.
Both execution paths produce T, S, twice the signed major axis and face ID,
including Z-over-Y-over-X tie precedence, predication, output scaling, independent
GPR write masks and a fifth scalar operation. Unverified layouts retain fallback.
This generates coordinates consumed by the supported cube sampling path described
in [cube maps and mip levels](cube-mip-sampling.md).

The pixel, vertex and geometry shader GPR/stack query APIs now read the
`SQ_PGM_RESOURCES` word at the beginning of their GX2 headers. GPR count occupies
bits 0–7 and stack entries bits 8–15. These queries previously returned zero.

Draw-level vertex diagnostics inspect the first submitted index, including draws
whose indices do not start at zero.

Completed HLE display lists remain available when additional buffers are
recorded. They are guest GPU state and cannot be discarded as a cache batch:
clearing older lists silently drops their shader and register bindings. Recording
again at the same buffer address replaces that list; a session reset clears the
registry. Calling a list copies its commands into the current submission or open
recording once, without an extra temporary copy of every payload.
Completed recordings hold immutable shared command vectors. Whole-list copies
through HLE `memcpy`, `memmove` or `OSBlockMove` retain that snapshot at the new
address; recording into the original buffer does not alter an existing copy.
Partial copies do not create executable recordings. HLE copy/set writes starting
at a known destination invalidate its previous recording, and host-session cleanup
releases aliases. Direct guest stores and arbitrary embedded ranges still need
full command-buffer serialization or additional copy tracking.

`GX2GetCurrentDisplayList` reports the active recording buffer and its supplied
capacity, independently of the used-byte count returned by `GX2EndDisplayList`.
An inactive query returns false and preserves output pointers. Optional bounded
`WEMU_GX2_DL_MISS_TRACE=<first swap>` diagnostics report unknown list addresses,
sizes, callers and their first 16 words. Raw/copied GPU lists without an HLE
recording are not yet decoded by this registry.

GX2 format `0x816` is an unsigned packed R11G11B10 HDR color surface. The CPU
reference now decodes its five-bit exponent and six/six/five-bit fractions,
rounds writes to nearest even, preserves RGB channel masks, and supplies implicit
alpha 1. Clear, float blending, filtered texture sampling and scanout use the
typed values; brightness above 1 survives until final display conversion.

Vulkan uses `VK_FORMAT_B10G11R11_UFLOAT_PACK32` for these attachments and sampled
images. Completed render images can remain resident for subsequent draws and
texture reads. CPU snapshots and images from another backend retain their packed
format and row pitch on upload. Unsupported host attachment/blend/sample features
fall back to reference rendering. Tests cover quantization, blending, masks,
padding, retained image versions, component selectors and upload fallback.

The software rasterizer rejects covered fragments against stored depth before
interpolating shader parameters or sampling textures when successful shader
analysis proves there is no depth export. It does not write depth early: discard
and conditional color behavior still run for surviving fragments. Depth-export
shaders and shaders outside the analyzed subset retain the late comparison.
Point and triangle regressions verify occlusion and a shader depth export that
passes even though its geometric depth would fail. The replay diagnostic counter
`earlyDepthRejectedFragments()` reports avoided fragment executions.
For constant-depth triangles, a lazily computed stored-depth range can reject
the entire primitive before walking pixels. Bounds exclude row padding, expand
after each depth write, and remain conservative when extrema are overwritten.
NaNs disable range rejection. Varying-depth geometry retains per-pixel testing.
`earlyDepthRejectedTriangles()` counts these rejected primitives; a NOTEQUAL
regression checks that successive overlapping triangles observe expanded bounds.

When all color channels for a draw are disabled and successful shader analysis
proves an unconditional color-only export without discard or depth output, replay
omits the unused fragment calculations. CPU rendering retains coverage and depth
updates; Vulkan uses a constant-output shader with color writes disabled, avoiding
unnecessary texture uploads and shader execution. Conditional exports and depth
exports retain their original shader behavior. This also allows otherwise
unsupported texture resources to remain unused during a proven depth-only pass.
The cached effect proof removes known read-only texture clauses from an analysis
copy of the program; that copy is never executed. The remaining ALU, control flow
and exports still pass ordinary validation. Balanced PUSH/POP regions can restore
all lanes before a terminal color export when no jumps or root-mask writes occur.
Unknown texture/memory operations, kill instructions and depth exports prevent
this proof.

The reference pixel path also preflights an unsupported texture opcode that is
unconditionally reached before any execute-mask change or branch. Every reference
invocation would return no shader result, so replay can immediately use its same
fallback without repeating partial ALU execution for every pixel. Branch-dependent
failures retain ordinary execution. This does not implement the missing opcode.

Replay retains all eight color-buffer bindings across context switches. Direct
export metadata includes burst destinations even when the program needs reference
execution. If only slot zero is writable, its surface, blend state and channel
mask apply regardless of which buffer was bound most recently. The register-based
color control and channel mask APIs capture the same state as their argument-based
counterparts, including values recorded in display lists.

Unconditional direct exports to disabled additional targets can be omitted during
Vulkan translation. This specialization has a separate shader-cache key and a
submission-time check that only target zero is enabled. Independent writable color targets can use separate passes with a selected guest
export mapped to native location zero. Shared depth/stencil, aliased attachments,
texture feedback and multiwrite replication retain fallback. Context switches
preserve multiwrite state. This is not complete simultaneous MRT support.
A regression binds a smaller, disabled slot one last, renders a burst export
through the high semantic mapping to slot zero, and checks that slot one remains
intact with both color-state APIs and context reloads.

Color and depth buffer commands also snapshot their view mip, first slice and
slice count at offsets `0x74`, `0x78` and `0x7C`. Paired clears retain both complete
descriptors. A supported one-slice array view becomes a canonical 2D attachment
with the selected slice's address, size and tile swizzle. A single cube face uses
the same attachment normalization. Supported mip views use the shared surface
layout calculator before selecting their slice.
Rendered depth arrays
sample their canonical per-layer values on the reference path; native array
texture upload remains conservative. Unsupported multislice attachment views retain fallback behavior.

Depth-only reference coverage uses the depth attachment's extent when a disabled
or unexported color binding has a different size. It forms no out-of-range color
pointers. Native depth-only passes now omit the color attachment and use the
depth extent independently, preserving the unused color version without transfers. Tests reuse descriptors before replay,
render outside a smaller color attachment, and verify that the other depth layer
retains its value with both reference and Vulkan-backed replay.

Reference geometry preparation keeps a bounded, per-draw post-transform cache
for indexed shaders that do not read textures. Repeated indices reuse the full
position and parameter exports; a new draw always starts empty so changed uniforms,
attributes and context bindings are observed. Texture-reading vertex programs
retain ordinary execution because a software primitive can modify a sampled alias.
Execution/hit counters and a diagnostic cache switch support equivalence checks.

Triangles with real reference vertex positions are clipped against the homogeneous
X/Y planes before perspective division, including the eye-plane crossing case.
New vertices interpolate parameter exports in clip space; viewport depth and
reciprocal W are recomputed afterwards. Wholly invisible primitives never reach
rasterization. This prevents behind-camera geometry from wrapping across the
screen and lets supported clipped draws reach Vulkan with positive W. Native
vertex fallback uses the same clipping. This is X/Y clipping, not a complete
implementation of GX2 depth-clip or guard-band controls;
existing depth-clamp and legacy position-fallback behavior remains in place.

Readonly texture instructions do not prevent early depth rejection when the
separate shader-effect analysis proves an unconditional color-only result. Hidden
fragments can be rejected before even an unsupported material shader's reference
fallback, while depth-exporting or coverage-changing programs retain late tests.
No texture opcode is implemented by this optimization.

Primary references:

- [WUT GX2 shader declarations](https://github.com/devkitPro/wut/blob/master/include/gx2/shaders.h)
- [Decaf shader resource queries](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/gx2/gx2_shaders.cpp)
- [Decaf SQ register fields](https://github.com/decaf-emu/decaf-emu/blob/master/src/libgpu/latte/latte_registers_sq.h)
- [Cemu Latte ALU lowering](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompilerEmitGLSL.cpp)
- [Vulkan packed unsigned float formats](https://docs.vulkan.org/spec/latest/chapters/formats.html)
- [WUT surface and buffer view layouts](https://github.com/devkitPro/wut/blob/master/include/gx2/surface.h)
- [Decaf GX2 register APIs](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/gx2/gx2_registers.cpp)
- [Decaf color register fields](https://github.com/decaf-emu/decaf-emu/blob/master/src/libgpu/latte/latte_registers_cb.h)

The CPU fallback prepares an immutable decoded vertex program once per draw and
executes it with fresh attributes, uniforms and sampler callbacks for each
vertex. The bytecode-taking interpreter API remains available as the reference
entry point. Native replay reserves geometry staging vectors from the submitted
primitive count; clipped triangles can grow beyond that initial reservation.
These changes avoid per-vertex program-cache lookups and repeated growth/copying
of temporary vertex and triangle arrays without reusing runtime shader results
across draws.
