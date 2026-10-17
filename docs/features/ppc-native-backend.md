# Shared PPC native backend

The experimental LLVM 19 backend uses one integer frontend and lowering for
runtime JIT, emitted host objects and persistent object caching. Enable the build
with `-DWEMU_ENABLE_LLVM=ON`. `WEMU_NATIVE_CPU=1` selects hot native prefixes;
the interpreter handles unsupported instructions and stays available as the
reference. LLVM-enabled desktop builds select native execution by default and
retain the interpreter for unsupported instructions and failed compilation. Builds without
LLVM use the interpreter. Set the desktop QSettings key `emulation/nativeCpu` to
`false` for a reference launch; inherited `WEMU_*` experiments stay isolated.

The integer frontend also handles signed/unsigned comparisons, CR record
forms, sign extension, negation, count-leading-zero, LR/CTR moves, direct
branches and conditional branches/returns without link. The execution-state ABI
carries CR, summary overflow, LR, CTR and the next PC. LR retains the current
interpreter's offset representation. CTR indirect branches stay interpreted
because they can dispatch HLE imports. Conditional link forms, carry/overflow
updates and floating-point operations also retain interpreter execution.

Supported memory operations include byte, halfword and word loads/stores,
signed halfword loads, immediate/indexed addressing and valid address-update
forms. Native code receives main RAM, stack and import-region descriptors.
Main RAM uses a checked fast path; other region descriptors are examined only
when that path does not fit. Subtraction-based bounds checks precede every
access, including unaligned accesses and region ends. Loads/stores preserve guest
big-endian values. No
unchecked guest address becomes a host dereference.

On an unmapped access the native function returns the number of completed
instructions, commits their register changes and leaves the failing instruction
untouched. The interpreter executes the remaining decoded suffix, including its
existing precise fault policy, without fetching and dispatching it again.
Update registers change only after a successful access. Interpreter blocks
terminate at stores. Native lookahead can extend through stores only while each
store guards the complete native instruction range.
A write touching any of that range exits immediately after the store, commits
register/address updates and returns at the following instruction for refetch.
Hook boundaries, instruction budgets and live-code validation remain in the
interpreter/dispatcher.

Decoded versions become eligible after 256 native dispatch opportunities;
this admission check occurs before inserting into the native cache. Cold versions
cannot displace compiled code or cause repeated object linking. Replacing any
validated instruction resets that version's admission count.

The runtime cache holds at most 16,384 entries and requires a supported prefix
of at least four instructions to amortize native dispatch. Short supported
backward loops can qualify because multiple budgeted iterations amortize the
call. New live-code versions are decoded once; hot execution still validates
their words but skips repeated frontend decoding. A decoded block keeps a
non-owning binding to its compiled native code,
avoiding the second cache lookup and per-call reference counting while the
interpreter still validates live words. Bindings carry a dispatcher generation.
Evicting/invalidation of compiled code changes that generation before releasing
code; a new dispatcher gets a distinct generation. Each lookup checks the
generation before dereferencing the pointer. This dispatcher runs on the single
emulation thread and compiled code cannot reenter it. Cache ownership stays
bounded. Tests can select smaller cache/prefix limits explicitly.

LLVM O2 runs before both JIT and object emission. The host ABI uses an explicit
execution-state structure and returns an executed-instruction count. Cache
identity includes this ABI/semantics version, compiler-source fingerprint, LLVM
version, host architecture/features and data layout. Older register-only objects
cannot be reused under the new ABI. Persistent objects are trusted compiler output,
never guest code; existing integrity/permission checks remain in place.

The desktop launcher stores compiler-produced objects in
`QStandardPaths::CacheLocation/ppc-native` (normally
`~/.cache/WEMU/WEMU/ppc-native` on Linux). The core creates the leaf with mode
0700 and checks ownership and permissions; unavailable caches fall back to JIT.
The cache is shared across titles because keys include validated guest words and
PC, compiler semantics and host capabilities. It stores native code, not saves or
a snapshot of game state: every launch still executes the real boot. First-use
compilation can be slow; subsequent launches link compatible cached objects.
A normal session exit reports `[NATIVECACHE] session hits/misses/writes` in its log.
For standalone experiments, set `WEMU_NATIVE_CACHE_DIR` to a private directory to
retain compiler-produced objects. `WEMU_NATIVE_PROFILE=1` adds prefix-length, unsupported-opcode and native
cache/dispatch statistics; it adds CPU bookkeeping and should be disabled for
headline on/off timing comparisons. Unsupported floating-point, flags and control
flow still use the interpreter. This backend is shared across games and contains
no title-specific patches or timing changes.

## Bounded backward branches

Fully supported blocks can repeat a direct backward branch to
their own start inside native code. Each invocation executes whole iterations
within the current instruction budget, capped at 64 retired instructions.
Unsupported instructions, code writes, HLE dispatch and branch targets outside the
validated block return to the interpreter's dispatch loop. No additional code
range is entered without live-word/hook validation.

The native function returns the actual retired count and next PC. A load fault
commits completed iterations and the current iteration's completed prefix before
returning at the failing instruction. The interpreter retains its fault handling
and the existing service/preemption boundary. Standalone native execution with a
zero budget retains one-block behavior. ABI4 and the source fingerprint invalidate
objects emitted under earlier calling conventions.

Native lookahead is enabled only when native execution is active. It retains the
interpreter's original predecoded store boundary and validates every additional
live instruction word before using a compiled binding. Hook addresses and
unsupported instructions stop lookahead. Semantics version 5 separates objects
compiled before cross-store execution and code-write exits.

Locked-cache memory is mapped by the shared memory service. It is currently
outside the native ABI's three region descriptors, so a native load/store exits
precisely before that access and the interpreter completes it. Existing cached
objects keep their ABI and remain usable.

## PC observation during native execution

`WEMU_WATCH_PC` can observe absolute guest PCs while retaining native execution
and persistent caching elsewhere. Decoded blocks and native lookahead stop before
an observed address. A block starting at that address executes through the
reference handlers, so native loop repetition cannot hide repeated observations.
Changing the observer list clears decoded bindings, including on interpreter
reset. Word/register/string watches still require instruction-by-instruction
execution.
