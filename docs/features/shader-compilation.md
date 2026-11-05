# Shader compilation without stutter

The Wii U GPU executes Latte microcode that titles ship precompiled, so the
console never compiles a shader while a game runs. WEMU has to translate every
Latte program before a host GPU can execute it, and today it does so at the
moment of the first draw that uses it.

## Current path

1. `LatteShaderLowering` turns Latte microcode into GLSL.
2. `SpirvCompiler` writes the GLSL to a file and spawns `glslangValidator -Os`
   and `spirv-val`. The SPIR-V result is cached in `~/.cache/wemu/spirv`.
3. `VulkanRasterBackend` creates a graphics pipeline for each new combination of
   shaders and fixed-function state (blend, depth/stencil, topology, formats).
   This runs on the replay worker, inside the frame, against a `VkPipelineCache`
   that only lives in memory.

Only the driver's own disk cache survives between runs. A first Mario Kart 8
run spent 27 s on the frame that starts a race; the same frame took 1.3 s on the
next run.

## Roadmap

Every step keeps game files read-only: caches live under the user's cache
directory, keyed by title and host device, and nothing changes what the guest
observes.

| Step | Goal | Done when |
|---|---|---|
| 0 | Measure where first-use time goes | Frame profile separates GLSL→SPIR-V compilation from driver pipeline creation, GX2Invalidate calls on shader memory can be traced, and an MK8 cold/warm measurement is recorded below |
| 1 | Persist the Vulkan pipeline cache | Pipeline cache data is loaded at startup and written atomically, per device and driver, with corrupt or foreign data ignored |
| 2 | Per-title manifest and preloading | Every created pipeline is recorded from its Latte inputs; the next launch recreates all of them in parallel before the guest starts, with visible progress |
| 3 | Compile in process | No process is spawned per shader; SPIR-V validation is opt-in |
| 4 | Shader objects and dynamic state | Fixed-function state no longer multiplies compilations where the device supports `VK_EXT_shader_object` (or the graphics pipeline library as fallback) |
| 5 | Anticipate from load-time hints | Shaders announced by the title before their first draw are compiled in the background |
| 6 | First-ever shaders without skipped draws | A never-seen shader costs a short fast compile and is upgraded in the background; no draw is dropped |

An interpreting ubershader is only considered if step 6 still leaves visible
hitches.

## Measurements

Mario Kart 8, RTX 4070 Laptop (NVIDIA 580), 180 s from boot with `WEMU_AUTO_A=1`,
which reaches a race at frame 1729. The cold run uses empty WEMU SPIR-V and
NVIDIA caches (`WEMU_SHADER_CACHE_DIR`, `__GL_SHADER_DISK_CACHE_PATH`); the warm
run reuses them.

| Replay-thread work | Cold | Warm |
|---|---|---|
| Latte→GLSL lowering | 1246 × 0.13 ms = 0.17 s | 1262 × 0.13 ms = 0.16 s |
| GLSL→SPIR-V compilation | 463 × 96.5 ms = 44.7 s | 3 × 127 ms = 0.38 s |
| Pipeline creation | 429 × 26.5 ms = 11.4 s | 431 × 1.4 ms = 0.62 s |
| Race-start frame 1729 | 28.9 s (22.3 s for 234 shaders, 5.5 s for 214 pipelines) | 1.26 s (0.26 s pipelines, 0.07 s lowering) |
| Average FPS | 32.5 | 49.1 |

The cost of one compilation, timed on 40 cached shaders with the same tools:

| Part | Median |
|---|---|
| Process spawn | 0.8 ms |
| `glslangValidator` on an empty shader (built-in symbol tables) | ≈55 ms |
| `glslangValidator -Os` on a real shader | 77 ms |
| `glslangValidator` without `-Os` | 57 ms |
| `spirv-val` | 3.6 ms |

MK8 issues about five `GX2Invalidate` calls per frame with the shader bit over
the whole address space (`0, 0xFFFFFFFF`, modes `0x8` and `0xF`). Only 33
calls name a precise `CPU_SHADER` (`0x48`) range; they cover 10 of the 838
programs bound in the run, at most one frame before their first draw.

Consequences for the roadmap:

- Compilation, not the driver, dominates a cold start. Most of it is the
  compiler re-initializing for every shader, which step 3 removes. Disabling
  `-Os` removes most of what remains.
- NVIDIA's own disk cache already brings pipeline creation down to ≈1.4 ms.
  Step 1 gives the same result on drivers or setups without that cache. Step 2
  moves the remaining warm race-start work (lowering and pipelines) to the
  preload screen.
- `GX2Invalidate` cannot anticipate MK8's shaders, so step 5 needs a different
  signal.
- In the warm run, frames 1734–1758 still spend 0.6–0.65 s each in replay
  without any shader work. That cost is outside this roadmap.

## Persistent pipeline cache

`VulkanRasterBackend` creates its `VkPipelineCache` from
`$XDG_CACHE_HOME/wemu/pipelines` (else `~/.cache/wemu/pipelines`;
`WEMU_PIPELINE_CACHE_DIR` overrides it, and an empty value disables it). Each
device gets its own file, named after the vendor, device, driver version and
pipeline-cache UUID. The data comes with a checksum, and reaches the driver
only when its Vulkan header names this device. A driver update therefore
starts a new file, and a corrupt or foreign file starts an empty cache.

When a frame has created pipelines, the backend snapshots the cache in the
background, at most every 10 seconds, and once more on shutdown. Every write
goes to a private temporary file that is then renamed, so a killed session
keeps the last complete snapshot.

On MK8 with NVIDIA's own disk cache disabled (`__GL_SHADER_DISK_CACHE=0`),
100 s from boot:

