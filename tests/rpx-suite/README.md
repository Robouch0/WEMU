# WEMU RPX test suite

A homebrew Wii U title written to load WEMU the way retail games do. It is
used to track performance (and catch rendering regressions) without booting
commercial games. The same RPX has two modes:

- **Bench** (`run.py`): runs the scenes back to back for a fixed number of
  frames and logs `[WCS]` lines through `OSReport`. `run.py` turns them into a
  per-scene report and compares it with a baseline.
- **Interactive** (GUI launch, or any launch without `mode=bench`): a viewer
  with an on-screen overlay. You switch scenes with the GamePad and run each
  scene's checks on demand. Each probe frame stays on screen with the expected
  colour as a swatch next to the pixel under test.

## Quick start

All commands run from the repository root. Prerequisites: devkitPPC + wut in
`/opt/devkitpro` (or `DEVKITPRO`), Python 3, and the Vulkan/Qt dependencies
WEMU already needs.

```sh
# 1. Build (the first line is needed only once)
tests/rpx-suite/tools/latte-assembler/setup.sh   # shader assembler
cmake --build build -j"$(nproc)"                  # WEMU core + GUI
tests/rpx-suite/build.sh --install                # suite RPX, also copied to games/wemu-suite

# 2. Without the GUI: all scenes, best of 3 runs, all metrics
tests/rpx-suite/run.py --save-baseline   # once per machine: baselines/<host>-<cpu>.json
tests/rpx-suite/run.py                   # later runs compare with it; exit 1 on a regression
xdg-open "$(ls -d tests/rpx-suite/results/*/ | tail -1)summary.html"

# 3. With the GUI: frame profiler on, then the - sequence in the viewer
WEMU_FRAME_PROFILE=1 build/gui/appgui
#    open "WEMU RPX Test Suite", press -, wait for the summary screen, A, then +
tests/rpx-suite/run.py --analyze-log "$(ls -t ~/.local/share/WEMU/WEMU/logs/session-*.log | head -1)"
xdg-open "$(ls -d tests/rpx-suite/results/session-*/ | tail -1)summary.html"
```

The GUI must be built from a revision that passes `WEMU_FRAME_PROFILE` through
(`gui/src/emulator/LaunchEnvironment.hpp`). Otherwise the GUI report has no FPS
figures. The bench sets the variable itself.

### Known failures (WEMU bugs the suite exposes, November 2026)

| scene | probe or check | cause in WEMU |
|---|---|---|
| `forward_lit` | `texture_mip_level3` | `GX2CopySurface` rejects mip levels above 0, so they are never uploaded |
| `textures` | `format_RGB5A1`, `format_RGBA4`, `format_RGB10A2` | these formats are decoded as RGBA8 |
| `postfx` | `post_hdr_clear`, `post_hdr_shader` | RGBA16F targets clamp at 1.0 |
| `loops` | `loop_32_iterations` | `LOOP_START`/`LOOP_END` are skipped |
| `branching`, `loops` (after another scene) | all probes | shader cache keyed on the reusable guest address of the shader (`Gx2Replayer.hpp` `m_vsCache`/`m_psCache`) |
| every probe | `guest readback` | `GX2CopySurface` runs on the CPU at call time, before rendering has landed in guest memory |

These are listed in `expectations.json`, each with the bug it points to in
`docs/quality/regressions.md`. `run.py` reports them as `known`, so only a new
failure fails a run. When a bug is fixed its probes are reported as `fixed`:
remove the entry then. Entries marked `order_dependent` pass when their scene
runs alone, so passing is not reported as fixed.

## What it exercises

Everything goes through the real OS and GX2 paths a retail title uses: its own
`GX2Init` command pool, render targets in MEM1, scan buffers in the foreground
bucket, a context state, ProcUI, FS reads from `/vol/content`, and multi-core
OSThreads. No libwhb graphics.

Shaders are **Latte (R7xx) assembly compiled offline into GFD `.gsh` files**,
loaded with `GFDGetVertexShader`/`GFDGetPixelShader`. That is the same binary
format retail games ship. They use uniform-block mode (KCACHE-locked constant
buffers, little-endian data), VLIW groups across x/y/z/w/t, PV/PS forwarding,
`DOT4`, transcendental units, `SAMPLE`/`SAMPLE_C`, and TEX destination swizzles.

