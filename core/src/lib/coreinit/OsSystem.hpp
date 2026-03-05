/*
** EPITECH PROJECT, 2025
** core
** File description:
** coreinit — system calls HLE (timing, mutexes, logging)
*/

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"

// ─── Timing ───────────────────────────────────────────────────────────────────

// OSTick OSGetTick(void)
// Returns an incrementing 32-bit tick counter (~62,500 ticks per frame at 60 Hz).
inline std::uint32_t OSGetTick(Core::Interpreter & /*cpu*/)
{
    static std::uint32_t s_tick = 0u;
    s_tick += 62500u;
    return s_tick;
}

// void OSSleepTicks(int64_t ticks)
// r3 = high 32 bits, r4 = low 32 bits (PPC32 ABI for int64_t).
// No-op: single-threaded emulation, sleep is meaningless.
inline void OSSleepTicks(Core::Interpreter & /*cpu*/,
                          std::uint32_t /*ticksHigh*/,
                          std::uint32_t /*ticksLow*/) {}

// ─── Mutex ────────────────────────────────────────────────────────────────────
// Single-threaded emulation: lock/unlock are no-ops.
// OSInitMutex must zero the 28-byte OSMutex struct so the program sees a clean state.

// void OSInitMutex(OSMutex *mutex)
inline void OSInitMutex(Core::Interpreter &cpu, std::uint32_t mutexAddr)
{
    const std::size_t host = cpu.m_binary.m_memory.translate(mutexAddr);
    if (host)
        std::memset(reinterpret_cast<void *>(host), 0, 28u); // sizeof(OSMutex) = 28
}

// void OSLockMutex(OSMutex *mutex)
inline void OSLockMutex(Core::Interpreter & /*cpu*/, std::uint32_t /*mutex*/) {}

// void OSUnlockMutex(OSMutex *mutex)
inline void OSUnlockMutex(Core::Interpreter & /*cpu*/, std::uint32_t /*mutex*/) {}

// ─── Debug output ─────────────────────────────────────────────────────────────

// void OSReport(const char *fmt, ...)
// Variadic — cannot use REGISTER_SYSCALL. Custom handler reads r3 as format string
// and passes r4..r10 as the remaining arguments (all 32-bit on PPC32).
inline void OSReport_handler(Core::Interpreter &cpu)
{
    const std::size_t fmtHost = cpu.m_binary.m_memory.translate(cpu.m_gpr[3]);
    if (!fmtHost) return;
    const char *fmt = reinterpret_cast<const char *>(fmtHost);
    std::fprintf(stderr, "[OSReport] ");
    // Passing all GPR args as uint32_t covers the vast majority of format strings.
    std::fprintf(stderr, fmt,
                 cpu.m_gpr[4], cpu.m_gpr[5], cpu.m_gpr[6], cpu.m_gpr[7],
                 cpu.m_gpr[8], cpu.m_gpr[9], cpu.m_gpr[10]);
    std::fprintf(stderr, "\n");
}

// void OSFatal(const char *message)
// Logs the message and terminates the emulator.
inline void OSFatal(Core::Interpreter & /*cpu*/, const char *message)
{
    std::fprintf(stderr, "[OSFatal] %s\n", message ? message : "(null)");
    std::exit(1);
}

// ─── Registration ─────────────────────────────────────────────────────────────

inline void RegisterOsSystemFunctions()
{
    // OSReport is variadic — register its custom handler directly.
    Core::syscallHandler.registerSyscall("OSReport", &OSReport_handler);

    REGISTER_SYSCALL(OSGetTick);
    REGISTER_SYSCALL(OSSleepTicks);
    REGISTER_SYSCALL(OSInitMutex);
    REGISTER_SYSCALL(OSLockMutex);
    REGISTER_SYSCALL(OSUnlockMutex);
    REGISTER_SYSCALL(OSFatal);
}
