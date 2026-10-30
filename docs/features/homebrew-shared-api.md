# Shared RPX homebrew API exploration

**Updated results:** see [desktop compatibility validation](homebrew-visible-compatibility.md).
The results below describe an earlier pass and are retained as history.

The implementation uses the same API behavior for every title. It contains no
game-name checks, game-specific instruction replacements, or per-title boot
hooks. The installed Flappy Bird, Space Game and Blockamok Remix executables
remain byte-for-byte identical to their upstream RPX builds.

## General changes

- `OSSwapAtomic` exchanges a guest 32-bit value and returns its previous value.
- `OSAddAtomic` updates a guest 32-bit value with wraparound and returns its
  previous value. A host HLE call cannot be preempted by another guest thread,
  so the read/modify/write is atomic under the current execution model.
- `OSIsThreadSuspended` reads the scheduler's authoritative state. A sleeping,
  ready, running or finished thread is not reported as suspended.
- `OSGetThreadAffinity` returns the scheduler's affinity mask, including changes
  and handle reuse. Unknown handles return zero rather than invented state.
- Socket creation, connection, sending, receiving and related operations now
  return `-1` when unavailable. WEMU still has no host socket backend. Returning
  zero previously invented a usable descriptor or reported a zero-byte send,
  which could trap normal logging loops. Guest socket errno remains unmodeled;
  this is explicit offline failure behavior, not network support.

Signatures and atomic return semantics were checked against the primary
[WUT atomic declarations](https://github.com/devkitPro/wut/blob/master/include/coreinit/atomic.h),
[Decaf atomic implementation](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/coreinit/coreinit_atomic.cpp),
and [WUT thread declarations](https://github.com/devkitPro/wut/blob/master/include/coreinit/thread.h).

## Validation

The core and test executable build successfully. All 69 tests in the selected
filesystem, loader, memory, scheduler and system-configuration suites pass:

```sh
build/core/tests/wemu_tests \
  --gtest_filter='SchedulerTest.*:SystemConfigTest.*:FsTest.*:LoaderTest.*:MemoryTest.*'
```

New coverage checks atomic return values, signed increments and 32-bit overflow;
thread creation/resume/sleep/exit/reuse and affinity; and socket failure without
modifying receive buffers or output lengths. Existing scheduler tests still pass.

The saved baseline executable and changed core each completed 180 frames for
MK8, Smash and Pikmin 3 with the same probe options. Captures at frames 60, 120
and 180 are byte-identical for each game: all nine comparisons match. Both cores
exit cleanly in all six startup runs. Smash and Pikmin also deliver the same
numbers of movie frames in the measured windows. Space Game completes another
300-frame run with timed button, stick and touch input.

These are bounded startup/rendering comparisons, not full gameplay testing.
Host timing varies between runs; no speedup or sustained-performance guarantee
is inferred from the recorded rates.

## What this exposes next

Original Flappy Bird now gets beyond its logger and starts its sound worker
without a game patch. It still produces no frames: `OSSuspendThread` is missing,
and the AX callback accesses an uninitialized sound-handler pointer. The
remaining work needs general suspend/resume semantics and audio initialization
ordering, with tests for nested suspension and blocking/wakeup interactions.
Heap-size queries are also missing.

Blockamok still produces no frames. Its atomic and affinity calls are now
handled, but GX2 display/allocator and frame-heap APIs remain missing. The
instruction at guest PC `0x020DB368` decodes as **`sthbrx`**, a normal PPC
byte-reversed halfword store that the CPU does not currently implement. A
future correction should implement that instruction's semantics and test CPU
backend agreement; replacing that instruction in a particular game is not
appropriate.

No further GPU, scheduler suspension, heap or CPU-instruction changes were made
in this pass. Those require separate focused changes and commercial regression
checks. Doom work remains stopped.

## Local evidence

`reports/homebrew-shared-api/` contains:

- `wemu-baseline`: the emulator executable saved before these shared API changes.
- `original-homebrew-sha256.json`: original game hashes, verified after testing.
- `focused-tests.log`: 69 passing tests.
- `comparison.json`: before/after summaries and all nine capture comparisons.
- `baseline-{mk8,smash,pikmin3}/` and `commercial-{mk8,smash,pikmin3}/`: boot evidence.
- `flappy-original/`, `blockamok-original/`, `space-original/`: unmodified RPX probes.

The earlier game-patching experiment is archived as text in this report
directory and has been removed from active workspace utilities. No patched
game executable is installed or selected by the desktop library.
