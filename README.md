# WEMU — Wii U Emulator

WEMU is an open-source Wii U emulator for Linux, written in C++23. The project is currently in early development, targeting a public beta in July 2026.

Our goal is to accurately emulate the Wii U's PowerPC CPU and GPU in order to run Wii U software. What sets WEMU apart from existing solutions like Cemu is a companion feature: **use your phone as the Wii U GamePad**. A web application streams the GamePad screen from the emulator to your phone and sends touch inputs back, replacing the physical GamePad entirely.

---

## Current State

WEMU is not yet playable. The following components are in active development:

| Component | Status |
|---|---|
| Big-endian ELF binary loader (with ZLIB section support) | Done |
| PowerPC interpreter (ADD family of instructions) | In progress |
| Vulkan rendering pipeline (GLFW) | In progress |
| Qt6/QML launcher UI | In progress |
| USB/gamepad input (SDL2) | Done |
| RPX file format support | Planned |
| Wii U title library browser | Planned |
| Phone-as-GamePad web app | Planned |

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

**Platform:** Linux only.

### Dependencies

```bash
# Debian/Ubuntu
sudo apt install cmake g++ zlib1g-dev libvulkan-dev libglfw3-dev libsdl2-dev \
     qt6-base-dev qt6-declarative-dev glslang-tools
```

### Build

```bash
git clone https://github.com/Robouch0/WEMU.git
cd WEMU

# Core emulator + Qt GUI
mkdir cmake-build-debug && cd cmake-build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Vulkan renderer (standalone)
cd ../vulkan && mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Running Tests

```bash
cd core && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest

# Run a specific test
./wemu_tests --gtest_filter=InstructionTest.ADD_NoOE_NoRc
```

---

## Architecture Overview

```
WEMU/
├── core/           # PowerPC interpreter, ELF loader, CPU state
│   ├── src/binary/ # ELF loader + big-endian decoder
│   ├── src/cpu/    # Interpreter, registers, instruction implementations
│   │   └── tables/ # X-macro tables: cpu_instructions.anh, cpu_fields.anh
│   └── tests/      # GoogleTest unit tests
├── gui/            # Qt6/QML launcher and input management
│   └── src/input/  # IInputDevice, KeyboardInput, SDLGamepadInput
├── vulkan/         # Vulkan/GLFW rendering engine
└── libs/
    ├── cpu_IR/     # CPU intermediate representation interface
    └── gfx_IR/     # GPU intermediate representation interface
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
