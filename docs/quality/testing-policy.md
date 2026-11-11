# Testing Policy

This document defines the testing strategy, coverage requirements, quality gates, and testing conventions for WEMU.

---

## 1. Critical Path

The critical path encompasses the core emulator stages required to boot, execute, and present a Wii U application. A regression in any stage halts emulator execution or causes severe behavioral failure.

| Stage | Subsystem / Files | Why Critical |
|---|---|---|
| **1. Game Loading** | `core/src/binary/Loader.cpp` | Parses ELF and RPX headers, unpacks zlib-compressed sections, and loads program segments into guest memory. If loading fails or corrupts segments, no title can launch. |
| **2. PowerPC Execution** | `core/src/cpu/interpreter/`, `core/src/cpu/jit/` | Executes 32-bit PowerPC 750CL / Espresso instructions via the interpreter or x64 JIT. Instruction encoding, flag setting (`CR`, `XER`), and register arithmetic must be exact for game logic and branching. |
| **3. OS Emulation (HLE)** | `core/src/hle/Coreinit.cpp`, `CoreinitExtra.cpp`, `Fs.cpp`, `MemHeap.cpp` | Implements Wii U operating system syscalls, including thread scheduling, synchronization primitives, memory allocation, and filesystem operations. Incomplete or incorrect syscall behavior leads to deadlocks or crashes. |
| **4. GX2 Command Processing** | `core/src/hle/Gx2.cpp`, `core/src/gfx/Gx2Replayer.cpp` | Intercepts Wii U GX2 graphics API calls, validates surface formats, and queues draw calls and state updates into replayer structures. |
| **5. Shader Translation** | `core/src/gfx/latte/`, `core/src/gfx/vulkan/` | Translates AMD Latte (R700) GPU bytecode into SPIR-V shaders and manages Vulkan pipelines. Instruction translation bugs produce visual corruption or GPU device crashes. |
| **6. GUI & Presentation** | `gui/src/emulator/`, `core/src/video/` | Manages surface swapchains, frame pacing, audio mixing, user input forwarding, and presentation to the user window. |

### Coverage Target for Critical Path
- **Target:** $\ge 80\%$ line coverage on all critical-path files; $\ge 70\%$ line coverage across the overall WEMU codebase.
- **Scope Distinction:** Hardware initialization and window surface routines (`gfx/init`, `GpuRenderGraph`, `GpuQuadRasterizer`) require an active GPU display surface. These components are validated by the integration test suite rather than headless unit tests.

---

## 2. Testing Strategy

WEMU employs a layered testing strategy balancing fast local feedback, automated pull request validation, and end-to-end functional verification.

```
       +---------------------------------------------+
       |             Manual Testing                  |
       |  (GUI viewer, GamePad, retail title checks) |
       +---------------------------------------------+
       |         RPX Integration Test Suite          |
       |  (13 scenes, probes, determinism, baselines)|
       +---------------------------------------------+
       |               GUI Tests                     |
       |   (12 Qt/lifecycle/protocol test programs)  |
       +---------------------------------------------+
       |            Core Unit Tests                  |
       |     (~1,215 GoogleTest cases, CI gates)     |
       +---------------------------------------------+
```

