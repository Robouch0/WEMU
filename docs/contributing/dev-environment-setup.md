# Dev Environment Setup

This guide covers everything needed to build WEMU from scratch on Linux.

> **Platform:** Linux only. Windows and macOS are not supported.

---

## Compiler requirements

| Sub-project | C++ standard | Minimum compiler |
|---|---|---|
| `core` (standalone) | C++23 | GCC 13 / Clang 17 |
| `core` (via root build) | C++23 | GCC 13 / Clang 17 |
| `gui` | C++20 | GCC 11 / Clang 14 |
| `vulkan` | C++17 | GCC 9 / Clang 10 |

`core` uses `std::float32_t` (C++23). Make sure your compiler supports it. On Fedora 38+ and Ubuntu 24.04+, the default system GCC is recent enough. On Ubuntu 22.04, you may need to install `gcc-13` manually.

---

## Installing dependencies

### Fedora

```bash
sudo dnf install -y \
  cmake \
  gcc-c++ \
  zlib-devel \
  qt6-qtbase-devel \
  qt6-qtdeclarative-devel \
  qt6-qtquickcontrols2-devel \
  SDL2-devel \
  vulkan-devel \
  vulkan-validation-layers \
  glfw-devel \
  shaderc \
  pkgconf-pkg-config \
  clang-tools-extra
```

### Ubuntu / Debian

```bash
sudo apt install -y \
  cmake \
  g++ \
  zlib1g-dev \
  qt6-base-dev \
  qt6-declarative-dev \
  libqt6quickcontrols2-6 \
  libsdl2-dev \
  libvulkan-dev \
  libglfw3-dev \
  glslang-tools \
  pkg-config \
  clang-format \
  clang-tidy
```

> On Ubuntu 22.04, Qt 6.6.2 is not available in the default apt repositories. Install it via the [Qt online installer](https://www.qt.io/download-qt-installer) or build from source. Ubuntu 24.04 ships Qt 6.4+ which may require a manual Qt 6.6 install as well.

### Arch Linux

```bash
sudo pacman -S \
  cmake \
  gcc \
  zlib \
  qt6-base \
  qt6-declarative \
  qt6-quickcontrols2 \
  sdl2 \
  vulkan-devel \
  glfw \
  shaderc \
  pkgconf \
  clang
```

---

## Building

### Option A — Full build (core + gui + IR libs)

Builds everything except the Vulkan renderer, from the root of the repository.

```bash
mkdir cmake-build-debug && cd cmake-build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

Produced binaries:
- `cmake-build-debug/core/wemu` — the emulator CLI
- `cmake-build-debug/gui/appgui` — the Qt launcher

> GoogleTest is fetched automatically from GitHub during the first CMake run. An internet connection is required.

---

### Option B — Core only (with tests)

Use this when working exclusively on the emulator engine or tests. The core has its own standalone `CMakeLists.txt` that sets C++23 and includes the test suite.

```bash
cd core
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

Produced binaries:
- `core/build/wemu` — the emulator CLI
- `core/build/tests/wemu_tests` — the GoogleTest binary

---

### Option C — Vulkan renderer (standalone)

The Vulkan renderer has its own build, independent from the rest of the project. Shaders are compiled automatically during the build via `glslc`.

```bash
cd vulkan
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Produced binary: `vulkan/build/WEMU`

> The CMake build calls `vulkan/src/shaders/compile.sh`, which runs `glslc` on the GLSL source files and outputs `.spv` files. Make sure `glslc` (from `shaderc`) is in your `PATH`.

---

## Running the emulator

```bash
# From core/build/
./wemu path/to/binary.elf
```

The emulator currently prints the ELF header, symbol table, and an instruction trace to stdout.

---

## Running tests

```bash
# From core/build/
ctest                          # run all tests
ctest --output-on-failure      # show output only on failure

# Run a specific test or test group directly
./tests/wemu_tests --gtest_filter=InstructionTest.*
./tests/wemu_tests --gtest_filter=InstructionTest.ADD_NoOE_NoRc
```

---

## CLion setup

The repository includes `.idea/` configuration files. To open the project in CLion:

1. **File → Open** → select the root `WEMU/` directory (for the full build) or `WEMU/core/` (for core only)
2. CLion will detect the `CMakeLists.txt` and offer to load the CMake project — click **Load CMake Project**
3. Set the CMake build directory to `cmake-build-debug` to match the existing configuration

For the `vulkan/` sub-project, open it separately as its own CMake project.

---

## Code style

The `core/` sub-project enforces formatting via clang-format and static analysis via clang-tidy. Both are checked in CI on every pull request.

### clang-format

Configuration: `core/.clang-format` (LLVM-based style).

Key settings:
- **Indent:** 4 spaces
- **Column limit:** 120
- **Brace style:** custom — opening brace on same line for control flow, new line after functions
- **Include order:** system headers (`<...>`) first, then project headers (`"..."`)

Auto-fix a single file:

```bash
clang-format -i core/src/cpu/instructions/MyInstruction.cpp
```

Auto-fix all source files in `core/`:

```bash
find core/src core/tests \( -name '*.cpp' -o -name '*.hpp' \) | xargs clang-format -i
```

Check only (exits non-zero if any file is not clean, no changes written):

```bash
find core/src core/tests \( -name '*.cpp' -o -name '*.hpp' \) | xargs clang-format --dry-run --Werror
```

### clang-tidy

Configuration: `core/.clang-tidy`. Requires a `compile_commands.json` generated by CMake.

```bash
# Step 1 — generate compile_commands.json (only needed once, or after CMakeLists changes)
cmake -S core -B core/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Step 2 — run clang-tidy on all core sources
find core/src -name '*.cpp' | xargs clang-tidy -p core/build/
```

Run both commands from the repository root (`WEMU/`).
