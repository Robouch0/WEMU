# Regressions and Bugs Caught by Tests

This document tracks regressions, behavioral defects, and integration failures discovered during automated and suite-level testing of WEMU.

---

## 1. Summary of Active Issues

All items below were detected on 2026-11-06 by the WEMU RPX integration test suite (`tests/rpx-suite/`) unless stated otherwise.

| ID | Issue Description | Caught by (Test / Scene) | Manifestation | Code Location | Status |
|---|---|---|---|---|---|
| **REG-01** | Mip levels $> 0$ never uploaded | Probe `texture_mip_level3` (`forward_lit`) | Expected level-3 color `(255, 255, 0)`, received level-0 `(255, 0, 0)` | `core/src/hle/Gx2.cpp` (`gx2_CopySurface`) | Open |
| **REG-02** | HDR render targets clamp at 1.0 | Probes `post_hdr_clear`, `post_hdr_shader` (`postfx`) | Value expected 139, observed 85 | `core/src/gfx/` / Vulkan target pipeline | Open |
| **REG-03** | CPU readback of GPU results returns zeros | Every probe (`guest readback` column) | `guest readback` outputs `(0, 0, 0)` | `core/src/hle/Gx2.cpp`, `core/src/gfx/Gx2Replayer.cpp` | Open |
| **REG-04** | Formats RGB5A1, RGBA4, RGB10A2 decoded as RGBA8 | Probes `format_RGB5A1`, `format_RGBA4`, `format_RGB10A2` (`textures`) | Texture pixel values decoded incorrectly | `core/src/gfx/Gx2Replayer.cpp` (~line 2360) | Open |
| **REG-05** | Shader loops silently skipped | Probe `loop_32_iterations` (`loops`) | Expected inside Julia set color, received outside color | `core/src/gfx/` / Latte shader lowering | Open |
| **REG-06** | Stale shader loaded after memory address reuse | All branch probes (`branching` after `postfx`/`overdraw`) | Stale shader translation executed | `core/src/gfx/Gx2Replayer.hpp` (lines 505, 515) | Open |
| **REG-07** | Resident memory growth under sustained load | Scene `endurance` (3,600 frames) | Core resident memory grew 187 MiB in one run | Uninvestigated | Not reproduced (latest bench: +2 MiB) |
| **TOOL-01**| Toolchain metadata comment stripping | RPX suite build verification | Shaders failed linking due to missing attributes | `tests/rpx-suite/tools/latte-assembler/setup.sh` | Fixed (Patched) |

---

## 2. Detailed Bug Analyses

### REG-01: Mip Levels > 0 Never Uploaded
The `GX2CopySurface` HLE implementation rejects any copy surface transfer whose target mip level is non-zero, logging `[GX2COPY] unsupported transfer`. As a result, downscaled texture mips are never uploaded to the GPU cache. Probe `texture_mip_level3` in the `forward_lit` scene expects a distinct level-3 color `(255, 255, 0)`, but instead samples the root level-0 color `(255, 0, 0)`. The failure originates in `core/src/hle/Gx2.cpp` within `gx2_CopySurface`.

### REG-02: HDR Render Targets Clamp at 1.0
Floating-point render targets using format `RGBA16F` clamp values greater than 1.0 to 1.0, regardless of whether the value was produced by a framebuffer clear or written by a pixel shader. In scene `postfx`, probes `post_hdr_clear` and `post_hdr_shader` expect a post-tonemapped color value of 139, but receive 85 because intermediate HDR values are saturated before bloom and tone mapping. The clamp occurs in `core/src/gfx/` within Vulkan framebuffer and pipeline format initialization.

### REG-03: CPU Readback of GPU Results Returns Zeros
The Wii U OS allows games to copy rendered surface buffers back into guest memory using `GX2CopySurface`. In WEMU, `gx2_CopySurface` executes immediately on the host CPU at call time rather than being queued after pending GPU command buffers finish rendering, and render targets are never synchronized back into guest physical memory. Consequently, every test probe's `guest readback` check returns all zeros `(0, 0, 0)`. The defect spans `core/src/hle/Gx2.cpp` and `core/src/gfx/Gx2Replayer.cpp`.