| Level | Scope | Framework / Tool | Automation | Rationale & Scope |
|---|---|---|---|---|
| **Core Unit Tests** | CPU instructions, registers, memory maps, JIT blocks, math utilities | GoogleTest | Automated (CI + local `ctest`) | ~1,215 test cases across 12 areas (`arithmetic`, `binary`, `branch`, `float`, `interpreter`, `jit`, `load`, `logical`, `shift`, `spr`, `store`, `system`). Deterministic; the whole binary takes about 2 minutes in a Debug coverage build, most of it in `system`. |
| **GUI Tests** | Remote protocol, launch environment, profile manager, title scanner, preferences, session lifecycle | Qt Test / Custom runners | Automated (CI + local `ctest`) | 12 test binaries verifying GUI configuration, state transitions, and background process management using headless Vulkan (lavapipe). |
| **Functional / Integration** | Full pipeline: Loader $\rightarrow$ CPU $\rightarrow$ HLE $\rightarrow$ GX2 $\rightarrow$ Vulkan $\rightarrow$ Framebuffer | Homebrew RPX suite (`tests/rpx-suite/`) | Automated locally (`run.py`), manual in CI | Exercises 13 scenes under retail-like workloads. Evaluates pixel color probes, multi-core determinism, frame pacing, and memory stability against baselines. |
| **Manual Verification** | Interactive viewer, visual inspection, commercial compatibility | WEMU GUI (`WEMU_FRAME_PROFILE=1`) | Manual | Verifies interactive GamePad input, UI styling, and real-world audio/visual feel. Direct hardware comparison with a retail Wii U cannot be automated in CI; CPU-calculated probe math is used instead. |

---

## 3. Code Coverage

### Current Coverage Figures (Verified 2026-11-06)

Measurements obtained locally with `gcov-14` / `lcov` on `core` unit tests:
- **WEMU Core Code:** **69.1% line coverage** / **66.4% function coverage**
- **With Vendored Third-Party Code:** **61.0% line coverage** (includes `stb_image.h`)

#### Line Coverage by Subsystem

| Subsystem / Area | Line Coverage | Status / Assessment |
|---|---|---|
| `cpu/memory` | 97% | Excellent |
| `ui` | 96% | Excellent |
| `cpu/jit` | 93% | Excellent |
| `cpu/instructions` | 93% | Excellent |
| `audio` | 90% | High |
| `video` | 82% | High |
| `gfx` | 71% | Meets target |
| `hle` | 69% | Near target |
| `binary` (ELF/RPX loader) | 67% (Loader.cpp 75% on 2026-11-12) | Below target |
| `cpu/interpreter` | 62% | Below target |
| `cpu/recompiler` | 47% | Under development |
| `cpu/tables` | 32% | Low (mostly static instruction tables) |
| `utils` | 22% | Low (helpers tested through callers) |
| `gfx/init`, `GpuRenderGraph`, `GpuQuadRasterizer` | 0% | Requires live GPU/window; validated by RPX suite |

#### Weak Critical-Path Files

The following critical-path files are currently below the $\ge 80\%$ line coverage target:

| File | Line Coverage | Primary Missing Coverage |
|---|---|---|
| `core/src/hle/Coreinit.cpp` | 56% | Unimplemented or rarely-called thread/heap management APIs |
| `core/src/hle/CoreinitExtra.cpp` | 56% | Extended system configuration and peripheral queries |
| `core/src/hle/Fs.cpp` | 58% | Asynchronous file I/O operations and error edge cases |
| `core/src/cpu/interpreter/Interpreter.cpp` | 61% | Almost all misses are in `Interpreter::run()` (JIT/native dispatch, profiling, thread switching), which needs a full scheduler and memory setup to test |
| `core/src/binary/Loader.cpp` | 75% (was 65%) | Relocation and import-resolution branches used only by specific titles |
| `core/src/gfx/Gx2Replayer.cpp` | 65% | Rare surface tiling layouts and unsupported texture formats |
| `core/src/hle/Gx2.cpp` | 68% | Secondary buffer size calculation variants and mip surface copies |

#### Update 2026-11-12
After the loader, Fs, coreinit, run-loop, GX2 tests and the removal of game-specific code: **74.8%** line coverage of WEMU core code, 1,286 tests.

| Critical-path file | 2026-11-06 | 2026-11-12 |
|---|---|---|
| `binary/Loader.cpp` | 64.7% | 98.6% |
| `hle/Fs.cpp` | 58.3% | 92.7% |
| `hle/Coreinit.cpp` | 56.0% | 81.3% |
| `hle/CoreinitExtra.cpp` | 56.4% | 91.7% |
| `cpu/interpreter/Interpreter.cpp` | 60.3% | 85.3% |
| `hle/Gx2.cpp` | 68.5% | 81.3% |
| `gfx/Gx2Replayer.cpp` | 65.2% | 65.2% (next) |

