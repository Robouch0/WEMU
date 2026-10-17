#include <cstdint>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

#include "binary/Loader.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "utils/BeDecoder.hpp"

static_assert(!std::is_copy_constructible_v<Core::Binary>);
static_assert(std::is_nothrow_move_constructible_v<Core::Binary>);
static_assert(std::is_same_v<decltype(std::declval<const Core::Loader &>().getBinary()), const Core::Binary &>);

TEST(MemoryOwnershipTest, BinaryMovesIntoInterpreterWithoutCopyingRamOrLosingMetadata)
{
    constexpr auto app = Core::Memory::ApplicationCode;
    Core::Binary binary{.m_memory = Core::Memory(32)};
    binary.header.e_entry = app;
    binary.sdaBase = 0x10002000;
    binary.sda2Base = 0x10003000;
    binary.symbols.push_back(Core::Symbol{.name = "memory_ownership_test"});
    binary.symbols.back().raw.header.st_value = 0xC0000010;
    binary.m_memory.write<std::uint32_t>(app, 0x38600007); // addi r3, r0, 7
    auto *ram = binary.m_memory.hostPtr(app);
    auto *stack = binary.m_memory.hostPtr(Core::Memory::STACK_BASE);
    auto *imports = binary.m_memory.hostPtr(Core::Memory::DIMPORT_BASE);
    Core::Interpreter cpu(std::move(binary));
    EXPECT_EQ(cpu.m_memory.hostPtr(app), ram);
    EXPECT_EQ(cpu.m_memory.hostPtr(Core::Memory::STACK_BASE), stack);
    EXPECT_EQ(cpu.m_memory.hostPtr(Core::Memory::DIMPORT_BASE), imports);
    EXPECT_EQ(&cpu.m_memory, &cpu.m_binary.m_memory);
    EXPECT_EQ(cpu.m_binary.header.e_entry, app);
    EXPECT_EQ(cpu.m_binary.sdaBase, 0x10002000u);
    EXPECT_EQ(cpu.m_binary.sda2Base, 0x10003000u);
    ASSERT_EQ(cpu.m_binary.symbols.size(), 1u);
    EXPECT_EQ(cpu.m_binary.symbols.front().name, "memory_ownership_test");
    Utils::BeDecoder instructions(std::span<const char>(cpu.m_memory.getMemory()));
    cpu.m_pc = 0;
    ASSERT_TRUE(cpu.step(instructions, app));
    EXPECT_EQ(cpu.m_gpr[3], 7u);
    // Replacing an instruction through either public view must affect the next fetch.
    cpu.m_binary.m_memory.write<std::uint32_t>(app, 0x38600009);
    cpu.m_pc = 0;
    ASSERT_TRUE(cpu.step(instructions, app));
    EXPECT_EQ(cpu.m_gpr[3], 9u);
    cpu.reset();
    EXPECT_EQ(cpu.m_memory.hostPtr(app), ram);
}