| scene | what it loads |
|---|---|
| `forward_lit` | 64 textured objects + ground, 8 per-pixel Blinn-Phong lights, TV view plus a separate top-down DRC view |
| `textures` | 11 formats (R8, RG8, RGB565, RGB5A1, RGBA4, RGBA8, RGB10A2, BC1/3/4/5), plus a 2048² ground texture |
| `shadows` | depth-only pass from light 0 into a sampled 1024² depth map, 4-tap PCF via `SAMPLE_C` |
| `geometry` | ~1.4M triangles per frame from dense spheres with 32-bit indices |
| `draw_calls` | 1025 draws per frame, each with its own uniform block (submission/HLE cost) |
| `instancing` | 4096 animated instances in one draw (per-instance attributes, CPU-rebuilt every frame) |
| `overdraw` | 48 full-screen textured layers, alternating alpha/additive blending (fill rate) |
| `postfx` | RGBA16F scene → half-res bright pass → horizontal + vertical 9-tap blur → tonemap/vignette composite |
| `branching` | nested if/else through the predicate stack (`ALU_PUSH_BEFORE`, `JUMP`, `ELSE`, `ALU_POP_AFTER`) |
| `loops` | Julia set in a 32-iteration `LOOP_START`/`LOOP_END` loop driven by a loop constant |
| `cpu_jobs` | 12288 particles simulated on all three cores (message queues, mutex, atomics) into a dynamic vertex buffer |
| `stress` | shadows + 100 objects + particles + HDR post on TV, separate DRC view |
| `endurance` | the stress load held for 3600 frames: drift, stutters and memory growth |

## Checks (no reference images)

There is no real console to generate reference images, so correctness comes
from checks that need none:

- **Probes.** After a scene's timed frames, a deterministic frame is drawn. The
  RPX predicts its centre pixel with a CPU copy of the shader math (lighting,
  shadow occlusion, mip selection, bloom/tonemap) and logs the expected value.
  `run.py` checks it against the TV frame WEMU delivers. The RPX's own
  `GX2CopySurface` readback is reported next to it as `guest readback`.
- **Self-consistency.** The particle simulation is replayed on one core and
  compared bit-for-bit with the three-core result. The bounce counters updated
  through the mutex and through atomics must agree. Draw counts must match.
- **Errors.** Any `[WCS] error` line fails its scene.

## Building

Needs devkitPPC and wut (`/opt/devkitpro`, or set `DEVKITPRO`), Python 3, and
latte-assembler from decaf-emu. Build latte-assembler once:

```sh
tests/rpx-suite/tools/latte-assembler/setup.sh            # clones decaf-emu into its build dir
tests/rpx-suite/tools/latte-assembler/setup.sh ~/src/decaf-emu   # or use/refresh a checkout
```

`setup.sh` pins the decaf-emu commit and builds only the `assemble` command.
It also patches a decaf bug that silently dropped every `; $KEY = value`
metadata comment (attribute names, export counts, semantics).

```sh
tests/rpx-suite/build.sh      # -> tests/rpx-suite/build/game/code/wcs.rpx
```

`shaders/programs.py` generates the Latte assembly (uniform-block layouts must
match `src/world.c` / `src/postfx.c`). `shaders/latte.py` is the small builder it
uses. The C code is compiled with `-O2`, so the guest runs the instruction mix
of optimised retail code.

## Interactive viewer (GUI)

```sh
tests/rpx-suite/build.sh --install   # copies the game to games/wemu-suite
```

The game then shows up in the GUI library as "WEMU RPX Test Suite". Controls:

| button | action |
|---|---|
| L / R, D-pad left / right | previous / next scene |
| A | restart the scene, run it for 5 s, then its checks (probe screens: A skips) |
| − | full sequence: every scene for 600 frames, then its checks, then a summary screen |
| B | pause the animation |
| X | hide the overlay |
| + | quit |

The fps shown is guest time. The GUI paces to 60 Hz, so use `run.py` for
performance numbers. "PASS/FAIL self-checks" covers what the guest can verify
by itself (particle determinism, draw counts, errors). For probes, compare the
pixel inside the yellow box with the swatch; `run.py` checks the same probes
automatically. "guest readback" shows the RPX's own `GX2CopySurface` readback
(currently 0,0,0 on WEMU).

### FPS stability from a GUI session

The RPX can't time the host, because WEMU's guest clock advances once per frame.
To get FPS figures for a GUI run, start the GUI with `WEMU_FRAME_PROFILE=1`. That
is the one `WEMU_*` variable the GUI passes through, and it only measures. Run
the − sequence, then:

