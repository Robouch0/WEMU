# Application startup and worker synchronization

An RPX may export `__preinit_user` to prepare application heaps before its entry
point runs. WEMU resolves this function through the executable export table,
including relocated function addresses, without requiring debug symbols.

The boot call runs on the main guest thread with writable references to the
MEM1, foreground and MEM2 heap handles in r3, r4 and r5. Returning commits the
handles and restores the original entry arguments, stack pointer and return
address. Applications without the export start at their entry point directly.
The main stack backs the full megabyte below its initial pointer, including
frames larger than 64 KB.

Worker initialization also relies on these HLE behaviors:

- A thread's return value reaches `OSJoinThread` callers that join before or
  after completion.
- Condition waits release recursive mutex ownership while sleeping and restore
  it before returning to the guest.
- Semaphore waits consume one permit after a signal wakes them.
- DMA memory copies handle word counts, overlap, endian modes and completion
  timestamps.
- `inflateInit_` initializes the existing host zlib bridge.
- Offline friend enumeration initializes its output count to zero.
- `FSGetStatAsync` delivers the actual operation status through a completion
  callback or an `FSAsyncResult` message, including missing optional files.
  Returning submission success without completion leaves asset workers waiting.

The GX2R buffer bindings, geometry-ring sizing and packed blend, color and depth
register paths supply the vertex data and state needed for startup rendering.
Integer arguments beyond r10 are read from the PPC stack parameter area.
GX2R-owned buffers use the writable default allocator function pointers when an
application replaces them during preinit. Allocator callbacks run on the calling
guest thread and restore its stack and return address before completing the
buffer operation. User-backed buffers retain caller ownership on destruction.

The local Smash Bros. check now plays the complete opening movie, reaches the
title, main menu, Smash submenu and fighter roster, and accepts Player 1 input.
Skipping the movie also stops decoding and transitions through the title into
the menu. These checks use the Qt session transport with the desktop launch
environment, including deferred/lazy readback. See
[GX2 movie rendering](gx2-movie-rendering.md) for the shared rendering and
scheduling fixes, separate present/movie measurements and reproduction steps.
Match gameplay and complete playthroughs remain unverified. Short Mario Kart 8
and Pikmin 3 launches continue submitting frames without crash contexts.

Regression coverage includes a synthetic RPX export and entry sequence, worker
return values, recursive condition waits, semaphore permits, DMA copies, zlib
decompression, offline enumeration, stack bounds and captured GX2 register and
buffer state, asynchronous filesystem completions and guest replacement
allocators. Run the checks with:

```sh
ctest --test-dir build --output-on-failure --parallel 1
```
