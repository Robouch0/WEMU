/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/
#include "Interpreter.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "audio/Output.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/types/InstructionTraits.hpp"
#include "gfx/Renderer.hpp"
#include "hle/ApplicationBoot.hpp"
#include "hle/AsyncCallbacks.hpp"
#include "hle/Ax.hpp"
#include "hle/Gx2.hpp"
#include "utils/Diagnostics.hpp"
#include "utils/FrameProfiler.hpp"
#include "utils/Logger.hpp"
#ifdef WEMU_HAS_LLVM
    #include "cpu/recompiler/Dispatcher.hpp"
#endif

Core::Interpreter::Interpreter(Core::Binary binary) : m_binary(std::move(binary)), m_memory(m_binary.m_memory)
{
    initInstructionMap();
    for (const auto &sym: m_binary.symbols)
        if (sym.raw.header.st_value >= 0xC0000000u && !sym.name.empty())
            m_importBySentinel.emplace(sym.raw.header.st_value, &sym.name);
}

[[nodiscard]] bool Core::Interpreter::step(Utils::BeDecoder &decoder, const std::uint32_t ppc_pc)
{
    decoder.seek(m_pc);
    const EncodedInstruction encodedInstruction(decoder.extractSwap<std::uint32_t>());
    // Primary opcode 0 is never a valid PowerPC instruction. Hitting it means an indirect branch
    // (blr/bctr through a bad pointer) landed us in a zeroed region; halt with context instead of
    // spinning through megabytes of zero-padding.
    if (encodedInstruction.raw == 0) {
        Diag::dumpCrashContext(*this, ppc_pc, "zero/illegal instruction (off the rails)");
        m_failed = true;
        return false;
    }
    m_nextPc = m_pc + Core::INSTR_SIZE;
    try {
        executeInstruction(encodedInstruction);
        // debugDumpGPR();
    } catch (Core::Exception &e) {
        Utils::Log::error("[PC=0x{:08X}] {}", ppc_pc, e.what());
        if (e.isFatal()) {
            Diag::dumpCrashContext(*this, ppc_pc, e.what());
            // Bring-up policy: a fatal fault on a secondary thread kills only that thread
            // (joiners are woken) so the title keeps booting and the next blocker surfaces in
            // the same run. Real hardware would crash the whole title here.
            const std::uint32_t faulting = m_scheduler.currentHandle();
            if (faulting != m_scheduler.mainHandle() && m_scheduler.exitCurrent(*this)) {
                Utils::Log::error("[EMU] thread 0x{:08X} killed by fault at PC=0x{:08X}; continuing boot", faulting, ppc_pc);
                return true;
            }
            m_failed = true;
            return false;
        }
    }
    return true;
}