```sh
tests/rpx-suite/run.py --analyze-log ~/.local/share/WEMU/WEMU/logs/session-<time>.log
```

This produces the same report as the bench from the most recent sequence in the
log, measured at the GUI's real 60 Hz pacing, where missed refreshes are what
matters. Probes are listed as "not verified": a log has no frames to check them
against.

## Running the bench

Build the core first (`build/core/wemu`), then:

```sh
tests/rpx-suite/run.py                                    # all scenes, 600 frames, best of 3 runs
tests/rpx-suite/run.py --scenes shadows,postfx --frames 300 --repeat 1
tests/rpx-suite/run.py --save-baseline                    # record this machine's baseline
tests/rpx-suite/run.py --baseline none                    # no performance comparison
tests/rpx-suite/run.py --capture-every 300                # also save TV frames as PNG
```

`run.py` writes `mode=bench` to `build/game/content/suite.cfg` for the run and
restores the previous file afterwards. Results go to `tests/rpx-suite/results/<timestamp>/`: `summary.html`,
`summary.md`, `result.json`, `junit.xml`, and the core log for each run. Timing comes from the core's
`WEMU_FRAME_PROFILE` records between each scene's begin/end markers. The first
60 frames of a scene (shader compilation) are excluded, and the median frame
time counts. Runs are unthrottled (`WEMU_UNTHROTTLED=1`), and the runner
acknowledges every frame immediately, so window presentation and vsync are not
measured.

Every report opens with the verdict: `PASS`, `FAIL` (a check that is not a
known failure failed, or a run did not finish) or `SLOWER` (a scene is slower
than the baseline). `summary.html` follows it with four headline tiles, one
table of scenes with the change against the baseline and a trend sparkline
over earlier runs, then frame-time charts per scene with the 60/30 fps lines,
a stability and memory table, probe swatches, and warnings. The trend and the
`vs previous run` column only use earlier runs in `results/` from the same
machine with the same scene list and `--repeat`. The stability figures:

- **1% low**: fps of the slowest 1% of frames.
- **jitter**: standard deviation of frame time.
- **stutters**: frames over twice the median that also miss a 60 Hz refresh.
- **missed 60 Hz**: frames over 20.8 ms.
- **stable**: frames within 25% (at least 2 ms) of the median.
- **drift**: fps of the last quarter against the first.
- **memory**: resident size of the core, sampled twice a second.

Warnings (unstable pacing, stutters, drift worse than −10%, more than 64 MiB of
memory growth in a scene) are reported but never fail a run.

The exit code is 0 for `PASS` and 1 otherwise.

**Baseline.** `--save-baseline` writes `baselines/<host>-<cpu>.json` (not in git: it only fits one machine), and later
runs on that machine pick it up by themselves (`--baseline PATH` or
`--baseline none` to override). It is not saved when the run has new failures
or the machine state is unreliable (`--force` overrides the second). A scene
is only compared when it ran the same number of frames as in the baseline, and
a baseline of a subset (`--scenes`/`--frames`) needs an explicit path.

**Machine state.** Each run records the CPU, GPU, power source, power profile,
governor and load. On battery, in a power-saving profile or on a busy host the
report says "performance not judged": checks still count, slower scenes are
listed but do not change the verdict.

**Noise.** The fastest of the `--repeat 3` runs counts, and the `run spread`
column shows how much the runs disagreed. Scenes that load the CPU repeat
within a few percent. GPU-light scenes do not: on a laptop the same scene can
run at two speeds from one run to the next (about 110 and 250 fps for
`postfx`), and the whole difference is GPU wait time. Pinning the core to the
performance cores of a hybrid CPU does not remove it (`--performance-cores`
is there to retest). So a scene counts as slower or faster only when the
change is beyond `--tolerance` (default 10%) and its runs do not overlap the
baseline's runs; otherwise it is reported as noise. The speed index only
averages scenes whose spread stays under 15% in both.

## Adding a scene

Add an entry to `SCENES` in `src/scenes.c`; it appears in both modes. The fields are `init`, `render`
(one frame; return true if the DRC target was drawn), `check` (probes and
self-checks), and `shutdown`. Reuse the `arena`/`world`, `postfx` and
`particles` modules where you can. New shader programs go in
`shaders/programs.py`, `src/shaders.h` and the `PROGRAMS` table.
