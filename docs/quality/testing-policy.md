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

Every input that comes from outside the process is checked before it is used. When it is invalid, WEMU refuses it and keeps running wherever that is possible.

| Input | Comes from | Checks | When it is invalid | Tests |
|---|---|---|---|---|
| **Game executable (RPX/ELF)** | The user's games folder | `core/src/binary/Loader.cpp`: ELF magic, 32-bit big-endian, `EM_PPC`, a usable section table and `e_shstrndx`; every offset and size inside the file; section names inside `.shstrtab`; symbol tables with a real `sh_entsize`; import thunks inside their section; sections (inflated `SHF_DEFLATED` ones too) capped at 512 MiB before any buffer is sized | The core stops with `LoaderException: <file>: <reason>` (exit 84). Nothing is mapped into guest memory | `core/tests/binary/test_loader_validation.cpp` (20 tests: truncation sweep, seeded 40-round corruption sweep, every rejection naming the file) |
| **Game files read by the title** | `content/` of the title, through `FSOpenFile` and friends | `core/src/hle/Fs.cpp`: guest paths are normalised (`..` removed) and must lie inside a mounted volume (`/vol/content`, `/vol/save`, the SD card); anything else maps to no host file. Missing files return `FS_STATUS_NOT_FOUND` (-6) | The title gets the error code a real Wii U returns and handles it itself | `core/tests/system/test_fs.cpp` (including `/vol/external01/../../…` being refused) |
| **Guest pointers and sizes in syscalls** | The title's own code | `Memory::read`/`write`/`hostPtr` check the address range; `calloc` rejects `n * size` overflow | Out-of-range access raises `MemoryException`; the faulting guest thread is killed (resilience scenario 2) | `core/tests/system/test_memory.cpp`, `test_libc_heap.cpp` |
| **Phone gamepad messages** | The local network (TCP) | `gui/src/remote/`: length-prefixed frames with a size limit; input samples need the exact size; pairing needs the 4-digit PIN, and 5 wrong PINs lock pairing for 30 s and change the PIN | A malformed frame drops that phone (`[REMOTE] GamePad disconnected (<reason>)`); a wrong PIN is answered `bad_pin` | `gui/tests/RemoteProtocolTest.cpp` |
| **Core-to-GUI session packets** | The core process (local socket) | `WemuSession::take`: lengths checked before any payload is allocated; frame size must match width × height | The GUI shows the error and stops the session | `gui/tests/SessionProtocolTest.cpp` |
| **Settings and title metadata** | `QSettings`, `meta.xml` of each title | Language, region and volume are range-checked; malformed region metadata falls back to the system region | The default value is used | `gui/tests/SessionLifecycleTest.cpp` (`SessionSettingsTest`), `TitleScannerTest.cpp` |

### Anticipated failure cases

