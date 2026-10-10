/*
** EPITECH PROJECT, 2026
** core
** File description:
** Diagnostics -- boot-triage tooling: unknown-import tracking, HLE call trace, crash backtrace
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Core {
    class Interpreter;
}

namespace Core::Diag {

    // Records a call to an import that has no HLE handler. Logs the first occurrence (with the
    // argument registers) and counts the rest; dumpUnknownImports() prints the aggregate.
    void noteUnknownImport(const std::string &name, const Interpreter &cpu);

    // Prints every unknown import hit during the run, sorted by call count (the HLE worklist).
    void dumpUnknownImports();

    // True when WEMU_TRACE_HLE=1: Sc logs every handled HLE call with r3..r6.
    bool traceHle();

    // WEMU_WATCH_PC="2672d98,25b8170": guest PCs to announce when first executed (max 20 hits each).
    const std::vector<std::uint32_t> &watchPcs();
    void noteWatchHit(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_WATCH_WORD="101d0d64": guest words polled every step; logs PC when a value changes.
    // WEMU_WATCH_WORD_INIT=1 also logs the first readable value.
    const std::vector<std::uint32_t> &watchWords();
    void pollWatchWords(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_WATCH_CSTR="24e9334:r4,2439fb8:*r4,2439fb8:@101d3320": when PC is
    // reached, print the NUL-terminated guest string pointed to by the selected
    // GPR, by *(GPR), or by a literal guest address. Diagnostic only.
    bool hasWatchCstrs();
    void noteWatchCstrs(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_WATCH_REG="20e4bac:r3=0,2126054:r29": when PC is reached, print
    // registers/backtrace only if the selected GPR matches the value. If =HEX is
    // omitted, zero is assumed. Diagnostic only.
    bool hasWatchRegs();
    void noteWatchRegs(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_PC_COUNT="25467d4,2547174": guest PCs whose execution count is tallied over the whole
    // run (no per-hit dump) and reported by dumpPcCounts() at exit. Answers "does this run once or
    // every frame?" cheaply where WATCH_PC's per-hit register dumps are too heavy.
    const std::vector<std::uint32_t> &countPcs();
    void notePcCount(std::uint32_t ppcPc);
    void dumpPcCounts();

    // Prints the WEMU_PC_COUNT tallies from a SIGINT/SIGTERM handler, so a boot that hangs and is
    // killed by `timeout` still reports its counts (dumpPcCounts() only runs on a clean return).
    // No-op unless WEMU_PC_COUNT is set.
    void installPcCountSignalDump();

    // EXPERIMENT (WEMU_ORDER_FIX=1), default OFF: MK8 evaluates its title sequence-change decision
    // exactly once, and in our boot that lands a few instructions BEFORE the menu transition object
    // is installed -- so the decision reads the previous object's pending code (2) instead of the
    // menu's (3), schedules nothing, and is never revisited. While enabled and the pending code is
    // not yet 3, deferTitleSeqCalc() returns true at the sequence calc and the run loop returns to
    // LR instead of entering it, so MK8 simply retries the decision on a later frame.
    bool titleSeqOrderFix();
    bool deferTitleSeqCalc(Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_HEARTBEAT="50000000" (decimal instruction count): periodically log the current PC/LR
    // so long silent runs show whether the guest is progressing or spinning. 0 = disabled.
    std::uint64_t heartbeatInterval();
    void pollHeartbeat(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_RES_TRACE=1: trace the transition-resource lookup helper at 0x024D3054. Optional
    // WEMU_RES_TRACE_LR=<hex> filters by caller LR. This diagnoses clean HLE/resource-state gaps
    // that were previously hidden by WEMU_PATCH entries.
    bool resourceLookupTrace();
    void noteResourceLookup(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_READY_TRACE=1: trace MK8/sead-style resource readiness gates around 0x02740478 and
    // 0x027404E0, including per-entry +98/+9c/+b0 state and the 0x02740764 completion path.
    // Optional WEMU_READY_TRACE_LR=<hex> filters by caller LR; WEMU_READY_TRACE_MAX=N caps the
    // number of logged hits. Diagnostic only, default off.
    bool readinessTrace();
    void noteReadinessGate(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_ITEM_RES_TRACE=1: trace MK8's item BFRES/model resource object constructor/ready paths
    // around 0x020E38C4. Optional WEMU_ITEM_RES_TRACE_NAME=<substring> follows only resource objects
    // whose requested subresource name matches (for example SHorn); WEMU_ITEM_RES_TRACE_MAX=N caps
    // output. Diagnostic only, default off.
    bool itemResourceTrace();
    void noteItemResourceTrace(const Interpreter &cpu, std::uint32_t ppcPc);

    // EXPERIMENT (WEMU_FLVC_SKIP_NULL_TARGET=1), default OFF: at MK8's FLAN/FLVC apply dispatcher,
    // skip one list entry when the generated animation-target slot has no bound pane/material
    // pointer. This is used only to expose the next blocker while the upstream binding gap is
    // investigated; normal runs keep the native crash.
    // WEMU_FLVC_TRACE_NULL=1: trace where such null/unbound FLVC targets enter or survive the
    // binding/update path. Diagnostic only, default off.
    bool flvcNullTrace();
    void noteFlvcNullTrace(const Interpreter &cpu, std::uint32_t ppcPc);
    bool skipNullFlvcTarget(Interpreter &cpu, std::uint32_t ppcPc);

    // EXPERIMENT (WEMU_MK8_ZERO_UI_ARRAYS=1), default OFF: zero MK8 menu/UI
    // primary pointer arrays immediately after their guest heap allocation in the async layout builder.
    // This isolates the current menu crash where main observes stale decompressed pixel bytes in
    // not-yet-initialized array entries while a worker is still populating them.
    bool mk8UiArrayZeroFill();
    void zeroMk8UiArrayAlloc(Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_MK8_WORKQ_TRACE=1: trace MK8's guest-side worker queue around 0x02542284
    // (push) and 0x02542380 (pop). Diagnostic only, default off.
    bool mk8WorkQueueTrace();
    void noteMk8WorkQueueTrace(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_SEAD_HEAP_TRACE=1: trace MK8/sead guest heap allocator failures and large allocations
    // around 0x0273B07C. Diagnostic only, default off.
    bool seadHeapTrace();
    void noteSeadHeapTrace(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_SEAD_HEAP_CTX_TRACE=1: trace MK8/sead current-heap TLS get/set traffic around
    // 0x0273D360/0x0273D448. Diagnostic only, default off.
    bool seadHeapContextTrace();
    void noteSeadHeapContextTrace(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_PATCH="20ddb8c:3C600300,20ddb90:60000000": guest words to overwrite after load
    // (experimental in-memory patches; addresses and values in hex).
    void applyGuestPatches(Interpreter &cpu);

    // "0xADDR <name+0xOFF>" for a guest (real, not offset-space) address, using the nearest
    // preceding function/object symbol; bare hex when nothing is close enough.
    std::string symbolize(const Interpreter &cpu, std::uint32_t address);

    // On a fatal fault: registers + a stack walk through the PowerPC back-chain, symbolized.
    void dumpCrashContext(const Interpreter &cpu, std::uint32_t ppcPc, const std::string &reason);

    // Lists every scheduler thread with its state, wait key/join target, and symbolized PC/LR.
    // Used by dumpCrashContext and by the WEMU_THREAD_DUMP periodic snapshot (progression triage).
    void dumpThreads(const Interpreter &cpu, const char *indent);

} // namespace Core::Diag
