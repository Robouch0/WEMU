# Technical Debt

This document catalogues existing technical debt, structural architectural compromises, and deferred maintenance tasks across the WEMU project.

---

## 1. Technical Debt Inventory

Costs are estimated as **S** ($< 2$ days), **M** ($2\text{--}5$ days), or **L** ($> 1$ week).

| Item / Subsystem | Impact & Risk | Why Deferred | Rough Cost | Owner |
|---|---|---|---|---|
| ~~**1. Unvalidated RPX/ELF Loader**~~ (resolved) | High: Malformed binaries cause out-of-bounds reads, memory exhaustion, or buffer overflows. | Initial focus was on booting known valid homebrew. | **S** | Resolved 2026-11-12 |
| **2. Unimplemented HLE Syscalls Return 0** | High: Unimplemented OS routines log a warning and return 0 in `r3`, masking fatal missing functionality with silent state corruption. | Prioritized common boot APIs over complete OS coverage. | **M** | `TODO(team): Assign owner` |
| **3. Interpreter Unimplemented Instructions** | High: An unrecognised opcode kills the guest thread that hit it and boot continues, so a title can hang without an obvious cause. | Implemented opcodes opportunistically as games required them. | **L** | `TODO(team): Assign owner` |
| **4. GX2CopySurface Mip Uploads** | Medium: Textures using multiple mip levels display distorted or blurred level-0 data when scaled down. | Focus was initial basic surface blits. | **S** | `TODO(team): Assign owner` |
| **5. HDR Render Target Clamping** | Medium: Saturated lighting, bloom, and tonemapping in complex shaders clamp at 1.0. | Requires pipeline format refactor in Vulkan backend. | **M** | `TODO(team): Assign owner` |
| **6. GPU-to-CPU Readback Synchronization** | Medium: Guest code cannot read rendered surfaces; snapshots and dynamic render-to-texture fail. | Requires asynchronous GPU fences and host-to-guest transfer staging. | **M** | `TODO(team): Assign owner` |
| **7. Missing Texture Decoders (RGB5A1, RGBA4, RGB10A2)** | Medium: Meshes and UI panes using packed 16-bit or 10-bit textures exhibit corrupted colors. | RGBA8 decoder was used as a universal catch-all during prototyping. | **S** | `TODO(team): Assign owner` |
| **8. Shader Loops (`LOOP_START` / `LOOP_END`)** | Medium: Shaders with dynamic or counted loops skip iterations entirely, breaking procedural effects. | Native lowering compiler initially only supported linear and forward-branching control flow. | **L** | `TODO(team): Assign owner` |
| **9. Shader Cache Keying on Address** | Medium: Reloaded or dynamically generated shaders at reused memory addresses reuse stale compiled bytecode. | Address lookup was simplest implementation for static homebrew binaries. | **S** | `TODO(team): Assign owner` |
| **10. Core Resident Memory Leak** | Medium: Seen once as 187 MiB of growth over 3,600 frames; the latest bench measured +2 MiB, so it is unconfirmed. | Requires memory profiling and tracking lifetime of Vulkan command buffers and staging pools. | **M** | `TODO(team): Assign owner` |
| **11. CI Linting & Formatting Excludes `gui/`** | Low: GUI code quality and formatting drift over time; potential bugprone patterns in Qt bridges. | Clang-tidy build setup was complex to integrate with Qt MOC headers. | **S** | `TODO(team): Assign owner` |
| **12. Coverage Excludes GUI & Unpublished in CI** | Low: Team lacks automated PR visibility into coverage regressions; GUI coverage remains unmeasured. | Coverage toolchain slows down CI execution; local reporting was deemed sufficient initially. | **S** | `TODO(team): Assign owner` |
| **13. RPX Suite Timing Noise on Laptops** | Low: Benchmarking results vary with thermal throttling and CPU power states; false regression warnings. | High-precision host performance counters require standardized server hardware. | **S** | `TODO(team): Assign owner` |
| **14. CI Checks Not Listed as Required** | Medium: A pull request with failing tests could be merged once it has its approvals. | Rulesets were set up for reviews first. | **S** | `TODO(team): Assign owner` |
| ~~**15. Title-Specific Workarounds in Core Loops**~~ (resolved) | Medium: Mario Kart 8 code keyed to that game's addresses in the run loop, `gx2_swap`, `CoreinitExtra` and `Diagnostics`. | Added during MK8 bring-up. | **M** | Resolved 2026-11-12 |
| **16. `OSTicksToCalendarTime` Is a Fixed Date** | Low: Every title sees the same date in 2026, whatever time it passes. | `OSGetTime` counts from boot, not from 2000-01-01, so a real conversion needs a wall-clock base first. | **S** | `TODO(team): Assign owner` |
| **17. `GX2CalcTVSize`/`GX2CalcDRCSize` Ignore Mode, Format and Buffering** | Medium: Always the 720p (854x480 for the GamePad), 32-bit, double-buffered size. A 1080p or triple-buffered title gets an undersized scan buffer. | Scan-out was written for 720p first. | **M** | `TODO(team): Assign owner` |
| **18. Remaining Stubs** | Low: `__os_snprintf` writes an empty string; `OSGetTitleID` returns 0; `nn::act` ID getters return constants. Not tested on purpose: a test would only freeze placeholder behaviour. | Implemented as needed to boot titles. | **S** | `TODO(team): Assign owner` |