void Core::Interpreter::run()
{
    Profile::timeline().reset();
    // Instruction and data accesses must observe the same guest memory.
    Diag::applyGuestPatches(*this);
    auto instructionDecoder = Utils::BeDecoder(std::span<const char>(m_memory.getMemory()));
    std::uint32_t ppc_pc = m_binary.header.e_entry;
    m_hooks_min = 0xFFFFFFFFu;
    m_hooks_max = 0u;
    for (const auto &[addr, _]: m_hooks) {
        if (addr < m_hooks_min)
            m_hooks_min = addr;
        if (addr > m_hooks_max)
            m_hooks_max = addr;
    }

    m_pc = ppc_pc - Core::Memory::MemoryMap::ApplicationCode;
    Utils::Log::info("[EMU] Starting at entrypoint 0x{:08X}", ppc_pc);
    m_scheduler.bootstrap(*this);
    Core::Hle::ApplicationBoot applicationBoot;
    applicationBoot.begin(*this);
    std::uint32_t lastPpc = ppc_pc;
    // Periodic service tick: AX/async pumps must run often enough to unblock guest producers even
    // when the current thread is spinning in pure PPC code.
    constexpr std::uint64_t kServiceTickInstructions = 100000;
    // Timeslice preemption is intentionally separate from the service tick. A too-small preempt
    // quantum exposes partially initialized guest structures between worker/main threads in places
    // where the real multi-core timing usually lets the worker finish its local init burst first.
    const std::uint64_t preemptInstructions = []() -> std::uint64_t {
        const char *e = std::getenv("WEMU_PREEMPT_INSTRUCTIONS");
        if (!e || !*e)
            return kServiceTickInstructions;
        char *end = nullptr;
        const auto v = std::strtoull(e, &end, 0);
        return (end && *end == '\0' && v >= kServiceTickInstructions) ? v : kServiceTickInstructions;
    }();
    // Drive both schedules off m_retired so JIT block execution cannot underflow a countdown.
    std::uint64_t nextServiceTickAt = kServiceTickInstructions;
    std::uint64_t nextPreemptAt = preemptInstructions;
    // Diagnostics are configured once from the environment, so hoist the "is this feature on?"
    // checks out of the per-instruction loop — each was a function call (with a thread-safe static
    // guard) run billions of times during boot.
    const bool haveWatchPcs = !Diag::watchPcs().empty();
    if (m_observedPcs != Diag::watchPcs()) {
        m_observedPcs = Diag::watchPcs();
        clearBlockCache();
    }
    const bool haveWatchWords = !Diag::watchWords().empty();
    const bool haveWatchCstrs = Diag::hasWatchCstrs();
    const bool haveWatchRegs = Diag::hasWatchRegs();
    const bool haveHeartbeat = Diag::heartbeatInterval() != 0;
    const bool haveCountPcs = !Diag::countPcs().empty();
    const bool haveOrderFix = Diag::titleSeqOrderFix();
    const bool haveResourceLookupTrace = Diag::resourceLookupTrace();
    const bool haveReadinessTrace = Diag::readinessTrace();
    const bool haveItemResourceTrace = Diag::itemResourceTrace();
    const bool haveFlvcNullTrace = Diag::flvcNullTrace();
    const bool haveMk8UiArrayZeroFill = Diag::mk8UiArrayZeroFill();
    const bool haveMk8WorkQueueTrace = Diag::mk8WorkQueueTrace();
    const bool haveSeadHeapTrace = Diag::seadHeapTrace();
    const bool haveSeadHeapContextTrace = Diag::seadHeapContextTrace();
    const bool haveDriveMenuExperiment = std::getenv("WEMU_DRIVE_MENU") != nullptr;
    // Light-JIT block cache. Verified byte-identical to the pure interpreter and ~15% faster, so
    // it is ON by default; set WEMU_JIT=0 to fall back to the plain per-instruction interpreter
    // (the correctness reference). Per-instruction diagnostics use the slow path. PC observers
    // instead bound decoded/native blocks so unrelated execution can stay accelerated.
    m_jitEnabled = []() {
        const char *e = std::getenv("WEMU_JIT");
        return !e || e[0] != '0';
    }();
    const bool jitFastPath = m_jitEnabled && !haveWatchWords && !haveWatchCstrs && !haveWatchRegs && !haveHeartbeat && !haveCountPcs &&
                             !haveOrderFix && !haveResourceLookupTrace && !haveReadinessTrace && !haveItemResourceTrace && !haveFlvcNullTrace &&
                             !haveMk8UiArrayZeroFill && !haveMk8WorkQueueTrace && !haveSeadHeapTrace && !haveSeadHeapContextTrace &&
                             !haveDriveMenuExperiment;
    const bool x64Requested = [] {
        const char *e = std::getenv("WEMU_X64_JIT");
        return e && e[0] == '1';
    }();
#ifdef WEMU_HAS_LLVM
    const bool profileNative = [] {
        const auto *value = std::getenv("WEMU_NATIVE_PROFILE");
        return value && value[0] == '1';
    }();
    std::array<std::uint64_t, 65> prefixCounts{};
    std::array<std::uint64_t, 64> prefixStops{};
    std::array<std::uint64_t, 2048> prefixExtendedStops{};
    Ppc::ExecutionState nativeState{m_gpr, {}};
    const auto &ram = m_memory.getMemory();
    nativeState.regions[0] = {m_memory.hostPtr(Memory::ApplicationCode, ram.size()), ram.size(), Memory::ApplicationCode};
    nativeState.regions[1] = {m_memory.hostPtr(Memory::STACK_BASE, Memory::STACK_SIZE), Memory::STACK_SIZE, Memory::STACK_BASE};
    nativeState.regions[2] = {m_memory.hostPtr(Memory::DIMPORT_BASE, Memory::DIMPORT_SIZE), Memory::DIMPORT_SIZE, Memory::DIMPORT_BASE};
    std::unique_ptr<Ppc::Dispatcher> native;
    if (const char *enabled = std::getenv("WEMU_NATIVE_CPU"); enabled && enabled[0] == '1' && jitFastPath && !x64Requested) {
        try {
            const char *cache = std::getenv("WEMU_NATIVE_CACHE_DIR");
            Profile::Scope compiling(Profile::Stage::Compile);
            // Decoded versions count hotness before cache admission, so cold
            // prefixes cannot evict compiled code or repeatedly link objects.
            native = std::make_unique<Ppc::Dispatcher>(16384, 1, cache ? cache : "", 4);
            // Native stores bypass Memory, so its write generations cannot vouch for guest bytes.
            m_memory.setUntrackedStores(true);
            Utils::Log::error("[NATIVECPU] enabled: supported hot prefixes with interpreter fallback");
            if (cache && *cache)
                Utils::Log::error("[NATIVECPU] private object cache enabled={}", native->cacheEnabled());
        } catch (const std::exception &error) {
            Utils::Log::error("[NATIVECPU] unavailable: {}", error.what());
        }
    }
#endif
    // WEMU_IPS=1: report interpreter throughput (instructions/sec) periodically. Works with the JIT
    // fast path (unlike WEMU_HEARTBEAT, which disables it), so it can measure the JIT's real gain.
    const bool profileX64 = [] {
        const char *e = std::getenv("WEMU_JIT_PROFILE");
        return e && e[0] == '1';
    }();
    const bool reportIps = [&]() {
        const char *e = std::getenv("WEMU_IPS");
        if (profileX64)
            return true;
#ifdef WEMU_HAS_LLVM
        if (profileNative)
            return true;
#endif
        return e && e[0] == '1';
    }();
    auto ipsMark = std::chrono::steady_clock::now();
    const auto threadDumpPeriod = std::chrono::milliseconds([] {
        const char *e = std::getenv("WEMU_THREAD_DUMP_MS");
        return e ? std::strtoul(e, nullptr, 10) : 0ul;
    }());
    auto threadDumpMark = ipsMark;
    std::uint64_t ipsLast = 0;
    constexpr std::uint32_t kDriveMenuInlineSentinel = 0x0FFFFFECu;
    constexpr std::uint32_t kSentinelLow = 0x0FFFFFE0u;
    struct InlineDriveMenuCall {
            bool active{false};
            std::uint32_t phase{0};
            std::uint32_t task{0};
            std::uint32_t drainFunc{0};
            std::uint32_t returnPc{0};
            Core::ConditionRegister cr{};
            std::uint32_t lr{0};
            std::uint32_t ctr{0};
            std::array<std::uint32_t, 32> gpr{};
            Core::FixedPointExceptionRegister xer{};
            std::array<double, 32> fpr{};
            std::array<double, 32> ps1{};
            std::array<std::uint32_t, 8> gqr{};
            Core::FloatingPointStatusAndControlRegister fpscr{};
    } inlineDriveMenuCall;
    const auto saveInlineDriveMenuState = [&]() {
        inlineDriveMenuCall.cr = m_cr;
        inlineDriveMenuCall.lr = m_lr;
        inlineDriveMenuCall.ctr = m_ctr;
        inlineDriveMenuCall.xer = m_xer;
        inlineDriveMenuCall.fpscr = m_fpscr;
        for (int i = 0; i < 32; i++) {
            inlineDriveMenuCall.gpr[i] = m_gpr[i];
            inlineDriveMenuCall.fpr[i] = m_fpr[i];
            inlineDriveMenuCall.ps1[i] = m_ps1[i];
        }
        for (int i = 0; i < 8; i++)
            inlineDriveMenuCall.gqr[i] = m_gqr[i];
    };
    const auto restoreInlineDriveMenuState = [&]() {
        m_cr = inlineDriveMenuCall.cr;
        m_lr = inlineDriveMenuCall.lr;
        m_ctr = inlineDriveMenuCall.ctr;
        m_xer = inlineDriveMenuCall.xer;
        m_fpscr = inlineDriveMenuCall.fpscr;
        for (int i = 0; i < 32; i++) {
            m_gpr[i] = inlineDriveMenuCall.gpr[i];
            m_fpr[i] = inlineDriveMenuCall.fpr[i];
            m_ps1[i] = inlineDriveMenuCall.ps1[i];
        }
        for (int i = 0; i < 8; i++)
            m_gqr[i] = inlineDriveMenuCall.gqr[i];
    };
    const auto startInlineDriveMenuCall = [&](const std::uint32_t func, const std::uint32_t task, const std::uint32_t returnPc,
                                              const std::uint32_t drainFunc, const std::uint32_t forcedTask0) {
        if (inlineDriveMenuCall.active)
            return;
        saveInlineDriveMenuState();
        inlineDriveMenuCall.active = true;
        inlineDriveMenuCall.phase = 1;
        inlineDriveMenuCall.task = task;
        inlineDriveMenuCall.drainFunc = drainFunc;
        inlineDriveMenuCall.returnPc = returnPc;
        if (forcedTask0 != 0xFFFFFFFFu)
            m_memory.write<std::uint32_t>(task, forcedTask0);
        m_pc = func - Core::Memory::MemoryMap::ApplicationCode;
        m_nextPc = m_pc;
        m_lr = kDriveMenuInlineSentinel - Core::Memory::MemoryMap::ApplicationCode;
        m_gpr[3] = task;
    };
    static const bool hotBlocks = std::getenv("WEMU_HOT_BLOCKS") != nullptr; // TEMP-HOTBLOCKS
    std::unordered_map<std::uint32_t, std::uint64_t> hotBlockCounts; // TEMP-HOTBLOCKS
    struct HotDump { // TEMP-HOTBLOCKS
            std::unordered_map<std::uint32_t, std::uint64_t> &counts;
            ~HotDump()
            {
                std::vector<std::pair<std::uint64_t, std::uint32_t>> v;
                v.reserve(counts.size());
                for (auto [pc, n]: counts)
                    v.push_back({n, pc});
                std::sort(v.rbegin(), v.rend());
                std::uint64_t total = 0;
                for (auto &e: v)
                    total += e.first;
                for (std::size_t i = 0; i < std::min<std::size_t>(40, v.size()); ++i)
                    std::fprintf(stderr, "[HOTBLOCK] pc=0x%08X instrs=%llu share=%.2f%%\n", v[i].second + Core::Memory::MemoryMap::ApplicationCode,
                                 (unsigned long long) v[i].first, 100.0 * double(v[i].first) / double(total ? total : 1));
            }
    } hotDump{hotBlockCounts}; // TEMP-HOTBLOCKS
    static const std::uint32_t driveMenuTask = []() -> std::uint32_t {
        const char *e = std::getenv("WEMU_DRIVE_MENU");
        return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0;
    }();
    static std::uint32_t driveMenuCalls = 0;
    static const bool drainDriveMenuActive = [] {
        const char *e = std::getenv("WEMU_DRAIN_ACTIVE");
        return e && e[0] == '1';
    }();
    static const bool drainDriveMenuScheduled = [] {
        const char *e = std::getenv("WEMU_DRAIN_SCHEDULED");
        return e && e[0] == '1';
    }();
    static const bool inlineDriveMenuScene = [] {
        const char *e = std::getenv("WEMU_INLINE_SCENE");
        return e && e[0] == '1';
    }();
    static const std::uint32_t forcedDriveMenuTask0 = []() -> std::uint32_t {
        const char *e = std::getenv("WEMU_FORCE_TASK0");
        return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0xFFFFFFFFu;
    }();
    const bool haveFlvcSkip = [] {
        const char *e = std::getenv("WEMU_FLVC_SKIP_NULL_TARGET");
        return e && e[0] == '1';
    }();
    // One branch per dispatch instead of one per diagnostic when none is configured.
    const bool havePcDiagnostics = haveWatchPcs || haveWatchWords || haveWatchCstrs || haveWatchRegs || haveCountPcs || haveHeartbeat ||
                                   haveResourceLookupTrace || haveReadinessTrace || haveItemResourceTrace || haveFlvcNullTrace ||
                                   haveMk8UiArrayZeroFill || haveMk8WorkQueueTrace || haveSeadHeapTrace || haveSeadHeapContextTrace || haveFlvcSkip ||
                                   haveOrderFix || driveMenuTask;
#ifdef WEMU_HAS_X64_JIT
    // Compiled blocks chain without passing through this loop, so per-dispatch PC diagnostics
    // keep the interpreter.
    m_x64Jit.reset();
    if (x64Requested && jitFastPath && !havePcDiagnostics && !hotBlocks) {
        m_x64Jit = Jit::X64Jit::create(*this, profileX64);
        Utils::Log::error("[X64JIT] {}", m_x64Jit ? "enabled" : "unavailable on this host; using the interpreter");
    }
#endif
    std::uint64_t schedulerClockRetired = m_retired;
    // A thread that retires thousands of instructions without a single store is polling memory
    // that another core writes (job counters, completion flags). On hardware that core runs in
    // parallel; here it only advances once the poller leaves the host thread, so hand the slice
    // to other cores early instead of spinning it out. Switching at a block boundary is the same
    // operation as the periodic timeslice preemption. WEMU_SPIN_YIELD=0 disables it.
    constexpr std::uint64_t kSpinYieldInstructions = 2048;
    const bool spinYield = [] {
        const char *e = std::getenv("WEMU_SPIN_YIELD");
        return !e || e[0] != '0';
    }();
    std::uint64_t lastStoreRetired = m_retired;
#ifdef WEMU_HAS_X64_JIT
    if (m_x64Jit)
        m_x64Jit->setSpinThreshold(spinYield ? kSpinYieldInstructions : ~std::uint64_t{0});
#endif
    const auto noteBlockRetired = [&](const bool stores) {
        if (stores) {
            lastStoreRetired = m_retired;
        } else if (m_retired - lastStoreRetired >= kSpinYieldInstructions && spinYield) {
            lastStoreRetired = m_retired;
            if (!m_interruptsDisabled)
                m_scheduler.preempt(*this);
        }
    };
    // Resolves m_pc through the previous block's successors, falling back to getBlock(). The
    // previous block is only trusted within the generation it was dispatched in.
    const Block *previousBlock = nullptr;
    std::uint64_t previousGeneration = 0;
    [[maybe_unused]] const auto chainedBlock = [&](Utils::BeDecoder &decoder) -> const Block & {
        const Block *previous = previousGeneration == m_blockGeneration ? previousBlock : nullptr;
        if (previous)
            for (const auto &next: previous->successors)
                if (next.pc == m_pc && next.generation == m_blockGeneration && next.block->validatedEpoch == m_memory.codeEpoch())
                    return *(previousBlock = next.block);
        const Block &block = getBlock(decoder, m_pc);
        if (previous && previousGeneration == m_blockGeneration) {
            previous->successors[1] = previous->successors[0];
            previous->successors[0] = {m_pc, &block, m_blockGeneration};
        }
        previousBlock = &block;
        previousGeneration = m_blockGeneration;
        return block;
    };
    while (m_running) {
        if (m_retired >= nextServiceTickAt) {
            // Desktop events must not depend on the guest calling VPADRead or WHBProc.
            if (m_renderer && !m_renderer->poll_events()) {
                stop();
                break;
            }
            if (m_renderer && m_renderer->pause_requested()) {
                Profile::Scope paused(Profile::Stage::Pause);
                Core::Audio::output().pause(true);
                m_renderer->service_pause([&] {
                    Utils::Log::error("[SESSION] paused retired={} tick={} presents={} movies={}", m_retired, m_scheduler.now(),
                                      m_renderer->guest_present_count(), m_renderer->movie_frame_count());
                });
                Utils::Log::error("[SESSION] {} retired={} tick={} presents={} movies={}", m_renderer->is_open() ? "resumed" : "stopped", m_retired,
                                  m_scheduler.now(), m_renderer->guest_present_count(), m_renderer->movie_frame_count());
                Core::Audio::output().pause(false);
                ipsMark = threadDumpMark = std::chrono::steady_clock::now();
                ipsLast = m_retired;
                if (!m_renderer->is_open()) {
                    stop();
                    break;
                }
            }
            // Espresso time base runs at coreClock / 20. Keep deadlines moving even
            // while a runnable thread does not call an OS clock or yield service.
            const auto timerTicks = (m_retired - schedulerClockRetired) / 20;
            schedulerClockRetired += timerTicks * 20;
            m_scheduler.advanceTicks(timerTicks);
            nextServiceTickAt = m_retired + kServiceTickInstructions;
            Core::Ax::OnFrameTick(*this); // may mark the AX callback thread Ready
            Core::Async::OnTick(*this); // may arm the deferred async-callback pump thread
            if (reportIps || threadDumpPeriod.count() > 0) {
                const auto now = std::chrono::steady_clock::now();
                const double secs = std::chrono::duration<double>(now - ipsMark).count();
                if (reportIps && secs >= 2.0) {
                    Utils::Log::error("[IPS] {:.1f}M instr/sec ({} blocks cached)", static_cast<double>(m_retired - ipsLast) / secs / 1e6,
                                      m_blockCache.size());
#ifdef WEMU_HAS_X64_JIT
                    if (m_x64Jit && profileX64) {
                        if (m_renderer)
                            Utils::Log::error("[X64JIT] presents={}", m_renderer->guest_present_count());
                        m_x64Jit->report();
                    }
#endif
#ifdef WEMU_HAS_LLVM
                    if (profileNative) {
                        for (unsigned i = 0; i < prefixCounts.size(); ++i)
                            if (prefixCounts[i])
                                Utils::Log::error("[NATIVEPREFIX] length={} dispatches={}", i, prefixCounts[i]);
                        for (unsigned i = 0; i < prefixStops.size(); ++i)
                            if (prefixStops[i])
                                Utils::Log::error("[NATIVESTOP] opcode={} dispatches={}", i, prefixStops[i]);
                        for (unsigned i = 0; i < prefixExtendedStops.size(); ++i)
                            if (prefixExtendedStops[i])
                                Utils::Log::error("[NATIVESTOP31] xo={} rc={} dispatches={}", i / 2, i % 2, prefixExtendedStops[i]);
                    }
                    if (native) {
                        const auto &s = native->stats();
                        Utils::Log::error("[NATIVECPU] compiled={} calls={} instructions={} invalidations={} evictions={} entries={}", s.compiled,
                                          s.calls, s.instructions, s.invalidations, s.evictions, native->size());
                        if (native->cacheEnabled())
                            Utils::Log::error("[NATIVECACHE] hits={} misses={} writes={} link_failures={}", s.cacheHits, s.cacheMisses, s.cacheWrites,
                                              s.cacheLinkFailures);
                    }
#endif
                    ipsMark = now;
                    ipsLast = m_retired;
                }
                if (threadDumpPeriod.count() > 0 && now - threadDumpMark >= threadDumpPeriod) {
                    std::fprintf(stderr, "[DIAG] threads @ retired=%llu (wall-time snapshot):\n", static_cast<unsigned long long>(m_retired));
                    Diag::dumpThreads(*this, "    ");
                    threadDumpMark = now;
                }
            }
            // Observe the thread that consumed this timeslice, before selecting another one.
            static const bool noTickPreempt = std::getenv("WEMU_NO_TICK_PREEMPT") != nullptr; // isolation switch
            if (m_retired >= nextPreemptAt && !m_interruptsDisabled && !noTickPreempt) {
                nextPreemptAt = m_retired + preemptInstructions;
                m_scheduler.preempt(*this);
            }
        }
        ppc_pc = m_pc + Core::Memory::MemoryMap::ApplicationCode;
        // Every return sentinel lies in [kSentinelLow, ApplicationData) and nothing valid lives
        // below the code base, so ordinary code addresses skip all of these checks at once.
        static_assert(Core::Gx2::bufferAllocatorReturn == kSentinelLow);
        static_assert(Core::Hle::preinitReturn > Core::Gx2::bufferAllocatorReturn && Core::Hle::preinitReturn != Core::H264::callbackSentinel);
        static_assert(Core::H264::callbackSentinel >= kSentinelLow && kDriveMenuInlineSentinel >= kSentinelLow &&
                      Core::ASYNC_CB_SENTINEL >= kSentinelLow && Core::AX_FRAME_SENTINEL >= kSentinelLow &&
                      Core::RETURN_SENTINEL < Core::Memory::MemoryMap::ApplicationData);
        if (ppc_pc - Core::Memory::MemoryMap::ApplicationCode >= kSentinelLow - Core::Memory::MemoryMap::ApplicationCode) [[unlikely]] {
            if (ppc_pc == Core::Gx2::bufferAllocatorReturn) {
                Core::Gx2::onBufferAllocatorReturn(*this);
                continue;
            }
            if (ppc_pc == Core::Hle::preinitReturn && applicationBoot.active) {
                applicationBoot.finish(*this);
                continue;
            }
            if (ppc_pc == Core::H264::callbackSentinel) {
                Core::H264::onCallbackReturn(*this);
                continue;
            }
            // The AX callback thread finished one frame callback: run the next or park it.
            if (ppc_pc == Core::AX_FRAME_SENTINEL) {
                Core::Ax::OnSentinelReturn(*this);
                continue;
            }
            // The async-callback pump finished one deferred call: run the next or park it.
            if (ppc_pc == Core::ASYNC_CB_SENTINEL) {
                Core::Async::OnSentinelReturn(*this);
                continue;
            }
            // WEMU_INLINE_SCENE=1: run the MK8 drive-menu diagnostic callbacks on the current guest
            // thread instead of the synthetic nnAsync thread. Scene construction handlers touch
            // per-thread/global context that is not valid on nnAsync; this trampoline preserves the
            // interrupted thread's registers and resumes at the original PC after the injected call(s).
            if (ppc_pc == kDriveMenuInlineSentinel && inlineDriveMenuCall.active) {
                if (inlineDriveMenuCall.phase == 1 && inlineDriveMenuCall.drainFunc) {
                    restoreInlineDriveMenuState();
                    inlineDriveMenuCall.phase = 2;
                    m_pc = inlineDriveMenuCall.drainFunc - Core::Memory::MemoryMap::ApplicationCode;
                    m_nextPc = m_pc;
                    m_lr = kDriveMenuInlineSentinel - Core::Memory::MemoryMap::ApplicationCode;
                    m_gpr[3] = inlineDriveMenuCall.task;
                    continue;
                }
                const std::uint32_t returnPc = inlineDriveMenuCall.returnPc;
                restoreInlineDriveMenuState();
                inlineDriveMenuCall.active = false;
                m_pc = returnPc - Core::Memory::MemoryMap::ApplicationCode;
                m_nextPc = m_pc;
                continue;
            }
            // A thread (or the main thread) returned to the trampoline address seeded in LR: it has
            // finished. Switch to another runnable thread, or stop if this was the last one.
            if (ppc_pc == Core::RETURN_SENTINEL) {
                if (m_scheduler.exitCurrent(*this)) {
                    Utils::Log::debug("[EMU] thread finished; switched to next runnable thread.");
                    continue;
                }
                Utils::Log::info("[EMU] All threads finished; stopping.");
                break;
            }
            // Guard against execution running off the rails into low/zeroed memory (e.g. a branch to a
            // null pointer). Nothing valid lives below the code base, so stop instead of spinning
            // through illegal zero instructions forever.
            if (ppc_pc < Core::Memory::MemoryMap::ApplicationCode) {
                Utils::Log::error("[EMU] PC ran off into invalid memory (0x{:08X}); halting. last PC=0x{:08X}", ppc_pc, lastPpc);
                Diag::dumpCrashContext(*this, lastPpc, "PC ran off into invalid memory");
                m_failed = true;
                break;
            }
        }
        lastPpc = ppc_pc;
        if (havePcDiagnostics) [[unlikely]] {
            if (haveWatchPcs && std::ranges::find(Diag::watchPcs(), ppc_pc) != Diag::watchPcs().end())
                Diag::noteWatchHit(*this, ppc_pc);
            if (haveWatchWords)
                Diag::pollWatchWords(*this, ppc_pc);
            if (haveWatchCstrs)
                Diag::noteWatchCstrs(*this, ppc_pc);
            if (haveWatchRegs)
                Diag::noteWatchRegs(*this, ppc_pc);
            if (haveCountPcs && std::ranges::find(Diag::countPcs(), ppc_pc) != Diag::countPcs().end())
                Diag::notePcCount(ppc_pc);
            if (haveHeartbeat)
                Diag::pollHeartbeat(*this, ppc_pc);
            if (haveResourceLookupTrace)
                Diag::noteResourceLookup(*this, ppc_pc);
            if (haveReadinessTrace)
                Diag::noteReadinessGate(*this, ppc_pc);
            if (haveItemResourceTrace)
                Diag::noteItemResourceTrace(*this, ppc_pc);
            if (haveFlvcNullTrace)
                Diag::noteFlvcNullTrace(*this, ppc_pc);
            if (haveMk8UiArrayZeroFill)
                Diag::zeroMk8UiArrayAlloc(*this, ppc_pc);
            if (haveMk8WorkQueueTrace)
                Diag::noteMk8WorkQueueTrace(*this, ppc_pc);
            if (haveSeadHeapTrace)
                Diag::noteSeadHeapTrace(*this, ppc_pc);
            if (haveSeadHeapContextTrace)
                Diag::noteSeadHeapContextTrace(*this, ppc_pc);
            if (haveFlvcSkip && Diag::skipNullFlvcTarget(*this, ppc_pc))
                continue;
            // EXPERIMENT (WEMU_ORDER_FIX=1, default off): skip MK8's title sequence calc while its
            // decision input is still stale, so the game retries it on a later frame. Unlike m_hooks
            // this is conditional per call, which is the whole point -- see Diag::deferTitleSeqCalc.
            if (haveOrderFix && Diag::deferTitleSeqCalc(*this, ppc_pc)) {
                m_pc = m_lr;
                continue;
            }
            // WEMU_DRIVE_MENU is an MK8-only diagnostic experiment.  Its title forward
            // edge is stored in staleSlot+0x147 and otherwise reaches the driver before
            // the post-present probe can hold it.
            if (driveMenuTask && ppc_pc == 0x024D6430) {
                try {
                    const std::uint32_t incoming = driveMenuTask + 0xDC;
                    if (m_memory.read<std::uint32_t>(incoming + 0x34) == 0) {
                        const std::uint32_t staleSlot = m_memory.read<std::uint32_t>(0x101D6944);
                        if (staleSlot)
                            m_memory.write<std::uint8_t>(staleSlot + 0x147, 0);
                        if (m_gpr[3])
                            m_memory.write<std::uint8_t>(m_gpr[3] + 0x143, 0);
                        if (++driveMenuCalls % 60 == 0) {
                            if (inlineDriveMenuScene) {
                                const std::uint32_t drainFunc = drainDriveMenuScheduled ? 0x0253A4F8 : (drainDriveMenuActive ? 0x0253A3C4 : 0);
                                startInlineDriveMenuCall(0x025467D0, driveMenuTask, ppc_pc, drainFunc, forcedDriveMenuTask0);
                                continue;
                            } else {
                                Core::Async::enqueue(0x025467D0, driveMenuTask);
                                if (drainDriveMenuActive)
                                    Core::Async::enqueue(0x0253A3C4, driveMenuTask);
                            }
                        }
                    }
                } catch (const Core::MemoryException &e) {
                    Utils::Log::debug("[DIAG] Cannot inspect drive-menu state: {}", e.what());
                }
            }
        }
        if (hooked(ppc_pc)) {
#ifdef WEMU_HAS_X64_JIT
            if (m_x64Jit)
                m_x64Jit->installHook(m_pc);
#endif
            Profile::Scope hle(Profile::Stage::Hle);
            m_hooks.find(ppc_pc)->second(*this);
            m_pc = m_lr;
            continue;
        }
#ifdef WEMU_HAS_X64_JIT
        // Compiled blocks run chained until something above needs the loop again: service tick,
        // hook, sentinel, HLE call, code write or a block that is not compiled yet.
        if (m_x64Jit) {
            Jit::X64Jit &jit = *m_x64Jit;
            jit.sync(m_memory.codeEpoch(), m_hooks.size());
            bool ready = jit.prepare(m_pc);
            if (!ready) {
                const Block &blk = getBlock(instructionDecoder, m_pc);
                if (!blk.instrs.empty()) {
                    Profile::Scope compiling(Profile::Stage::Compile);
                    std::array<Jit::GuestInstruction, 64> guest;
                    for (std::size_t i = 0; i < blk.instrs.size(); ++i)
                        guest[i] = {blk.instrs[i].instr.raw, blk.instrs[i].id};
                    ready = jit.compile(m_pc, std::span(guest.data(), blk.instrs.size()), blk.stores);
                }
            }
            if (ready) {
                const Jit::X64Jit::Exit exit = jit.run(m_retired, nextServiceTickAt, lastStoreRetired);
                switch (exit.reason) {
                    case Jit::ExitReason::Budget:
                    case Jit::ExitReason::LookupMiss:
                        m_pc = m_nextPc = exit.pc;
                        break;
                    case Jit::ExitReason::Done:
                        m_pc = m_nextPc = exit.pc;
                        noteBlockRetired(exit.stores);
                        break;
                    case Jit::ExitReason::Suffix: {
                        const Block &blk = getBlock(instructionDecoder, exit.blockPc);
                        m_pc = exit.pc;
                        m_retired += runBlock(blk, exit.index);
                        noteBlockRetired(blk.stores);
                        break;
                    }
                    case Jit::ExitReason::Fatal:
                        noteBlockRetired(exit.stores);
                        break;
                    default:
                        break;
                }
                continue;
            }
        }
#endif
        // Fast path: execute a whole pre-decoded block. The tick cadence keys off m_retired (updated
        // here), so whole-block execution stays accounted for without a separate countdown.
        if (jitFastPath) {
#ifdef WEMU_HAS_LLVM
            const Block &blk = getBlock(instructionDecoder, m_pc, bool(native));
#else
            const Block &blk = chainedBlock(instructionDecoder);
#endif
            if (!blk.instrs.empty()) {
                if (hotBlocks) // TEMP-HOTBLOCKS
                    hotBlockCounts[m_pc] += blk.instrs.size(); // TEMP-HOTBLOCKS
#ifdef WEMU_HAS_LLVM
                if (profileNative) {
                    ++prefixCounts[blk.nativePrefixLength];
                    if (blk.nativePrefixLength < blk.instrs.size()) {
                        const auto word = blk.instrs[blk.nativePrefixLength].instr.raw;
                        ++prefixStops[word >> 26];
                        if ((word >> 26) == 31)
                            ++prefixExtendedStops[word & 2047];
                    }
                }
                if (native && blk.nativeWords.size() >= 2 && (blk.nativeWords.size() >= 4 || blk.nativeLoop)) {
                    const auto budget =
                            static_cast<unsigned>(std::min<std::uint64_t>(64, nextServiceTickAt > m_retired ? nextServiceTickAt - m_retired : 0));
                    try {
                        nativeState.condition = m_cr.raw;
                        nativeState.summaryOverflow = m_xer.so;
                        nativeState.link = m_lr;
                        nativeState.counter = m_ctr;
                        nativeState.instructionBudget = budget;
                        unsigned n{};
                        if (blk.nativeWords.size() <= budget) {
                            if (const auto *code = native->bound(blk.nativeCode)) {
                                n = code->execute(nativeState);
                                native->recordExecution(n);
                            } else {
                                if (blk.nativeVisits < 256)
                                    ++blk.nativeVisits;
                                if (blk.nativeVisits >= 256)
                                    n = native->execute(ppc_pc, blk.nativeWords, nativeState, budget, &blk.nativeCode);
                            }
                        }
                        if (n) {
                            m_pc = nativeState.nextPc - Memory::ApplicationCode;
                            m_nextPc = m_pc;
                            m_cr.raw = nativeState.condition;
                            m_lr = nativeState.link;
                            m_ctr = nativeState.counter;
                            // Finish this decoded version's remaining instructions without
                            // fetching and dispatching the same suffix as another block.
                            m_retired += n;
                            if (blk.nativeWords.size() < blk.instrs.size())
                                m_retired += runBlock(blk, n);
                            noteBlockRetired(blk.stores);
                            continue;
                        }
                    } catch (const std::exception &error) {
                        Utils::Log::error("[NATIVECPU] disabled after compile failure: {}", error.what());
                        native.reset();
                    }
                }
#endif
                const std::uint32_t n = runBlock(blk);
                m_retired += n;
                noteBlockRetired(blk.stores);
                continue;
            }
        }
        if (!step(instructionDecoder, ppc_pc))
            break;
        m_pc = m_nextPc;
        m_retired++;
    }
#ifdef WEMU_HAS_LLVM
    if (native && native->cacheEnabled()) {
        const auto &s = native->stats();
        Utils::Log::error("[NATIVECACHE] session hits={} misses={} writes={} link_failures={}", s.cacheHits, s.cacheMisses, s.cacheWrites,
                          s.cacheLinkFailures);
    }
#endif
    Utils::Log::info("[EMU] Exited. PC=0x{:08X}", ppc_pc);
}

