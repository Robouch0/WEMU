# Cube maps, mip levels and independent color attachments

Pixel shader translation and reference execution support normalized, direct,
zero-offset `SAMPLE_L` and `SET_CUBEMAP_INDEX` instructions. The latter selects
a cube within a bound cube array; it does not write a GPR. `SAMPLE_L` reads its
explicit LOD from the selected fourth coordinate. Existing base-level sampling
continues to use the established reference policy; implicit derivatives and
anisotropic mip selection are not implemented by this change.

Cube resources specialize the shader and cache key to `samplerCubeArray`.
Latte reduced S/T coordinates and face IDs are converted to Vulkan directions.
The CPU reference implements the corresponding face selection, edge sampling,
and three-face corner filtering. Vulkan cube-array support is queried and
enabled on the device; unsupported devices retain reference execution. Explicit
LOD honors supported min/mag filters, sampler LOD bounds, and nearest or linear
mip selection. Cube gather, cube comparison, texture offsets and volume mip
sampling remain unsupported. Float snapshots preserve HDR values and component
mapping; invalid or incomplete mip chains cannot reach native submission.

`GX2CalcSurfaceSizeAndAlignment` now reports mip allocation sizes and offsets for
supported single-sample 2D, array and cube surfaces. Cube base layouts and smaller
mip layouts use the vendored MIT R600/R700 AddrLib, pinned and attributed under
`core/third_party/addrlib`. Existing supported 2D/array base layouts are retained.
The first mip pointer names its allocation directly; offsets for later levels
are relative to that allocation. Cube mip storage can pad six logical faces to
eight physical slices. The descriptor records the macro-to-micro tile transition.
Attachment and texture views select the appropriate mip layout and face.

Decoded mip snapshots have a 64 MiB total cache budget and a 16-entry limit.
Replay validates source bytes across frames and tracks rendered slice versions;
feedback snapshots are not retained. Snapshot ownership spans each synchronous
backend upload. Vulkan uploads every supported level and layer, transitions the
complete subresource range, and includes image shape and sampler LOD state in
resource reuse keys. Refreshed content invalidates retained uploads.

Direct color export bursts can select their own source register for a particular
guest target. Replay uses separate native passes for independent writable color
attachments with no depth/stencil test, no multiwrite replication and no overlap
with another attachment or bound texture allocation. Each pass retains its own
surface, blend state and channel mask. Shader-cache specialization records the
selected export. The backend sees one enabled native output. Passes requiring
simultaneous attachments still use existing fallback behavior. This allows real
HDR cube-lighting generation; it does not implement integer render targets or
render-to-compressed texture aliases.

Vulkan vertex buffers use power-of-two capacity buckets. A changing clipped
vertex count can reuse the same resource bundle without recreating color/depth
attachments. Upload and draw commands still use the exact live count, and budget
accounting includes the allocated capacity. This reduces allocation churn as
models rotate; it does not reuse stale vertices or omit geometry.

Tests cover cube faces and array selection, mip blending, sampler limits, HDR
refresh, incomplete-chain rejection, reference edge/corner filtering, padded
cube mip allocation, distinct burst outputs with native/reference agreement,
and changing vertex counts without stale coverage. MK8 still exposes incomplete
effects, depth precision limits and unsupported compressed lighting generation.
Mario Kart Stadium now reaches presented course-intro visuals; controllable racing,
correct course lighting and sustained 30 FPS remain unproven.

Signed BC4 (`0x234`) and BC5 (`0x235`) use signed RGTC endpoints and interpolation,
including negative normalized values and the -128 endpoint clamp. Reference
sampling applies the view component selectors before filtering. Expanded native
snapshots retain mapped RGBA32F values instead of converting them to UNORM8.
The shared bounded decode cache accounts for the larger float storage,
keys the complete numeric format and refreshes after guest source changes.
Signed cube/array/mip views can use the existing float mip snapshot path. Tests
cover endpoint modes, point/linear sampling, component mapping and changed source
bytes, with exact CPU/native agreement on the bounded RGBA8 sampling fixture.

Pixel-stage packed R11G11B10 and RGBA32F array views use the float snapshot path
as well. Selected layers retain HDR values and component mappings. Snapshot
validation includes the canonical layers' render-write generations, so a clear
or draw refreshes their contents even when guest RAM has not changed. Supported
RGBA32F array slices use 16-byte texel addressing. Native vertex-stage HDR array
sampling and native depth array snapshots retain their existing fallback.

This corrects normal-map interpretation independently of any game or shader hash.
Together with guest polygon offset, it substantially improves the inspected MK8
preview materials and shadows. See [depth-only shadows](depth-only-shadows.md)
for the corresponding native depth and raster-state changes.

Guest-only base-level BC1–BC5 texture snapshots expand each compressed block at
its tiled address and scatter the sixteen decoded texels into the upload image.
This avoids repeatedly invoking the general sampler and expanding the same BC1
palette for neighboring texels when a new asset appears. Partial edge blocks are
clipped to the logical image dimensions. Signed BC4/BC5 retain float values and
component mapping. Source snapshots are compared exactly once per presentation, allowing vectorized
byte comparison instead of a serial hash and detecting changes without hash
collisions. Both source and decoded bytes count toward the 256 MiB cache budget
and 128 MiB entry limit; changed guest bytes refresh the decoded image. The
larger entry bound accommodates a near-4K BC atlas plus its compressed source
without expanding a fallback upload to RGBA32F. Rendered
aliases and cube/mip snapshots retain their existing versioned paths. Native vs
reference regressions cover BC1–BC5, linear/micro/macro tiling, partial blocks,
source mutation, and signed point/bilinear filtering.

Pixel-stage base-level BC1, unsigned/signed BC4 and signed BC5 can instead upload
tightly packed compressed blocks directly to Vulkan. The native path untiles
blocks without expanding their pixels, preserving source-byte validation,
immutable snapshot ownership and view component mapping. For a near-4K BC1
atlas, both the image and its staging upload are about 8 MiB each instead of
64 MiB each. Device compression support and sampled-format/filter features are
checked before submission. Unsupported representations keep the reference path;
BC2/BC3, unsigned BC5, vertex-stage compressed uploads and compressed arrays or
mip chains retain their existing expanded snapshots.

Hardware BC1–BC5 interpolation may vary by vendor. Tests bound unsigned native
results using each sampled block's endpoint distance and the documented BC error
tolerance, including the reference decoder's byte truncation; alpha coverage and
untouched pixels remain exact. The diagnostic native/reference comparison still
reports these differences rather than hiding them. See the
[BC error tolerance specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#19.5.2)
and [measured GPU decoder behavior](https://fgiesen.wordpress.com/2021/10/04/gpu-bcn-decoding/).
