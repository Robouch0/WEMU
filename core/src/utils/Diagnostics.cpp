/*
 ** EPITECH PROJECT, 2026
 ** core
 ** File description:
 ** Diagnostics -- boot-triage tooling: unknown-import tracking, HLE call trace, crash backtrace
 */

#include "Diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <string_view>
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

    // Sorted (address, name) pairs for named symbols placed in guest memory. Names are copied so
    // the index never points into a binary that has since been replaced.
    std::vector<std::pair<std::uint32_t, std::string>> buildSymbolIndex(const Core::Binary &bin)
    {
        std::vector<std::pair<std::uint32_t, std::string>> index;
        index.reserve(bin.symbols.size());
        for (const auto &symbol: bin.symbols) {
            if (symbol.name.empty() || symbol.meta.virtAddress == 0)
                continue;
            index.emplace_back(static_cast<std::uint32_t>(symbol.meta.virtAddress), symbol.name);
        }
        std::ranges::sort(index, {}, [](const auto &e) { return e.first; });
        return index;
    }

    // The index of the binary being run, rebuilt when its symbol table changes.
    const std::vector<std::pair<std::uint32_t, std::string>> &symbolIndex(const Core::Binary &bin)
    {
        static std::vector<std::pair<std::uint32_t, std::string>> index;
        static std::pair<std::size_t, std::uint64_t> builtFor{~std::size_t{0}, 0};
        // A replaced table can reuse the old one's storage, so compare its contents, not its address.
        std::uint64_t fingerprint = 0xCBF29CE484222325ull;
        for (const auto &symbol: bin.symbols)
            fingerprint = (fingerprint ^ (std::hash<std::string>{}(symbol.name) + symbol.meta.virtAddress)) * 0x100000001B3ull;
        const auto current = std::pair{bin.symbols.size(), fingerprint};
        if (current != builtFor) {
            index = buildSymbolIndex(bin);
            builtFor = current;
        }
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
                out.push_back(WatchCstrSpec{pc, 0, false, true, addr});
                p = (*end == ',') ? end + 1 : end;
                continue;
            }
            if (*p != 'r' && *p != 'R')
                break;
            p++;
            const auto gpr = static_cast<unsigned long>(std::strtoul(p, &end, 10));
            if (end == p || gpr > 31)
                break;
            out.push_back(WatchCstrSpec{pc, static_cast<std::uint8_t>(gpr), deref, false, 0});
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
            out.push_back(WatchRegSpec{pc, static_cast<std::uint8_t>(gpr), value});
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
                out.push_back(WatchDerefSpec{static_cast<std::uint8_t>(reg), off});
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

    bool hasWatchCstrs() { return !watchCstrs().empty(); }

    bool hasWatchRegs() { return !watchRegs().empty(); }

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