InstructionID Core::Interpreter::findInstructionID(const EncodedInstruction &instr)
{
    const auto &candidates = m_instructionMap[instr.opcd];

    for (const auto &[id, fields, _]: candidates) {
        const bool found = std::ranges::all_of(fields.begin(), fields.end(), [&](const auto &p) {
            const auto &[field, value] = p;
            switch (field) {
                case Field::F_AA:
                    return instr.aa == value;
                case Field::F_LK:
                    return instr.lk == value;
                case Field::F_XO10:
                    return instr.xo10 == value;
                case Field::F_XO9:
                    return instr.xo9 == value;
                case Field::F_XO5:
                    return instr.xo5 == value;
                case Field::F_OPCD:
                    return true;
                default:
                    throw Core::InterpreterException("Unknown field.");
                    return false;
            }
        });
        if (found)
            return id;
    }
    throw Core::InterpreterException("No instruction found with this fields. (opcode == " + std::to_string(instr.opcd) + ")");
}

const Core::Interpreter::Block &Core::Interpreter::getBlock(Utils::BeDecoder &decoder, const std::uint32_t startPc, bool nativeLookahead)
{
    const auto observed = [&](std::uint32_t pc) { return !m_observedPcs.empty() && std::ranges::find(m_observedPcs, pc) != m_observedPcs.end(); };
    const bool observedStart = observed(startPc + Memory::ApplicationCode);
    if (observedStart)
        nativeLookahead = false;
    const auto &ram = m_memory.getMemory();
    // Cached versions are valid while their guest words are unchanged in memory.
    // Without a code-page write since the last comparison the words cannot have changed. Native
    // stores bypass Memory, so native dispatch keeps comparing on every lookup.
    const auto current = [&](const Block &block) {
        if (block.instrs.empty())
            return false;
#ifdef WEMU_HAS_LLVM
        if (nativeLookahead && !block.nativeLookahead)
            return false;
#endif
        const std::uint64_t epoch = m_memory.codeEpoch();
        if (block.validatedEpoch == epoch && !nativeLookahead)
            return true;
        const bool valid = std::size_t(startPc) + block.guestWords.size() * 4 <= ram.size() &&
                           std::memcmp(ram.data() + startPc, block.guestWords.data(), block.guestWords.size() * 4) == 0;
        if (valid)
            block.validatedEpoch = epoch;
        return valid;
    };
    BlockLookup &slot = m_blockLookup[blockLookupSlot(startPc)];
    if (slot.pc == startPc && current(*slot.block))
        return *slot.block;
    if (const auto it = m_blockCache.find(startPc); it != m_blockCache.end()) {
        if (current(it->second)) {
            slot = {startPc, &it->second};
            return it->second;
        }
        if (slot.block == &it->second)
            slot = {};
        ++m_blockGeneration;
        m_blockCache.erase(it);
    }

    Block b;
    std::uint32_t pc = startPc;
    for (int count = 0; count < 64; count++) {
        decoder.seek(pc);
        const std::uint32_t word = decoder.extractSwap<std::uint32_t>();
        if (word == 0)
            break; // illegal word: leave it to the slow path, which reports it with context
        const EncodedInstruction ei(word);
        InstructionID id;
        try {
            DecodeCacheEntry &slot = m_decodeCache[(word * 2654435761u) >> 16];
            if (slot.raw == word) {
                id = slot.id;
            } else {
                id = findInstructionID(ei);
                slot = {word, id};
            }
        } catch (...) {
            break; // unknown encoding: stop here so the slow path executes and reports it
        }
        b.instrs.push_back({m_handlers[static_cast<std::size_t>(id)], ei, false, id});
        pc += Core::INSTR_SIZE;
        // A block ends at any control-flow instruction: bc (16), sc (17), b (18), the opcode-19
        // family (bclr/bcctr/rfi/...), and traps (3/twi). After such an instruction the next PC is
        // data-dependent, so the block boundary is where we hand control back to run().
        if (endsBlock(word))
            break;
        // A store may replace a later instruction. Return to fetch/validation
        // before executing any instruction after a guest memory write.
        const bool stores = isStoreInstruction(id);
        b.instrs.back().store = stores;
        b.stores |= stores;
        // Interpreted blocks continue past stores: Memory::write flags a store into a page of
        // decoded code and runBlock() leaves the block right after it. Native lookahead keeps its
        // own store policy below, so its reference blocks still stop here.
        if (stores && nativeLookahead)
            break;
        // Stop just before a hooked address so run()'s HLE hook dispatch still fires for it.
        const std::uint32_t ppc = pc + Core::Memory::MemoryMap::ApplicationCode;
        if (observed(ppc) || hooked(ppc))
            break;
    }
#ifdef WEMU_HAS_LLVM
    // Compute eligibility once per decoded version, not on every dispatch.
    // getBlock's live-word validation invalidates this prefix along with instrs.
    for (const auto &instruction: b.instrs) {
        if (!Ppc::Block::supports(instruction.instr.raw))
            break;
        ++b.nativePrefixLength;
    }
    b.nativeLookahead = nativeLookahead;
    if (!observedStart && (b.nativePrefixLength >= 2 || nativeLookahead))
        for (unsigned i = 0; i < b.nativePrefixLength; ++i)
            b.nativeWords.push_back(b.instrs[i].instr.raw);
    // The reference block still stops at stores. Native code can continue only
    // because every store exits when it touches any of this validated code.
    if (nativeLookahead && !b.nativeWords.empty() && b.nativePrefixLength == b.instrs.size() && Ppc::Block::stores(b.nativeWords.back())) {
        while (b.nativeWords.size() < 64) {
            const auto next = startPc + static_cast<std::uint32_t>(b.nativeWords.size() * 4);
            const auto ppc = next + Memory::ApplicationCode;
            if (observed(ppc) || (ppc >= m_hooks_min && ppc <= m_hooks_max && m_hooks.contains(ppc)) ||
                std::uint64_t(next) + 4 > m_memory.getMemory().size())
                break;
            decoder.seek(next);
            const auto word = decoder.extractSwap<std::uint32_t>();
            if (!Ppc::Block::supports(word))
                break;
            b.nativeWords.push_back(word);
            if (Ppc::Block::branches(word))
                break;
        }
        b.nativePrefixLength = b.nativeWords.size();
    }
    b.stores |= std::ranges::any_of(b.nativeWords, [](std::uint32_t word) { return Ppc::Block::stores(word); });
    b.nativeLoop = Ppc::Block::loopsToStart(startPc + Memory::ApplicationCode, b.nativeWords);
    const std::size_t validatedWords = std::max(b.instrs.size(), b.nativeWords.size());
#else
    const std::size_t validatedWords = b.instrs.size();
#endif
    b.guestWords.resize(validatedWords);
    if (validatedWords) {
        std::memcpy(b.guestWords.data(), ram.data() + startPc, validatedWords * 4);
        for (std::size_t offset = startPc; offset < startPc + validatedWords * 4; offset += std::size_t{1} << Memory::kCodePageShift)
            m_memory.markCodePage(offset);
        m_memory.markCodePage(startPc + validatedWords * 4 - 1);
    }
    b.validatedEpoch = m_memory.codeEpoch();
    Block &stored = m_blockCache.insert_or_assign(startPc, std::move(b)).first->second;
    slot = {startPc, &stored};
    return stored;
}

