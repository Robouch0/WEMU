# Architecture Overview & Data Flow

This document explains how WEMU's sub-projects fit together, what the intended end-to-end data flow is, and what is already implemented versus what is still planned.

---

## Repository layout

```
WEMU/
├── core/           # PowerPC interpreter, ELF/RPX loader, CPU state — the heart of the emulator
├── gui/            # Qt6/QML launcher and input management
├── vulkan/         # Vulkan/GLFW rendering engine (standalone)
├── libs/
│   ├── cpu_IR/     # CPU intermediate representation interface
│   └── gfx_IR/     # GPU intermediate representation interface
└── docs/           # Contributor documentation
```

The root `CMakeLists.txt` builds `core`, `gui`, `libs/cpu_IR`, and `libs/gfx_IR` together. `vulkan/` has its own independent build.

---

## Sub-project roles

### `core/` — The emulator engine

This is where the actual emulation happens. It is a standalone C++ library (`core`) and executable (`wemu`).

**Responsibilities:**
- Parse Wii U binary files (ELF/RPX format)
- Hold CPU state (registers, condition codes, etc.)
- Fetch, decode, and execute PowerPC instructions

**Entry point:** `core/src/main.cpp` — takes a binary file path as argument, loads it, and runs the interpreter.

---

### `libs/cpu_IR/` and `libs/gfx_IR/` — IR interfaces

These are static libraries that define a translation boundary between raw hardware emulation and higher-level processing.

- `cpu_IR` exposes `cpu_frontend::CpuTranslator::decodeInstruction(uint32_t)` → `IRInstruction`
- `gfx_IR` exposes `graphics_frontend::GfxTranslator::translateCommand(uint32_t)` → `GraphicsIR`

Their current implementations are stubs. Their intended role is to translate raw PowerPC instructions and GPU commands into a normalized IR that the rest of the system can consume without depending on the specifics of the hardware encoding.

---

### `vulkan/` — The renderer

A standalone `WemuEngineVulkan` class that wraps the full Vulkan pipeline: instance, surface (GLFW), physical/logical device, swap chain, render pass, graphics pipeline, framebuffers, vertex/index/uniform buffers, command buffers, and synchronization objects.

Shaders are written in GLSL and compiled to SPIR-V via `vulkan/src/shaders/compile.sh`, which is invoked automatically as a CMake custom target before the executable builds.

**Current status:** Works independently as a graphics sandbox. Not yet connected to the emulator core.

---

### `gui/` — The launcher

A Qt6 application using QML for the UI. Its responsibilities are:

- Display the launcher interface (intro sequence, main menu, settings)
- Manage input devices via `InputManager`, which polls registered `IInputDevice` implementations on a `QTimer`
- Current input backends: `KeyboardInput` (Qt key events) and `SDLGamepadInput` (SDL2 gamepad polling)

**Current status:** The launcher and input stack work independently. Input events are not yet forwarded to the emulator core.

---

## Current data flow

What actually runs end-to-end today:

```
ELF/RPX file (path argument)
        │
        ▼
  Core::Loader
  ─ reads the file via BeDecoder (big-endian byte swapping)
  ─ parses ELF header, section table, symbol table
  ─ decompresses SHF_DEFLATED sections with zlib
        │
        ▼
  Core::Binary
  ─ Elf32_Ehdr header
  ─ vector<Section>  (name, Elf32_Shdr, raw data)
  ─ vector<Symbol>   (name, Elf32_Sym)
        │
        ▼
  Core::Interpreter
  ─ reads 32-bit words from the .text section
  ─ byte-swaps each word (PowerPC is big-endian)
  ─ matches opcode → extended opcode in the dispatch map
  ─ calls the instruction's function pointer
  ─ prints each instruction name and address to stdout
        │
        ▼
  stdout (instruction trace)
```

---

## Intended data flow (planned)

The target architecture once all components are connected:

```
  RPX file
      │
      ▼
  Core::Loader ──► Core::Binary
                        │
                        ▼
                  Core::Interpreter
                  ┌───────┴───────────────┐
                  │                       │
                  ▼                       ▼
          cpu_frontend::          graphics_frontend::
          CpuTranslator           GfxTranslator
          (cpu_IR lib)            (gfx_IR lib)
                  │                       │
                  ▼                       ▼
          [higher-level         WemuEngineVulkan
           CPU execution]        (Vulkan renderer)
                                          │
                  ┌───────────────────────┘
                  │
                  ▼
         Display output
              ▲
              │
       InputManager (gui)
       ─ KeyboardInput
       ─ SDLGamepadInput
       ─ [future: phone GamePad via web app]
```

The IR libraries (`cpu_IR`, `gfx_IR`) are the planned boundary: the interpreter will emit IR rather than executing everything directly, which decouples the hardware-level decoding from the execution backend and renderer.

---

## Key internal flows

### Binary loading

```
BeDecoder(filepath)           // opens file, stores raw bytes
    │
    ├─ extractSwap<T>()       // reads T bytes, swaps endianness
    └─ extract<T>()           // reads T bytes, no swap (used for e_ident, raw data)

Loader::loadHeader()          // fills Elf32_Ehdr field by field
Loader::loadSections()        // for each section:
    ├─ seek to section header offset
    ├─ loadSectionHeader()    // fills Elf32_Shdr
    └─ loadSectionData()      // copies raw bytes
        or
       loadAndDecompressSectionData()  // if SHF_DEFLATED flag is set
Loader::loadSymbols()         // reads .symtab + resolves names from .strtab
```

### Instruction dispatch

```
Interpreter::initInstructionMap()     // at construction: groups INSTRUCTIONARRAY[]
                                      // entries by primary opcode into a map

Interpreter::run()                    // for each 32-bit word in .text:
    │
    ├─ EncodedInstruction(word)       // wrap raw word in the bit-field union
    └─ executeInstruction(instr)
           │
           ├─ findInstructionID()     // look up opcd in map, then check XO9/XO10/OE
           └─ INSTRUCTIONARRAY[id].function(*this, instr)  // call the handler
```

### Input polling (GUI)

```
InputManager (QObject)
    │
    ├─ QTimer (fires every tick)
    │       └─ updateAll()           // calls update() on each registered IInputDevice
    │
    └─ Q_INVOKABLE methods           // called from QML:
           isButtonPressed(name)
           getAxis(name)
           connectedDevices()
```

---

## What is implemented vs. planned

| Feature | Status |
|---|---|
| Big-endian ELF loader (header, sections, symbols) | Done |
| ZLIB section decompression (`SHF_DEFLATED`) | Done |
| PowerPC interpreter + dispatch system | In progress |
| ADD instruction family | Done |
| Other instruction families | Planned |
| RPX-specific metadata (`e_type == 0xFE01`) | Detected, not fully parsed |
| Wii U title library browser | Planned |
| CPU IR integration | Planned |
| GPU IR integration | Planned |
| Vulkan renderer connected to emulator | Planned |
| GUI launcher connected to emulator | Planned |
| Phone-as-GamePad web app | Planned |
