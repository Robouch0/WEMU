    /*
** EPITECH PROJECT, 2026
** core
** File description:
** Diagnostics -- boot-triage tooling: unknown-import tracking, HLE call trace, crash backtrace
*/

#include "Diagnostics.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <map>
#include <tuple>
#include <vector>
#ifndef _WIN32
    #include <csignal>
    #include <unistd.h>
#endif

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/memory/Memory.hpp"
#include "utils/Logger.hpp"

namespace {

    std::map<std::string, std::uint64_t> g_unknownImports;

    // Sorted (address, name) pairs for named symbols placed in guest memory.
    std::vector<std::pair<std::uint32_t, const std::string *>> buildSymbolIndex(const Core::Binary &bin)
    {
        std::vector<std::pair<std::uint32_t, const std::string *>> index;
        index.reserve(bin.symbols.size());
        for (const auto &symbol: bin.symbols) {
            if (symbol.name.empty() || symbol.meta.virtAddress == 0)
                continue;
            index.emplace_back(static_cast<std::uint32_t>(symbol.meta.virtAddress), &symbol.name);
        }
        std::ranges::sort(index, {}, [](const auto &e) { return e.first; });
        return index;
    }

} // namespace

namespace Core::Diag {

    void noteUnknownImport(const std::string &name, const Interpreter &cpu)
    {
        if (const auto [it, first] = g_unknownImports.try_emplace(name, 0); first) {
            Utils::Log::error("[HLE] Unknown import {} (r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} r6=0x{:08X}) -> returning 0", name, cpu.m_gpr[3],
                              cpu.m_gpr[4], cpu.m_gpr[5], cpu.m_gpr[6]);
        }
        g_unknownImports[name]++;
    }

    void dumpUnknownImports()
    {
        if (g_unknownImports.empty())
            return;
        std::vector<std::pair<std::uint64_t, const std::string *>> byCount;
        byCount.reserve(g_unknownImports.size());
        for (const auto &[name, count]: g_unknownImports)
            byCount.emplace_back(count, &name);
        std::ranges::sort(byCount, std::greater{}, [](const auto &e) { return e.first; });

        std::cout << "\n[DIAG] " << g_unknownImports.size() << " imports hit without an HLE handler (count, name):" << std::endl;
        for (const auto &[count, name]: byCount)
            std::cout << std::format("  {:8}  {}", count, *name) << std::endl;
    }

    bool traceHle()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_TRACE_HLE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    static std::vector<std::uint32_t> parseHexList(const char *envName)
    {
        std::vector<std::uint32_t> out;
        if (const char *env = std::getenv(envName)) {
            for (const char *p = env; *p;) {
                char *end = nullptr;
                const unsigned long v = std::strtoul(p, &end, 16);
                if (end == p)
                    break;
                out.push_back(static_cast<std::uint32_t>(v));
                p = (*end == ',') ? end + 1 : end;
            }
        }
        return out;
    }

    struct WatchCstrSpec {
        std::uint32_t pc = 0;
        std::uint8_t gpr = 0;
        bool deref = false;
        bool literal = false;
        std::uint32_t literalAddr = 0;
    };

    struct WatchRegSpec {
        std::uint32_t pc = 0;
        std::uint8_t gpr = 0;
        std::uint32_t value = 0;
    };

    struct WatchDerefSpec {
        std::uint8_t gpr = 0;
        std::uint32_t offset = 0;
    };

    static std::vector<WatchCstrSpec> parseWatchCstrs()
    {
        std::vector<WatchCstrSpec> out;
        const char *env = std::getenv("WEMU_WATCH_CSTR");
        if (!env)
            return out;
        for (const char *p = env; *p;) {
            char *end = nullptr;
            const auto pc = static_cast<std::uint32_t>(std::strtoul(p, &end, 16));
            if (end == p || *end != ':')
                break;
            p = end + 1;
            bool deref = false;
            if (*p == '*') {
                deref = true;
                p++;
            }
            if (*p == '@') {
                p++;
                const auto addr = static_cast<std::uint32_t>(std::strtoul(p, &end, 16));
                if (end == p)
                    break;
                out.push_back(WatchCstrSpec {pc, 0, false, true, addr});
                p = (*end == ',') ? end + 1 : end;
                continue;
            }
            if (*p != 'r' && *p != 'R')
                break;
            p++;
            const auto gpr = static_cast<unsigned long>(std::strtoul(p, &end, 10));
            if (end == p || gpr > 31)
                break;
            out.push_back(WatchCstrSpec {pc, static_cast<std::uint8_t>(gpr), deref, false, 0});
            p = (*end == ',') ? end + 1 : end;
        }
        return out;
    }

    static std::vector<WatchRegSpec> parseWatchRegs()
    {
        std::vector<WatchRegSpec> out;
        const char *env = std::getenv("WEMU_WATCH_REG");
        if (!env)
            return out;
        for (const char *p = env; *p;) {
            char *end = nullptr;
            const auto pc = static_cast<std::uint32_t>(std::strtoul(p, &end, 16));
            if (end == p || *end != ':')
                break;
            p = end + 1;
            if (*p != 'r' && *p != 'R')
                break;
            p++;
            const auto gpr = static_cast<unsigned long>(std::strtoul(p, &end, 10));
            if (end == p || gpr > 31)
                break;
            std::uint32_t value = 0;
            if (*end == '=') {
                p = end + 1;
                value = static_cast<std::uint32_t>(std::strtoul(p, &end, 16));
                if (end == p)
                    break;
            }
            out.push_back(WatchRegSpec {pc, static_cast<std::uint8_t>(gpr), value});
            p = (*end == ',') ? end + 1 : end;
        }
        return out;
    }

    static std::vector<WatchDerefSpec> parseWatchDerefs()
    {
        std::vector<WatchDerefSpec> out;
        const char *env = std::getenv("WEMU_WATCH_DEREF");
        if (!env)
            return out;
        for (const char *p = env; *p;) {
            char *end = nullptr;
            const long reg = std::strtol(p, &end, 10);
            if (end == p)
                break;
            std::uint32_t off = 0;
            if (*end == '+') {
                char *offEnd = nullptr;
                off = static_cast<std::uint32_t>(std::strtoul(end + 1, &offEnd, 16));
                if (offEnd == end + 1)
                    break;
                end = offEnd;
            }
            if (reg >= 0 && reg < 32)
                out.push_back(WatchDerefSpec {static_cast<std::uint8_t>(reg), off});
            if (*end != ',')
                break;
            p = end + 1;
        }
        return out;
    }

    static const std::vector<WatchDerefSpec> &watchDerefs()
    {
        static const std::vector<WatchDerefSpec> specs = parseWatchDerefs();
        return specs;
    }

    static std::uint32_t watchDerefWords()
    {
        static const std::uint32_t words = []() {
            const char *env = std::getenv("WEMU_WATCH_DEREF_WORDS");
            if (!env)
                return 8u;
            return std::clamp(static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)), 1u, 64u);
        }();
        return words;
    }

    static const std::vector<WatchCstrSpec> &watchCstrs()
    {
        static const std::vector<WatchCstrSpec> specs = parseWatchCstrs();
        return specs;
    }

    static const std::vector<WatchRegSpec> &watchRegs()
    {
        static const std::vector<WatchRegSpec> specs = parseWatchRegs();
        return specs;
    }

    const std::vector<std::uint32_t> &watchPcs()
    {
        static const std::vector<std::uint32_t> pcs = parseHexList("WEMU_WATCH_PC");
        return pcs;
    }

    const std::vector<std::uint32_t> &watchWords()
    {
        static const std::vector<std::uint32_t> words = parseHexList("WEMU_WATCH_WORD");
        return words;
    }

    bool hasWatchCstrs()
    {
        return !watchCstrs().empty();
    }

    bool hasWatchRegs()
    {
        return !watchRegs().empty();
    }

    const std::vector<std::uint32_t> &countPcs()
    {
        static const std::vector<std::uint32_t> pcs = parseHexList("WEMU_PC_COUNT");
        return pcs;
    }

    // Tallies live in a slot array parallel to countPcs() (not a map) so the signal handler below can
    // read them without walking a container another thread may be mid-mutation on.
    static std::vector<std::uint64_t> &pcCountSlots()
    {
        static std::vector<std::uint64_t> slots(countPcs().size(), 0);
        return slots;
    }

    void notePcCount(const std::uint32_t ppcPc)
    {
        const auto &pcs = countPcs();
        const auto it = std::ranges::find(pcs, ppcPc);
        if (it == pcs.end())
            return;
        pcCountSlots()[static_cast<std::size_t>(it - pcs.begin())]++;
    }

    void dumpPcCounts()
    {
        if (countPcs().empty())
            return;
        std::cout << "\n[DIAG] WEMU_PC_COUNT tallies (PC: executions):" << std::endl;
        for (std::size_t i = 0; i < countPcs().size(); i++)
            std::cout << std::format("  0x{:08X}: {}", countPcs()[i], pcCountSlots()[i]) << std::endl;
    }