| | First run | Next run |
|---|---|---|
| Pipeline creation | 415 × 24.7 ms = 10.2 s | 417 × 0.31 ms = 0.13 s |
| Race-start frame 1729, pipelines | 5.04 s | 29 ms |
| Cache file | 13 MB written | 13 MB loaded |

## Pipeline manifest and preloading

Lowering caches attach a `Latte::ShaderOrigin` to every shader they produce:
the program words and lowering parameters that `Latte::lowerOrigin` needs to
produce the same source again. When the native backend creates a pipeline, it
records a `PipelineRecipe`: the origins of both stages, the fragment variant,
and the plain pipeline state the backend needs to rebuild it. That state is the
key without module ids, the draw state pipeline creation reads, and the
render-pass formats. Pipelines whose shaders have no origin are not recorded.

Recipes are stored per executable, in
`$XDG_CACHE_HOME/wemu/manifests/<rpx stem>-<FNV-1a of the rpx>.manifest`
(`WEMU_PIPELINE_MANIFEST_DIR` overrides the directory, and an empty value
disables manifests). An update with a different RPX starts its own manifest.
Programs and origins are stored once each. Parsing checks a format version, a
checksum and the range of every field, and a file that fails any check is
ignored.

`PreloadGx2Pipelines` runs before `interpreter.run()`. It loads the manifest,
lowers each origin through the replayer's own caches, and calls
`VulkanRasterBackend::preload`. That function compiles the missing SPIR-V
modules, then creates the pipelines, both on up to six worker threads. Each
module and pipeline is filed under the same cache key a draw computes, so the
first draw finds it. Recipes that no longer lower, compile or decode are
skipped.

While this runs, a black host frame shows "Preparing shaders", the count and a
progress bar. That frame is not a guest present: it is not paced, profiled or
counted. The guest has not started, so its clock and audio see nothing.

MK8, 100 s from boot, with the manifest recorded by an earlier session:

| | No manifest | Preload, warm caches | Preload, empty SPIR-V, pipeline and NVIDIA caches |
|---|---|---|---|
| Preload before the title starts | — | 404 pipelines in 0.24 s | 417 pipelines in 12.9 s |
| Race-start frame 1729 | 22.8 s (234 shaders, 214 pipelines) | 0.90 s, no compilation | 0.94 s, no compilation |
| Replay frames over 50 ms dominated by shader work | 37 | 8 (content the recording run never reached) | 0 |

At the race start, 312 lowerings (35 ms) still happen during the game. They
belong to draws that never created a native pipeline, so no recipe records
them.

## In-process compilation

When CMake finds glslang's development package (`glslang-dev` on Debian and
Ubuntu, `glslang-devel` on Fedora; Arch and Homebrew ship it in `glslang`),
`SpirvCompiler` links glslang and the SPIRV-Tools optimizer and compiles in
process. It uses the front end, options and target of
`glslangValidator -V -Od|-Os --target-env vulkan1.0`. All 447 shaders of an
MK8 SPIR-V cache came out word for word identical with `-Os`, and a sample of
20 with `-Od`.
No process is started per shader. glslang is initialized once and then
compiles concurrently, which the preload uses. SPIR-V validation is opt-in
(`WEMU_SPIRV_VALIDATE=1`): it never rejected glslang output in those 447
shaders, and it costs about as much as compiling unoptimized. Builds without
the package keep running `glslangValidator` and `spirv-val`.

Per shader, on those 447 MK8 shaders:

| | Median per shader |
|---|---|
| `glslangValidator -Os` + `spirv-val` processes | 96.5 ms (in-emulator) |
| In process, `-Os` | 35 ms |
| In process, unoptimized | 5.6 ms |
| In-process validation | 4.8 ms |

MK8 with every cache empty and no manifest (100 s): compilation drops from
44.7 s to 15.7 s, and the race-start frame from 28.9 s to 15.4 s (9.1 s for
234 shaders, 5.3 s for cold-driver pipelines). A cold preload from the
manifest drops from 12.9 s to 6.8 s. Most of what remains is the size
optimizer, which step 6 can defer.

## First-ever shaders

A draw never waits for a shader it could skip: a missing module or pipeline is
always created before the draw. Step 6 therefore came down to making that
creation short. On MK8, per-draw GPU timestamps (`WEMU_NATIVE_GPU_TIMING=1`)
for 188 matching kinds of draws were equal with optimized and unoptimized
SPIR-V. The median ratio was 1.00, the full-screen draws stayed within ±4 %,
and the driver optimizes either form itself. The size optimizer cost 30 ms per
shader for nothing, so modules are now unoptimized (`-Od`) unless
`WEMU_SPIRV_OPTIMIZE=1`. Optimized modules get separate disk-cache entries.
The unoptimized mode also accepts entries cached before this change, which are
optimized and equally valid. No module is replaced in the background, because
nothing would gain from it. All Vulkan tests also pass with every module
compiled unoptimized.

The driver takes longer on unoptimized SPIR-V (36 ms instead of 24.5 ms per
cold pipeline), but the total is lower. MK8, 100 s:

| | Optimized (step 3) | Unoptimized |
|---|---|---|
| Fully cold: compilation | 431 × 36 ms = 15.7 s | 443 × 6.5 ms = 2.9 s |
| Fully cold: pipeline creation | 406 × 24.5 ms = 9.9 s | 414 × 37 ms = 15.4 s |
| Fully cold: race-start frame | 15.4 s | 10.3 s |
| Cold preload from the manifest | 6.8 s | 4.6 s |
| Race FPS, warm caches | 47.0 | 48.2 |

What remains at a cold race start is mostly driver pipeline creation, 7.8 s for
214 pipelines needed by the same frame.
