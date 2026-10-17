![License](https://img.shields.io/github/license/Robouch0/WEMU)
![Stars](https://img.shields.io/github/stars/Robouch0/WEMU?style=social)
![Issues](https://img.shields.io/github/issues/Robouch0/WEMU)
![Last Commit](https://img.shields.io/github/last-commit/Robouch0/WEMU/dev)
![Language](https://img.shields.io/github/languages/top/Robouch0/WEMU)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey?logo=linux)
![GitHub Actions Status](https://img.shields.io/github/actions/workflow/status/Robouch0/WEMU/wemu-checks.yml?branch=dev&label=Build)

# WEMU — Wii U Emulator

WEMU is an open-source Wii U emulator for Linux, written in C++23. The project is in early development: homebrew demos run, while commercial-title booting and rendering remain experimental.

Our goal is to accurately emulate the Wii U's PowerPC CPU and GPU in order to run Wii U software. What sets WEMU apart from existing solutions like Cemu is a companion feature: **use your phone as the Wii U GamePad**. A web application streams the GamePad screen from the emulator to your phone and sends touch inputs back, replacing the physical GamePad entirely.

---

## Current State

Commercial games are not yet fully playable. Current components include:

| Component | Status      |
|---|-------------|
| Big-endian ELF/RPX loader (ZLIB section decompression via zlib) | Done        |
| PowerPC interpreter, paired-single operations, and cached instruction blocks | Partial instruction coverage |
| Optional LLVM 19.1 CPU compiler and object cache | Experimental |
| Cooperative scheduling, heaps, layered filesystem, AX callbacks, and H.264 decoding | Implemented; compatibility is incomplete |
| GX2 replay, Latte shader lowering, and Vulkan rendering | Experimental |
| Qt6/QML library with isolated emulator processes | Implemented |
| USB/gamepad input (SDL2) | Done        |
| RPX file format support (SHF_DEFLATED parsing) | Done        |
| Wii U title library browser | Done |
| Phone-as-GamePad web app | Planned     |

---

## Building from Source

**Platform:** Linux only (Ubuntu/Debian, Fedora, Arch).

### Quick start (recommended)

```bash
git clone https://github.com/Robouch0/WEMU.git
cd WEMU
./setup.sh
```

`setup.sh` detects your distribution, installs system dependencies, then builds the core,
GUI, and shader tool. It also builds the legacy standalone Vulkan tree if that tree is present.

```
Usage:
  ./setup.sh                      # install deps + build everything
  ./setup.sh --test               # build + run unit tests
  ./setup.sh --no-build           # install deps only
  ./setup.sh --clean              # wipe build dirs before configuring
  ./setup.sh --build-type Release # default is Release
  ./setup.sh --jobs 8             # override parallel job count
```

After a successful build:

- GUI binary: `./build/gui/appgui`
- Core binary: `./build/core/wemu`
- Shader diagnostic tool: `./build/core/wemu_shader_lower`
- Optional legacy Vulkan build: `./vulkan/build/`

### Manual build

<details>
<summary>Expand for manual steps</summary>

```bash
# Debian/Ubuntu dependencies
sudo apt install cmake g++ zlib1g-dev libvulkan-dev libglfw3-dev libsdl2-dev libsdl2-image-dev \
     libavcodec-dev libavutil-dev libswscale-dev qt6-base-dev qt6-declarative-dev \
     glslang-tools spirv-tools mesa-vulkan-drivers ninja-build ccache

# Core emulator + Qt GUI
git clone https://github.com/Robouch0/WEMU.git && cd WEMU
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

</details>

### Launching games

Run `./build/gui/appgui`. The GUI finds the repository's `games/` folder automatically;
a previously selected library folder is remembered. The folder can also be selected in
the GUI or overridden with `--library /path/to/games`.

Commercial dumps use `code/*.rpx`, `content/`, and Wii U title metadata. Installed updates
are paired with their base games. RPX demos can use `code/*.rpx` without a title ID, or
be loose `.rpx` files inside the library or its immediate subfolders. Demo metadata and
artwork are optional. Local game data is not included in this integration.

The launcher sets the native Vulkan rasterizer, resident texture/target, deferred/lazy
readback, and frame-clock options automatically. Each game runs in its own process;
Stop ends that process, and Open Log displays its session output. These experimental
paths do not guarantee complete graphical accuracy or real-time playback.

Build requirements include CMake >= 3.28.1, a C++23 compiler, Qt >= 6.6.2, SDL2,
SDL2_image, Vulkan, zlib, and FFmpeg >= 6.0. Native CPU compilation is optional:

```bash
cmake -S . -B build-llvm -DCMAKE_BUILD_TYPE=Release -DWEMU_ENABLE_LLVM=ON
cmake --build build-llvm --parallel 2
```

This requires LLVM 19.1 development files. `WEMU_NATIVE_CPU=1` opts into native CPU
execution for direct core launches; GUI launches use the default cached interpreter.

### Running Tests

```bash
# Via setup.sh
./setup.sh --test

# Or manually (from the repository root)
ctest --test-dir build --output-on-failure

# Include headless Vulkan shader/readback comparisons when a driver is available
WEMU_TEST_VULKAN=1 ctest --test-dir build --output-on-failure

# Run a specific test
./build/core/tests/wemu_tests --gtest_filter=InstructionTest.ADD_NoOE_NoRc
```

### Diagnostic tools

`python3 scratchpad/rpxtool.py --rpx /path/to/game.rpx secs` lists RPX sections; its
`word`, `callers`, and `xref` commands inspect guest code. `disasm` and `store`
also require the optional Python `capstone` package. The RPX path is always
explicit; no personal game path is embedded in the tool.

`python3 scratchpad/raster_profile.py session.log` summarizes completed draw timings.
`python3 scratchpad/compare_raster_profiles.py before.log after.log` compares matching
draws. Add `--ignore-pixel-count` when comparing software and native paths: native
draws currently omit pixel counts. Their totals describe draw work, not whole-session FPS.

Native shader support includes isolated whole-quad
ALU whose helper results cannot escape a masked region; general quad execution,
implicit derivatives remain incomplete. Shared depth rendering supports depth
exports, compare/write state, clears, perspective varyings and versioned GPU
depth images. Float32 and UNORM16/24 depth views use canonical float storage;
fixed-point writes are rounded in a GPU compute pass after depth comparison.
Stencil, floating 24-bit depth, multisampling and complete clipping remain
outside the validated native subset. `WEMU_NATIVE_DEPTH=0` selects software
depth rendering for comparison while retaining the other native draw paths.
Linear R32 float textures can upload their guest GPU bytes directly, including
padded rows, without a temporary float expansion. Tiled sources retain layout
conversion; component mapping, filtering and content-based reuse are preserved.

---

## Architecture Overview

```
WEMU/
├── core/               # Emulator executable, CPU, HLE, and GPU pipeline
│   ├── src/binary/     # ELF/RPX loader + big-endian decoder
│   ├── src/cpu/        # Interpreter, registers, instruction implementations
│   │   └── tables/     # X-macro tables: cpu_instructions.anh, cpu_fields.anh
│   ├── src/gfx/        # GX2 replay, Latte shaders, and Vulkan rendering
│   ├── src/hle/        # OS services, scheduler, heaps, filesystem, and audio callbacks
│   ├── src/video/      # H.264 decoding
│   └── tests/          # GoogleTest unit tests
├── gui/                # Qt6/QML launcher and input management
│   └── src/input/      # IInputDevice, KeyboardInput, SDLGamepadInput
└── scratchpad/         # RPX inspection and raster profiling tools

```

The loaded binary owns guest RAM. The loader transfers it to the interpreter without
copying the buffer; instruction fetch, data accesses, HLE, and diagnostics share that
same memory. Memory is move-only, and typed reads/writes validate the complete access
range while preserving big-endian values and unaligned guest accesses.

### Adding a PowerPC Instruction

Instructions are registered through an X-macro table. To add a new instruction:

1. Add an entry to `core/src/cpu/tables/cpu_instructions.anh`:
   ```c
   INSTR(MYINSTR, OPCD(x), XO9(y))
   ```

2. Implement the function in `core/src/cpu/instructions/`:
   ```cpp
   void Core::Instruction::MYINSTR(Core::Interpreter &cpu, const EncodedInstruction &instr)
   {
       // access cpu.m_gpr[], cpu.m_cr, cpu.m_xer, etc.
   }
   ```

The instruction will be automatically registered in the dispatch table and enumerated in `InstructionID` — no other files need to be modified.

---

## Contributing

Feature branches are developed off `dev`. Only `dev` can be merged into `main`.

1. Fork the repository and create a branch from `dev`
2. Open a pull request targeting `dev`

---

## Legal Notice

WEMU is an independent and unofficial open-source Wii U emulator. It is not affiliated with, authorized, sponsored, or endorsed by Nintendo.

WEMU does not include or distribute Nintendo games, firmware, cryptographic keys, SDK files, copyrighted assets, or other proprietary Nintendo material.

Users are responsible for obtaining any games, system files, keys, or other required data lawfully and for complying with the laws applicable in their jurisdiction.

Nintendo, Wii U, and other Nintendo names and trademarks remain the property of their respective owners.

---

## License

WEMU's original source code is licensed under the [MIT License](LICENSE).

The MIT License permits use, copying, modification, redistribution, sublicensing use of WEMU.

The MIT License applies only to code and other material for which the WEMU contributors have the necessary rights. It does not grant any rights to Nintendo software, games, firmware, trademarks, cryptographic material, or other third-party intellectual property.

WEMU also uses third-party open-source libraries that remain subject to their respective licenses. See [Third-Party Licenses](THIRD_PARTY_LICENSES.md) for details.

Copyright © 2025-2026 WEMU contributors.