WEMU contains no game-specific patches or addresses (tech-debt item 15): every title runs the same code.

### Reproducing Coverage Locally

To generate coverage reports without exceeding memory limits on 16 GB machines:

```bash
# 1. Configure with coverage enabled and LLVM disabled
cmake -S core -B build-cov -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON -DWEMU_ENABLE_LLVM=OFF -DCMAKE_CXX_COMPILER=g++-14

# 2. Build with limited concurrency (maximum 3 parallel jobs to avoid memory exhaustion)
cmake --build build-cov -j3

# 3. Run unit tests with headless Vulkan enabled
WEMU_TEST_VULKAN=1 ctest --test-dir build-cov --output-on-failure

# 4. Capture coverage data and generate HTML
lcov --gcov-tool gcov-14 --capture --directory build-cov --output-file build-cov/coverage.info
lcov --remove build-cov/coverage.info '/usr/*' '*/tests/*' '*/external/*' --output-file build-cov/coverage_clean.info
genhtml build-cov/coverage_clean.info --output-directory build-cov/coverage_html
```

> **Note:** Coverage reports are not currently published by CI workflows by team consensus.

---

## 4. Input Validation and Resilience

### Input validation
Game files are the main untrusted input. `core/src/binary/Loader.cpp` validates every RPX/ELF before anything is mapped into guest memory:

- ELF identity: magic, 32-bit, big-endian, `EM_PPC`, a usable section table, and a valid `e_shstrndx`.
- Bounds: every section offset and size must fit in the file. Section names must lie inside `.shstrtab`. Symbol tables need a real `sh_entsize`. Import thunks must land inside their section.
- Allocation limits: sections, including inflated `SHF_DEFLATED` ones, are capped at 512 MiB before any buffer is sized.
- Errors: a failure raises `Core::LoaderException` with the file path and the reason (for example "truncated or malformed file", "corrupt compressed section ... (zlib error N)"). The loader never silently drops data.

### Resilience scenarios
| # | Scenario | Expected behaviour | Test |
|---|---|---|---|
| 1 | A truncated, corrupted or hostile RPX is opened | The load stops with a `LoaderException` naming the file. No crash, no huge allocation, no out-of-bounds read. Valid titles still load. | `core/tests/binary/test_loader_validation.cpp`: 19 tests, including a truncation sweep, a seeded 40-round corruption sweep and an opt-in real-title check (`WEMU_TEST_RPX_DIR=games`) |
| 2 | A secondary guest thread faults (bad memory access) | Only that thread is killed and logged (`[EMU] thread ... killed by fault ...; continuing boot`). The session keeps running and the scheduler returns to the main thread. | `SchedulerTest.SecondaryThreadFaultDoesNotReportWholeSessionStopped` (`core/tests/system/test_scheduler.cpp`) |
| 3 | Sustained load for 3,600 frames | No crash; frame-time drift, stutters and memory growth are measured and reported as warnings | RPX suite `endurance` scene (`tests/rpx-suite/run.py`) |

---

## 5. Team Conventions & Quality Gates

### Test Location and Auto-Discovery
- Core unit tests reside in subdirectories of `core/tests/<area>/test_*.cpp`.
- `core/tests/CMakeLists.txt` uses `file(GLOB_RECURSE TEST_SOURCES "*.cpp")`. New test `.cpp` files are automatically compiled and linked without manual CMake updates.
- GUI unit tests reside in `gui/tests/*Test.cpp`.
- For detailed setup and fixture usage, refer to [writing-unit-tests.md](../contributing/writing-unit-tests.md).

