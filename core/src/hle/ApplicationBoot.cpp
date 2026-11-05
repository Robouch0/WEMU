#include "ApplicationBoot.hpp"

#include <algorithm>

#include "MemHeap.hpp"
#include "cpu/interpreter/Interpreter.hpp"

void Core::Hle::ApplicationBoot::begin(Interpreter &cpu)
{
    const auto preinit = cpu.m_binary.functionExport("__preinit_user");
    if (!preinit)
        return;
    std::copy(std::begin(cpu.m_gpr), std::end(cpu.m_gpr), registers.begin());
    entry = cpu.m_pc;
    lr = cpu.m_lr;
    // Reserve an ABI stack frame with three writable heap-handle references.
    const auto frame = cpu.m_gpr[1] - 0x30;
    if (!cpu.m_memory.hostPtr(frame, 0x30))
        throw Core::MemoryException("Invalid stack for __preinit_user");
    cpu.m_memory.write<std::uint32_t>(frame, cpu.m_gpr[1]);
    heapRefs = frame + 8;
    const auto heaps = applicationHeapHandles(cpu);
    for (unsigned i = 0; i < heaps.size(); ++i) {
        cpu.m_memory.write<std::uint32_t>(heapRefs + i * 4, heaps[i]);
        cpu.m_gpr[3 + i] = heapRefs + i * 4;
    }
    cpu.m_gpr[1] = frame;
    cpu.m_pc = cpu.m_nextPc = preinit - Core::Memory::ApplicationCode;
    cpu.m_lr = preinitReturn - Core::Memory::ApplicationCode;
    active = true;
    std::cout << std::format("[BOOT] __preinit_user @ 0x{:08X}", preinit) << std::endl;
}

void Core::Hle::ApplicationBoot::finish(Interpreter &cpu)
{
    std::array<std::uint32_t, 3> heaps{};
    for (unsigned i = 0; i < heaps.size(); ++i)
        heaps[i] = cpu.m_memory.read<std::uint32_t>(heapRefs + i * 4);
    setApplicationHeapHandles(heaps);
    std::copy(registers.begin(), registers.end(), std::begin(cpu.m_gpr));
    cpu.m_lr = lr;
    cpu.m_pc = cpu.m_nextPc = entry;
    active = false;
    std::cout << "[BOOT] __preinit_user complete; starting application" << std::endl;
}
