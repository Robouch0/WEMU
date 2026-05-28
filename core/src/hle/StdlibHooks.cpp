#include "StdlibHooks.hpp"

#include "binary/Binary.hpp"
#include "cpu/interpreter/Interpreter.hpp"

#include <cstring>

namespace Core {

void installStdlibHooks(Interpreter &interp, const Binary &binary)
{
    struct {
        const char *name;
        Interpreter::HookFn fn;
    } stdlib_hooks[] = {
        {"memalign",     [](Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], cpu.m_gpr[3] ? cpu.m_gpr[3] : 8); }},
        {"malloc",       [](Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[3], 8); }},
        {"calloc",       [](Interpreter &cpu) {
            uint32_t n = cpu.m_gpr[3] * cpu.m_gpr[4];
            uint32_t a = cpu.m_memory.heapAllocate(n, 8);
            if (a)
                if (auto *p = cpu.m_memory.hostPtr(a))
                    std::memset(p, 0, n);
            cpu.m_gpr[3] = a;
        }},
        {"realloc",      [](Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 8); }},
        {"free",         [](Interpreter &) {}},
        {"__wut_sbrk_r", [](Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 4); }},
        {"_sbrk_r",      [](Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 4); }},
    };

    for (auto &h : stdlib_hooks)
        for (const auto &sym : binary.symbols)
            if (sym.name == h.name &&
                sym.raw.header.st_value >= 0x02000000u &&
                sym.raw.header.st_value <  0x10000000u)
                interp.m_hooks[sym.raw.header.st_value] = h.fn;
}

} // namespace Core