### REG-04: Texture Formats RGB5A1, RGBA4, RGB10A2 Decoded as RGBA8
In `core/src/gfx/Gx2Replayer.cpp` (~line 2360), texture formats `RGB5A1`, `RGBA4`, and `RGB10A2` lack dedicated unswizzle and decode branches. They fall through into an "Everything else: treat as RGBA8" fallback block. Probes `format_RGB5A1`, `format_RGBA4`, and `format_RGB10A2` in scene `textures` fail because 16-bit and packed 32-bit color data are read with incorrect byte strides and component masks.

### REG-05: Shader Loops Silently Skipped
The Latte shader translator lacks native lowering for `LOOP_START` and `LOOP_END` control flow instructions. Because the native shader lowering path currently rejects backward jumps, loop bodies are skipped entirely during translation. Probe `loop_32_iterations` in scene `loops` calculates a 32-iteration Julia set; WEMU renders the outside background color rather than the loop-computed fractal interior.

### REG-06: Stale Shader Loaded After Memory Address Reuse
Shader caches `m_vsCache` and `m_psCache` (`core/src/gfx/Gx2Replayer.hpp` lines 505 and 515) are keyed solely on the guest memory address of the GX2 shader structure. When a new shader is dynamically loaded into a previously freed guest address space, the replayer retrieves the stale compiled shader instead of translating the new code. The bug triggers reliably when scene `branching` executes after `postfx` or `overdraw`. Bypassing address reuse in guest memory resolves the failure.

### REG-07: Resident Memory Growth Under Sustained Load
During the 3,600-frame `endurance` benchmark scene, the resident set size (RSS) of the core process increases by 187 MiB, despite the guest application allocating no additional memory after scene setup. The full bench of 2026-11-06 22:16 (`results/20261106-221657`) measured only +2 MiB for the same scene, so the growth is not reproducible yet. It stays on the list because the `endurance` scene and its memory warning exist to catch it if it returns. The source has not been isolated.

### TOOL-01: Latte Assembler Dropping Metadata Comments
During RPX test suite development, decaf-emu's `latte-assembler` was found to silently drop all `; $KEY = value` comment lines. These lines contain critical shader metadata including export counts, semantic bindings, and attribute names. Without these comments, assembled GFD `.gsh` binaries fail shader reflection in WEMU. The defect was resolved by applying a patch during assembler compilation in `tests/rpx-suite/tools/latte-assembler/setup.sh`.

---

## 3. Regressions Caught by CI Before Merge

GitHub Actions (`.github/workflows/wemu-checks.yml`) runs the unit tests, clang-format and clang-tidy on every pull request. Each entry below was checked against the run's job logs.

