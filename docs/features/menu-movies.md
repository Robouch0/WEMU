# Menu movies and packed textures

GX2 R5_G6_B5_UNORM textures decode as little-endian 16-bit GPU elements, with
red in bits 0–4, green in bits 5–10 and blue in bits 11–15. Sampling and cached
Vulkan uploads share the decoder. Linear, micro-tiled and macro-tiled surfaces
use 16-bit addressing; component selection still belongs to the texture view.
Mip and array layout queries also recognize this format. Packed render-target
writes remain outside this change.

The `mvplayer.rpl` HLE lives in `hle/MvPlayer.cpp`. Players and decoders belong
to the interpreter and reset with it. Local `/vol/content` movie URIs resolve
through the same content roots and update layers as filesystem reads. FFmpeg
libavformat demuxes the file, libavcodec decodes its video stream, and libswscale
converts frames to RGBA. Building now also requires the libavformat development
package (`libavformat-dev` on Debian/Ubuntu); CI installs it.

The supported interface covers opening, preparation, start/resume, stop, seek,
position and size queries, closing, destruction, and embedded TV/DRC views.
Playback follows the existing guest scheduler clock. Seeking flushes codec
reordering state. Repeated draws at the same timestamp reuse decoded pixels;
queued image commands own their matrix and pixel data independently of player
lifetime. This path currently provides video only, without a movie audio sink
or the full SDK observer and asynchronous callback interfaces.

`DrawImage` commands preserve ordering with GX2 work. The replayer uses the
current target, viewport and scissor, plus the view's clip and UV transforms.
Vulkan draws retain deferred render targets; software rasterization provides a
reference fallback. Quads crossing W=0 are rejected by this initial subset.
These are library and texture-format implementations, without title addresses,
fixed panel coordinates or replacement movie assets. Existing `h264.rpl`
playback retains its own interface and decoder.

`WEMU_MVP_TRACE=1` enables bounded method/view diagnostics. Regression coverage
uses a generated two-second MP4 fixture, including B-frame ordering, seeking,
end-of-stream, independent players and guest-clock progression. Rendering tests
compare Vulkan and reference image draws, including transformed UVs, scissoring
and preservation of untouched target pixels.

The movie compositor can consume a 3D colour lookup texture rendered by guest
shaders. Linear volume attachments now map individual Z slices to separate
render targets. Volume snapshots resolve these rendered slices, and the
reference sampler filters normalized Z with the same address modes as Vulkan.
Independent colour exports can therefore populate a volume through the existing
MRT pass mechanism. Thick volume attachment layouts remain unsupported.

AR-relative GPR reads are supported by the reference interpreter and shader
translator. The address must come from a previous ALU group in the same clause;
loop addressing and relative destinations retain fallback. Dynamic reads preserve
the complete register file, check its bounds and retain conservative whole-quad
provenance checks. Shaders without dynamic GPR reads keep scalar register lowering.