### Test Structure & Naming Conventions
- Test files must follow the pattern `test_<instruction_or_module>.cpp`.
- Test cases use GoogleTest syntax:
  - Instruction tests use the shared fixture: `TEST_F(InstructionTest, SCENARIO_NAME)`
  - Standalone tests use: `TEST(SuiteName, BehaviourName)`
- Test names must clearly describe the scenario under test (e.g., `ADDO_WithOverflow_SetsOVandSO`, `ADD_NoOE_NoRc`).

### Mandatory Rules for Pull Requests
1. **Regression Test on Bug Fix:** Every bug fix PR must include a unit test or RPX probe reproducing the failure before the fix and passing after.
2. **New Instructions & Syscalls:** Every newly implemented PowerPC opcode or HLE syscall must include unit tests covering standard values, edge cases, and flag side effects.
3. **CI Pass Required:** No code may merge into `dev` or `main` without passing all GitHub Actions checks. The rulesets enforce reviews (below); see [tech-debt.md](tech-debt.md) item 14 for which checks are required.

### Branch Protection & Review Policies
- **`dev` Branch:** Requires at least **1 approved review** from a maintainer and passing CI checks.
- **`main` Branch:** Requires at least **2 approved reviews** and passing CI checks.
- **Review History (as of 2026-11-05):** 111 merged PRs, 107 with recorded reviews, 204 total reviews submitted, 15 "changes requested" outcomes.

### Continuous Integration Pipeline (.github/workflows/wemu-checks.yml)
The CI pipeline runs on every push and PR targeting `dev` or `main`:

| Job Name | Platform / Environment | Scope |
|---|---|---|
| **Full Build (core + gui)** | Ubuntu 26.04 (GCC 14) | Full release build; executes core and GUI tests using headless Vulkan (lavapipe). |
| **Unit Tests** | Ubuntu 26.04 (LLVM OFF) & Ubuntu 24.04 (LLVM ON, LLVM 19) | Standalone core build and test execution across compiler/LLVM matrix. |
| **macOS arm64** | macOS 15 (Ninja, LLVM ON/OFF) | Cross-platform build and unit tests with MoltenVK. |
| **clang-format** | Ubuntu 24.04 (clang-format 19) | Validates code formatting across `core/src` and `core/tests` (`--dry-run --Werror`). |
| **clang-tidy** | Ubuntu 24.04 (clang-tidy 19, GCC 14) | Static analysis on `core/src` with warnings as errors (`-warnings-as-errors='*'`). |

#### Clang-Tidy Configuration
Defined in `core/.clang-tidy`:
- **Active Checks:** `clang-diagnostic-*`, `clang-analyzer-*`, `bugprone-*`, `performance-*`
- **Disabled Checks:** `-bugprone-easily-swappable-parameters`, `-bugprone-macro-parentheses`, `-performance-avoid-endl`
- **Header Filter:** Excludes vendored headers (`stb_image.h`).
- **Scope Limitation:** Static analysis and formatting checks currently cover `core/`, but do not yet analyze `gui/`.

#### Historical CI Reliability
Over 200 recorded runs (2026-04-16 to 2026-11-05):
- **140 successful runs** (70%)
- **60 failed runs** (30%, primarily lint violations and headless Vulkan runner timeouts)

---

## 6. How to Run Tests

For full build prerequisites, see the [Root README.md](../../README.md) and [writing-unit-tests.md](../contributing/writing-unit-tests.md).

### Quick Commands

```bash
# Run all unit tests from an existing build directory
ctest --test-dir build --output-on-failure

# Run core tests directly with filter
./build/core/tests/wemu_tests --gtest_filter=InstructionTest.ADD*

# Run GUI tests with headless Vulkan (Lavapipe)
export WEMU_TEST_VULKAN=1
export VK_DRIVER_FILES="/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
ctest --test-dir build -R ".*Test.*" --output-on-failure

# Run the RPX integration suite (see tests/rpx-suite/README.md)
tests/rpx-suite/run.py --baseline tests/rpx-suite/baselines/$(hostname).json
```
