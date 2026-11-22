# Locked-cache memory and DMA

Coreinit now provides real per-core locked-cache allocation and transfers instead
of returning null buffers. Each of the three emulated cores has 16 KiB at
`0xFFC00000`, `0xFFC40000`, or `0xFFC80000`. Memory owns separate backing bytes;
checked guest loads/stores and HLE pointers can access each bank, while the gaps
and crossing accesses remain unmapped.

Allocation rounds positive requests up to 512 bytes. Allocations, largest free
runs, total free space and nested DMA enable counts belong to each Interpreter
session and core. Freeing an interior, unaligned, other-core or already freed
pointer leaves allocation state unchanged. The scheduler's affinity-derived
core ID selects the bank; enabling DMA requires affinity to that single core.

`LCLoadDMABlocks` and `LCStoreDMABlocks` copy actual bytes synchronously. Counts
are 32-byte blocks, with zero meaning 128 blocks. Both complete memory ranges
are checked before any write. Invalid ranges raise the existing MemoryException;
there is no null-pointer workaround. Completed transfers leave queue length zero,
so `LCWaitDMAQueue` observes completion immediately. No artificial delays, guest
clock changes, media skips or title-specific code changes are involved.

The present memory model is coherent host RAM; this implementation does not
model noncoherent CPU cache lines, asynchronous DMA timing or a multicore host
scheduler. Accesses outside the native PPC memory descriptors use precise
interpreter fallback. Tests cover allocation exhaustion/fragmentation, per-core
isolation, nested enables, actual transfers in both directions, zero counts,
boundary faults and native fallback.

API behavior and bank geometry were cross-checked against the primary
[Cemu implementation](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/coreinit/coreinit_LockedCache.cpp)
and [Decaf implementation](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/coreinit/coreinit_lockedcache.cpp).