std::uint32_t Core::Interpreter::runBlock(const Block &block, std::size_t first)
{
    for (std::size_t i = first; i < block.instrs.size(); i++) {
        const DecodedInstr &di = block.instrs[i];
        m_nextPc = m_pc + Core::INSTR_SIZE;
        try {
            di.fn(*this, di.instr);
            if (di.store && m_memory.codeWritten()) [[unlikely]] {
                // The store may have replaced a later instruction of this block: refetch from the
                // next PC so getBlock() revalidates the words against memory.
                m_memory.clearCodeWritten();
                m_pc = m_nextPc;
                return static_cast<std::uint32_t>(i + 1 - first);
            }
        } catch (Core::Exception &e) {
            // A fatal fault moved m_pc to the next thread or stopped the interpreter. Non-fatal:
            // fall through and advance past the instruction, mirroring step().
            if (handleBlockException(e))
                return static_cast<std::uint32_t>(i + 1 - first);
        }
        m_pc = m_nextPc;
    }
    return static_cast<std::uint32_t>(block.instrs.size() - first);
}

Core::Interpreter::SyscallFn Core::Interpreter::syscallFor(const std::string &name)
{
    if (m_syscallCacheVersion != syscallHandler.version) {
        m_syscallCache.clear();
        m_syscallCacheVersion = syscallHandler.version;
    }
    const auto [it, inserted] = m_syscallCache.try_emplace(&name, nullptr);
    if (inserted) {
        const auto found = syscallHandler.syscallTable.find(name);
        it->second = found != syscallHandler.syscallTable.end() ? found->second : nullptr;
    }
    return it->second;
}

