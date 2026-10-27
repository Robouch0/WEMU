# Homebrew runtime and presentation fixes

Updated October 27, 2026. This supersedes the performance and startup limitations
in [the earlier desktop checks](homebrew-visible-compatibility.md).

## Observed results

- Flappy Bird GX2: 360 desktop frames at 59.2 FPS, compared with 3.7 FPS before.
  A separate 600-frame run measured 58.6 FPS. Input starts gameplay. Its audio
  diagnostics recorded no dropped frames and one 384-sample underrun; this is
  not a listening-based assessment of all music and effects.
- Crispy Doom U: Doom shareware now renders gameplay. A 1,200-frame desktop run
  measured 58.0 FPS, with menu selection and movement/turn input. The selected
  title's `sdcard/` directory is mounted automatically by the desktop launcher.
- Blockamok Remix: completed 2,400 desktop frames without the previous allocation
  overrun, averaging 53.5 FPS during a run sharing the host with builds/probes.
  A separate 1,800-frame run measured 59.1 FPS. Its game view fills the TV output.
  These bounded runs do not prove that every possible crash is fixed.
- Space Game: desktop screenshots match the delivered framebuffer exactly below
  the FPS overlay. RGBX padding is normalized so zero padding cannot blend old
  frames into the new image. The old installed RPX beta still draws into only
  part of its TV canvas; WEMU does not guess a crop from the nonblack pixels.

RPX is an executable format, not a resolution. Smaller GX2 render targets now
scale to the TV output instead of being pasted into its upper-left corner.
An application can still deliberately draw borders or use only part of its
own full-size canvas. Doom's 4:3 menus can legitimately have side borders.

## Shared implementation changes

**CPU decoding and loading.** Cache operations used nine-bit opcode matches,
which made `dcbt` also match `lhbrx`. The halfword load therefore silently did
nothing through the decoder even though direct calls to its implementation
worked. Doom read stale register values as texture patch counts and requested
invalid allocation sizes. Cache operations now use their full ten-bit opcodes;
`eieio` also uses its correct opcode. Tests go through decoding and execution.
Cafe's Green Hills relative-halfword relocations 251–253 are also applied using
their RPL numbering, with a synthetic RPX regression fixture. See the primary
[Decaf relocation implementation](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/loader/cafe_loader_reloc.cpp).

**Allocation.** Complete WUT runtimes retain their own initialized allocator and
sbrk arena. Replacing them with the HLE bump pool bypassed their memory ownership
and never reclaimed allocations. Fallback HLE allocations now support coalescing
free ranges, aligned reuse, checked allocation bounds, and realloc preservation.
The recorded Blockamok crash wrote through address `0x6D000000` in
`WIIU_SDL_QueueGeometry` after repeated allocations exhausted mapped RAM.

**Audio and timers.** Streaming voices can loop into a second buffer located
above the old end marker, then receive a new end marker in the next callback.
The old implementation stopped such voices. End markers now use address equality
and permit that streaming pattern. OS alarms also fire on the guest time base
through the deferred callback pump, with corrected aligned arguments, cancellation,
periods, user data and a valid context snapshot. This supports SDL timed waits;
callbacks remain cooperatively dispatched rather than hardware interrupts.

**Graphics.** The reference renderer already represents AA targets as resolved
per-pixel color. Those targets can now use accelerated shading with the same
representation; resolved depth is supported on both paths. This removes the
software bottleneck in Flappy's GamePad pass. It is not full hardware MSAA:
per-sample coverage and multisample texture fetches remain unsupported. GPU/reference
tests cover the resolved representation. Smaller TV targets are resampled, and
Qt receives opaque RGBX pixels rather than treating guest padding as alpha.

No game-name checks, instruction replacements in game files, or title-specific
boot hooks were added. The original RPX files remain installed.

## Verification

The final focused run passes 81 CPU, loader, allocator, voice, alarm/scheduler,
graphics and x64 memory tests with Vulkan checks enabled. All six GUI protocol
tests pass, including opaque scanout, along with desktop launch-environment tests.

Pre-commit Linux validation also passes the complete CTest suites for GCC 14
full core/GUI and standalone core builds with LLVM 19 enabled and disabled,
using Mesa lavapipe for headless Vulkan. Clang-format 19 passes all 316 core
source/test files, and clang-tidy 19 passes all 101 core translation units with
warnings treated as errors. The optional movie-audio fixture remains skipped
when local media is absent. macOS jobs require the remote CI runner.

MK8, Smash and Pikmin 3 each completed the same 180-frame startup comparison.
All nine captures at frames 60, 120 and 180 match the previous working build
byte-for-byte. This covers sampled startup rendering, not every retail gameplay
path or sustained performance under identical host load.

Build/test logs, crash evidence, desktop capture configurations, source frames,
audio diagnostics and `comparison-final.json` are under `reports/homebrew-next/`.
Desktop captures are under `desktop-{flappy,doom,blockamok,space}/`.
