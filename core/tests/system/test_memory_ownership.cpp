#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

#include "binary/Loader.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "utils/BeDecoder.hpp"

static_assert(!std::is_copy_constructible_v<Core::Binary>);
static_assert(std::is_nothrow_move_constructible_v<Core::Binary>);
static_assert(std::is_same_v<decltype(std::declval<const Core::Loader &>().getBinary()), const Core::Binary &>);

TEST(LoaderTest, NobitsNeedsNoFileBytesAndDataImportsFollowItsMemoryExtent)
{
    // Minimal RPX: a BSS larger than the file, followed by a data import.
    constexpr char names[] = "\0.shstrtab\0.symtab\0.strtab\0.bss\0.dimport_test\0";
    constexpr unsigned headers = 52, count = 6, payload = headers + count * 40;
    std::vector<char> bytes(payload + sizeof(names) + 16 + 1 + 4, 0);
    const auto word = [&](unsigned off, unsigned value) {
        value = std::byteswap(value);
        std::memcpy(bytes.data() + off, &value, 4);
    };
    const auto half = [&](unsigned off, std::uint16_t value) {
        value = std::byteswap(value);
        std::memcpy(bytes.data() + off, &value, 2);
    };
    std::memcpy(bytes.data(),
                "\x7f"
                "ELF\x01\x02\x01",
                7);
    half(16, 0xFE01);
    half(18, EM_PPC);
    word(20, 1);
    word(32, headers);
    half(40, 52);
    half(46, 40);
    half(48, count);
    half(50, 1);
    std::memcpy(bytes.data() + payload, names, sizeof(names));
    const auto section = [&](unsigned index, unsigned name, unsigned type, unsigned flags, unsigned addr, unsigned offset, unsigned size,
                             unsigned entSize) {
        const auto off = headers + index * 40;
        word(off, name);
        word(off + 4, type);
        word(off + 8, flags);
        word(off + 12, addr);
        word(off + 16, offset);
        word(off + 20, size);
        word(off + 32, 4);
        word(off + 36, entSize);
    };
    section(1, 1, SHT_STRTAB, 0, 0, payload, sizeof(names), 0);
    section(2, 11, SHT_SYMTAB, 0, 0, payload + sizeof(names), 16, 16);
    section(3, 19, SHT_STRTAB, 0, 0, payload + sizeof(names) + 16, 1, 0);
    section(4, 27, SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 0x10000000, 0, 0x4000, 0);
    section(5, 32, SHT_RPL_IMPORTS, SHF_ALLOC, 0xC0000000, bytes.size() - 4, 4, 0);
    char path[] = "/tmp/wemu-loader-test-XXXXXX";
    const auto fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    close(fd);
    struct Cleanup {
            const char *path;
            ~Cleanup() { std::filesystem::remove(path); }
    } cleanup{path};
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
    Core::Loader loader(path);
    const auto &binary = loader.getBinary();
    EXPECT_TRUE(binary.sections[4].raw.data.empty());
    EXPECT_EQ(binary.sections[4].meta.size, 0x4000u);
    EXPECT_EQ(binary.m_memory.read<unsigned>(0x10003FFC), 0u);
    EXPECT_EQ(binary.sections[5].meta.virtAddress, 0x10004000u);
}

TEST(LoaderTest, CafeRelativeHalfwordRelocationsUseGreenHillsNumbering)
{
    constexpr char names[] = "\0.shstrtab\0.symtab\0.strtab\0.bss\0.rela.data\0";
    constexpr unsigned headers = 52, count = 6, payload = headers + count * 40;
    constexpr unsigned symbols = payload + sizeof(names), strings = symbols + 16, relas = strings + 1;
    std::vector<char> bytes(relas + 3 * 12, 0);
    const auto word = [&](unsigned off, unsigned value) {
        value = std::byteswap(value);
        std::memcpy(bytes.data() + off, &value, 4);
    };
    const auto half = [&](unsigned off, std::uint16_t value) {
        value = std::byteswap(value);
        std::memcpy(bytes.data() + off, &value, 2);
    };
    std::memcpy(bytes.data(),
                "\x7f"
                "ELF\x01\x02\x01",
                7);
    half(16, 0xFE01);
    half(18, EM_PPC);
    word(20, 1);
    word(32, headers);
    half(40, 52);
    half(46, 40);
    half(48, count);
    half(50, 1);
    std::memcpy(bytes.data() + payload, names, sizeof(names));
    const auto section = [&](unsigned index, unsigned name, unsigned type, unsigned address, unsigned offset, unsigned size, unsigned entry) {
        const auto off = headers + index * 40;
        word(off, name);
        word(off + 4, type);
        word(off + 12, address);
        word(off + 16, offset);
        word(off + 20, size);
        word(off + 32, 4);
        word(off + 36, entry);
    };
    section(1, 1, SHT_STRTAB, 0, payload, sizeof(names), 0);
    section(2, 11, SHT_SYMTAB, 0, symbols, 16, 16);
    section(3, 19, SHT_STRTAB, 0, strings, 1, 0);
    section(4, 27, SHT_NOBITS, 0x10000000, 0, 16, 0);
    word(headers + 4 * 40 + 8, SHF_ALLOC | SHF_WRITE);
    section(5, 32, SHT_RELA, 0, relas, 36, 12);
    for (unsigned i = 0; i < 3; ++i) {
        const auto target = 0x10000000 + i * 2;
        word(relas + i * 12, target);
        word(relas + i * 12 + 4, 251 + i);
        word(relas + i * 12 + 8, target + 0x18004);
    }
    char path[] = "/tmp/wemu-relocation-test-XXXXXX";
    const auto fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    close(fd);
    struct Cleanup {
            const char *path;
            ~Cleanup() { std::filesystem::remove(path); }
    } cleanup{path};
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
    Core::Loader loader(path);
    const auto &memory = loader.getBinary().m_memory;
    EXPECT_EQ(memory.read<std::uint16_t>(0x10000000), 2u);
    EXPECT_EQ(memory.read<std::uint16_t>(0x10000002), 1u);
    EXPECT_EQ(memory.read<std::uint16_t>(0x10000004), 0x8004u);
}

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
