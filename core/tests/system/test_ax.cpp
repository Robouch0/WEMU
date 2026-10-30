#include <array>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Ax.hpp"

TEST_F(InstructionTest, AxFinalMixCallbacksReceivePlanarBuffersAtAudioDeadline)
{
    RegisterAxFunctions();
    cpu->m_scheduler = Core::Scheduler{};
    cpu->m_scheduler.bootstrap(*cpu);
    const auto output = cpu->m_memory.heapAllocate(8, 4);
    constexpr auto app = Core::Memory::MemoryMap::ApplicationCode;
    const auto call = [&](const char *name, std::uint32_t a, std::uint32_t b) {
        cpu->m_gpr[3] = a;
        cpu->m_gpr[4] = b;
        Core::syscallHandler.get(name)(*cpu);
    };
    call("AXInit", 0, 0);
    for (unsigned device = 0; device < 2; ++device) {
        call("AXRegisterDeviceFinalMixCallback", device, app + 0x100 + device * 4);
        EXPECT_EQ(cpu->m_gpr[3], 0u);
        call("AXGetDeviceFinalMixCallback", device, output);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output), app + 0x100 + device * 4);
    }
    call("AXRegisterDeviceFinalMixCallback", 2, app + 0x200);
    EXPECT_EQ(cpu->m_gpr[3], 0xFFFFFFFFu);
    call("AXGetDeviceFinalMixCallback", 2, output);
    EXPECT_EQ(cpu->m_gpr[3], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output), app + 0x104);
    cpu->m_scheduler.advanceTicks(186468);
    Core::Ax::OnFrameTick(*cpu);
    EXPECT_EQ(cpu->m_scheduler.threads().size(), 1u);
    cpu->m_scheduler.advanceTicks(1);
    Core::Ax::OnFrameTick(*cpu);
    ASSERT_TRUE(cpu->m_scheduler.preempt(*cpu));
    const auto check = [&](unsigned channels, unsigned devices) {
        const auto parameter = cpu->m_gpr[3];
        EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(parameter + 4), channels);
        EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(parameter + 6), 144);
        EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(parameter + 8), devices);
        EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(parameter + 10), channels);
        const auto pointers = cpu->m_memory.read<std::uint32_t>(parameter);
        for (unsigned c = 0; c < channels * devices; ++c) {
            const auto data = cpu->m_memory.read<std::uint32_t>(pointers + c * 4);
            EXPECT_EQ(data, pointers + channels * devices * 4 + c * 144 * 4);
            for (unsigned i = 0; i < 144; ++i)
                EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(data + i * 4), 0u);
        }
    };
    EXPECT_EQ(cpu->m_pc, 0x100u);
    check(6, 1);
    Core::Ax::OnSentinelReturn(*cpu);
    EXPECT_EQ(cpu->m_pc, 0x104u);
    check(4, 2);
    Core::Ax::OnSentinelReturn(*cpu);
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), cpu->m_scheduler.mainHandle());
    Core::Ax::OnFrameTick(*cpu);
    EXPECT_EQ(cpu->m_scheduler.threads().back()->state, Core::ThreadContext::State::Sleeping);
    EXPECT_EQ(cpu->m_scheduler.threads().back()->wakeTick, 372938u);
    for (unsigned device = 0; device < 2; ++device) {
        call("AXRegisterDeviceFinalMixCallback", device, 0);
        call("AXGetDeviceFinalMixCallback", device, output);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output), 0u);
    }
    call("AXQuit", 0, 0);
    RegisterAxFunctions();
    cpu->m_scheduler = Core::Scheduler{};
}

TEST_F(InstructionTest, AxLateServicePreservesFramesAndWakesBeforeApplicationSleeper)
{
    RegisterAxFunctions();
    cpu->m_scheduler = Core::Scheduler{};
    cpu->m_scheduler.bootstrap(*cpu);
    Core::syscallHandler.get("AXInit")(*cpu);
    cpu->m_gpr[3] = Core::Memory::MemoryMap::ApplicationCode + 0x100;
    Core::syscallHandler.get("AXRegisterFrameCallback")(*cpu);
    cpu->m_scheduler.advanceTicks(745875); // Four exact 3 ms periods.
    Core::Ax::OnFrameTick(*cpu);
    ASSERT_TRUE(cpu->m_scheduler.preempt(*cpu));
    for (unsigned frame = 0; frame < 4; ++frame) {
        EXPECT_EQ(cpu->m_pc, 0x100u);
        EXPECT_EQ(cpu->m_scheduler.now(), 745875u);
        Core::Ax::OnSentinelReturn(*cpu);
    }
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), cpu->m_scheduler.mainHandle());
    EXPECT_EQ(cpu->m_scheduler.threads().back()->state, Core::ThreadContext::State::Sleeping);
    EXPECT_EQ(cpu->m_scheduler.threads().back()->wakeTick, 932344u);
    cpu->m_scheduler.sleep(*cpu, 1000000);
    EXPECT_EQ(cpu->m_scheduler.now(), 932344u);
    EXPECT_NE(cpu->m_scheduler.currentHandle(), cpu->m_scheduler.mainHandle());
    Core::Ax::OnSentinelReturn(*cpu);
    EXPECT_EQ(cpu->m_pc, 0x100u);
    Core::syscallHandler.get("AXQuit")(*cpu);
    RegisterAxFunctions();
    cpu->m_scheduler = Core::Scheduler{};
}

TEST_F(InstructionTest, AxVoiceOffsetsRoundTripPreservesAdjacentMemory)
{
    RegisterAxFunctions();
    const auto voice = cpu->m_memory.heapAllocate(0x80, 4);
    const auto source = cpu->m_memory.heapAllocate(0x20, 4);
    const auto output = cpu->m_memory.heapAllocate(0x20, 4);
    const std::array<std::uint32_t, 5> offsets{0x000A0001, 32, 4095, 128, 0x20000000};
    cpu->m_memory.write<std::uint32_t>(voice + 0x30, 0x12345678);
    cpu->m_memory.write<std::uint32_t>(voice + 0x48, 0xABCDEF01);
    cpu->m_memory.write<std::uint32_t>(output, 0x12345678);
    cpu->m_memory.write<std::uint32_t>(output + 0x18, 0xABCDEF01);
    for (std::size_t i = 0; i < offsets.size(); ++i)
        cpu->m_memory.write<std::uint32_t>(source + i * 4, offsets[i]);
    cpu->m_gpr[3] = voice;
    cpu->m_gpr[4] = source;
    Core::syscallHandler.get("AXSetVoiceOffsets")(*cpu);
    for (std::size_t i = 0; i < offsets.size(); ++i)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(voice + 0x34 + i * 4), offsets[i]);
    cpu->m_gpr[3] = voice;
    cpu->m_gpr[4] = output + 4;
    Core::syscallHandler.get("AXGetVoiceOffsets")(*cpu);
    for (std::size_t i = 0; i < offsets.size(); ++i)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output + 4 + i * 4), offsets[i]);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(voice + 0x30), 0x12345678u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(voice + 0x48), 0xABCDEF01u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output), 0x12345678u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output + 0x18), 0xABCDEF01u);

    // Partial overlap must not feed newly written words back into the copy.
    cpu->m_gpr[3] = voice;
    cpu->m_gpr[4] = voice + 0x38;
    Core::syscallHandler.get("AXGetVoiceOffsets")(*cpu);
    for (std::size_t i = 0; i < offsets.size(); ++i)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(voice + 0x38 + i * 4), offsets[i]);
}
