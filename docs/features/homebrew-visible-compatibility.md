# RPX homebrew compatibility: October 27, 2026

**Newer results:** [runtime and presentation fixes](homebrew-runtime-fixes.md).
The limitations below describe the earlier implementation.

This supersedes the boot-only results in the earlier homebrew notes. All three
installed RPX files still match their original upstream SHA-256 hashes. No
game-specific patches, title checks, or boot hooks were added.

## Desktop results

| Application | Observed result | Remaining limitation |
| --- | --- | --- |
| Space Game RPX beta | Desktop rendering and timed button/stick input; 300-frame check | Small game area in TV output; this upstream beta has no audio |
| Flappy Bird GX2 | Full scene renders, A starts gameplay, bird and pipes move | About 3.7 FPS in the desktop probe; multisampled GamePad passes use software rasterization |
| Blockamok Remix | Title screen, Start enters gameplay, controls respond; 720-frame desktop run around 58 FPS | Game view occupies 960×540 of the 1280×720 TV output; audio has not been checked by listening |

These are short functional checks, not complete game compatibility certification.
Flappy is not yet running at a usable gameplay speed.

## Shared corrections

- OSScreen uses two buffers per display, alternates the drawing buffer on flips,
  and respects the GamePad's 896-pixel storage stride and 854-pixel visible width.
- PPC `lhbrx` and `sthbrx` work in the interpreter and x64 JIT.
- Thread suspension has a nesting count independent of sleeping and waiting.
  Resuming a thread no longer wakes an unrelated sleep or synchronization wait.
- Expanded-heap allocation-size queries use live allocation metadata.
- Frame heaps maintain head/tail cursors and guest-visible checkpoint chains.
  Recording a checkpoint reserves its storage; restoring releases the checkpoint
  and all newer allocations at either end.
- GX2 reports connected TV/GamePad modes and supports GX2R allocation callbacks,
  owned and caller-owned surfaces, and surface locking. Custom resource callbacks
  receive resource flags, size and alignment rather than the default-heap ABI.
- Pixel shaders receive screen-space position when `SPI_PS_IN_CONTROL_0` requests
  it. Both reference and native rendering honor the selected register; native
  shader caches distinguish this state, and GX2 context switches preserve it.

Flappy's scene was already correct before its last full-screen pass. That pass
requests pixel position in R0; replacing that input with an ordinary interpolant
caused the solid pink output. Blockamok's graphics initialization first failed
because frame-heap checkpoint recording was unimplemented, then because GX2R
surface creation was missing.

API layouts were checked against local WUT headers, the
[WUT graphics heap implementation](https://github.com/devkitPro/wut/blob/master/libraries/libwhb/src/gfx_heap.c),
[Cemu frame heaps](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/coreinit/coreinit_MEM_FrmHeap.cpp),
[Cemu OSScreen](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/coreinit/coreinit_OSScreen.cpp),
and [Decaf SPI register definitions](https://github.com/decaf-emu/decaf-emu/blob/master/src/libgpu/latte/latte_registers_spi.h).

## Commercial checks and evidence

MK8, Smash and Pikmin 3 completed the same 180-frame x64 startup runs as the saved
baseline. Captures at frames 60, 120 and 180 are byte-identical for every game:
all nine comparisons match. This covers the sampled startup/rendering paths;
it does not establish absence of regressions throughout all gameplay or measure
sustained performance under identical host load.

Evidence is under `reports/homebrew-visible/`: `comparison.json`, original RPX
hashes, build/test logs, reference-renderer captures, and actual Qt desktop
captures in `desktop-space/`, `desktop-flappy/`, and `desktop-blockamok/`.
The desktop JSON configurations and input scripts reproduce those checks using
`QT_WEMU_SESSION_PROBE`, with wall-time input for menus that do not present frames.

The final focused run passes 78 tests, including the new native pixel-position,
resource-callback ABI, surface ownership/mips, frame-heap rollback, suspension,
and byte-reversed halfword cases. The broader graphics/API run passed 183 cases;
its one new-test failure was a half-step UNORM rounding expectation (127 versus
128), corrected to allow either valid rounding and rerun successfully.

The next compatibility work is correct accelerated multisampling and investigation
of SDL's game-view placement. Neither is worked around by changing game binaries
or by accepting frame counts as proof of correct output. Doom remains on hold.
