# PowerPC x86-64 recompiler

`WEMU_X64_JIT=1` compiles the interpreter's decoded blocks to x86-64 and runs
them chained, returning to the interpreter loop only for what the loop handles:
service ticks and preemption, HLE hooks and syscalls, return sentinels, code
writes and faults. It targets Linux x86-64 only (System V ABI, `mmap` and
`mprotect`) and needs FMA3, AVX2, BMI2, LZCNT and MOVBE; elsewhere, or when code
memory cannot be mapped, the interpreter runs as before. The CMake option
`WEMU_ENABLE_X64_JIT` (default ON) builds it; the code lives in
`core/src/cpu/jit/` and uses the vendored Xbyak assembler
(`core/third_party/xbyak/`). It replaces the experimental LLVM backend when both
are requested.

## Execution model

- **Same blocks as the interpreter.** Blocks are compiled from
  `Interpreter::getBlock()`, so they end at the same branches, hooks and
  64-instruction cap, and the code pages they cover are marked the same way.
  Retired-instruction counting, the service-tick budget and spin-yield
  bookkeeping follow the interpreter block by block, so a compiled run takes
  the same preemption decisions. Spin-yield keeps a deadline (last block with
  stores + threshold) that store-less blocks compare against.
- **Guest state stays in the `Interpreter`.** Generated code addresses `m_gpr`,
  `m_fpr`, `m_ps1`, CR, XER, LR, CTR, GQRs and FPSCR through `rbx`; HLE, the
  scheduler's context switches and diagnostics see the usual fields. The
  interpreter's representations are kept: LR as an offset from
  `ApplicationCode`, XER and FPSCR in their `Registers.hpp` layouts.
- **Host registers.** `rbx` guest state, `r12` main-RAM bias, `r13` retired
  count, `r14` slow-page bitmap, `r15` `Jit::Context`. An entry stub saves the
  callee-saved registers and blocks leave through shared exit stubs. No C++
  exception crosses generated code.
- **Chaining.** A block exit to a constant pc is a direct jump, patched once the
  target is compiled (and reset when the target is invalidated); register
  branches (`blr`, `bctr`) probe a direct-mapped pc-to-code table inline. A
  `bc` right after a compare tests the CR value still in a register.
- **HLE without leaving.** After `sc` or a `bctr` to an import, compiled code goes
  on unless the call switched threads, stopped the interpreter, wrote decoded
  code or changed the hooks. Hooked addresses get a lookup-table entry that runs
  the hook and continues at LR.
- **Call-outs.** Instructions without a native emitter call their interpreter
  handler through a per-instruction `noexcept` wrapper (`Callouts.cpp`) that
  reproduces `runBlock`: guest exceptions follow
  `Interpreter::handleBlockException`, other exceptions are parked and rethrown
  once generated code returned, a store into decoded code and an HLE call (`sc`,
  `bcctr` to an import) leave the block.

## Memory

Loads and stores to main RAM run inline (bounds check, `movbe`). Stores test
Memory's slow-page bitmap first: code pages and watched pages, and stores
crossing a page, go through `Memory::write`, so code epochs and watched-page
write generations stay exact and `setUntrackedStores` is not needed. The stack
region is accessed inline from cold code; other regions go through `hostPtr`.
An unmapped address leaves the block before the instruction (`Suffix` exit) and
the interpreter executes the rest of the block, raising the usual
`MemoryException` with its thread-kill policy.

## Invalidation

A new `Memory::codeEpoch()` (some store or HLE wrote a page holding decoded code)
unlinks every compiled block and clears the lookup table, so no block runs again
before `prepare()` compares its words with memory: unchanged blocks are relinked,
changed ones are compiled again. Blocks need no check of their own on entry. A
store into decoded code leaves right after the store, as in `runBlock`. A change
in the number of HLE hooks, `clearBlockCache()`, a new spin-yield threshold and a
full code buffer drop all code. Code epochs do not change during a MK8 race.

## Native coverage

Integer arithmetic, logic, rotations, shifts and compares (OE forms use
call-outs), CR logic and moves, LR/CTR/XER/GQR moves, integer and floating-point
loads and stores (update, indexed and byte-reversed forms), direct, conditional
and register branches, scalar floating point (single results rounded through
`cvtsd2ss`/`cvtss2sd` like the interpreter's casts), paired singles and
`psq_l`/`psq_st` with float GQR types. Which NaN an arithmetic operation returns
depends on how the compiler ordered the interpreter's operands, so an arithmetic
result that is NaN replays the instruction through its handler: results stay
bit-identical.
Integer GQR types, `lmw`/`stmw`, string, reservation and cache-zeroing
instructions, time-base reads and syscalls use call-outs.

## Diagnostics

- `WEMU_JIT_PROFILE=1`: every two seconds, compiled blocks, exits by reason and
  the executed instruction mix (`[X64JIT]`, `[X64JIT_MIX]`).
- `WEMU_JIT_CALLOUT=integer,compare,load,store,branch,system,float,paired` (or
  `all`): keeps those families on call-outs, to bisect a divergence in a game.
- `WEMU_JIT_PERF_MAP=1`: writes `/tmp/perf-<pid>.map` so `perf report` names
  compiled blocks (`ppc_<guest address>`).
- Per-dispatch PC diagnostics (`WEMU_WATCH_PC` and the other trace switches)
  keep the interpreter.

## Tests

`core/tests/jit/` runs each block through `runBlock` and through the recompiler
from identical states and compares registers (floating point bit for bit),
memory windows, retired counts and exits. Generators cover every native family
with random encodings and edge values (carries, overflow, shift amounts, NaN
payloads, denormals, conversion limits), and addresses in RAM, on the stack, in
code pages, across pages and unmapped.
