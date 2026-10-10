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

Device-only transfer buffers prefer local GPU memory. Image allocations retain
the existing compatible-memory selection: forcing local images improved GPU
raster time but regressed total frame delivery with the current CPU consumers.
`WEMU_NATIVE_DEVICE_LOCAL_IMAGES=1` enables that diagnostic comparison. Upload
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
