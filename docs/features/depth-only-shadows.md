# Depth-only shadows and polygon offset

A disabled or unexported color binding no longer forces depth rendering to share
that binding's dimensions. The native raster backend builds a depth-only render
pass and framebuffer with no color attachment, selects the depth extent for its
viewport and scissor, and allocates no color upload/readback image or buffers.
The borrowed color snapshot remains part of the existing replay ABI, but its bytes
or live GPU version are returned unchanged. Deferred depth tickets retain their
own version and row padding across resource reuse and backend destruction.

Replay does not mark an unused color binding as written. Fully culled triangle
lists and quads return before attribute fetches, skinning, texture preparation or
attachment access. Point rendering remains independent of face culling.

Geometric depth uses Vulkan's position interpolation. Native vertex outputs mark
`gl_Position` invariant and precise so separately translated depth and material
programs retain position consistency. Programs that export depth still execute
their actual Z export; it replaces geometric depth. EQUAL comparisons retain their
original semantics without an epsilon or relaxed comparison function.

`GX2SetPolygonOffset`, `GX2InitPolygonOffsetReg`, `GX2GetPolygonOffsetReg` and
`GX2SetPolygonOffsetReg` capture front/back constant offsets, slope factors and the
clamp. Register storage orders scale before offset and stores scales multiplied
by 16; capture converts back to API units. Commands snapshot values at capture
time, and context save/restore retains them. Getter outputs are written after the
source fields have been read, including when output pointers overlap the source.

The software triangle path adds a constant primitive bias before geometric depth
comparison and storage. It uses the maximum absolute window-depth derivative,
the destination's fixed-point resolution or float primitive exponent, and the
signed clamp. Early rejection uses the same biased depth. Explicit shader Z
exports override this adjustment.

Vulkan float depth uses dynamic raster bias and checks support for a nonzero
bias clamp. Canonical 16/24-bit depth is stored temporarily in D32 and quantized
by the existing GPU conversion pass. Its bias uses the guest destination's units
in the fragment stage, rather than the temporary float attachment's units.
The bias inputs occupy the final 16 bytes of the 128-byte shared push-constant
range. Values are dynamic; bias enable and depth format participate in pipeline
reuse. The calculation runs before guest fragment control flow, and an explicit
Z export replaces its result.

The conversion pass reconstructs canonical float values with integer arithmetic.
For a nonzero integer below `2^n-1`, shift its leading bit to bit 23 to obtain
`t`. The normalized significand is `t + round(t / (2^n-1))`; the correction is
one for 24-bit depth, or `(t + 32767) / 65535` for 16-bit depth. This replaces
per-pixel serial long division while retaining the same rounding. Zero and one
remain explicit endpoints. A GPU regression checks every representable 16-bit
and 24-bit integer against canonical CPU floats; boundary tests also exercise
quantization of noncanonical incoming depth values.

Different offsets for two visible face orientations retain reference execution;
a culled orientation does not constrain the surviving face's native bias. Point
and line offset, polygon fill modes, full Z clipping/half-Z and depth/stencil
multisampling are not implemented by this change. Replay still requires a valid
retained color descriptor through its existing API, even when that descriptor is
unused by the depth-only Vulkan render pass.

Regressions cover depth coverage beyond a smaller color binding, row padding,
unused live color versions, retained depth tickets, signed slope/constant/clamp
values across all three depth formats, explicit depth exports, dynamic pipeline
reuse, and sloped prepass/material EQUAL coverage with distinct vertex programs.

Existing native verification can be bounded with `WEMU_NATIVE_VERIFY_FRAME`
(zero-based present count) and `WEMU_NATIVE_VERIFY_DRAW` (one-based draw number),
in addition to `WEMU_NATIVE_VERIFY_EVERY`. `WEMU_NATIVE_VERIFY_DIR` saves reference
and native HDR draw snapshots as PPM previews. `WEMU_SURFACE_DUMP_LIMIT` adjusts
the existing framebuffer texture dump cap, within 1–256 images. These diagnostics
are opt-in and do not change guest shader instructions or resource contents.
