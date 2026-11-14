# Testing Policy

This document defines the testing strategy, coverage requirements, quality gates, and testing conventions for WEMU.

---

## 1. Critical Path

The critical path encompasses the core emulator stages required to boot, execute, and present a Wii U application. A regression in any stage halts emulator execution or causes severe behavioral failure.

| Stage | Subsystem / Files | Why Critical |
|---|---|---|
| **1. Game Loading** | `core/src/binary/Loader.cpp` | Parses ELF and RPX headers, unpacks zlib-compressed sections, and loads program segments into guest memory. If loading fails or corrupts segments, no title can launch. |
| **2. PowerPC Execution** | `core/src/cpu/interpreter/`, `core/src/cpu/jit/`, `core/src/cpu/instructions/`, `core/src/cpu/memory/` | Executes 32-bit PowerPC 750CL / Espresso instructions via the interpreter or x64 JIT. Instruction encoding, flag setting (`CR`, `XER`), and register arithmetic must be exact for game logic and branching. |
| **3. OS Emulation (HLE)** | `core/src/hle/Coreinit.cpp`, `CoreinitExtra.cpp`, `Fs.cpp`, `MemHeap.cpp` | Implements Wii U operating system syscalls, including thread scheduling, synchronization primitives, memory allocation, and filesystem operations. Incomplete or incorrect syscall behavior leads to deadlocks or crashes. |
| **4. GX2 Command Processing** | `core/src/hle/Gx2.cpp`, `core/src/gfx/Gx2Replayer.cpp` | Intercepts Wii U GX2 graphics API calls, validates surface formats, and queues draw calls and state updates into replayer structures. |
| **5. Shader Translation** | `core/src/gfx/Latte*.cpp`, `SpirvCompiler.cpp`, `VulkanRasterBackend.cpp` | Translates AMD Latte (R700) GPU bytecode into SPIR-V shaders and manages Vulkan pipelines. Instruction translation bugs produce visual corruption or GPU device crashes. |
| **6. GUI & Presentation** | `gui/src/emulator/`, `core/src/video/` | Manages surface swapchains, frame pacing, audio mixing, user input forwarding, and presentation to the user window. |

