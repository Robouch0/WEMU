/*
** EPITECH PROJECT, 2025
** core
** File description:
** Add
*/

#include <cstdlib>
#include <unordered_map>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "utils/Diagnostics.hpp"

namespace Core::Instruction {

    void SC(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        Core::Symbol sym = cpu.m_binary.symbols[instr.bd];

        cpu.m_hle_redirected = false;
        if (syscallHandler.syscallTable.contains(sym.name)) {
            const auto handler = syscallHandler.get(sym.name);
            // Deliberately not Log::info: the trace flag must work regardless of compile-time log level.
            static const std::uint32_t traceOnlyLr = []() -> std::uint32_t {
                const char *env = std::getenv("WEMU_TRACE_HLE_LR");
                return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 16)) : 0;
            }();
            static const char *traceName = std::getenv("WEMU_TRACE_HLE_NAME");
            static const std::uint64_t traceEvery = []() -> std::uint64_t {
                const char *env = std::getenv("WEMU_TRACE_HLE_EVERY");
                if (!env || *env < '0' || *env > '9')
                    return 1;
                char *end{};
                const auto value = std::strtoull(env, &end, 10);
                return value && !*end ? value : 1;
            }();
            static std::unordered_map<std::string, std::uint64_t> traceCounts;
            const std::uint32_t lr = cpu.m_lr + Memory::MemoryMap::ApplicationCode;
            const bool requested = Diag::traceHle() || (traceOnlyLr != 0 && traceOnlyLr == lr) ||
                                   (traceName && *traceName && sym.name.find(traceName) != std::string::npos);
            const auto traceCount = requested ? ++traceCounts[sym.name] : 0;
            if (requested && (traceCount <= 8 || traceCount % traceEvery == 0)) {
                const std::uint32_t a3 = cpu.m_gpr[3], a4 = cpu.m_gpr[4], a5 = cpu.m_gpr[5], a6 = cpu.m_gpr[6];
                const std::uint32_t a7 = cpu.m_gpr[7], a8 = cpu.m_gpr[8];
                handler(cpu);
                std::cout << std::format("[HLE] {}(r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} r6=0x{:08X} r7=0x{:08X} r8=0x{:08X}) -> 0x{:08X} LR=0x{:08X} "
                                         "count={} tick={}",
                                         sym.name, a3, a4, a5, a6, a7, a8, cpu.m_gpr[3], lr, traceCount, cpu.m_scheduler.now())
                          << std::endl;
            } else {
                handler(cpu);
            }
        } else {
            Diag::noteUnknownImport(sym.name, cpu);
            cpu.m_gpr[3] = 0;
        }
        // A thread-switching HLE (yield/sleep/exit) sets m_hle_redirected and takes control of the
        // next PC; otherwise this is a normal call that returns to the link register.
        if (!cpu.m_hle_redirected)
            cpu.m_nextPc = cpu.m_lr;
    }
} // namespace Core::Instruction