bool Core::Interpreter::handleBlockException(const Core::Exception &e)
{
    const std::uint32_t ppc = m_pc + Core::Memory::MemoryMap::ApplicationCode;
    Utils::Log::error("[PC=0x{:08X}] {}", ppc, e.what());
    if (!e.isFatal())
        return false;
    Diag::dumpCrashContext(*this, ppc, e.what());
    // Same bring-up policy as step(): a fatal fault on a secondary thread kills only that thread
    // and the boot continues; on the main thread it stops the interpreter.
    const std::uint32_t faulting = m_scheduler.currentHandle();
    if (faulting != m_scheduler.mainHandle() && m_scheduler.exitCurrent(*this)) {
        Utils::Log::error("[EMU] thread 0x{:08X} killed by fault at PC=0x{:08X}; continuing boot", faulting, ppc);
        return true;
    }
    m_failed = true;
    m_running = false;
    return true;
}

void Core::Interpreter::executeInstruction(const EncodedInstruction &instr)
{
    // Decode via the direct-mapped cache (see the header). The multiply-shift hash spreads the
    // encoding's information-bearing bits (opcode + operands) across the index.
    const std::uint32_t raw = instr.raw;
    DecodeCacheEntry &slot = m_decodeCache[(raw * 2654435761u) >> 16];
    InstructionID id;
    if (slot.raw == raw) {
        id = slot.id;
    } else {
        id = findInstructionID(instr);
        slot = {raw, id};
    }

    // instructionIDToString() builds a std::string per call; only pay for it when trace logging is
    // actually compiled in, otherwise the argument alone would run for every instruction executed.
    if constexpr (Utils::Log::kLevel <= Utils::Log::Level::Trace)
        Utils::Log::trace("{}", instructionIDToString(id));
    m_handlers[static_cast<std::size_t>(id)](*this, instr);
}