### CI-01: Vertex cache stopped reusing animated geometry
- **Date:** 2026-11-05
- **Change:** `6cf8863` "perf(gfx): content-keyed vertex cache, batched attribute decode and cheaper buffer snapshots" ([PR #295](https://github.com/Robouch0/WEMU/pull/295), branch `feat/enhance-gui-mainmenu-and-settings-page`)
- **Caught by:** `Gx2CaptureTest.DirectVertexArraysAreReusedUntilGuestVertexBytesChange` (`core/tests/system/test_gx2capture.cpp`), failing in every `Unit Tests` and `Full Build` job of runs [`42016554442`](https://github.com/Robouch0/WEMU/actions/runs/42016554442) and [`42018136260`](https://github.com/Robouch0/WEMU/actions/runs/42018136260)
- **What it would have broken:** the new cache counted admissions per vertex *content*. Animated meshes have new content every frame, so they never reached the cache even though the shadow and colour passes draw them twice per frame. That is the per-frame cost the change was meant to remove.
- **Fix:** `d59555d` "fix(gfx): count vertex cache admissions per draw site so animated meshes reuse their second pass". It keys admissions on the draw site (registers, attribute bindings, index count, mode) and updates the test so every pose has new content.
- **Blocked, then passed:** red on 2026-11-05 11:08 and 11:20; the fix's run [`42047482195`](https://github.com/Robouch0/WEMU/actions/runs/42047482195) passed at 14:41, and PR #295 was merged into `dev` at 15:02.

### CI-02: Implicit widening in a pointer offset (static analysis)
- **Date:** 2026-11-05
- **Change:** `6cf8863` (same PR as CI-01)
- **Caught by:** `clang-tidy` job, run [`42016554442`](https://github.com/Robouch0/WEMU/actions/runs/42016554442): `core/src/gfx/Gx2Replayer.cpp:78:40: error: result of multiplication in type 'unsigned int' is used as a pointer offset after an implicit widening conversion to type 'size_t' [bugprone-implicit-widening-of-multiplication-result]`
- **What it would have broken:** the product is computed in 32 bits before it is widened, so a large enough offset would wrap and address the wrong memory.
- **Fix:** `2a3e959` "style(gfx): apply clang-format 19 and fix a clang-tidy widening warning". Its run [`42018136260`](https://github.com/Robouch0/WEMU/actions/runs/42018136260) no longer fails `clang-format` or `clang-tidy` (only the CI-01 test still failed).

### CI-03: Compressed texture rounding differs between GPU drivers
- **Date:** 2026-10-24
- **Change:** `ddd87fb`, branch `fix/smash-startup-rendering` ([PR #291](https://github.com/Robouch0/WEMU/pull/291))
- **Caught by:** `Gx2CaptureTest.DeclaredAttributesAndPixelSemanticRenderWithEveryIndexEndian`, [`40132681724`](https://github.com/Robouch0/WEMU/actions/runs/40132681724)
- **What it showed:** the native Vulkan path decoded BC2/BC3 colour with different rounding on the CI's llvmpipe driver (169 where the CPU reference gives 170). Byte-exact comparison would have failed on some user GPUs as well.
- **Fix:** `81edf5f` "fix(ci): resolve formatting, audio lint, and Vulkan test failures". The test allows ±1 on BC2/BC3 colour channels only. Alpha and every other format stay byte-exact.
- **Blocked, then passed:** red on 2026-10-24 21:05 (every job); the fix's run [`40134346461`](https://github.com/Robouch0/WEMU/actions/runs/40134346461) passed at 21:28, and PR #291 was merged into `dev` on 2026-10-25.

---

## 4. Defects Found by the Loader Validation Tests (2026-11-12)

`core/tests/binary/test_loader_validation.cpp` feeds the RPX/ELF loader truncated, corrupted and hostile files. Before the hardening in `core/src/binary/Loader.cpp`, these inputs exposed:

| ID | Input | Behaviour before | Behaviour now |
|---|---|---|---|
| **LDR-01** | `.symtab` with `sh_entsize = 0` | integer division by zero (crash) | `LoaderException` "symbol entry size ..." (`ZeroSymbolEntrySizeIsRejectedInsteadOfDividingByZero`) |
| **LDR-02** | corrupt zlib stream in a `SHF_DEFLATED` section | section silently emptied, title booted with missing code or data | `LoaderException` "corrupt compressed section ... (zlib error N)" (`CorruptCompressedStreamIsRejected`) |
| **LDR-03** | section header claiming a multi-GB `sh_size`, or a compressed section declaring an absurd inflated size | allocated whatever the file declared (out of memory) | rejected before allocating; inflated sections are capped at 512 MiB (`HugeSectionSizeIsRejectedBeforeAllocating`, `CompressedSectionWithAbsurdInflatedSizeIsRejected`) |
| **LDR-04** | `sh_name` past the end of `.shstrtab` | out-of-bounds read while building section names | rejected (`SectionNameOffsetOutsideTheNameTableIsRejected`) |
| **LDR-05** | truncated file, bad magic, little-endian/64-bit or non-PowerPC ELF | read past the end or misparsed headers | clear `LoaderException` naming the file (`TruncatedFilesAreRejectedCleanly`, `BadMagicIsRejected`, ...) |

A seeded corruption sweep (`CorruptedHeadersNeverCrash`, 40 rounds) checks that random header damage only ever ends in a `LoaderException`. `RealTitlesStillLoad` (opt-in, `WEMU_TEST_RPX_DIR=games`) confirmed that the 13 local titles, including Mario Kart 8, Pikmin 3 and Smash, still load with the checks on.

---

## 5. Defects Found While Raising Critical-Path Coverage (2026-11-12)

Each defect below was found by a new test, fixed, and checked by running the test against the old code (it fails) and the fixed code (it passes).

| ID | Defect | Effect on a title | Test | Fix |
|---|---|---|---|---|
| **COV-01** | Nested `OSUninterruptibleSpinLock` saved the interrupt state on every acquire, so after the outer release interrupts stayed disabled. A release by a thread that did not hold the lock also restored its state. | Preemption silently stops for the rest of the session once a title nests a spinlock. | `CoreinitSyncTest.NestedSpinLockRestoresInterruptsOnlyAtTheOutermostRelease`, `SpinLockHeldByAnotherThreadCannotBeTakenOrReleasedFromOutside` | Per-lock owner, depth and saved state (`core/src/hle/Coreinit.cpp`) |
| **COV-02** | `OSBlockMove` and `OSBlockSet` returned 0 instead of the destination pointer (wut declares `void *`). | Code that uses the result, like `memmove`/`memset`, gets NULL. | `CoreinitSyncTest.BlockMoveHandlesOverlapBothWaysAndReturnsTheDestination`, `BlockSetUsesOnlyTheLowByteAndStopsAtTheGivenSize` | Return `dst` |
| **COV-03** | `OSGetThreadPriority` always returned 16. | A title that saves a priority and restores it later resets the thread to 16. | `CoreinitRuntimeTest.RaisingAReadyThreadAboveTheCallerHandsItTheCpu` | Read the scheduler's value (new `Scheduler::thread()` lookup) |
| **COV-04** | `OSGetSystemInfo` and `OSGetSharedData` cached their guest pointer in a function `static`, valid only in the first guest memory. | A second emulator instance (or a reset memory) reads clock info / the system font from unallocated memory. | `CoreinitRuntimeTest.SystemInfoDescribesTheEspressoClocksInEachGuestMemory`, `CoreinitExtraTest.SharedFontComesFromTheConfiguredFileInEveryGuestMemory` | Re-create the data when the memory changes or no longer holds it |
| **COV-05** | Decoded fetch-shader layouts were cached by program address only, the same class of bug as REG-06. | After a title loads a different fetch shader at a reused address, its vertices are read with the old attribute formats. | `Gx2FetchShaderTest.NewMicrocodeAtAReusedAddressIsDecodedAgain` | Cache the microcode with the layout and decode again when it differs (`core/src/hle/Gx2.cpp`) |
| **COV-06** | The interpreter's hook filter was rebuilt only when the number of hooks changed. | Replacing the hook set with one of the same size left new hooks unseen (found when two tests registered one hook each). | `RunLoopTest.WorkerThreadFinishesAndTheTitleCarriesOnUntilMainReturns` (after `HookedAddressRunsTheHostHandlerAndReturnsToTheCaller`) | Force a rebuild at the start of `run()` |
| **COV-07** | `SetFsContentRoot` kept the content layers added for the previous root. | A new title in the same process would read files from the previous title's update layers. | `FsScenarioTest.UpdateContentWinsAndTheBaseGameFillsInWhatTheUpdateLacks` | Clear the layers when the root changes (`core/src/hle/Fs.cpp`) |

The RPX suite was run after these fixes (`results/20261112-052306`, then best of 3 for `cpu_jobs`, `textures`, `shadows`): same probe results as 2026-11-06 and no frame-rate regression.

