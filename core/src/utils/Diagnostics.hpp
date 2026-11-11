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

    // WEMU_HEARTBEAT="50000000" (decimal instruction count): periodically log the current PC/LR
    // so long silent runs show whether the guest is progressing or spinning. 0 = disabled.
    std::uint64_t heartbeatInterval();
    void pollHeartbeat(const Interpreter &cpu, std::uint32_t ppcPc);

    // WEMU_PATCH="<address>:<word>,...": guest words to overwrite after load
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