#ifndef _WIN32
    static void sigWrite(const char *text, const std::size_t length)
    {
        for (std::size_t done = 0; done < length;) {
            const auto written = ::write(STDOUT_FILENO, text + done, length - done);
            if (written <= 0)
                return;
            done += static_cast<std::size_t>(written);
        }
    }

    // std::format allocates, so the handler formats by hand (async-signal-safe).
    static char *sigPutHex32(char *out, std::uint32_t value)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
            *out++ = "0123456789ABCDEF"[(value >> shift) & 0xF];
        return out;
    }

    static char *sigPutDec(char *out, std::uint64_t value)
    {
        char digits[24];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);
        while (n)
            *out++ = digits[--n];
        return out;
    }

    extern "C" void pcCountSignalHandler(const int sig)
    {
        static const char header[] = "\n[DIAG] WEMU_PC_COUNT tallies (killed by signal):\n";
        sigWrite(header, sizeof(header) - 1);
        for (std::size_t i = 0; i < countPcs().size(); i++) {
            char line[64];
            char *p = line;
            *p++ = ' ';
            *p++ = ' ';
            *p++ = '0';
            *p++ = 'x';
            p = sigPutHex32(p, countPcs()[i]);
            *p++ = ':';
            *p++ = ' ';
            p = sigPutDec(p, pcCountSlots()[i]);
            *p++ = '\n';
            sigWrite(line, static_cast<std::size_t>(p - line));
        }
        ::_exit(128 + sig);
    }