---

## 2. Technical Debt Notes

### Item 1: Unvalidated RPX/ELF Loader Input
- **Detail:** In `core/src/binary/Loader.cpp` (~lines 150 and 158), section headers from the file are trusted directly. Section sizes are passed straight into memory allocations without bounds checking, and there is no strict validation of the ELF magic header or segment overlaps.
- **Risk:** A malformed or corrupted game executable can crash the host process or trigger unbounded allocations.
- **Status:** Resolved 2026-11-12. `Loader` now checks the ELF identity (magic, 32-bit big-endian, `EM_PPC`), keeps every section and section-table offset inside the file, caps sections at 512 MiB before allocating, rejects corrupt zlib streams and bad `sh_name`/`sh_entsize` values, and turns decoder overruns into a `LoaderException` that names the file. `core/tests/binary/test_loader_validation.cpp` covers each case, plus a truncation sweep and a seeded corruption sweep. See [regressions.md](regressions.md#4-defects-found-by-the-loader-validation-tests-2026-11-12), LDR-01 to LDR-05.

### Item 2: Unimplemented HLE Syscalls Return Zero
- **Detail:** In `core/src/cpu/instructions/Sc.cpp`, when a syscall is invoked whose symbol is not registered in `SyscallHandler`, the emulator logs `Diag::noteUnknownImport` and forces `cpu.m_gpr[3] = 0`.
- **Risk:** In C APIs, `0` often denotes success. The guest game assumes the requested operation (such as filesystem access, thread synchronization, or network init) completed successfully, leading to difficult-to-diagnose crashes thousands of instructions later.
- **Remediation:** Implement an explicit fallback policy: configurable strict aborts during development and stub tables with proper error return codes (e.g., standard OS error codes) for non-critical services.

### Item 3: Interpreter Unimplemented Instruction Handling
- **Detail:** When decoding an unmapped PowerPC instruction in `core/src/cpu/interpreter/Interpreter.cpp`, the emulator throws `Core::InterpreterException("No instruction found with this fields...")`. The fault handler in the same file logs `[EMU] thread ... killed by fault at PC=...; continuing boot` and keeps the other threads running.
- **Risk:** A title that depends on the killed thread waits forever, and the only trace is one log line.
- **Remediation:** Complete coverage for remaining floating-point, vector (Paired Singles), and system control opcodes; enforce hard aborts on unknown opcodes.

### Item 4: GX2CopySurface Non-Zero Mip Rejection
- **Detail:** `gx2_CopySurface` in `core/src/hle/Gx2.cpp` currently checks if the mip level is zero and aborts transfer if `mipLevel > 0`.
- **Risk:** Retail games generating full mip hierarchies for textures suffer from severe aliasing or missing textures on distant geometry.
- **Remediation:** Add subresource calculation to compute destination mip offsets and upload each level into the corresponding Vulkan image view layer.

### Item 5: HDR Render Target Clamping (RGBA16F)
- **Detail:** Vulkan framebuffer pipelines currently clamp floating-point render targets to $[0.0, 1.0]$.
- **Risk:** Advanced post-processing pipelines (HDR bloom, depth of field, exposure adjustments) produce washed-out or flat lighting.
- **Remediation:** Verify Vulkan format selection (`VK_FORMAT_R16G16B16A16_SFLOAT`) and adjust pipeline blend state and clear values to avoid intermediate clamping.

### Item 6: GPU-to-CPU Readback Synchronization
- **Detail:** When games request surface copies from VRAM back to system RAM via `GX2CopySurface`, WEMU executes the copy immediately on the CPU without awaiting pending GPU rasterization. Render target memory is not flushed back to guest memory.
- **Risk:** Features such as pause-screen snapshots, mini-maps generated on the GPU, and in-game camera feeds display blank black screens.
- **Remediation:** Implement asynchronous fences and staging buffers to copy GPU framebuffer memory back into guest physical RAM before returning control to guest threads.

### Item 7: Missing Texture Decoders (RGB5A1, RGBA4, RGB10A2)
- **Detail:** Texture unswizzling in `core/src/gfx/Gx2Replayer.cpp` (~line 2360) lacks unpacking logic for packed 16-bit (`RGB5A1`, `RGBA4`) and 32-bit (`RGB10A2`) formats, treating them as standard `RGBA8`.
- **Risk:** Sprites, font glyphs, and UI panes that use compact packed color formats appear scrambled or chromatically inverted.
- **Remediation:** Add explicit unpacking routines for 16-bit and 30-bit packed color components with unit tests verifying bit-level conversions.

### Item 8: Missing Shader Loop Control Flow
- **Detail:** The Latte shader compiler lacks native lowering for `LOOP_START` and `LOOP_END` assembly opcodes. Native lowering currently rejects backward branch targets.
- **Risk:** Procedural shaders, ray-marched effects, and iterative math routines cannot execute, rendering geometry blank or with flat background colors.
- **Remediation:** Introduce loop control blocks and structured control flow support in the Latte-to-SPIR-V translation backend.

### Item 9: Shader Cache Keying on Address Alone
- **Detail:** `m_vsCache` and `m_psCache` in `core/src/gfx/Gx2Replayer.hpp` (lines 505 and 515) store compiled shaders mapped by the 32-bit guest pointer address of the shader structure.
- **Risk:** Games that dynamically allocate and free shader buffers in temporary arenas run into collision bugs where a new shader receives a previous shader's compiled pipeline.
- **Remediation:** Key the shader cache on a fast hash (e.g., xxHash3) of the shader bytecode and input binding configuration rather than the guest pointer.

### Item 10: Resident Memory Growth in Long Runs
- **Detail:** One run of the 3,600-frame `endurance` scene in `tests/rpx-suite` showed 187 MiB of RSS growth while guest allocations stayed static. The full bench of 2026-11-06 22:16 measured +2 MiB for the same scene, so the leak is not confirmed.
- **Risk:** Long gameplay sessions suffer from host memory exhaustion and stuttering caused by OS paging.
- **Remediation:** Run memory profiling with Valgrind Massif or heaptrack to identify unbounded command buffer allocations or uncollected surface staging buffers.

### Item 11: Static Analysis and Formatting Excludes GUI Code
- **Detail:** `.github/workflows/wemu-checks.yml` limits `clang-format-19` to `core/src` and `core/tests`, and `clang-tidy-19` to `core/src`.
- **Risk:** The Qt-based GUI code (`gui/src/` and `gui/tests/`) can accumulate styling inconsistencies, memory leaks, and undefined behaviors unchecked by CI.
- **Remediation:** Configure `clang-tidy` to run against `gui/` with appropriate header exclusions for Qt-generated files (`moc_*`, `ui_*`, `qrc_*`).

### Item 12: Test Coverage Excludes GUI & Unpublished in CI
- **Detail:** Code coverage is collected solely for `core` unit tests in local environments. Coverage is not published to CI dashboards or pull requests, and `gui/tests/` coverage is not aggregated.
- **Risk:** Developers cannot easily track coverage deltas during code reviews, allowing untested code to enter the repository unnoticed.
- **Remediation:** Deferred on purpose: the team does not want a CI coverage job or artifact for now. Until then, coverage is measured locally (see [testing-policy.md](testing-policy.md#3-code-coverage)) and recorded in that file.

### Item 13: Benchmark Timing Noise on Portable Hardware
- **Detail:** The RPX test suite performance benchmarks fluctuate on laptops due to thermal throttling, battery vs. AC power states, and CPU frequency governors.
- **Risk:** Automated performance checks can produce false-positive regression alerts or mask real regressions when run on non-standardized developer hardware.
- **Remediation:** Current mitigation uses best-of-3 runs (`--repeat 3`) and relative baseline comparisons (`--baseline`). Long-term resolution is running automated regression benchmarks on dedicated CI bare-metal runners.

### Item 14: CI Checks Not Listed as Required Status Checks
- **Detail:** As read through the GitHub API on 2026-11-12, the `Dev protection` ruleset requires 1 approving review and blocks deletion and force-pushes, but lists no required status checks. `Main protection` requires 2 approvals and only the `check-source-branch` check. The `wemu-checks.yml` jobs (`Full Build (core + gui)`, `Unit Tests`, `macOS arm64`, `clang-format`, `clang-tidy`) do not appear as required.
- **Risk:** The policy says CI must pass before merge, but GitHub does not enforce it.
- **Remediation:** Add those job names as required status checks in both rulesets, or close this item if they are enforced somewhere the API does not report.

### Item 15: Title-Specific Workarounds in Core Loops (resolved 2026-11-12)
- **What was removed:** the MK8 drive-menu trampoline and title-sequence deferral in `Interpreter::run()`; ten MK8/sead/FLVC tracers and skips in `utils/Diagnostics.cpp` (about 800 lines); the scene-state debugging in `gx2_swap`; `InstallTitleSeqPoolFix`, `InstallSceneConfigWorkaround`, `InstallSceneUpdateForce` and `InstallSubsysReadyForce`. No game address is left in `core/src`. Generic debugging tools (`WEMU_WATCH_*`, `WEMU_PC_COUNT`, `WEMU_HEARTBEAT`, `WEMU_PATCH`) stay.
- **Why it was safe:** everything except the two `Install*` fixes was behind environment variables, and GUI sessions already ran without those two.
- **Check:** Mario Kart 8, Pikmin 3 and Smash Bros were run for 1,500 frames through the GUI session protocol before and after (`scratchpad/session_probe.py`, A pressed at frames 900 and 1200). All 15 captured frames are byte-identical, and the frame rates match within 1% (MK8 59.2 vs 58.8 fps, Pikmin 3 10.0 vs 10.0, Smash 21.6 vs 21.5).

### Item 16: `OSTicksToCalendarTime` Returns a Fixed Date
- **Detail:** `core/src/hle/CoreinitExtra.cpp` writes one constant `OSCalendarTime` for any input.
- **Remediation:** Decide on a guest wall-clock base (host time at boot, or a fixed date for deterministic runs), add it to `OSGetTime`, then convert ticks (bus clock / 4 per second, epoch 2000-01-01) with tests on known dates and leap years.

### Item 17: Scan Buffer Size Ignores the Requested Mode
- **Detail:** `gx2_CalcTVSize` and `gx2_CalcDRCSize` in `core/src/hle/Gx2.cpp` ignore r3 (render mode), r4 (surface format) and r5 (buffering mode).
- **Remediation:** Compute the size from the mode's resolution, the format's bytes per pixel and the buffer count. Test 480p/720p/1080p, 8- and 10-bit formats, and double/triple buffering.

