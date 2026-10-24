# GX2 movie rendering and tiled depth allocations

Triangle strips, triangle fans and quad strips use the same primitive assembly
in reference rasterization, Vulkan vertex draws and software fallback after a
rejected native draw. Strip winding alternates before face culling. Incomplete
trailing primitives are ignored; unsupported line/adjacency/rectangle modes
are not interpreted as triangle lists. This does not implement primitive restart.

Vertex and pixel shaders with a null direct program pointer use their embedded
GX2R buffer's storage and element-count times element-size. The multiplication
is checked in 64 bits against the existing 64 KiB shader limit. This restores
movie shaders that perform NV12 colour conversion, rather than falling back to
sampling the luma texture alone. Direct shader pointers retain priority.

Single-sample 2D and array depth allocations now use AddrLib's depth layout,
including padded tile rows, alignment and supported mip offsets. Previously the
legacy allocation rounded the byte count but used the unpadded height. The
last macro tile could exceed the advertised image size, so depth backing
creation failed and retried a large CPU decode on every draw. Those draws then
ran through reference rasterization. The shared layout calculator also accepts
24-bit UNORM depth/stencil elements. Scanout and multisampled allocation remain
outside this change.

These are shared GX2 implementations, without title-specific shader replacements
or rendering shortcuts. Regression coverage checks full-rectangle colour and
coverage with face culling, indexed/base/first vertices, direct and GX2R shader
storage, software and Vulkan rendering, every reachable texel in partially used
depth tile rows, and tiled depth clears, exports, occlusion and sampling.

## Reproduce core delivery measurements

`scratchpad/session_probe.py` drives the desktop socket protocol, acknowledges
every TV frame and optionally saves PPM captures. It writes `core.log`,
`frames.csv` and `summary.json` to the requested directory. No extra Python
packages are required. Run separate processes for before/after measurements;
keep the CPU choice, cache state, frame window and diagnostic flags equal.

```bash
python3 scratchpad/session_probe.py /path/to/code/game.rpx \
  --output /tmp/wemu-probe --cpu llvm \
  --native-cache "$HOME/.cache/WEMU/WEMU/ppc-native" \
  --frames 300 --measure-from 150 --profile --capture-every 50
```

The core must be built with the Qt desktop transport. `--cpu llvm` requires an
LLVM-enabled build; `--cpu x64` selects the existing experimental x86-64 backend.
The default uses the cached interpreter. The selected backend's startup log
should be checked when using a differently configured build. These measurements
exclude Qt texture upload and desktop vsync; movie FPS counts delivered decoded
images separately from guest presents.

In a local Smash intro comparison with the same LLVM/native Vulkan configuration
and guest presents 150–240, mean frame time fell from 184.4 ms to 29.5 ms
(5.4 to 33.9 guest presents/s). Replay-thread time fell from 182.5 ms to 3.3 ms.
Both measurements used frame/draw profiling and periodic image captures.
The movie quad filled the screen with colour after the fixes. Movie images
advance about once every two guest presents in this window, so this is still
below real-time movie playback. These figures describe this host and scene,
not a speed guarantee for other titles.

The same fixed core using its existing x86-64 recompiler delivered 59.1 guest
presents/s and 29.2 movie images/s over presents 150–600, with frame profiling
and a capture every 100 presents. Mario Kart 8 and Pikmin 3 also completed
200-present smoke runs through that backend. These smoke runs establish startup
and frame delivery, not complete gameplay compatibility.

That short movie sample did not detect a later stall. Longer runs exposed
repeated decoder End/Begin recovery after roughly eight seconds of media:
presentations continued near 60 Hz while decoded images fell to about one per
second. The game considered its video producer late because the present path
advanced a whole display interval immediately, without letting workers run
during that interval.

With `WEMU_FRAME_CLOCK=1`, swaps now sleep the submitting thread until an
anchored display deadline. Ready workers execute before time is fast-forwarded;
CPU work and explicit waits count toward the same deadline. Anchoring prevents
late presents from accumulating extra elapsed movie time. `OSYieldThread` also
charges 64 time-base ticks for the kernel scheduling work omitted by HLE. This
is a conservative timing approximation (1280 core cycles), rather than a
cycle-accurate kernel implementation. Without it, yielding poll loops performed
millions of host context switches while waiting for a single display interval.
Clock queries themselves still do not advance guest time.

Skipping the intro exercises another scheduling path. Previously, explicit
`OSYieldThread` calls chose the globally highest-priority ready thread, even
when that thread belonged to a different emulated core. Busy movie workers could
then prevent their lower-priority controller from finishing worker startup. The
first A press displayed the title, but decoding continued; pressing A again
revealed the movie instead of completing the menu transition. Explicit yields
now first select a ready thread on the yielding core, using priority and round
robin within that set. With no local candidate, they retain the global selection.
Instruction-boundary preemption continues to share time with other cores.
A regression reproduces the controller/worker priority and affinity pattern.
A normal-paced probe pressing A at presents 600 and 1,200 now closes the decoder
after 255 delivered frames, keeps the title visible, then reaches the main menu.

