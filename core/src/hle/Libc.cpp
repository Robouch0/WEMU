/*
** EPITECH PROJECT, 2025
** core
** File description:
** HLE libc stubs (malloc, free, memalign, rand, exit, ...)
*/

#include "Libc.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"

static void hle_memalign(Core::Interpreter &cpu)
{
    const std::uint32_t align = cpu.m_gpr[3];
    const std::uint32_t size = cpu.m_gpr[4];

    cpu.m_gpr[3] = cpu.m_memory.heapAllocate(size, align ? align : 8);
}

static void hle_malloc(Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[3], 8); }

static void hle_calloc(Core::Interpreter &cpu)
{
    const std::uint32_t n = cpu.m_gpr[3];
    const std::uint32_t size = cpu.m_gpr[4];
    const std::uint32_t addr = cpu.m_memory.heapAllocate(n * size, 8);
    if (addr) {
        std::uint8_t *p = cpu.m_memory.hostPtr(addr);
        if (p)
            std::memset(p, 0, n * size);
    }
    cpu.m_gpr[3] = addr;
}

static void hle_realloc(Core::Interpreter &cpu)
{
    // bump allocator: just allocate new block (old one leaked, acceptable for pong)
    cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 8);
}

static void hle_free(Core::Interpreter &cpu) { (void) cpu; }

static void hle_memcpy(Core::Interpreter &cpu)
{
    const std::uint32_t dst = cpu.m_gpr[3];
    const std::uint32_t src = cpu.m_gpr[4];
    std::uint32_t n = cpu.m_gpr[5];

    if (dst && src && n) {
        std::uint8_t *d = cpu.m_memory.hostPtr(dst);
        const std::uint8_t *s = cpu.m_memory.hostPtr(src);
        if (d && s)
            std::memcpy(d, s, n);
    }
    // r3 stays = dst
}

static void hle_memset(Core::Interpreter &cpu)
{
    const std::uint32_t dst = cpu.m_gpr[3];
    const std::uint32_t val = cpu.m_gpr[4];
    std::uint32_t n = cpu.m_gpr[5];

    if (dst && n) {
        std::uint8_t *d = cpu.m_memory.hostPtr(dst);
        if (d)
            std::memset(d, (int) val, n);
    }
    // r3 stays = dst
}

static void hle_rand(Core::Interpreter &cpu) { cpu.m_gpr[3] = static_cast<std::uint32_t>(std::rand()); }
static void hle_srand(Core::Interpreter &cpu)
{
    std::srand(cpu.m_gpr[3]);
    cpu.m_gpr[3] = 0;
}

static void hle_exit(Core::Interpreter &cpu)
{
    fprintf(stderr, "[HLE] exit(%d) called\n", static_cast<int>(cpu.m_gpr[3]));
    cpu.m_running = false;
    cpu.m_gpr[3] = 0;
}

static void hle_abort(Core::Interpreter &cpu)
{
    fprintf(stderr, "[HLE] abort() called\n");
    cpu.m_running = false;
}

void RegisterLibcFunctions()
{
    Core::syscallHandler.registerSyscall("memalign", hle_memalign);
    Core::syscallHandler.registerSyscall("malloc", hle_malloc);
    Core::syscallHandler.registerSyscall("calloc", hle_calloc);
    Core::syscallHandler.registerSyscall("realloc", hle_realloc);
    Core::syscallHandler.registerSyscall("free", hle_free);
    Core::syscallHandler.registerSyscall("memcpy", hle_memcpy);
    Core::syscallHandler.registerSyscall("memset", hle_memset);
    Core::syscallHandler.registerSyscall("rand", hle_rand);
    Core::syscallHandler.registerSyscall("srand", hle_srand);
    Core::syscallHandler.registerSyscall("exit", hle_exit);
    Core::syscallHandler.registerSyscall("_Exit", hle_exit);
    Core::syscallHandler.registerSyscall("abort", hle_abort);
}
