# Points, float targets and vertex textures

GX2 mode 1 now assembles a point list instead of triangles. The reference and
native Vulkan paths render unit-size points with scissor, channel masks,
ordered blending and depth testing. Indexed points share the existing index
endian and base-vertex handling. Triangle and quad assembly remain available.
Programmable point-size/point-limit state is still unimplemented.

GX2 format `0x823` has genuine RGBA32 float storage: 16 bytes per pixel, with
negative values and values above one retained through clear, shading, blending
and texture sampling. It is not an R32 target. Float blending supports add,
subtract, reverse subtract, minimum and maximum. Presentation converts floats
to RGBA8; intermediate targets retain their float precision. The Vulkan image
format, transfers, preserved row padding, pipeline cache keys and versioned
readback tickets all carry the target format.

Device-only transfer buffers prefer local GPU memory. On a discrete GPU, images
also prefer local memory: the first compatible type is system memory there, so
rendering and sampling crossed PCIe (MK8's character select spent ~75 ms per
frame waiting on fences on an RTX 3060 Laptop GPU, ~1 ms with local images).
Integrated GPUs retain the compatible-memory selection, where forcing local
images improved GPU raster time but regressed total frame delivery.
`WEMU_NATIVE_DEVICE_LOCAL_IMAGES=0/1` overrides the choice. Upload
and readback buffers keep host access. An LRU resource cache, limited to 64
bundles with a 768 MiB estimated eviction threshold, retains hot draws when new variants
arrive instead of rebuilding the entire cache.

## Shared shader translation

Vertex and fragment lowering use the same decoded Latte programs, ALU
expressions, predicate/execution masks and forward control flow. Vertex lowering
adds POS0/PARAM exports, captured attributes, compact uniform-block references
and explicit base-level texture samples. Validated DOT4 reductions preserve
ordered products and simultaneous register writes; DX10 comparisons preserve
integer masks in both native lowering and the interpreter. Static GPR
addresses emit individual register locals, and glslang optimizes the validated
SPIR-V modules before Vulkan pipeline creation.

The native vertex ABI receives four fetched attribute vectors. Attribute
fetching, endian conversion and index assembly still run on the CPU; shader
execution, texture sampling and rasterization run on Vulkan. Viewport and pixel
semantic mappings travel with each draw. Vertex uniforms and samplers use
separate bindings from the fragment stage. Exact-program vertex translation and
SPIR-V caches avoid recompiling unchanged programs.

The backend supports points and triangles. Replay dispatches point batches of
at least 64 vertices and eligible vertex texture shaders to Vulkan. Other draws
keep the established CPU vertex placement and interpolation; translating their
triangle vertices introduced small subtitle-edge differences in the comparison.
Texture shaders avoid CPU reads of resident surfaces even for small draws. Unsupported shaders or resource state return to
the existing reference execution. Arrays, feedback into this draw's targets,
unsupported texture addressing, stencil and multisampling remain outside this
native subset. This is not a complete implementation of the Latte ISA.

## Resident textures and verification

Vertex and fragment stages can consume typed GPU render targets. A resident
D32 depth image transfers into an R32 sampling image through a GPU buffer;
the transfer buffer uses device-local memory, and the CPU does not read or
decode its pixels. Version checks protect snapshots,
and the texture barriers include both shader stages.

Native/reference verification explicitly executes the CPU vertex shader for
checked draws, materializes the pre-draw surfaces and compares float targets
numerically. Normal native draws avoid this diagnostic work. Unit tests cover
HDR blending and masks, row padding, older image versions, uniform windows,
resident depth sampling by points and triangles, DOT4/predicate behavior,
DX10 masks including NaN inputs, and a captured indexed HDR rendering chain
for all four supported index byte orders.

No new build dependency or desktop setting is needed. Native vertex execution
is enabled with the native raster backend; `WEMU_NATIVE_GPU_TIMING=1` records GPU timestamps when the queue supports them;
these diagnostics run separately from normal rendering. `WEMU_NATIVE_VERTEX=0` selects CPU
vertex execution for controlled comparisons. The standalone shader diagnostic
also accepts `.vert` output:

```sh
build/core/wemu_shader_lower captured-vs.hex translated.vert
```

These paths use captured GPU state and real resources. They do not change guest
clocks, video pacing or game code.

## Float-to-integer ALU coverage

