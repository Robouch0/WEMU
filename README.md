# POC — PowerPC "Hello World" for the WEMU Interpreter

> **Branch:** `POC-helloworld_power_pc`
> **Scope:** A minimal Wii U homebrew written in C, compiled to a native PowerPC `.rpx` via the [WUT](https://github.com/devkitPro/wut) toolchain. This binary serves as a controlled, reproducible **test input** for WEMU's PowerPC interpreter and Cafe OS emulation layer.

---

## 🎯 Why this POC exists

Building a Wii U emulator means emulating two very specific things:

1. The **PowerPC "Espresso" CPU** (3-core, 32-bit, derivative of the Broadway/Gekko lineage)
2. The **Cafe OS** runtime — its loaders (RPX/RPL), syscalls, IPC to the IOSU, and user-facing libraries (`coreinit`, `gx2`, `whb`, etc.)

Before plugging in real commercial titles — which exercise the GPU, the audio DSP, dozens of system libraries and gigabytes of assets — we need a **small, fully understood binary** that we can:

- Build deterministically from source
- Inspect statically (instruction-by-instruction if needed)
- Run on the real console to capture an authoritative reference behaviour
- Replay through our interpreter to validate each emulation layer in isolation

That is what this POC delivers: the **smallest meaningful Wii U program** that still touches the surfaces we need to emulate first — process lifecycle, screen buffers, cache flushing, and logging.

---

## 📦 What this POC contains

| File | Purpose |
|------|---------|
| `POC/main.c` | The homebrew source — initialises logging, ProcUI, OSScreen, and renders text on both the TV and the GamePad (DRC) until the OS requests shutdown. |
| `POC/CMakeLists.txt` | Build configuration. Uses `wut.toolchain.cmake` and produces a `.rpx` via `wut_create_rpx()`. |

The program is intentionally derived from the canonical [`wut` `helloworld` sample](https://github.com/devkitPro/wut/tree/master/samples/cmake/helloworld) so that any divergence in execution between the real console and our emulator can be attributed to **our** implementation, not to custom code.

---

## 🔬 What the program actually does

In one frame loop (`while (WHBProcIsRunning())`), the binary:

1. Initialises **two log sinks** — `WHBLogCafeInit()` (Cafe internal log, visible in Decaf and crash logs) and `WHBLogUdpInit()` (broadcast over UDP, capturable from a host PC with `udplogserver`).
2. Initialises **WHB ProcUI**, the wrapper that handles Cafe OS lifecycle events (HOME menu overlay, suspend/resume, foreground/background transitions).
3. Initialises **OSScreen**, the simple Cafe OS framebuffer API.
4. Allocates **two screen buffers** with `memalign(0x100, ...)` — one for the TV, one for the DRC (GamePad) — using the sizes returned by `OSScreenGetBufferSizeEx`.
5. Each iteration:
   - Clears each buffer to opaque black (`0x00000000`)
   - Writes two lines of text via `OSScreenPutFontEx` on each display
   - Flushes the data cache for the buffer ranges (`DCFlushRange`) — required because the GPU reads from main memory and PowerPC caches are not coherent with it
   - Flips the buffers (`OSScreenFlipBuffersEx`) to present
6. On shutdown request, frees buffers, deinits OSScreen / ProcUI / logging, and returns.

This exercises a small but representative slice of what any real game also does on every frame.

---

## 🧪 Why this is a valuable test for the emulator

The POC was chosen because each line of code maps cleanly to **a specific subsystem WEMU must implement**:

| What the binary does | What WEMU has to emulate to run it |
|----------------------|------------------------------------|
| Loads as a `.rpx` | RPX/RPL loader, ELF-derived format parsing, relocation, symbol resolution against `.rpl` system libraries |
| Calls `WHBLogCafeInit` / `WHBLogUdpInit` | `whb` library shim, syscall path for kernel logging, network stack stubbing for UDP |
| `WHBProcInit` and the `WHBProcIsRunning` loop | Cafe OS process lifecycle, HOME button events, foreground/background state machine |
| `OSScreenInit`, `GetBufferSizeEx`, `SetBufferEx`, `EnableEx` | `coreinit` OSScreen API, GX2 framebuffer plumbing, display configuration for TV (1080p) and DRC (854x480) |
| `memalign(0x100, ...)` | `coreinit` allocator, MEM heaps, alignment guarantees |
| `OSScreenClearBufferEx`, `OSScreenPutFontEx` | Pixel writes into emulated VRAM, embedded font rendering |
| `DCFlushRange` | PowerPC data cache emulation — critical for any frame to actually appear on screen |
| `OSScreenFlipBuffersEx` | Double-buffer swap, frame pacing, vsync handling |
| `free`, deinit cascade | Clean teardown, leak detection, ensuring repeatable runs |

If our interpreter can boot this `.rpx` and produce the expected text on both screens, it has crossed the **first real milestone**: a complete, end-to-end execution path through the PowerPC CPU, the loader, the system libraries, and the framebuffer pipeline.

---

## 🛠️ Building

### Prerequisites

This POC builds **outside** of WEMU's main build system because it targets the Wii U, not the host. You need:

- [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman)
- The Wii U development meta-package, which pulls in `devkitPPC`, `wut`, and the related tooling:

```bash
(sudo) (dkp-)pacman -Syu --needed wiiu-dev
```

### Build (CMake)

```bash
cd POC
mkdir build && cd build
$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-cmake ..
make
```

Output: `helloworld.rpx` — a PowerPC `.rpx` ready to be loaded either by a real Wii U (via Aroma / Homebrew Launcher) or by WEMU.

---

## ▶️ Running

### On a real Wii U (reference run — to capture ground-truth behaviour)

1. Copy `helloworld.rpx` to the SD card under `sd:/wiiu/apps/helloworld/`
2. Boot into Aroma / the Homebrew Launcher and select it
3. The TV displays `Hello world! This is the TV.` and the GamePad displays `Hello world! This is the DRC.`
4. (Optional) Run `udplogserver` on a PC on the same network to capture the UDP log output

### Through WEMU (the actual point of this POC)

```bash
# from the WEMU project root, on the relevant interpreter branch
./wemu --interpreter path/to/helloworld.rpx
```

Expected: the same two strings rendered on the emulated TV / DRC outputs, and the same log lines on the host console.

Any divergence — wrong instructions stepped, unhandled syscall, missing `coreinit` symbol, garbled framebuffer, missing cache flush behaviour — points directly at the subsystem to fix next.

---

## 📊 What we use this POC to measure

This binary is small enough that we can attach concrete numbers to it, which is exactly what the Greenlight jury is asking for under "impact synthesis":

- ✅ **Boot success** — does `main` start at all?
- ✅ **Instruction coverage** — which PowerPC opcodes are exercised by a single boot? (Used to prioritise interpreter work.)
- ✅ **Symbol coverage** — which `coreinit` / `whb` / `gx2` exports are required? (Used to prioritise system-library stubs.)
- ✅ **Frame correctness** — does the framebuffer match the reference capture from the real console, pixel-for-pixel?
- ✅ **Reproducibility** — same binary, same inputs, same output across N runs.

These metrics feed directly back into the PGE5 roadmap: each gap identified here becomes a tracked task.

---

## 🔗 References

- [`devkitPro/wut`](https://github.com/devkitPro/wut) — Wii U toolchain (the `helloworld` sample is the upstream basis for this POC)
- [`devkitPro/wut` — `helloworld` sample](https://github.com/devkitPro/wut/tree/master/samples/cmake/helloworld)
- [WiiUBrew — Homebrew development guide](https://wiiubrew.org/wiki/Homebrew_development_guide)
- [WiiUBrew — RPX format](https://wiiubrew.org/wiki/RPL)
- [WiiUBrew — OSScreen](https://wiiubrew.org/wiki/OSScreen) (Cafe OS framebuffer API)
- Cafe OS / IOSU split — high-level overview by Maschell: <https://maschell.github.io/homebrew/2019/11/20/new-environment-part1.html>

---

## 📍 Status

| Item | Status |
|------|:------:|
| Builds against current `wiiu-dev` (devkitPPC + wut) | ✅ |
| Runs on real hardware via Aroma | ✅ |
| Runs through WEMU's PowerPC interpreter | 🟡 *(in progress — this is what the POC is for)* |
| Pixel-accurate framebuffer match vs. real console | 🔴 *(planned, see PGE5 roadmap)* |