void Core::Interpreter::initInstructionMap()
{
    for (auto &instrInfo: INSTRUCTIONARRAY) {
        const auto opcode = instrInfo.matchFields[0].second;
        if (m_instructionMap.contains(opcode)) {
            m_instructionMap[opcode].push_back(instrInfo);
        } else {
            m_instructionMap.emplace(opcode, std::vector{instrInfo});
        }
    }

    // Extract bare function pointers from the std::function handlers for fast dispatch. The table
    // is indexed by InstructionID; every entry is a plain function so target<> always succeeds.
    constexpr std::size_t count = std::size(INSTRUCTIONARRAY);
    m_handlers.assign(count, nullptr);
    for (const auto &info: INSTRUCTIONARRAY) {
        const auto *fn = info.function.target<RawHandler>();
        m_handlers[static_cast<std::size_t>(info.id)] = fn ? *fn : nullptr;
    }
}

void Core::Interpreter::updateCR0(const std::int32_t &result, const EncodedInstruction &instr, const bool forceUpdate)
{
    if (!instr.rc && !forceUpdate)
        return;
    std::uint32_t flags = 0;

    if (result == 0)
        flags |= ConditionRegisterFlag::Zero;
    else if (result < 0)
        flags |= ConditionRegisterFlag::Negative;
    else
        flags |= ConditionRegisterFlag::Positive;
    if (m_xer.so)
        flags |= ConditionRegisterFlag::SummaryOverflow;
    m_cr.cr0 = flags;
}

void Core::Interpreter::updateCR1(const EncodedInstruction &instr) noexcept
{
    if (!instr.rc)
        return;
    std::uint32_t flags = 0;

    if (m_fpscr.fx)
        flags |= ConditionRegisterFlag::Negative;
    if (m_fpscr.fex)
        flags |= ConditionRegisterFlag::Positive;
    if (m_fpscr.vx)
        flags |= ConditionRegisterFlag::Zero;
    if (m_fpscr.ox)
        flags |= ConditionRegisterFlag::SummaryOverflow;
    m_cr.cr1 = flags;
}

void Core::Interpreter::updateOverflow(const bool overflow, const EncodedInstruction &instr)
{
    if (!instr.oe)
        return;

    m_xer.ov = overflow;
    if (m_xer.ov)
        m_xer.so = true;
}

void Core::Interpreter::updateOverflow(const std::int32_t &a, const std::int32_t &b, const std::int32_t &result, const EncodedInstruction &instr)
{
    const bool aSign = a < 0;
    const bool bSign = b < 0;
    const bool resultSign = result < 0;
    const bool overflow = (aSign == bSign) && (aSign != resultSign);

    this->updateOverflow(overflow, instr);
}