#endif

    void installPcCountSignalDump()
    {
        if (countPcs().empty())
            return;
        pcCountSlots(); // build the slot array now, not inside the handler
#ifndef _WIN32
        struct sigaction action = {};
        action.sa_handler = &pcCountSignalHandler;
        sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
#endif
    }

    bool titleSeqOrderFix()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_ORDER_FIX");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    bool deferTitleSeqCalc(Interpreter &cpu, const std::uint32_t ppcPc)
    {
        // The title sequence's calc; entering it starts (and, with a stale pending code, wastes)
        // MK8's single sequence change. Self-guarded on the real prologue so this stays inert if
        // another title happens to execute this address.
        constexpr std::uint32_t kCalcPc = 0x02547174;
        constexpr std::uint32_t kCalcFirstWord = 0x7C0802A6; // mflr r0
        if (ppcPc != kCalcPc)
            return false;

        // Fail open: if the pending code never becomes the menu's, let MK8 run as it does today
        // rather than deferring forever.
        constexpr std::uint32_t kMenuPending = 3;
        constexpr std::uint32_t kMaxDeferrals = 100000;
        static std::uint32_t deferrals = 0;
        if (deferrals >= kMaxDeferrals)
            return false;

        std::uint32_t pending = 0;
        try {
            if (cpu.m_memory.read<std::uint32_t>(kCalcPc) != kCalcFirstWord)
                return false;
            const std::uint32_t mgr = cpu.m_memory.read<std::uint32_t>(0x101D693C);
            const std::uint32_t obj = mgr ? cpu.m_memory.read<std::uint32_t>(mgr) : 0;
            pending = obj ? cpu.m_memory.read<std::uint32_t>(obj + 0x138) : 0;
        } catch (...) {
            return false;
        }
        if (pending == kMenuPending) {
            if (deferrals)
                Utils::Log::error("[DIAG] WEMU_ORDER_FIX: pending=3, letting sequence calc run (deferred {}x)", deferrals);
            return false;
        }
        if (deferrals == 0)
            Utils::Log::error("[DIAG] WEMU_ORDER_FIX: deferring sequence calc 0x{:08X} (pending={} , want 3)", kCalcPc, pending);
        deferrals++;
        if (deferrals == kMaxDeferrals)
            Utils::Log::error("[DIAG] WEMU_ORDER_FIX: deferral cap reached; running calc with pending={}", pending);
        return true;
    }

    static void dumpBacktrace(const Interpreter &cpu, const char *indent)
    {
        auto &mut = const_cast<Interpreter &>(cpu); // Memory::read is non-const; diagnostics only read
        std::uint32_t sp = cpu.m_gpr[1];
        for (int frame = 0; frame < 24; frame++) {
            std::uint32_t callerSp = 0;
            try {
                callerSp = mut.m_memory.read<std::uint32_t>(sp);
            } catch (...) {
                std::cout << std::format("{}#{:<2} <back-chain unreadable @ sp=0x{:08X}>", indent, frame, sp) << std::endl;
                break;
            }
            if (callerSp == 0 || callerSp <= sp)
                break;
            std::uint32_t savedLr = 0;
            try {
                savedLr = mut.m_memory.read<std::uint32_t>(callerSp + 4);
            } catch (...) {
                break;
            }
            std::cout << std::format("{}#{:<2} sp=0x{:08X} ret=", indent, frame, callerSp)
                      << symbolize(cpu, savedLr + Memory::MemoryMap::ApplicationCode) << std::endl;
            sp = callerSp;
        }
    }

    static std::string readGuestCString(Interpreter &cpu, const std::uint32_t addr, const std::size_t maxLen = 256)
    {
        std::string out;
        if (!addr)
            return out;
        out.reserve(std::min<std::size_t>(maxLen, 64));
        for (std::size_t i = 0; i < maxLen; i++) {
            std::uint8_t ch = 0;
            try {
                ch = cpu.m_memory.read<std::uint8_t>(addr + static_cast<std::uint32_t>(i));
            } catch (...) {
                out += "<unreadable>";
                break;
            }
            if (ch == 0)
                break;
            out += (ch >= 0x20 && ch < 0x7f) ? static_cast<char>(ch) : '.';
        }
        return out;
    }

    void noteWatchCstrs(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_WATCH_CSTR_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 20;
        }();
        static std::map<std::tuple<std::uint32_t, std::uint8_t, bool, std::uint32_t>, std::uint32_t> hits;
        auto &mut = const_cast<Interpreter &>(cpu);
        for (const auto &spec: watchCstrs()) {
            if (spec.pc != ppcPc)
                continue;
            const auto key = std::tuple {spec.pc, spec.gpr, spec.deref, spec.literalAddr};
            const auto count = ++hits[key];
            if (count > maxHits)
                continue;
            std::uint32_t addr = spec.literal ? spec.literalAddr : cpu.m_gpr[spec.gpr];
            std::string source = spec.literal ? std::format("@0x{:08X}", spec.literalAddr) : std::format("r{}", spec.gpr);
            if (spec.deref && !spec.literal) {
                try {
                    addr = mut.m_memory.read<std::uint32_t>(addr);
                    source = "*" + source;
                } catch (...) {
                    source = "*" + source + "<unreadable>";
                    addr = 0;
                }
            }
            std::cout << std::format("[DIAG] watch cstr PC=0x{:08X} {} -> 0x{:08X} hit #{} \"{}\" thread=0x{:08X} LR=",
                                     ppcPc, source, addr, count, readGuestCString(mut, addr), cpu.m_scheduler.currentHandle())
                      << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
            dumpBacktrace(cpu, "       ");
        }
    }

    void noteWatchRegs(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_WATCH_REG_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 32;
        }();
        static std::map<std::tuple<std::uint32_t, std::uint8_t, std::uint32_t>, std::uint32_t> hits;
        for (const auto &spec: watchRegs()) {
            if (spec.pc != ppcPc || cpu.m_gpr[spec.gpr] != spec.value)
                continue;
            const auto key = std::tuple {spec.pc, spec.gpr, spec.value};
            const auto count = ++hits[key];
            if (count > maxHits)
                continue;
            std::cout << std::format("[DIAG] watch reg PC=0x{:08X} r{}==0x{:08X} hit #{} thread=0x{:08X} LR=",
                                     ppcPc, spec.gpr, spec.value, count, cpu.m_scheduler.currentHandle())
                      << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
            for (int i = 0; i < 32; i += 8)
                std::cout << std::format("       r{:<2}: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", i, cpu.m_gpr[i],
                                         cpu.m_gpr[i + 1], cpu.m_gpr[i + 2], cpu.m_gpr[i + 3], cpu.m_gpr[i + 4], cpu.m_gpr[i + 5],
                                         cpu.m_gpr[i + 6], cpu.m_gpr[i + 7])
                          << std::endl;
            dumpBacktrace(cpu, "       ");
        }
    }

    void pollWatchWords(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        static std::map<std::uint32_t, std::uint32_t> last;
        static const bool logInitial = []() {
            const char *env = std::getenv("WEMU_WATCH_WORD_INIT");
            return env && env[0] == '1';
        }();
        auto &mut = const_cast<Interpreter &>(cpu);
        for (const std::uint32_t addr: watchWords()) {
            std::uint32_t v = 0;
            try {
                v = mut.m_memory.read<std::uint32_t>(addr);
            } catch (...) {
                continue;
            }
            const auto it = last.find(addr);
            if (it == last.end()) {
                last[addr] = v;
                if (logInitial) {
                    std::cout << std::format("[DIAG] watch word 0x{:08X}: initial 0x{:08X} thread=0x{:08X} LR=", addr, v,
                                             cpu.m_scheduler.currentHandle())
                              << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << " at PC="
                              << symbolize(cpu, ppcPc) << std::endl;
                    dumpBacktrace(cpu, "       ");
                }
            } else if (it->second != v) {
                std::cout << std::format("[DIAG] watch word 0x{:08X}: 0x{:08X} -> 0x{:08X} thread=0x{:08X} LR=", addr, it->second, v,
                                         cpu.m_scheduler.currentHandle())
                          << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << " at PC=" << symbolize(cpu, ppcPc)
                          << std::endl;
                dumpBacktrace(cpu, "       ");
                it->second = v;
            }
        }
    }

    std::uint64_t heartbeatInterval()
    {
        static const std::uint64_t n = []() -> std::uint64_t {
            const char *env = std::getenv("WEMU_HEARTBEAT");
            return env ? std::strtoull(env, nullptr, 10) : 0;
        }();
        return n;
    }

    void pollHeartbeat(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        static std::uint64_t count = 0;
        if (++count % heartbeatInterval() != 0)
            return;
        std::cout << std::format("[DIAG] heartbeat {}M instr, PC=", count / 1000000) << symbolize(cpu, ppcPc) << " LR="
                  << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
        static constexpr const char *stateNames[] = {"Ready", "Running", "Sleeping", "Waiting", "Finished", "Paused"};
        for (const auto &t: cpu.m_scheduler.threads()) {
            if (t->state == ThreadContext::State::Finished)
                continue;
            std::string line = std::format("       thread 0x{:08X} \"{}\" prio={} aff={} {}", t->osThreadPtr, t->name, t->priority,
                                           t->affinity, stateNames[static_cast<int>(t->state)]);
            if (t->state == ThreadContext::State::Waiting)
                line += t->joinTarget ? std::format(" join=0x{:08X}", t->joinTarget) : std::format(" key=0x{:08X}", t->waitKey);
            if (t.get() != cpu.m_scheduler.current())
                line += std::format(" pc=0x{:08X} lr=0x{:08X}", t->pc + Memory::MemoryMap::ApplicationCode,
                                    t->lr + Memory::MemoryMap::ApplicationCode);
            std::cout << line << std::endl;
        }
    }

    bool resourceLookupTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_RES_TRACE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    void noteResourceLookup(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (ppcPc != 0x024D3054)
            return;
        static const std::uint32_t lrFilter = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_RES_TRACE_LR");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 16)) : 0;
        }();
        const std::uint32_t guestLr = cpu.m_lr + Memory::MemoryMap::ApplicationCode;
        if (lrFilter && guestLr != lrFilter)
            return;
        static std::uint32_t hits = 0;
        if (++hits > 64)
            return;
        auto &mut = const_cast<Interpreter &>(cpu);
        const std::uint32_t base = cpu.m_gpr[3];
        const std::uint32_t indexPtr = cpu.m_gpr[4];
        try {
            const std::uint32_t index = mut.m_memory.read<std::uint32_t>(indexPtr);
            const std::uint32_t encoded = mut.m_memory.read<std::uint32_t>(base + index * 8);
            const std::uint8_t validFlag = mut.m_memory.read<std::uint8_t>(base + index * 8 + 4);
            const std::uint8_t disabled = mut.m_memory.read<std::uint8_t>(base + 0x55);
            const std::uint32_t manager = mut.m_memory.read<std::uint32_t>(0x101D693C);
            const std::uint32_t current = manager ? mut.m_memory.read<std::uint32_t>(manager) : 0;
            const std::uint32_t pending = current ? mut.m_memory.read<std::uint32_t>(current + 0x138) : 0;
            const std::uint32_t flags134 = current ? mut.m_memory.read<std::uint32_t>(current + 0x134) : 0;
            const std::uint32_t decoded = encoded ^ 0x37F1B26Bu;
            std::cout << std::format(
                "[DIAG] resLookup#{} base=0x{:08X} index={} encoded=0x{:08X} decoded=0x{:08X} disabled55={} validFlag={} current=0x{:08X} pending138={} f134=0x{:08X} thread=0x{:08X} LR=",
                hits, base, index, encoded, decoded, disabled, validFlag, current, pending, flags134, cpu.m_scheduler.currentHandle())
                      << symbolize(cpu, guestLr) << std::endl;
        } catch (...) {
            std::cout << std::format("[DIAG] resLookup#{} <unreadable> base=0x{:08X} indexPtr=0x{:08X}", hits, base, indexPtr)
                      << std::endl;
        }
    }

    bool readinessTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_READY_TRACE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    bool itemResourceTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_ITEM_RES_TRACE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    bool flvcNullTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_FLVC_TRACE_NULL");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    bool seadHeapTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_SEAD_HEAP_TRACE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    bool seadHeapContextTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_SEAD_HEAP_CTX_TRACE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    static std::uint32_t readWordOrZero(Interpreter &cpu, const std::uint32_t addr)
    {
        if (!addr)
            return 0;
        try {
            return cpu.m_memory.read<std::uint32_t>(addr);
        } catch (...) {
            return 0;
        }
    }

    static std::uint8_t readByteOrZero(Interpreter &cpu, const std::uint32_t addr)
    {
        if (!addr)
            return 0;
        try {
            return cpu.m_memory.read<std::uint8_t>(addr);
        } catch (...) {
            return 0;
        }
    }

    static bool cstrContains(const std::string &text, const char *needle)
    {
        return !needle || !needle[0] || text.find(needle) != std::string::npos;
    }

    static bool isItemResourceTracePc(const std::uint32_t ppcPc)
    {
        switch (ppcPc) {
            case 0x020E38C4: // item resource object constructor entry
            case 0x020E3D8C: // requested subresource/name formatting path
            case 0x020E3F14: // primary/effect lookup returned in r3
            case 0x020E3F8C: // primary payload accepted
            case 0x020E3FA4: // primary-ready flag set
            case 0x020E3FB8: // secondary-ready flag set
            case 0x020E4030: // secondary lookup returned in r3
            case 0x020E4054: // secondary-ready flag set after lookup
            case 0x020E406C: // fallback marks resource ready
            case 0x020E4070: // constructor epilogue area
                return true;
            default:
                return false;
        }
    }

    static std::uint32_t itemResourceCandidate(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (ppcPc == 0x020E3D8C || ppcPc == 0x020E3F14 || ppcPc == 0x020E3F8C || ppcPc == 0x020E3FA4 || ppcPc == 0x020E3FB8
            || ppcPc == 0x020E4030 || ppcPc == 0x020E4054 || ppcPc == 0x020E406C || ppcPc == 0x020E4070)
            return cpu.m_gpr[30];
        return cpu.m_gpr[3];
    }

    static const char *itemResourcePcLabel(const std::uint32_t ppcPc)
    {
        switch (ppcPc) {
            case 0x020E38C4:
                return "ctor";
            case 0x020E3D8C:
                return "name";
            case 0x020E3F14:
                return "primary-lookup-return";
            case 0x020E3F8C:
                return "primary-payload";
            case 0x020E3FA4:
                return "primary-ready";
            case 0x020E3FB8:
                return "secondary-ready-direct";
            case 0x020E4030:
                return "secondary-lookup-return";
            case 0x020E4054:
                return "secondary-ready-lookup";
            case 0x020E406C:
                return "fallback-ready";
            case 0x020E4070:
                return "epilogue";
            default:
                return "unknown";
        }
    }

    static void appendEflkTableSummary(std::string &line, Interpreter &cpu, const std::uint32_t manager,
                                       const std::string &needle)
    {
        const std::uint32_t table = readWordOrZero(cpu, manager + 0x2C);
        const std::uint32_t sectionCount = readWordOrZero(cpu, table + 0x4);
        const std::uint32_t sectionArray = readWordOrZero(cpu, table + 0x8);
        line += std::format(" eflkMgr=0x{:08X} table=0x{:08X} sections={} array=0x{:08X}", manager, table, sectionCount,
                            sectionArray);
        if (!table || !sectionArray || sectionCount > 256)
            return;

        std::uint32_t eflkSections = 0;
        std::uint32_t scannedNames = 0;
        std::uint32_t exactMatches = 0;
        std::uint32_t containsMatches = 0;
        std::vector<std::string> samples;
        std::vector<std::string> matches;
        std::vector<std::string> rawSlots;
        const std::uint32_t sectionsToScan = std::min<std::uint32_t>(sectionCount, 32);
        for (std::uint32_t s = 0; s < sectionsToScan; s++) {
            const std::uint32_t section = readWordOrZero(cpu, sectionArray + s * 4);
            const std::uint32_t magic = readWordOrZero(cpu, section);
            if (rawSlots.size() < 8)
                rawSlots.push_back(std::format("#{}:ptr=0x{:08X} magic=0x{:08X} w4=0x{:08X} w8=0x{:08X} wC=0x{:08X}", s,
                                               section, magic, readWordOrZero(cpu, section + 0x4),
                                               readWordOrZero(cpu, section + 0x8), readWordOrZero(cpu, section + 0xC)));
            if (magic != 0x65666C6Bu) // "eflk"
                continue;
            eflkSections++;
            const std::uint32_t version = readWordOrZero(cpu, section + 0x4);
            const std::uint32_t entryCount = readWordOrZero(cpu, section + 0x8);
            const std::uint32_t namesOff = readWordOrZero(cpu, section + 0xC);
            if (version != 9 || entryCount > 10000 || namesOff == 0)
                continue;
            const std::uint32_t entriesToScan = std::min<std::uint32_t>(entryCount, 4096);
            for (std::uint32_t i = 0; i < entriesToScan; i++) {
                const std::uint32_t nameOff = readWordOrZero(cpu, section + 0x10 + i * 8 + 4);
                const std::string name = readGuestCString(cpu, section + namesOff + nameOff, 80);
                if (name.empty())
                    continue;
                scannedNames++;
                if (samples.size() < 5)
                    samples.push_back(name);
                const bool exact = !needle.empty() && name == needle;
                const bool contains = !needle.empty() && name.find(needle) != std::string::npos;
                if (exact)
                    exactMatches++;
                if (contains)
                    containsMatches++;
                if ((exact || contains || name.find("Horn") != std::string::npos || name.find("horn") != std::string::npos)
                    && matches.size() < 8)
                    matches.push_back(std::format("#{}:{}", i, name));
            }
        }

        line += std::format(" eflkSections={} names={} needle=\"{}\" exact={} contains={}", eflkSections, scannedNames,
                            needle, exactMatches, containsMatches);
        if (!rawSlots.empty()) {
            line += " rawSlots=[";
            for (std::size_t i = 0; i < rawSlots.size(); i++) {
                if (i)
                    line += ";";
                line += rawSlots[i];
            }
            line += "]";
        }
        if (!samples.empty()) {
            line += " sample=[";
            for (std::size_t i = 0; i < samples.size(); i++) {
                if (i)
                    line += ",";
                line += samples[i];
            }
            line += "]";
        }
        if (!matches.empty()) {
            line += " matches=[";
            for (std::size_t i = 0; i < matches.size(); i++) {
                if (i)
                    line += ",";
                line += matches[i];
            }
            line += "]";
        }
    }

    void noteItemResourceTrace(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (!isItemResourceTracePc(ppcPc))
            return;

        static const char *nameFilter = std::getenv("WEMU_ITEM_RES_TRACE_NAME");
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_ITEM_RES_TRACE_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 128;
        }();
        static std::uint32_t hits = 0;
        static std::vector<std::uint32_t> trackedResources;
        static std::map<std::uint32_t, std::string> resourceNames;

        auto &mut = const_cast<Interpreter &>(cpu);
        const std::uint32_t res = itemResourceCandidate(cpu, ppcPc);
        std::string requested = (ppcPc == 0x020E3D8C) ? readGuestCString(mut, cpu.m_gpr[5]) : std::string {};
        if (ppcPc == 0x020E3D8C && res)
            resourceNames[res] = requested;
        else if (res) {
            if (const auto it = resourceNames.find(res); it != resourceNames.end())
                requested = it->second;
        }
        const std::string cachedName = res ? readGuestCString(mut, res + 0xB8) : std::string {};
        const bool matchesName = cstrContains(requested, nameFilter) || cstrContains(cachedName, nameFilter);
        const bool alreadyTracked = std::ranges::find(trackedResources, res) != trackedResources.end();
        if (nameFilter && nameFilter[0]) {
            if (ppcPc == 0x020E3D8C && matchesName && res && !alreadyTracked)
                trackedResources.push_back(res);
            else if (!alreadyTracked && !matchesName)
                return;
        }
        if (++hits > maxHits)
            return;

        const std::uint32_t guestLr = cpu.m_lr + Memory::MemoryMap::ApplicationCode;
        const std::uint32_t flagsWord = readWordOrZero(mut, res + 0x17C);
        const std::uint8_t flag17c = readByteOrZero(mut, res + 0x17C);
        const std::uint8_t flag17d = readByteOrZero(mut, res + 0x17D);
        const std::uint8_t flag17e = readByteOrZero(mut, res + 0x17E);
        const std::uint8_t flag17f = readByteOrZero(mut, res + 0x17F);
        const std::uint32_t primary = readWordOrZero(mut, res + 0x180);
        const std::uint32_t secondary = readWordOrZero(mut, res + 0x184);
        const std::uint32_t aux188 = readWordOrZero(mut, res + 0x188);
        std::string line = std::format(
            "[DIAG] itemRes#{} {} PC=0x{:08X} res=0x{:08X} req=\"{}\" cached=\"{}\" flags17c=0x{:08X}/{}/{}/{}/{} +180=0x{:08X} +184=0x{:08X} +188=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} r29=0x{:08X} r30=0x{:08X} r31=0x{:08X} thread=0x{:08X} LR=",
            hits, itemResourcePcLabel(ppcPc), ppcPc, res, requested, cachedName, flagsWord, flag17c, flag17d, flag17e,
            flag17f, primary, secondary, aux188, cpu.m_gpr[3], cpu.m_gpr[4], cpu.m_gpr[5], cpu.m_gpr[29],
            cpu.m_gpr[30], cpu.m_gpr[31], cpu.m_scheduler.currentHandle());
        line += symbolize(cpu, guestLr);
        if (ppcPc == 0x020E3F14) {
            const std::string needle = !requested.empty() ? requested : (!cachedName.empty() ? cachedName : std::string {});
            appendEflkTableSummary(line, mut, cpu.m_gpr[31], needle);
        }
        std::cout << line << std::endl;
    }

    static void appendReadinessList(std::string &line, Interpreter &cpu, const char *name, const std::uint32_t list,
                                    const std::uint32_t count)
    {
        line += std::format(" {}=0x{:08X}[{}]", name, list, count);
        const std::uint32_t limit = std::min<std::uint32_t>(count, 4);
        for (std::uint32_t i = 0; i < limit; i++) {
            const std::uint32_t entryPtr = readWordOrZero(cpu, list + i * 4);
            const std::uint32_t vtbl = readWordOrZero(cpu, entryPtr);
            const std::uint32_t mask14 = readWordOrZero(cpu, entryPtr + 0x14);
            const std::uint32_t pending98 = readWordOrZero(cpu, entryPtr + 0x98);
            const std::uint32_t busy9c = readWordOrZero(cpu, entryPtr + 0x9C);
            const std::uint32_t flagsB0 = readWordOrZero(cpu, entryPtr + 0xB0);
            const std::uint32_t a30 = readWordOrZero(cpu, entryPtr + 0x30);
            const std::uint32_t a34 = readWordOrZero(cpu, entryPtr + 0x34);
            const std::uint32_t a38 = readWordOrZero(cpu, entryPtr + 0x38);
            line += std::format(
                " #{}:obj=0x{:08X} vt=0x{:08X} +14=0x{:08X} +98={} +9c={} +b0=0x{:08X} +30/34/38={}/{}/{}",
                i, entryPtr, vtbl, mask14, pending98, busy9c, flagsB0, a30, a34, a38);
        }
    }

    void noteReadinessGate(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (ppcPc != 0x02740478 && ppcPc != 0x027404E0 && ppcPc != 0x02740644 && ppcPc != 0x02740694
            && ppcPc != 0x027406A8 && ppcPc != 0x027406C4 && ppcPc != 0x027406C8 && ppcPc != 0x02740764)
            return;
        static const std::uint32_t lrFilter = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_READY_TRACE_LR");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 16)) : 0;
        }();
        const std::uint32_t guestLr = cpu.m_lr + Memory::MemoryMap::ApplicationCode;
        if (lrFilter && guestLr != lrFilter)
            return;
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_READY_TRACE_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 48;
        }();
        static std::uint32_t hits = 0;
        if (++hits > maxHits)
            return;

        auto &mut = const_cast<Interpreter &>(cpu);
        const std::uint32_t obj =
            (ppcPc == 0x02740644 || ppcPc == 0x02740694 || ppcPc == 0x027406A8 || ppcPc == 0x027406C4
             || ppcPc == 0x027406C8 || ppcPc == 0x02740764)
                ? cpu.m_gpr[30]
                : cpu.m_gpr[3];
        const std::uint32_t list40Count = readWordOrZero(mut, obj + 0x40);
        const std::uint32_t list40 = readWordOrZero(mut, obj + 0x44);
        const std::uint32_t list48Count = readWordOrZero(mut, obj + 0x48);
        const std::uint32_t list48 = readWordOrZero(mut, obj + 0x4C);
        const std::uint32_t total50 = readWordOrZero(mut, obj + 0x50);
        const std::uint8_t started54 = readByteOrZero(mut, obj + 0x54);
        const std::uint32_t current58 = readWordOrZero(mut, obj + 0x58);
        std::string line = std::format(
            "[DIAG] readyGate#{} PC=0x{:08X} obj=0x{:08X} r3=0x{:08X} +40={} +44=0x{:08X} +48={} +4c=0x{:08X} +50={} +54={} +58=0x{:08X} thread=0x{:08X} LR=",
            hits, ppcPc, obj, cpu.m_gpr[3], list40Count, list40, list48Count, list48, total50, started54, current58,
            cpu.m_scheduler.currentHandle());
        line += symbolize(cpu, guestLr);
        appendReadinessList(line, mut, "wait40", list40, list40Count);
        appendReadinessList(line, mut, "load48", list48, list48Count);
        std::cout << line << std::endl;
    }

    void applyGuestPatches(Interpreter &cpu)
    {
        const char *env = std::getenv("WEMU_PATCH");
        if (!env)
            return;
        for (const char *p = env; *p;) {
            char *end = nullptr;
            const std::uint32_t addr = static_cast<std::uint32_t>(std::strtoul(p, &end, 16));
            if (!end || *end != ':')
                break;
            const std::uint32_t value = static_cast<std::uint32_t>(std::strtoul(end + 1, &end, 16));
            // Both views: m_memory backs data accesses, m_binary.m_memory backs instruction fetch.
            cpu.m_memory.write<std::uint32_t>(addr, value);
            cpu.m_binary.m_memory.write<std::uint32_t>(addr, value);
            std::cout << std::format("[DIAG] patched guest word 0x{:08X} = 0x{:08X}", addr, value) << std::endl;
            p = (*end == ',') ? end + 1 : end;
        }
    }

    static std::uint32_t flvcNullTraceMax()
    {
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_FLVC_TRACE_NULL_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 96;
        }();
        return maxHits;
    }

    static const char *flvcNullTracePcLabel(const std::uint32_t ppcPc)
    {
        switch (ppcPc) {
            case 0x02693068:
                return "append-direct";
            case 0x026932D0:
                return "append-compatible";
            case 0x026933A4:
                return "bind-direct-null-skipped";
            case 0x02692E54:
                return "apply-dispatch";
            default:
                return "unknown";
        }
    }

    static void appendGuestWords(std::string &line, Interpreter &cpu, const char *label, const std::uint32_t base,
                                 const std::uint32_t bytes)
    {
        line += std::format(" {}@0x{:08X}:", label, base);
        for (std::uint32_t off = 0; off < bytes; off += 4)
            line += std::format(" +{:02X}=0x{:08X}", off, readWordOrZero(cpu, base + off));
    }

    static void appendFlvcAnimSummary(std::string &line, Interpreter &cpu, const std::uint32_t anim)
    {
        const std::uint32_t block = readWordOrZero(cpu, anim + 0x08);
        const std::uint32_t vtable = readWordOrZero(cpu, anim + 0x14);
        const std::uint32_t helpers = readWordOrZero(cpu, anim + 0x18);
        const std::uint32_t pairs = readWordOrZero(cpu, anim + 0x1C);
        const std::uint32_t count = readWordOrZero(cpu, anim + 0x20) & 0xFFFF;
        const std::uint32_t capacity = readWordOrZero(cpu, anim + 0x22) & 0xFFFF;
        line += std::format(" animFields block=0x{:08X} vt=0x{:08X} helpers=0x{:08X} pairs=0x{:08X} count={} cap={}",
                            block, vtable, helpers, pairs, count, capacity);
        const std::uint32_t limit = std::min<std::uint32_t>(count, 6);
        for (std::uint32_t i = 0; i < limit; i++) {
            const std::uint32_t target = readWordOrZero(cpu, pairs + i * 8);
            const std::uint32_t curve = readWordOrZero(cpu, pairs + i * 8 + 4);
            const std::uint32_t bound = readWordOrZero(cpu, target + 8);
            const std::uint8_t kind = readByteOrZero(cpu, curve + 0x1D);
            const std::uint32_t magic = readWordOrZero(cpu, curve);
            line += std::format(" pair#{}=target:0x{:08X}/bound:0x{:08X}/curve:0x{:08X}/kind:{}/w0:0x{:08X}",
                                i, target, bound, curve, kind, magic);
        }
    }

    void noteFlvcNullTrace(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (ppcPc != 0x02693068 && ppcPc != 0x026932D0 && ppcPc != 0x026933A4 && ppcPc != 0x02692E54)
            return;

        auto &mut = const_cast<Interpreter &>(cpu);
        std::uint32_t anim = 0;
        std::uint32_t target = 0;
        std::uint32_t curve = 0;
        std::uint32_t node = 0;
        if (ppcPc == 0x02693068 || ppcPc == 0x026932D0) {
            anim = cpu.m_gpr[3];
            target = cpu.m_gpr[4];
            curve = cpu.m_gpr[5];
        } else if (ppcPc == 0x026933A4) {
            anim = cpu.m_gpr[29];
            node = cpu.m_gpr[30];
            target = cpu.m_gpr[4];
            curve = cpu.m_gpr[24];
        } else {
            anim = cpu.m_gpr[31];
            target = cpu.m_gpr[4];
            curve = cpu.m_gpr[5];
        }

        const std::uint32_t bound = readWordOrZero(mut, target + 8);
        if (target && bound)
            return;

        static std::uint32_t hits = 0;
        if (++hits > flvcNullTraceMax())
            return;

        const std::uint32_t guestLr = cpu.m_lr + Memory::MemoryMap::ApplicationCode;
        const std::uint8_t kind = readByteOrZero(mut, curve + 0x1D);
        std::string line = std::format(
            "[DIAG] FLVC null-target #{} {} PC=0x{:08X} anim=0x{:08X} target=0x{:08X} bound=0x{:08X} curve=0x{:08X} kind={} node=0x{:08X} curveName=\"{}\" boundName=\"{}\" thread=0x{:08X} LR=",
            hits, flvcNullTracePcLabel(ppcPc), ppcPc, anim, target, bound, curve, kind, node, readGuestCString(mut, curve, 0x18),
            readGuestCString(mut, bound ? bound + 0x80 : 0, 0x18), cpu.m_scheduler.currentHandle());
        line += symbolize(cpu, guestLr);
        appendFlvcAnimSummary(line, mut, anim);
        appendGuestWords(line, mut, "targetWords", target, 0x30);
        if (curve)
            appendGuestWords(line, mut, "curveWords", curve, 0x28);
        std::cout << line << std::endl;
        dumpBacktrace(cpu, "       ");
    }

    static std::uint32_t seadHeapTraceMax()
    {
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_SEAD_HEAP_TRACE_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 128;
        }();
        return maxHits;
    }

    static std::uint32_t seadHeapTraceMin()
    {
        static const std::uint32_t minSize = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_SEAD_HEAP_TRACE_MIN");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0)) : 0x100000;
        }();
        return minSize;
    }

    static std::uint32_t seadHeapTraceHeap()
    {
        static const std::uint32_t heap = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_SEAD_HEAP_TRACE_HEAP");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0)) : 0;
        }();
        return heap;
    }

    static bool isPowerOfTwo(const std::uint32_t value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    static std::uint32_t alignUpPow2(const std::uint32_t value, const std::uint32_t align)
    {
        if (align <= 1)
            return value;
        return (value + align - 1) & ~(align - 1);
    }

    static void appendSeadHeapListSummary(std::string &line, Interpreter &cpu, const char *label, const std::uint32_t head,
                                          const std::uint32_t request, const std::uint32_t align)
    {
        const std::uint32_t countField = readWordOrZero(cpu, head + 0x08);
        const std::uint32_t linkOffset = readWordOrZero(cpu, head + 0x0C);
        const std::uint32_t firstLink = readWordOrZero(cpu, head + 0x04);
        if (linkOffset == 0xFFFFFFFFu) {
            line += std::format(" {}List head=0x{:08X} off=-1 countField={} first=0x{:08X}", label, head, countField, firstLink);
            return;
        }

        const std::uint32_t sentinelNode = head - linkOffset;
        std::uint32_t link = firstLink;
        std::uint32_t nodes = 0;
        std::uint32_t fitNodes = 0;
        std::uint32_t largest = 0;
        std::uint32_t largestFit = 0;
        std::uint64_t total = 0;
        std::vector<std::string> firstNodes;

        for (; link && nodes < 256; nodes++) {
            const std::uint32_t node = link - linkOffset;
            if (node == sentinelNode)
                break;

            const std::uint32_t size = readWordOrZero(cpu, node + 0x08);
            std::uint32_t padding = 0;
            if (isPowerOfTwo(align)) {
                const std::uint32_t payloadBase = node + linkOffset + 0x10;
                padding = alignUpPow2(payloadBase, align) - payloadBase;
            }
            const std::uint64_t needed = static_cast<std::uint64_t>(request) + padding;
            const bool fits = needed <= size;
            total += size;
            largest = std::max(largest, size);
            if (fits) {
                fitNodes++;
                largestFit = std::max(largestFit, size);
            }
            if (firstNodes.size() < 4)
                firstNodes.push_back(std::format("0x{:08X}:sz=0x{:X}:pad=0x{:X}:next=0x{:08X}", node, size, padding,
                                                 readWordOrZero(cpu, node + linkOffset + 0x04)));

            const std::uint32_t nextLink = readWordOrZero(cpu, node + linkOffset + 0x04);
            if (nextLink == link) {
                link = nextLink;
                nodes++;
                break;
            }
            link = nextLink;
        }

        line += std::format(" {}List head=0x{:08X} off=0x{:X} countField={} nodes={} total=0x{:X} largest=0x{:X} fitNodes={} largestFit=0x{:X}",
                            label, head, linkOffset, countField, nodes, total, largest, fitNodes, largestFit);
        if (nodes >= 256 && link)
            line += " truncated";
        if (!firstNodes.empty()) {
            line += " first=[";
            for (std::size_t i = 0; i < firstNodes.size(); i++) {
                if (i)
                    line += ", ";
                line += firstNodes[i];
            }
            line += "]";
        }
    }

    static void appendSeadHeapFields(std::string &line, Interpreter &cpu, const std::uint32_t heap, const std::uint32_t request,
                                     const std::uint32_t align)
    {
        const std::uint32_t name = readWordOrZero(cpu, heap + 0x28);
        line += std::format(
            " heapFields +1c=0x{:08X} +20=0x{:08X} +24=0x{:08X} +28=0x{:08X}/\"{}\" +50=0x{:08X} +90=0x{:08X} +94=0x{:08X} +98=0x{:08X} +9c=0x{:08X} +a0={} +a4=0x{:08X} +a8=0x{:08X} +ac=0x{:08X} +b0={} +b4=0x{:08X}",
            readWordOrZero(cpu, heap + 0x1C), readWordOrZero(cpu, heap + 0x20), readWordOrZero(cpu, heap + 0x24),
            name, readGuestCString(cpu, name, 0x40), readWordOrZero(cpu, heap + 0x50), readWordOrZero(cpu, heap + 0x90),
            readWordOrZero(cpu, heap + 0x94), readWordOrZero(cpu, heap + 0x98), readWordOrZero(cpu, heap + 0x9C),
            readWordOrZero(cpu, heap + 0xA0), readWordOrZero(cpu, heap + 0xA4), readWordOrZero(cpu, heap + 0xA8),
            readWordOrZero(cpu, heap + 0xAC), readWordOrZero(cpu, heap + 0xB0), readWordOrZero(cpu, heap + 0xB4));
        appendSeadHeapListSummary(line, cpu, "free", heap + 0x98, request, align);
        appendSeadHeapListSummary(line, cpu, "used", heap + 0xA8, request, align);
    }

    void noteSeadHeapTrace(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (ppcPc != 0x0273B238 && ppcPc != 0x0273B284)
            return;

        auto &mut = const_cast<Interpreter &>(cpu);
        const std::uint32_t heap = cpu.m_gpr[27];
        const std::uint32_t request = cpu.m_gpr[28];
        const std::uint32_t align = cpu.m_gpr[29];
        const std::uint32_t rounded = cpu.m_gpr[25];
        const std::uint32_t result = (ppcPc == 0x0273B284) ? cpu.m_gpr[3] : 0;
        const bool failed = (ppcPc == 0x0273B238) || result == 0;
        const std::uint32_t heapFilter = seadHeapTraceHeap();
        if (heapFilter && heap != heapFilter)
            return;
        if (!failed && request < seadHeapTraceMin())
            return;

        static std::uint32_t hits = 0;
        if (++hits > seadHeapTraceMax())
            return;

        const std::uint32_t savedLr = readWordOrZero(mut, cpu.m_gpr[1] + 0x6C);
        const std::uint32_t caller = savedLr ? savedLr + Memory::MemoryMap::ApplicationCode : cpu.m_lr + Memory::MemoryMap::ApplicationCode;
        std::string line = std::format(
            "[DIAG] SEAD heap {} #{} PC=0x{:08X} heap=0x{:08X} req=0x{:X} align=0x{:X} rounded=0x{:X} work=0x{:X} result=0x{:08X} thread=0x{:08X} caller=",
            failed ? "FAIL" : "large", hits, ppcPc, heap, request, align, rounded, cpu.m_gpr[26], result,
            cpu.m_scheduler.currentHandle());
        line += symbolize(cpu, caller);
        appendSeadHeapFields(line, mut, heap, rounded, align);
        std::cout << line << std::endl;
        if (failed)
            dumpBacktrace(cpu, "       ");
    }

    static std::uint32_t seadHeapContextTraceMax()
    {
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_SEAD_HEAP_CTX_TRACE_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 192;
        }();
        return maxHits;
    }

    static std::uint32_t seadHeapContextTraceThread()
    {
        static const std::uint32_t thread = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_SEAD_HEAP_CTX_TRACE_THREAD");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0)) : 0;
        }();
        return thread;
    }

    static bool seadHeapContextTraceGets()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_SEAD_HEAP_CTX_TRACE_GETS");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    void noteSeadHeapContextTrace(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (ppcPc != 0x0273D448 && ppcPc != 0x0273D474 && ppcPc != 0x0273D384)
            return;

        const std::uint32_t threadFilter = seadHeapContextTraceThread();
        if (threadFilter && cpu.m_scheduler.currentHandle() != threadFilter)
            return;

        const std::uint32_t current = (ppcPc == 0x0273D384) ? cpu.m_gpr[3] : 0;
        if (ppcPc == 0x0273D384 && current != 0 && !seadHeapContextTraceGets())
            return;

        static std::uint32_t hits = 0;
        if (++hits > seadHeapContextTraceMax())
            return;

        auto &mut = const_cast<Interpreter &>(cpu);
        const std::uint32_t thread = cpu.m_scheduler.currentHandle();
        std::string line;
        if (ppcPc == 0x0273D448) {
            line = std::format("[DIAG] SEAD heap ctx #{} set-enter thread=0x{:08X} new=0x{:08X} LR=", hits, thread,
                               cpu.m_gpr[4]);
            line += symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode);
        } else if (ppcPc == 0x0273D474) {
            line = std::format("[DIAG] SEAD heap ctx #{} set-return thread=0x{:08X} old=0x{:08X} installed=0x{:08X} LR=",
                               hits, thread, cpu.m_gpr[0], cpu.m_gpr[31]);
            line += symbolize(cpu, readWordOrZero(mut, cpu.m_gpr[1] + 0x14) + Memory::MemoryMap::ApplicationCode);
        } else {
            // Current-heap gets are frequent. Log every zero result because it is usually the
            // interesting fallback-to-root condition. Full non-zero get logging is explicit via
            // WEMU_SEAD_HEAP_CTX_TRACE_GETS=1.
            line = std::format("[DIAG] SEAD heap ctx #{} get-return thread=0x{:08X} current=0x{:08X} caller=", hits,
                               thread, current);
            line += symbolize(cpu, cpu.m_gpr[0] + Memory::MemoryMap::ApplicationCode);
        }
        std::cout << line << std::endl;
        if (ppcPc == 0x0273D448 || ppcPc == 0x0273D474 || cpu.m_gpr[3] == 0)
            dumpBacktrace(cpu, "       ");
    }

    bool skipNullFlvcTarget(Interpreter &cpu, const std::uint32_t ppcPc)
    {
        constexpr std::uint32_t kFlanApplyDispatchPc = 0x02692E54;
        constexpr std::uint32_t kAfterDispatchPc = 0x02692E64;
        if (ppcPc != kFlanApplyDispatchPc)
            return false;

        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_FLVC_SKIP_NULL_TARGET");
            return env && env[0] == '1';
        }();
        if (!enabled)
            return false;

        const std::uint32_t target = cpu.m_gpr[4];
        if (!target)
            return false;

        std::uint32_t bound = 0;
        try {
            bound = cpu.m_memory.read<std::uint32_t>(target + 8);
        } catch (...) {
            return false;
        }
        if (bound)
            return false;

        static std::uint32_t skipped = 0;
        if (skipped < 16) {
            std::uint8_t kind = 0;
            try {
                kind = cpu.m_memory.read<std::uint8_t>(cpu.m_gpr[5] + 0x1D);
            } catch (...) {
            }
            Utils::Log::error("[DIAG] WEMU_FLVC_SKIP_NULL_TARGET: skip #{} anim=0x{:08X} target=0x{:08X} curve=0x{:08X} kind={} LR=0x{:08X}",
                              skipped + 1, cpu.m_gpr[31], target, cpu.m_gpr[5], kind,
                              cpu.m_lr + Memory::MemoryMap::ApplicationCode);
        }
        skipped++;
        cpu.m_pc = kAfterDispatchPc - Memory::MemoryMap::ApplicationCode;
        return true;
    }

    bool mk8UiArrayZeroFill()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_MK8_ZERO_UI_ARRAYS");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    void zeroMk8UiArrayAlloc(Interpreter &cpu, const std::uint32_t ppcPc)
    {
        if (ppcPc != 0x023AA260)
            return;

        const std::uint32_t ptr = cpu.m_gpr[3];
        const std::uint32_t count = cpu.m_gpr[31];
        if (!ptr || count == 0 || count > 0x4000)
            return;
        std::uint8_t *dst = cpu.m_memory.hostPtr(ptr);
        if (!dst)
            return;

        std::memset(dst, 0, static_cast<std::size_t>(count) * sizeof(std::uint32_t));
        static std::uint32_t hits = 0;
        if (hits++ < 12) {
            std::cout << std::format("[DIAG] MK8 zero UI array PC=0x{:08X} ptr=0x{:08X} count={} bytes=0x{:X} thread=0x{:08X} LR=",
                                      ppcPc, ptr, count, count * 4, cpu.m_scheduler.currentHandle())
                      << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
        }
    }

    bool mk8WorkQueueTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_MK8_WORKQ_TRACE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    namespace {
        std::uint32_t readU32OrZero(const Interpreter &cpu, const std::uint32_t addr, bool &ok)
        {
            try {
                ok = true;
                return const_cast<Interpreter &>(cpu).m_memory.read<std::uint32_t>(addr);
            } catch (...) {
                ok = false;
                return 0;
            }
        }

        bool shouldTraceMk8Queue(const std::uint32_t queue)
        {
            static const std::uint32_t filter = []() -> std::uint32_t {
                const char *env = std::getenv("WEMU_MK8_WORKQ_TRACE_QUEUE");
                return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 16)) : 0;
            }();
            return filter == 0 || filter == queue;
        }

        std::string formatMk8Queue(const Interpreter &cpu, const std::uint32_t queue)
        {
            bool ok = false;
            const std::uint32_t buf = readU32OrZero(cpu, queue + 0x98, ok);
            if (!ok)
                return std::format("queue=0x{:08X} <unreadable>", queue);
            const std::uint32_t cap = readU32OrZero(cpu, queue + 0x9C, ok);
            const std::uint32_t head = readU32OrZero(cpu, queue + 0xA0, ok);
            const std::uint32_t count = readU32OrZero(cpu, queue + 0xA4, ok);
            std::string out = std::format("queue=0x{:08X} buf=0x{:08X} cap={} head={} count={} slots:", queue, buf, cap, head, count);
            const std::uint32_t n = std::min<std::uint32_t>(cap, 8);
            for (std::uint32_t i = 0; i < n; ++i) {
                const std::uint32_t value = readU32OrZero(cpu, buf + i * 4, ok);
                out += ok ? std::format(" [{}]=0x{:08X}", i, value) : std::format(" [{}]=????????", i);
            }
            return out;
        }
    }

    void noteMk8WorkQueueTrace(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        constexpr std::uint32_t kPushEntry = 0x02542284;
        constexpr std::uint32_t kPushStoreWrap = 0x025422EC;
        constexpr std::uint32_t kPushStore = 0x02542340;
        constexpr std::uint32_t kPopEntry = 0x02542380;
        constexpr std::uint32_t kPopLoad = 0x025423E0;
        constexpr std::uint32_t kPopCall = 0x0254240C;
        if (ppcPc != kPushEntry && ppcPc != kPushStoreWrap && ppcPc != kPushStore && ppcPc != kPopEntry && ppcPc != kPopLoad && ppcPc != kPopCall)
            return;

        const std::uint32_t queue = ppcPc == kPushEntry ? cpu.m_gpr[3] : ppcPc == kPushStoreWrap || ppcPc == kPushStore ? cpu.m_gpr[30]
                                                                                                                        : ppcPc == kPopEntry ? cpu.m_gpr[3] : cpu.m_gpr[31];
        if (!queue || !shouldTraceMk8Queue(queue))
            return;

        static std::uint32_t logged = 0;
        static const std::uint32_t maxLogs = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_MK8_WORKQ_TRACE_MAX");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 80;
        }();

        bool force = false;
        std::string detail;
        if (ppcPc == kPushEntry) {
            detail = std::format("push-enter item=0x{:08X}", cpu.m_gpr[4]);
        } else if (ppcPc == kPushStoreWrap || ppcPc == kPushStore) {
            bool ok = false;
            const std::uint32_t old = readU32OrZero(cpu, cpu.m_gpr[0] + cpu.m_gpr[12], ok);
            detail = std::format("push-store slot=0x{:08X} old={} item=0x{:08X}", cpu.m_gpr[0] + cpu.m_gpr[12],
                                 ok ? std::format("0x{:08X}", old) : "????????", cpu.m_gpr[31]);
        } else if (ppcPc == kPopEntry) {
            detail = "pop-enter";
        } else if (ppcPc == kPopLoad) {
            bool ok = false;
            const std::uint32_t value = readU32OrZero(cpu, cpu.m_gpr[11] + cpu.m_gpr[10], ok);
            force = ok && value == 0;
            detail = std::format("pop-load slot=0x{:08X} value={}", cpu.m_gpr[11] + cpu.m_gpr[10], ok ? std::format("0x{:08X}", value) : "????????");
        } else {
            force = cpu.m_gpr[29] == 0;
            detail = std::format("pop-call item=0x{:08X}", cpu.m_gpr[29]);
        }

        if (logged >= maxLogs && !force)
            return;
        logged++;
        std::cout << std::format("[DIAG] MK8 workq 0x{:08X} {} thread=0x{:08X} LR=", ppcPc, detail, cpu.m_scheduler.currentHandle())
                  << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << " | " << formatMk8Queue(cpu, queue) << std::endl;
    }

    void noteWatchHit(const Interpreter &cpu, const std::uint32_t ppcPc)
    {
        // WEMU_WATCH_FILTER="3:4C62F0B8": only record hits where rN == the hex value.
        static const std::pair<long, std::uint32_t> filter = []() -> std::pair<long, std::uint32_t> {
            const char *env = std::getenv("WEMU_WATCH_FILTER");
            if (!env)
                return {-1, 0};
            char *colon = nullptr;
            const long reg = std::strtol(env, &colon, 10);
            if (!colon || *colon != ':')
                return {-1, 0};
            return {reg, static_cast<std::uint32_t>(std::strtoul(colon + 1, nullptr, 16))};
        }();
        if (filter.first >= 0 && filter.first < 32 && cpu.m_gpr[filter.first] != filter.second)
            return;
        static std::map<std::uint32_t, std::uint32_t> hits;
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_WATCH_MAX");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 20;
        }();
        const std::uint32_t n = ++hits[ppcPc];
        if (n > maxHits)
            return;
        std::cout << std::format("[DIAG] watch PC 0x{:08X} hit #{} (LR=", ppcPc, n)
                  << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << ")" << std::endl;
        for (int i = 0; i < 32; i += 8)
            std::cout << std::format("       r{:<2}: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", i, cpu.m_gpr[i],
                                     cpu.m_gpr[i + 1], cpu.m_gpr[i + 2], cpu.m_gpr[i + 3], cpu.m_gpr[i + 4], cpu.m_gpr[i + 5],
                                     cpu.m_gpr[i + 6], cpu.m_gpr[i + 7])
                      << std::endl;

        // WEMU_WATCH_DEREF="9" (register number): also dump 8 guest words at [rN]. An optional hex
        // offset ("3+c0") dumps at [rN + off] instead, for fields deep inside a large object.
        // Multiple comma-separated entries are accepted, e.g. "31+158,7,1+8".
        // WEMU_WATCH_DEREF_WORDS=N adjusts the number of words dumped for every entry.
        for (const auto &deref: watchDerefs()) {
            auto &mut = const_cast<Interpreter &>(cpu);
            const std::uint32_t base = cpu.m_gpr[deref.gpr] + deref.offset;
            std::string line = std::format("       [r{}+0x{:X}=0x{:08X}]:", deref.gpr, deref.offset, base);
            for (std::uint32_t off = 0; off < watchDerefWords() * 4; off += 4) {
                try {
                    line += std::format(" {:08X}", mut.m_memory.read<std::uint32_t>(base + off));
                } catch (...) {
                    line += " ????????";
                }
            }
            std::cout << line << std::endl;
        }
    }

    std::string symbolize(const Interpreter &cpu, const std::uint32_t address)
    {
        static const auto index = buildSymbolIndex(cpu.m_binary);

        // Nearest symbol at or before the address, if reasonably close (real RPX symtabs mostly
        // carry import/export names, so distant matches would be misleading).
        constexpr std::uint32_t MAX_SYMBOL_DISTANCE = 0x20000;
        const auto it = std::ranges::upper_bound(index, address, {}, [](const auto &e) { return e.first; });
        if (it != index.begin()) {
            const auto &[symAddr, name] = *std::prev(it);
            if (address - symAddr < MAX_SYMBOL_DISTANCE)
                return std::format("0x{:08X} <{}+0x{:X}>", address, *name, address - symAddr);
        }
        return std::format("0x{:08X}", address);
    }

    void dumpCrashContext(const Interpreter &cpu, const std::uint32_t ppcPc, const std::string &reason)
    {
        std::cout << "\n[DIAG] ==== crash context: " << reason << " ====" << std::endl;
        std::cout << "  PC  = " << symbolize(cpu, ppcPc) << std::endl;
        std::cout << "  LR  = " << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
        std::cout << std::format("  CTR = 0x{:08X}  XER.SO={}  CR=0x{:08X}", cpu.m_ctr, static_cast<int>(cpu.m_xer.so), cpu.m_cr.raw)
                  << std::endl;
        for (int i = 0; i < 32; i += 4)
            std::cout << std::format("  r{:<2}=0x{:08X} r{:<2}=0x{:08X} r{:<2}=0x{:08X} r{:<2}=0x{:08X}", i, cpu.m_gpr[i], i + 1,
                                     cpu.m_gpr[i + 1], i + 2, cpu.m_gpr[i + 2], i + 3, cpu.m_gpr[i + 3])
                      << std::endl;

        // Peek the objects the fault most likely dereferenced: dump a few words at each distinct
        // GPR that points into mapped guest memory. Reveals half-built containers (e.g. a NULL
        // backing pointer in an otherwise-populated struct) right at the crash.
        auto &mut = const_cast<Interpreter &>(cpu);
        std::cout << "  object peeks (words at object-register targets):" << std::endl;
        std::vector<std::uint32_t> seen;
        for (const int reg: {3, 28, 29, 30, 31}) {
            const std::uint32_t base = cpu.m_gpr[reg];
            if (base < Memory::MemoryMap::ApplicationCode || mut.m_memory.hostPtr(base) == nullptr)
                continue;
            if (std::ranges::find(seen, base) != seen.end())
                continue;
            seen.push_back(base);
            std::string line = std::format("    r{:<2} @ 0x{:08X}:", reg, base);
            for (std::uint32_t off = 0; off < 0x2C; off += 4) {
                try {
                    line += std::format(" +{:02X}={:08X}", off, mut.m_memory.read<std::uint32_t>(base + off));
                } catch (...) {
                    line += std::format(" +{:02X}=??", off);
                    break;
                }
            }
            std::cout << line << std::endl;
        }

        // Walk the EABI back-chain: [sp] = caller sp, [caller sp + 4] = saved LR (offset-space).
        std::cout << "  backtrace (stack back-chain):" << std::endl;
        dumpBacktrace(cpu, "    ");

        std::cout << "  threads:" << std::endl;
        dumpThreads(cpu, "    ");
        std::cout << "[DIAG] ==== end crash context ====\n" << std::endl;
    }

    void dumpThreads(const Interpreter &cpu, const char *indent)
    {
        static constexpr const char *stateNames[] = {"Ready", "Running", "Sleeping", "Waiting", "Finished", "Paused"};
        for (const auto &t: cpu.m_scheduler.threads()) {
            std::string line = std::format("{}0x{:08X} \"{}\" prio={} affinity={:X} {}", indent, t->osThreadPtr, t->name, t->priority, t->affinity,
                                           stateNames[static_cast<int>(t->state)]);
            if (t->state == ThreadContext::State::Waiting)
                line += t->joinTarget ? std::format(" join=0x{:08X}", t->joinTarget) : std::format(" key=0x{:08X}", t->waitKey);
            if (t.get() != cpu.m_scheduler.current())
                line += std::format(" pc={} lr={}", symbolize(cpu, t->pc + Memory::MemoryMap::ApplicationCode),
                                    symbolize(cpu, t->lr + Memory::MemoryMap::ApplicationCode));
            else
                line += std::format(" pc={} lr={}", symbolize(cpu, cpu.m_pc + Memory::MemoryMap::ApplicationCode),
                                    symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode));
            std::cout << line << std::endl;
        }
        if (const auto *regs = std::getenv("WEMU_THREAD_REGS"); regs && regs[0] == '1') {
            for (int i = 0; i < 32; i += 4)
                std::cout << std::format("{}r{}={:08X} r{}={:08X} r{}={:08X} r{}={:08X}\n", indent,
                                         i, cpu.m_gpr[i], i + 1, cpu.m_gpr[i + 1],
                                         i + 2, cpu.m_gpr[i + 2], i + 3, cpu.m_gpr[i + 3]);
        }
        if (const auto *stack = std::getenv("WEMU_THREAD_STACK"); stack && stack[0] == '1')
            dumpBacktrace(cpu, indent);
    }

} // namespace Core::Diag
