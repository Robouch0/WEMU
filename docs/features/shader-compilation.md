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