### Coverage Target for Critical Path
- **Target:** $\ge 80\%$ line coverage on all critical-path files; $\ge 70\%$ line coverage across the overall WEMU codebase.
- **Why 80% on the critical path:** every stage there stops a title from booting or rendering when it breaks, and most of it is deterministic code (instruction semantics, loader parsing, syscalls) that unit tests reach cheaply. The last 20% is mostly error paths for hardware states a headless runner cannot produce, which the RPX suite and manual runs cover instead.
- **Why 70% overall:** the rest includes debugging tools, the experimental LLVM recompiler and hardware bring-up code, where a test would mostly freeze behaviour that is still changing.
- **Scope Distinction:** Window and device initialisation (`gfx/init`) requires an active display surface. It is excluded from the figure (see [Coverage exclusions](#coverage-exclusions)) and validated by running WEMU on a desktop.

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
       |     (~1,305 GoogleTest cases, CI gates)     |
       +---------------------------------------------+
```

| Level | Scope | Framework / Tool | Automation | Rationale & Scope |
|---|---|---|---|---|
| **Core Unit Tests** | CPU instructions, registers, memory maps, JIT blocks, math utilities | GoogleTest | Automated (CI + local `ctest`) | ~1,305 test cases across 12 areas (`arithmetic`, `binary`, `branch`, `float`, `interpreter`, `jit`, `load`, `logical`, `shift`, `spr`, `store`, `system`). Deterministic; the whole binary takes about 2 minutes in a Debug coverage build, most of it in `system`. |
| **GUI Tests** | Remote protocol, launch environment, profile manager, title scanner, preferences, session lifecycle | Qt Test / Custom runners | Automated (CI + local `ctest`) | 12 test binaries verifying GUI configuration, state transitions, and background process management using headless Vulkan (lavapipe). |
| **Functional / Integration** | Full pipeline: Loader $\rightarrow$ CPU $\rightarrow$ HLE $\rightarrow$ GX2 $\rightarrow$ Vulkan $\rightarrow$ Framebuffer | Homebrew RPX suite (`tests/rpx-suite/`) | Automated locally (`run.py`), manual in CI | Exercises 13 scenes under retail-like workloads. Evaluates pixel color probes, multi-core determinism, frame pacing, and memory stability against baselines. |
| **Manual Verification** | Interactive viewer, visual inspection, commercial compatibility | WEMU GUI (`WEMU_FRAME_PROFILE=1`) | Manual | Verifies interactive GamePad input, UI styling, and real-world audio/visual feel. Direct hardware comparison with a retail Wii U cannot be automated in CI; CPU-calculated probe math is used instead. |


### Functional suite (RPX)

`tests/rpx-suite/` is a homebrew title that loads like a retail game and runs 13 scenes through real GX2/Latte shaders. Commands and scene details are in [its README](../../tests/rpx-suite/README.md).

- **When to run it:** before merging a change to `core/src/gfx/`, `core/src/hle/Gx2.cpp` or the CPU back ends, and after any change meant to improve performance. It is not run in CI: it needs a GPU and a Wii U toolchain.
- **Verdict:** `run.py` ends with `PASS`, `FAIL` (a check failed that is not a known failure, or a run did not finish) or `SLOWER` (a scene is slower than this machine's baseline). The exit code is 0 only for `PASS`. A pull request reports the verdict line.
- **Known failures:** `tests/rpx-suite/expectations.json` lists the probes that fail because of an open bug, each pointing to its entry in [regressions.md](regressions.md). A fix that makes one pass is reported as `fixed`, and the same pull request removes the entry and closes the regression. A new bug found by the suite gets a `REG-` entry before it is added there.
- **Baseline:** one per machine (`tests/rpx-suite/run.py --save-baseline`, kept out of git), recorded on AC power. Runs on battery, in a power-saving profile or on a busy host still check correctness but report "performance not judged".
- **Noise:** the best of 3 runs counts, and a scene is only reported slower or faster when its runs do not overlap the baseline's runs. GPU-light scenes are often too noisy to judge on a laptop (see [tech-debt.md](tech-debt.md) item 13).
- **From the GUI:** start `WEMU_FRAME_PROFILE=1 build/gui/appgui`, open "WEMU RPX Test Suite", press − (keyboard M) for the full sequence, then `tests/rpx-suite/run.py --analyze-last`. This measures pacing at 60 Hz; probes are checked on screen only.

---

## 3. Code Coverage

### Current Coverage Figures (Verified 2026-11-15)

Measured locally with `gcov-14` / `lcov` on the `core` unit tests (1,305 tests, commit `d9502bd`), with the [exclusions](#coverage-exclusions) below. Per-file figures: [analysis/coverage-2026-11-15.txt](analysis/coverage-2026-11-15.txt).

| Scope | Lines | Functions | Target |
|---|---|---|---|
| **Critical path** (stages 1–6 of §1, core side) | **82.5%** (16,240 of 19,675) | **88.3%** | $\ge 80\%$: met |
| **All WEMU core code** | **82.6%** (20,310 of 24,603) | **80.8%** | $\ge 70\%$: met |

The critical path is 80% of the measured core code. Stage 6 is counted for `core/src/video/` only: the GUI side (`gui/src/emulator/`) is tested by the GUI test binaries, whose coverage is not measured ([tech-debt.md](tech-debt.md) item 12).

#### Critical Path by Stage

| Stage | Lines | Functions |
|---|---|---|
| 1. Game loading (`binary/Loader.cpp`) | 98.6% | 100% |
| 2. PowerPC execution (`cpu/interpreter`, `jit`, `instructions`, `memory`) | 95.0% | 98.8% |
| 3. OS emulation (`Coreinit`, `CoreinitExtra`, `Fs`, `MemHeap`) | 88.6% | 76.2% |
| 4. GX2 command processing (`hle/Gx2.cpp`, `gfx/Gx2Replayer.cpp`) | 68.7% | 81.0% |
| 5. Shader translation and Vulkan (`Latte*`, `SpirvCompiler`, `VulkanRasterBackend`) | 87.5% | 93.9% |
| 6. Presentation, core side (`video/`) | 81.8% | 95.7% |

#### Critical-Path Files Below 80%

| File | Lines | What is not covered | Checked instead by |
|---|---|---|---|
| `gfx/Gx2Replayer.cpp` | 65.7% of 5,612 | The software rasteriser's rarely used paths: tiling layouts, stream-out and texture formats no test title uses yet | The RPX suite (texture, shadow, post-processing and stream-out scenes) |
| `gfx/SpirvCompiler.cpp` | 75.7% | System-call failures around the shader compiler subprocess (spawn, pipe, reap, 15 s deadline) and the on-disk SPIR-V cache's I/O errors | Each failure throws or skips the cache with a message; a working machine never takes these paths |
| `cpu/jit/X64Jit.cpp` | 76.6% | Debugging switches (`WEMU_JIT_CALLOUT` family masks, the `WEMU_JIT_PERF_MAP` symbol map, the end-of-run report) and the block-compile failure path | A failed block falls back to the interpreter, which is covered |
| `video/H264Sps.hpp` | 69.7% | Malformed-header errors and H.264 header fields (scaling lists, picture order type 1) the tested streams do not use | Menu movies in the GUI; a malformed header is rejected, not decoded |

#### History

| Date | All core code (lines) | What changed |
|---|---|---|
| 2026-11-06 | 69.1% | First measurement (61.0% with `stb_image.h`) |
| 2026-11-12 | 82.5% | Loader validation, Fs, coreinit, run loop, GX2, JIT callout, diagnostics, AX and heap tests; game-specific and legacy GPU code removed |
| 2026-11-15 | 82.6% | Calloc overflow and symbol-index fixes, audio and sync tests |

| Critical-path file | 2026-11-06 | 2026-11-12 | 2026-11-15 |
|---|---|---|---|
| `binary/Loader.cpp` | 64.7% | 98.6% | 98.6% |
| `hle/Fs.cpp` | 58.3% | 92.7% | 92.7% |
| `hle/Coreinit.cpp` | 56.0% | 81.3% | 82.4% |
| `hle/CoreinitExtra.cpp` | 56.4% | 91.7% | 92.4% |
| `cpu/interpreter/Interpreter.cpp` | 60.3% | 85.3% | 88.8% |
| `hle/Gx2.cpp` | 68.5% | 81.3% | 81.4% |
| `gfx/Gx2Replayer.cpp` | 65.2% | 65.2% | 65.7% |

WEMU contains no game-specific patches or addresses (tech-debt item 15): every title runs the same code.

### Reproducing Coverage Locally

From the repository root. Keep the build at `-j3` or lower: more parallel jobs exhaust memory on a 16 GB machine.

```bash
# 1. Configure once: Debug, coverage on, LLVM off, GCC 14
cmake -S core -B build-cov -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON -DWEMU_ENABLE_LLVM=OFF -DCMAKE_CXX_COMPILER=g++-14

# 2. Build the tests
cmake --build build-cov --target wemu_tests -j3

# 3. Reset the counters and run every test (about 5 minutes)
find build-cov -name '*.gcda' -delete
WEMU_TEST_VULKAN=1 ./build-cov/tests/wemu_tests --gtest_brief=1

# 4. Collect, keep core/src only, apply the exclusions below
lcov --gcov-tool gcov-14 --capture --directory build-cov -o build-cov/cov.info --ignore-errors inconsistent,corrupt,mismatch,negative,unused -q
lcov --gcov-tool gcov-14 --extract build-cov/cov.info "$PWD/core/src/*" -o build-cov/cov-core.info --ignore-errors inconsistent,unused -q
lcov --gcov-tool gcov-14 --remove build-cov/cov-core.info '*stb_image*' '*/core/src/gfx/init/*' -o build-cov/cov-wemu.info --ignore-errors inconsistent,unused -q

# 5. Summary and HTML report
lcov --summary build-cov/cov-wemu.info --ignore-errors inconsistent
genhtml build-cov/cov-wemu.info -o build-cov/coverage_report --ignore-errors inconsistent,corrupt -q
xdg-open build-cov/coverage_report/index.html
```

`gcov-14` must match the compiler: the default `gcov` from GCC 15 cannot read GCC 14 data. The `make coverage` / `make coverage-html` targets in `core/CMakeLists.txt` apply the same exclusions and pick the matching `gcov` automatically.

#### Coverage exclusions
Only code that unit tests cannot reach is excluded, and every exclusion is listed here.

| Excluded | Why | How it is checked instead |
|---|---|---|
| `core/src/gfx/init/` | The standalone SDL window and Vulkan device setup (instance, surface, swap chain, device selection). It needs a real window and display, which a headless test machine does not have. The GUI does not use it: it presents through the session protocol. | Running WEMU on a desktop; the GUI path is covered by the session tests and the RPX suite. |
| `stb_image.h` | Vendored third-party image decoder. | Upstream. |
| `core/tests/` | Test code itself. | — |

Not excluded on purpose: `cpu/recompiler/` (compiled only with LLVM; measure it with `-DWEMU_ENABLE_LLVM=ON`) and log-call template instances in `utils/Logger.hpp`, which lcov 2.0 counts as separate functions. Per source function, 85% of functions are covered (2026-11-12).

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
- **`dev` Branch:** Requires at least **1 approved review** and the `Full Build (core + gui)` check (full build plus core and GUI tests) to pass.
- **`main` Branch:** Requires at least **2 approved reviews** and the `check-source-branch` check. Changes reach `main` from `dev`, where the build and tests are already required.
- **Review History (as of 2026-11-12):** 119 PRs, 111 merged, 107 with recorded reviews, 204 reviews, 13 PRs with a "changes requested" review.
- **Reviews that changed the code:** four documented cases in [code-review.md](code-review.md).

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
tests/rpx-suite/run.py   # compares with baselines/<host>-<cpu>.json when it exists
```