#ifndef _WIN32
    // A host crash otherwise ends the log mid-line. Writes one line in the logger's format, then
    // re-raises: SA_RESETHAND has restored the default action, so the process dies with the same
    // signal (exit status and core dumps are unchanged).
    extern "C" void crashSignalHandler(const int sig)
    {
        const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : "SIGABRT";
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now); // steady_clock's clock, as in Utils::Log
        const auto start = std::chrono::duration_cast<std::chrono::nanoseconds>(Utils::Log::detail::kStart.time_since_epoch()).count();
        const auto elapsedMs = static_cast<std::uint64_t>((now.tv_sec * 1'000'000'000LL + now.tv_nsec - start) / 1'000'000);
        char line[160];
        char *p = line;
        *p++ = '[';
        p = sigPutDec(p, elapsedMs / 1000);
        *p++ = '.';
        *p++ = static_cast<char>('0' + elapsedMs / 100 % 10);
        *p++ = static_cast<char>('0' + elapsedMs / 10 % 10);
        *p++ = static_cast<char>('0' + elapsedMs % 10);
        static const char middle[] = "] [ERROR] [EMU] host crash: signal ";
        for (const char c: std::string_view(middle, sizeof(middle) - 1))
            *p++ = c;
        p = sigPutDec(p, static_cast<std::uint64_t>(sig));
        *p++ = ' ';
        *p++ = '(';
        for (; *name; ++name)
            *p++ = *name;
        static const char tail[] = ") in the WEMU core; the lines above show the last guest activity\n";
        for (const char c: std::string_view(tail, sizeof(tail) - 1))
            *p++ = c;
        for (std::size_t done = 0, length = static_cast<std::size_t>(p - line); done < length;) {
            const auto written = ::write(STDERR_FILENO, line + done, length - done);
            if (written <= 0)
                break;
            done += static_cast<std::size_t>(written);
        }
        raise(sig);
    }
#endif

    void installCrashSignalReport()
    {
#ifndef _WIN32
        struct sigaction action = {};
        action.sa_handler = &crashSignalHandler;
        action.sa_flags = SA_RESETHAND;
        sigemptyset(&action.sa_mask);
        for (const int sig: {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
            sigaction(sig, &action, nullptr);
#endif
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
            const auto key = std::tuple{spec.pc, spec.gpr, spec.deref, spec.literalAddr};
            const auto count = ++hits[key];
            if (count > maxHits)
                continue;
            std::uint32_t addr = spec.literal ? spec.literalAddr : cpu.m_gpr[spec.gpr];
            std::string source = spec.literal ? std::format("@0x{:08X}", spec.literalAddr) : std::format("r{}", spec.gpr);
            if (spec.deref && !spec.literal) {
                try {
                    addr = mut.m_memory.read<std::uint32_t>(addr);
                    source.insert(0, "*");
                } catch (...) {
                    source.insert(0, "*");
                    source += "<unreadable>";
                    addr = 0;
                }
            }
            std::cout << std::format("[DIAG] watch cstr PC=0x{:08X} {} -> 0x{:08X} hit #{} \"{}\" thread=0x{:08X} LR=", ppcPc, source, addr, count,
                                     readGuestCString(mut, addr), cpu.m_scheduler.currentHandle())
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
            const auto key = std::tuple{spec.pc, spec.gpr, spec.value};
            const auto count = ++hits[key];
            if (count > maxHits)
                continue;
            std::cout << std::format("[DIAG] watch reg PC=0x{:08X} r{}==0x{:08X} hit #{} thread=0x{:08X} LR=", ppcPc, spec.gpr, spec.value, count,
                                     cpu.m_scheduler.currentHandle())
                      << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
            for (int i = 0; i < 32; i += 8)
                std::cout << std::format("       r{:<2}: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", i, cpu.m_gpr[i], cpu.m_gpr[i + 1],
                                         cpu.m_gpr[i + 2], cpu.m_gpr[i + 3], cpu.m_gpr[i + 4], cpu.m_gpr[i + 5], cpu.m_gpr[i + 6], cpu.m_gpr[i + 7])
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
                              << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << " at PC=" << symbolize(cpu, ppcPc) << std::endl;
                    dumpBacktrace(cpu, "       ");
                }
            } else if (it->second != v) {
                std::cout << std::format("[DIAG] watch word 0x{:08X}: 0x{:08X} -> 0x{:08X} thread=0x{:08X} LR=", addr, it->second, v,
                                         cpu.m_scheduler.currentHandle())
                          << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << " at PC=" << symbolize(cpu, ppcPc) << std::endl;
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
        const auto interval = heartbeatInterval();
        if (!interval || ++count % interval != 0)
            return;
        std::cout << std::format("[DIAG] heartbeat {}M instr, PC=", count / 1000000) << symbolize(cpu, ppcPc)
                  << " LR=" << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
        static constexpr const char *stateNames[] = {"Ready", "Running", "Sleeping", "Waiting", "Finished", "Paused"};
        for (const auto &t: cpu.m_scheduler.threads()) {
            if (t->state == ThreadContext::State::Finished)
                continue;
            std::string line = std::format("       thread 0x{:08X} \"{}\" prio={} aff={} {}", t->osThreadPtr, t->name, t->priority, t->affinity,
                                           stateNames[static_cast<int>(t->state)]);
            if (t->state == ThreadContext::State::Waiting)
                line += t->joinTarget ? std::format(" join=0x{:08X}", t->joinTarget) : std::format(" key=0x{:08X}", t->waitKey);
            if (t.get() != cpu.m_scheduler.current())
                line += std::format(" pc=0x{:08X} lr=0x{:08X}", t->pc + Memory::MemoryMap::ApplicationCode,
                                    t->lr + Memory::MemoryMap::ApplicationCode);
            std::cout << line << std::endl;
        }
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
            // Instruction fetch and data accesses share this live guest-memory buffer.
            cpu.m_memory.write<std::uint32_t>(addr, value);
            std::cout << std::format("[DIAG] patched guest word 0x{:08X} = 0x{:08X}", addr, value) << std::endl;
            p = (*end == ',') ? end + 1 : end;
        }
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
        std::cout << std::format("[DIAG] watch PC 0x{:08X} hit #{} (LR=", ppcPc, n) << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode)
                  << ")" << std::endl;
        for (int i = 0; i < 32; i += 8)
            std::cout << std::format("       r{:<2}: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", i, cpu.m_gpr[i], cpu.m_gpr[i + 1],
                                     cpu.m_gpr[i + 2], cpu.m_gpr[i + 3], cpu.m_gpr[i + 4], cpu.m_gpr[i + 5], cpu.m_gpr[i + 6], cpu.m_gpr[i + 7])
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
        const auto &index = symbolIndex(cpu.m_binary);

        // Nearest symbol at or before the address, if reasonably close (real RPX symtabs mostly
        // carry import/export names, so distant matches would be misleading).
        constexpr std::uint32_t MAX_SYMBOL_DISTANCE = 0x20000;
        const auto it = std::ranges::upper_bound(index, address, {}, [](const auto &e) { return e.first; });
        if (it != index.begin()) {
            const auto &[symAddr, name] = *std::prev(it);
            if (address - symAddr < MAX_SYMBOL_DISTANCE)
                return std::format("0x{:08X} <{}+0x{:X}>", address, name, address - symAddr);
        }
        return std::format("0x{:08X}", address);
    }

    void dumpCrashContext(const Interpreter &cpu, const std::uint32_t ppcPc, const std::string &reason)
    {
        std::cout << "\n[DIAG] ==== crash context: " << reason << " ====" << std::endl;
        std::cout << "  PC  = " << symbolize(cpu, ppcPc) << std::endl;
        std::cout << "  LR  = " << symbolize(cpu, cpu.m_lr + Memory::MemoryMap::ApplicationCode) << std::endl;
        std::cout << std::format("  CTR = 0x{:08X}  XER.SO={}  CR=0x{:08X}", cpu.m_ctr, static_cast<int>(cpu.m_xer.so), cpu.m_cr.raw) << std::endl;
        for (int i = 0; i < 32; i += 4)
            std::cout << std::format("  r{:<2}=0x{:08X} r{:<2}=0x{:08X} r{:<2}=0x{:08X} r{:<2}=0x{:08X}", i, cpu.m_gpr[i], i + 1, cpu.m_gpr[i + 1],
                                     i + 2, cpu.m_gpr[i + 2], i + 3, cpu.m_gpr[i + 3])
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
                std::cout << std::format("{}r{}={:08X} r{}={:08X} r{}={:08X} r{}={:08X}\n", indent, i, cpu.m_gpr[i], i + 1, cpu.m_gpr[i + 1], i + 2,
                                         cpu.m_gpr[i + 2], i + 3, cpu.m_gpr[i + 3]);
        }
        if (const auto *stack = std::getenv("WEMU_THREAD_STACK"); stack && stack[0] == '1')
            dumpBacktrace(cpu, indent);
    }

} // namespace Core::Diag