`OSIsThreadTerminated` now reports the scheduler's completed state, including
before a join and after guest handle reuse. Uncreated guest thread storage uses
Cafe's NONE/MORIBUND states. Returning the default unknown-import result for this
query kept Smash waiting after its title screen. The menu and Smash submenu
were reached after implementing it.

`GX2RDrawIndexed` now resolves the resource buffer and its element-scaled index
offset into the shared indexed draw command. Rectangle rendering coverage also
checks this path with nonzero index and base-vertex offsets.

Fighter selection also exposed active stencil draws that previously fell back
to software rasterization, taking hundreds of milliseconds per draw. Direct
Vulkan rendering now supports the UNORM24 depth/stencil surface through a
combined D32/S8 attachment. Front/back comparisons, all eight stencil operations,
reference/read/write masks and independent stencil clears share the captured
GX2 state with the reference rasterizer. Lazy readbacks preserve both planes
and their resident image versions. Unsupported backend configurations retain
reference rendering. Regression coverage compares native colour and stencil
bytes against the reference, including stencil/depth failures, clamp/wrap
boundaries, zero write masks and colourless updates.

`VPADGetLcdMode` now initializes the caller's output with the tracked GamePad LCD
mode; `VPADSetLcdMode` changes it. Leaving this output unwritten caused a
GamePad connection dialog after reaching fighter selection.

The shared CPU decoder, interpreter and x86-64 backend now implement `psq_lu`
and `psq_stu`. These are quantized paired loads/stores that update their address
register after a successful access. Smash uses them in its common copy loop;
missing handlers produced repeated unknown-opcode exceptions. Tests exercise
the actual copy pattern, signed offsets, integer quantization and fault behavior,
plus randomized interpreter/JIT comparisons.
The same copy routine uses `dcbi`, now handled consistently with the existing
coherent-memory data cache operations. WEMU does not model separate dirty data
cache lines, so invalidation leaves registers and guest bytes unchanged.

A normal-paced 9,000-present x86-64/Vulkan run delivered all 3,141 frames of the
104.8-second intro, with one decoder begin and no recovery resets. Presents
150–6,378 averaged 59.34 presents/s and 29.69 movie images/s over 104.96 seconds.
Button presses reached the main menu, Smash submenu and fighter roster, and
joined Player 1. The settled menu averaged 57.90 presents/s; fighter selection
averaged 59.37 over presents 8,100–9,000. Matching 300-present startup smoke runs
of Mario Kart 8 and Pikmin 3 measured 59.46 and 59.45 presents/s respectively;
Pikmin's movie delivery was 29.72 images/s. These measurements exclude desktop
upload/vsync and establish intro/menu/startup behavior, not match gameplay or a
complete playthrough. The final cache-instruction addition is checked separately
through the copy-loop regressions and a subsequent unthrottled 9,000-present
diagnostic run. That run again reached the roster and joined Player 1, delivered
all 3,141 movie frames, and reported zero CPU exceptions and decoder resets.

For a full intro and subsequent button presses, increase the probe window:

```bash
python3 scratchpad/session_probe.py /path/to/code/game.rpx \
  --output /tmp/wemu-long-probe --cpu x64 --frames 9000 --seconds 230 \
  --h264-trace --capture-every 300 \
  --press-at 6600 --press-at 6900 --press-at 7200 --press-at 7500 --press-at 7800
```

`--press-buttons` accepts a VPAD bitmask (default A, `0x8000`); each press lasts
six presents unless `--hold-frames` changes it. `--thread-dump-ms`, `--ips`,
`--jit-profile`, and `--trace-hle-name` provide optional diagnostics. Avoid
comparing performance runs with different tracing or other CPU/GPU workloads.
`--unthrottled` removes the host presentation limiter for faster diagnostic
runs; its frame rates must not be reported as normal playback performance.

Desktop launches now prefer the x86-64 backend when it is compiled into the
core. Builds without it retain LLVM when available; native CPU execution can
still be disabled through `emulation/nativeCpu`. Unsupported x86-64 hosts retain
the core's runtime interpreter fallback. Direct core launches keep their existing
environment-controlled CPU selection. GPU and CPU fixes are shared across titles.
The x86-64 backend translates Wii U CPU instructions for the host CPU; Vulkan
continues to render on the selected graphics device.

References:

- [WUT primitive modes](https://github.com/devkitPro/wut/blob/master/include/gx2/enum.h)
- [WUT shader and GX2R buffer layouts](https://github.com/devkitPro/wut/blob/master/include/gx2/shaders.h)
- [Decaf shader resource-buffer selection](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/gx2/gx2_shaders.cpp)