What the system does, what the user sees, and what the logs say. Core log lines have the form `[seconds since start] [LEVEL] [SUBSYSTEM] message` (see [Logs](#logs)).

| # | Failure | What the system does | What the user sees | What is logged |
|---|---|---|---|---|
| F1 | Malformed, truncated or non-ELF game file | The loader rejects it before mapping anything; the core exits 84 | "Emulation stopped (exit 84). See the session log." The library stays usable | `[LoaderException]: /path/title.rpx: corrupt compressed section at 0x785B (zlib error -3)` |
| F2 | Game files deleted or moved after the library scan | The GUI refuses to launch | "Game files are missing. Choose the games folder again." | — (nothing starts) |
| F3 | WEMU core executable missing | The GUI refuses to launch | "The WEMU core executable was not found." | — |
| F4 | A title calls an OS function WEMU does not implement | The call returns 0 and the title continues; most titles tolerate it | Usually nothing; sometimes a feature of the title does not work | `[ERROR] [HLE] Unknown import OSSetThreadCleanupCallback (r3=… r4=…) -> returning 0`, and a most-frequent-first summary at exit |
| F5 | A guest thread faults (bad pointer in the title) | Only that thread is killed; the scheduler moves on | The game keeps running, or stops if it was the main thread | `[ERROR] [EMU] thread 0x26000000 killed by fault at PC=0x…; continuing boot`, then a crash context (registers, backtrace, threads) |
| F6 | The core itself crashes (host segfault, abort) | The crash handler logs one line, then the process dies with the same signal | "Emulation stopped (exit N). See the session log." | `[6.927] [ERROR] [EMU] host crash: signal 11 (SIGSEGV) in the WEMU core; …` (added 2026-11-15, scenario R4) |
| F7 | A shader cannot be translated or compiled | That draw falls back to the software renderer | That object may look wrong; the game keeps running | The rejection reason (`lastError()`), with `[GX2TRACE]` when tracing is on |
| F8 | The phone gamepad loses Wi-Fi | After 6 s without a message the phone is dropped and its input released; the keyboard and other pads keep working | The phone disappears from the connected devices | `[REMOTE] GamePad disconnected (timeout)` |
| F9 | A USB/Bluetooth gamepad is unplugged | The device is removed from the input list | The device disappears from the list; other inputs keep working | — |
| F10 | The session log directory cannot be created | The GUI refuses to launch | "Cannot create the session log directory." | — |

### When a dependency goes down

| Dependency | Decision | Why |
|---|---|---|
| **GPU / Vulkan driver** | **Fail clearly.** The session stops with `[NATIVE_VULKAN] no usable Vulkan device (…): WEMU needs a GPU with a Vulkan driver` | A software fallback was tried (scenario R2): the software renderer draws lit scenes black at 3 fps. A stopped session with a reason is better than a game that looks broken |
| **Audio device** | **Degrade.** The game runs without sound | Sound is not needed to play or test a title |
| **The core process** (seen from the GUI) | **Fail and recover.** The GUI reports the exit, clears the session and allows another launch | `SessionLifecycleTest.CoreStartupFailureClearsSessionAndAllowsAnotherLaunch` |
| **Phone gamepad (network)** | **Degrade.** The phone is dropped after 6 s; it can reconnect with its saved token | Local input keeps working |
| **Shader compiler subprocess** | **Degrade per draw.** A shader that fails, or takes more than 15 s, leaves that draw to the software renderer | One bad shader must not stop the game |

No dependency is retried automatically: a missing GPU or audio device does not come back during a session, and the phone reconnects by itself.

### Resilience scenarios

Each was run on purpose on 2026-11-15 (R6 on 2026-11-13) (Linux, RTX 4070 Laptop), isolated from the developer setup: the core started by `tests/rpx-suite/run.py` or by hand, with its output in `/tmp`, under a 3 GB memory cap.

| # | What was broken | How | Expected | What happened | Follow-up |
|---|---|---|---|---|---|
| R1 | **Malformed input**: a truncated RPX, one with 64 flipped bytes, and a text file | `build/core/wemu /tmp/…rpx` | Rejected with the file and the reason; no crash | Exit 84 each time, no crash. The truncated and corrupted files were rejected **without naming the file** | Fixed in `e7136ae`: every rejection names the file (`LoaderValidationTest.EveryRejectionNamesTheFile`) |
| R2 | **Service cut off: no GPU** | `run.py --env VK_DRIVER_FILES=/nonexistent/icd.json` | The session stops with a clear reason | The core died with `create instance: Vulkan result -9`: no tag, no time, after a line claiming unsupported draws stay in software | A software fallback was tried and rejected (black frames, 3 fps). Fixed in `3567328`: a tagged message saying a Vulkan GPU is needed |
| R3 | **Service cut off: no audio** | `run.py --env SDL_AUDIODRIVER=wemu-none` | The game runs silent | Logged `[AUDIO] cannot initialize host audio: Audio target 'wemu-none' not available`; suite `PASS`, all probes correct | None needed |
| R4 | **The core killed mid-run** | `kill -SEGV` on the suite's core after 6 s | The runner reports the run as unfinished; the log says what happened | The runner reported "suite did not finish", verdict `FAIL`, exit code -11. The core log **stopped mid-line** | Fixed in `bde605a`: a crash handler logs `host crash: signal 11 (SIGSEGV)` and still dies with the signal |
| R5 | **Unexpected load**: 3 interpreters at once | `CoreinitExtraTest.SharedFont…` under a 3 GB cap | Runs in under 1 GB | Killed: each interpreter zero-filled 1.7 GB of guest RAM (5.5 GB in all), which OOM-killed the desktop twice | Fixed in `97bc7b6`: guest RAM is backed on first touch; the whole test binary peaks at 907 MB |
| R6 | **Sustained load**: 3,600 frames | RPX suite `endurance` scene, best of 3 (2026-11-13) | No crash; drift, stutters and memory growth reported | No crash, check `pass`, 0 stutters, memory −0.8 MiB over the scene, 120.7 fps | None needed |

Four of the six runs found a defect the unit tests had missed, and each is now covered by a test or a clearer message.

### Logs

- **Format:** every `Utils::Log` line starts with the seconds since the core started, then the level and the subsystem: `[   35.619] [ERROR] [AX] frame pump thread created (handle=0x26000B68)`. The session log's file name gives the start time (`session-20261114-234143-173.log`), so a line's wall-clock time is the name plus the prefix.
- **Where:** the GUI writes the core's output to `~/.local/share/WEMU/WEMU/logs/session-<date>-<time>.log`, one file per session. The GUI's own events (`[SESSION_CORE]`, `[SESSION_EXIT] code=… status=…`, `[REMOTE] …`) go to its standard error.
- **Context:** errors carry what is needed to act. An unknown import has its register arguments, a guest fault has the thread, PC and a full crash context (registers, nearby objects, backtrace, every thread), and a loader error has the file and the offset.
- **Levels:** the default level shows errors only (`WEMU_LOG_LEVEL=4`), so some status lines (`[X64JIT] enabled`) are logged as errors to stay visible. About 170 debugging traces use `printf`/`std::cout` directly and carry no time. Both are listed in [tech-debt.md](tech-debt.md) item 20.

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
- **Review History (GitHub API, 2026-11-15):** 119 PRs between 2025-06-10 and 2026-11-05 (111 merged, 5 closed, 3 open). 108 of them were reviewed by someone other than their author, and 13 received a change request: 165 reviews by another member (124 approvals, 24 comments, 15 change requests, 2 dismissed), plus 123 inline review comments on the code.

| Reviewer | PRs reviewed | Reviews | Reviews with a written comment | Changes requested | Inline comments |
|---|---|---|---|---|---|
| Robouch0 | 55 | 74 | 38 | 5 | 45 |
| Igoatyouu | 47 | 60 | 44 | 7 | 41 |
| NicolasMelet | 26 | 31 | 16 | 3 | 36 |

  Authors: Igoatyouu 48 PRs, Robouch0 45, NicolasMelet 22, four occasional contributors 1 each. Every regular member both writes and reviews.
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

#### Static analysis rules
The clang-tidy and clang-format rules, the checks disabled with their reasons, and the initial and final measurements are in [static-analysis.md](static-analysis.md).

#### Pipeline history
**Browsable history:** <https://github.com/Robouch0/WEMU/actions>. It shows every run with its result, successful and failed, since the repository started.

Measured on 2026-11-15 through the GitHub API: **631 runs** between 2025-11-14 and 2026-11-05, **538 successful and 93 failed** (85% / 15%).

| Month | Successful | Failed |
|---|---|---|
| 2025-11 | 23 | 5 |
| 2025-12 | 11 | 1 |
| 2026-01 | 22 | 0 |
| 2026-02 | 25 | 5 |
| 2026-03 | 174 | 8 |
| 2026-04 | 151 | 17 |
| 2026-05 | 41 | 10 |
| 2026-06 | 24 | 20 |
| 2026-07 | 4 | 0 |
| 2026-09 | 11 | 2 |
| 2026-10 | 44 | 21 |
| 2026-11 | 8 | 4 |

Runs by workflow: `Build WEMU Project` 343, `Mirror Repository` 131, `Build and Check WEMU` (the current `wemu-checks.yml`) 127, `Mobile GamePad app` 17, `Enforce Merge from Dev to Main Only` 13. Most failures are lint violations and headless Vulkan runner timeouts.

**Changes the pipeline stopped before they reached `main`:** CI-01 to CI-03 in [regressions.md](regressions.md#3-regressions-caught-by-ci-before-merge).

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