The shared vertex/fragment lowerer supports `FLT_TO_INT` and `FLT_TO_UINT`.
Integer results remain bit patterns in the shader register file, so subsequent
integer ALU reads receive the original bits. Signed finite conversion truncates
toward zero and keeps the low 32 bits using explicit IEEE-754 decomposition;
it avoids undefined C++ casts and host-dependent SPIR-V overflow behavior.
Unsigned conversion clamps negative inputs to zero and saturates positive
out-of-range inputs. The reference interpreter uses the same defined boundaries.

The R700 ISA documents truncation and low-bit signed conversion:
[AMD R700 instruction-set reference](https://www.x.org/docs/AMD/old/R700-Family_Instruction_Set_Architecture.pdf).
NaN maps to zero and infinities to the corresponding integer limit; these
exceptional-input policies follow the later
[AMD Evergreen reference](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/AMD_Evergreen-Family_Instruction_Set_Architecture.pdf)
and still need direct Latte hardware confirmation. Tests compare raw conversion
bits inside shaders, avoiding NaN payload loss through floating-point color
attachments. Both vertex/fragment SPIR-V and native/reference execution are checked.

These operations allow eligible vertex-texture shaders to remain on Vulkan.
Unsupported instructions/resources continue to fall back; ordinary small
triangle vertex passes retain the existing eligibility policy.

Ordinary triangle draws also execute eligible vertex programs on Vulkan by
default; `WEMU_NATIVE_TRIANGLE_VERTEX=0` restores reference vertex execution for
them. Interpreting vertex programs on the replay thread took about 60% of its
time in MK8's race and most of the character select's 17 ms replay. Compared with
reference execution, MK8's menus differ only at triangle edges and in rounding:
in the kart select, 498 of 921600 pixels differ by more than 2% and none by more
than 10%. The host rasterizer clips and applies its own coverage rules instead of
the replayer's CPU clipping and sub-half-pixel culling.

## Stack-pop control flow

`CF_POP` accepts an ignored address field. The
[AMD R700 ISA reference](https://www.x.org/docs/AMD/old/R700-Family_Instruction_Set_Architecture.pdf)
specifies that POP never branches. The translator retains pop-count, mask-stack
and supported-condition validation; it does not relax PUSH/ELSE/JUMP behavior.
Masked execution/restoration is compared with the reference and Vulkan for zero,
forward and out-of-range ignored addresses in both shader stages.

## Truncation and scalar trigonometry

Both shader stages support `TRUNC`, `SIN` and `COS`, including their reference
execution. Truncation rounds toward zero, preserving zero's sign and passing
through infinities/NaNs. Sine/cosine inputs are normalized turns; lowering converts
them to radians for the host function. This follows
[LLVM's R600 lowering](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AMDGPU/R600ISelLowering.cpp)
and [Cemu's Latte implementation](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompilerEmitGLSL.cpp).
Hardware trigonometric approximation is not bit-exactly reproduced.

The shared decoder classifies sine/cosine in the scalar ALU slot, preserving PS
forwarding independently of vector PV. Tests exercise both forwarding paths,
signed truncation, exceptional inputs, both-stage SPIR-V and Vulkan comparisons
within the supported trigonometric input domain. These are shared ISA operations;
no game-specific shader replacement is used.

## Static uniform blocks in both shader stages

Fragment translation now supports the same static kcache windows as vertex
translation. Shader references identify a bank and vec4 offset; GX2 replay supplies
four floats per reference from captured uniform-block bytes, and Vulkan refreshes
those values for each draw. Missing block words read zero. Both full snapshots
are checked before dispatch, with a 512-vector native uniform budget. Loop-indexed
windows and relative addressing retain software fallback.

This also corrects shared source decoding: Latte kcache selectors are 128–191,
while 256–511 selects the constant-register file even inside a clause using kcache.
Active static windows use their declared bank and 16-vector address units.
Vertex bank N now maps directly to GX2 uniform-block location N; the previous
unverified +1 default is removed. The existing WEMU_KCACHE_BIAS diagnostic remains
an explicit opt-in. Both reference execution and Vulkan lowering use the corrected
selector interpretation. Uniform-off legacy constant views remain supported.

Tests distinguish both windows/banks/offsets from the constant-register file,
verify reference floats and Vulkan UNORM output within the established one-unit
conversion tolerance, refresh changed values, reject incomplete uniform snapshots,
and replay a real GX2 block upload after its guest source has been reused.
Geometry/material pixels can change because previously incorrect block reads now
receive real data. This is a general shader correction, not a per-title override.

Primary references:
[Latte selectors and kcache modes](https://github.com/decaf-emu/decaf-emu/blob/master/src/libgpu/latte/latte_enum_sq.h),
[GX2 uniform-block register mapping](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/gx2/gx2_shaders.cpp),
and [Cemu uniform submission](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/gx2/GX2_shader_legacy.cpp).


## Address registers, export masks and helper-value lifetime

Latte `TRUNC` uses opcode `0x11`. Opcode `0x16` is `MOVA_FLOOR`, which updates
an address-register component even when its GPR write mask is disabled. The
shared decoder, reference interpreter and Vulkan lowerer now distinguish those
operations and implement `MOVA`, `MOVA_FLOOR` and `MOVA_INT`. AR-relative
constant-file reads select the instruction's AR component, apply the signed
offset, and return the documented NaN for an out-of-range constant index.
Address registers are valid only within their ALU clause. A relative read needs
an earlier address-writing group; simultaneous AR writes/relative reads,
uninitialized AR components, relative GPR/kcache operands and loop-indexed
addressing retain fallback. Native address writes with predication or result
modifiers also retain fallback. Ordinary shaders avoid the interpreter's
address-validation path when decoded programs contain no addressing operations.

Vertex POS/PARAM selector `7` masks a component. Both execution paths preserve
previously exported components, including across repeated exports. An entirely
masked export neither clears an earlier value nor invents a valid parameter.
Selector `6` remains unsupported by native lowering.

Whole-quad lowering tracks helper-value lifetime per register component and
PV/PS slot. Ordinary, unpredicated overwrites can remove stale helper values
inside the current mask; an unconditional overwrite after reconvergence can
remove them entirely. Texture writes participate in the same analysis. A
conditional jump applies stack restoration to its taken edge, then merges that
state at its forward target. It does not poison values on the fall-through
path. Predicated/partial writes, skipped overwrites and live helper values
escaping their original varying mask remain conservative software fallbacks.

The lowerer also proves uniform execute masks. Constants and zero-initialized
registers start uniform; interpolated attributes and sampled values do not.
Register/PV/PS facts follow simultaneous ALU groups, predication and forward
joins. Masked writes can lose a fact; only a proven all-lane overwrite replaces
it. Predicate facts use the pre-group state. After any varying execute mask,
this proof stays disabled until shader termination, keeping stack joins
conservative. Under a proven uniform mask the whole quad is active or inactive
together, so live values can survive reconvergence without extra helper writes.
This extends the proven subset; it does not implement general quad execution.
`SAMPLE` (`0x10`) and gather are classified as implicit, while `SAMPLE_LZ`
(`0x13`) is explicit base LOD. Varying whole-quad programs with implicit samples
carry a runtime resource requirement. Each implicit binding must expose exactly
one original source mip at level zero, identical minification/magnification
filters and no anisotropy. Those samples cannot observe derivatives, so the
existing explicit-base-LOD emission remains valid. Missing mip metadata,
multiple levels, differing filters and anisotropy retain software fallback
before any GPU submission. The requirement comes from the compiled shader;
caller-supplied binding metadata cannot bypass it. Live helper values escaping
their mask still fail lowering. Uniform-mask programs continue to obey the
existing native texture/LOD restrictions.

A shader-wide implicit-derivative rejection initially moved a full-screen
cinematic pass back to the CPU. Validating the actual resources per draw keeps
that derivative-free pass accelerated without accepting general quad execution.

Regression tests use literal ISA opcode encodings, negative/fractional address
indices, AR components and clause resets, masked POS/PARAM preservation with
Vulkan float-target readback, and conditional-pop helper-value lifetime. The
existing rejection tests still cover observable helper writes. A mixed-lane
Vulkan/reference test covers derivative-free sampling and rejects unsafe mip,
filter and anisotropy changes even after a compatible draw was cached.
Primary encodings are documented in
[Decaf's Latte instruction table](https://github.com/decaf-emu/decaf-emu/blob/master/src/libgpu/latte/latte_instructions_def.inl)
and the address/whole-quad behavior in AMD's *R600 Instruction Set Architecture*,
sections 3.6.2, 4.6.1–4.6.3 and the MOVA instruction descriptions.


Reference interpolation preserves exactly equal vertex attributes and window
depth values. Independently rounded barycentric weights can otherwise make a
constant depth plane drift by one floating-point unit, causing incorrect LESS or
EQUAL decisions against a previous plane. The general constant-value path avoids
that drift; varying attributes retain the existing interpolation. A captured
GX2 regression covers fractional geometry at exactly equal Float32 depth and
checks both reference occlusion and Vulkan readback.
