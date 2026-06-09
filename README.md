![License](https://img.shields.io/github/license/Robouch0/WEMU)
![Stars](https://img.shields.io/github/stars/Robouch0/WEMU?style=social)
![Issues](https://img.shields.io/github/issues/Robouch0/WEMU)
![Last Commit](https://img.shields.io/github/last-commit/Robouch0/WEMU/dev)
![Language](https://img.shields.io/github/languages/top/Robouch0/WEMU)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey?logo=linux)
![GitHub Actions Status](https://img.shields.io/github/actions/workflow/status/Robouch0/WEMU/wemu-checks.yml?branch=dev&label=Build)

# WEMU — Wii U Emulator

WEMU is an open-source Wii U emulator for Linux, written in C++23. The project is currently in early development, targeting a public beta in July 2026.

Our goal is to accurately emulate the Wii U's PowerPC CPU and GPU in order to run Wii U software. What sets WEMU apart from existing solutions like Cemu is a companion feature: **use your phone as the Wii U GamePad**. A web application streams the GamePad screen from the emulator to your phone and sends touch inputs back, replacing the physical GamePad entirely.

---

## Current State

WEMU is not yet playable. The following components are in active development:

| Component | Status      |
|---|-------------|
| Big-endian ELF/RPX loader (ZLIB section decompression via zlib) | Done        |
| PowerPC interpreter (arithmetic, logic, branches, compare, float, load/store, shift, SPR) | Done        |
| Vulkan rendering pipeline (GLFW) | In progress |
| Qt6/QML launcher UI | In progress |
| USB/gamepad input (SDL2) | Done        |
| RPX file format support (SHF_DEFLATED parsing) | Done        |
| Wii U title library browser | In progress |
| Phone-as-GamePad web app | Planned     |

---

## Beta Goals (July 2026)

| Feature | Description |
|---|---|
| **Build & Launch** | Build and launch on a standard Linux system in under 7 minutes on a blank VM |
| **Title Library** | Scan a directory of dumped Wii U titles and display name, publisher, version, and icon from `meta.xml` |
| **RPX Loader** | Load a `main.rpx`, parse its structure, and detect its entry point |
| **PowerPC Execution** | Execute a minimal RPX that produces a graphical output for at least 5 seconds |
| **Controller Binding** | Map a USB controller (e.g. Xbox One) to Wii U GamePad buttons with visual feedback |
| **Contributor Docs** | A new contributor can read the documentation and add a new PowerPC instruction that correctly updates registers |

---

## Building from Source

**Platform:** Linux only (Ubuntu/Debian, Fedora, Arch).

### Quick start (recommended)

```bash
git clone https://github.com/Robouch0/WEMU.git
cd WEMU
./setup.sh
```

`setup.sh` detects your distribution, installs all system dependencies, then builds `core`, `gui`, and the Vulkan renderer. It targets < 7 minutes on a 4-core VM.

```
Usage:
  ./setup.sh                      # install deps + build everything
  ./setup.sh --test               # build + run unit tests
  ./setup.sh --no-build           # install deps only
  ./setup.sh --clean              # wipe build dirs before configuring
  ./setup.sh --build-type Release # default is Debug
  ./setup.sh --jobs 8             # override parallel job count
```

After a successful build:
- GUI binary: `./cmake-build-debug/gui/appgui`
- Vulkan binary: `./vulkan/build/`

### Manual build

<details>
<summary>Expand for manual steps</summary>

```bash
# Debian/Ubuntu dependencies
sudo apt install cmake g++ zlib1g-dev libvulkan-dev libglfw3-dev libsdl2-dev \
     qt6-base-dev qt6-declarative-dev glslang-tools ninja-build ccache

# Core emulator + Qt GUI
git clone https://github.com/Robouch0/WEMU.git && cd WEMU
mkdir cmake-build-debug && cd cmake-build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)

# Vulkan renderer (standalone)
cd ../vulkan && mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

</details>

### Running Tests

```bash
# Via setup.sh
./setup.sh --test

# Or manually
cd cmake-build-debug && ctest --output-on-failure

# Run a specific test
./cmake-build-debug/wemu_tests --gtest_filter=InstructionTest.ADD_NoOE_NoRc
```

---

## Architecture Overview

```
WEMU/
├── core/               # PowerPC interpreter, ELF/RPX loader, CPU state
│   ├── src/binary/     # ELF/RPX loader + big-endian decoder
│   ├── src/cpu/        # Interpreter, registers, instruction implementations
│   │   └── tables/     # X-macro tables: cpu_instructions.anh, cpu_fields.anh
│   └── tests/          # GoogleTest unit tests
├── gui/                # Qt6/QML launcher and input management
│   └── src/input/      # IInputDevice, KeyboardInput, SDLGamepadInput
├── vulkan/             # Vulkan/GLFW rendering engine
├── BE-elfanalyzer/     # Utility for inspecting ELF/RPX binaries

```

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

WEMU does not include or distribute any Nintendo copyrighted material. To use this emulator, you must own a physical Wii U console and legally dump your own games and system files.

---

## License

MIT License — Copyright (c) 2025 Hugo Duchemin. See [LICENSE](LICENSE) for details.
