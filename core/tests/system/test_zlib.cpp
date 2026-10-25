#include <array>
#include <zlib.h>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Coreinit.hpp"

class ZlibTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterCoreinitFunctions();
        }
};

TEST_F(ZlibTest, DefaultInflateInitializesAndDecompressesGuestStream)
{
    constexpr unsigned stream = 0x28001000, input = 0x28002000, output = 0x28003000;
    constexpr char text[] = "WEMU guest zlib decompression regression";
    std::array<unsigned char, 256> compressed{};
    uLongf size = compressed.size();
    ASSERT_EQ(compress(compressed.data(), &size, reinterpret_cast<const Bytef *>(text), sizeof(text)), Z_OK);
    std::memset(cpu->m_memory.hostPtr(stream, 0x38), 0, 0x38);
    std::memcpy(cpu->m_memory.hostPtr(input, size), compressed.data(), size);
    cpu->m_gpr[3] = stream;
    cpu->m_gpr[4] = input;
    cpu->m_gpr[5] = 0x38;
    Core::syscallHandler.get("inflateInit_")(*cpu);
    ASSERT_EQ(cpu->m_gpr[3], Z_OK);
    ASSERT_NE(cpu->m_memory.read<unsigned>(stream + 0x1C), 0u);
    cpu->m_memory.write<unsigned>(stream, input);
    cpu->m_memory.write<unsigned>(stream + 4, size);
    cpu->m_memory.write<unsigned>(stream + 12, output);
    cpu->m_memory.write<unsigned>(stream + 16, sizeof(text));
    cpu->m_gpr[3] = stream;
    cpu->m_gpr[4] = Z_FINISH;
    Core::syscallHandler.get("inflate")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], Z_STREAM_END);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(stream + 8), size);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(stream + 20), sizeof(text));
    EXPECT_EQ(std::memcmp(cpu->m_memory.hostPtr(output, sizeof(text)), text, sizeof(text)), 0);
    cpu->m_gpr[3] = stream;
    Core::syscallHandler.get("inflateEnd")(*cpu);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(stream + 0x1C), 0u);
}

TEST_F(ZlibTest, DmaCopyHandlesOverlapEndianModesAndCompletionTimestamp)
{
    constexpr unsigned buffer = 0x28001000;
    std::uint64_t previous = 0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (unsigned i = 0; i < 16; ++i)
            cpu->m_memory.write<std::uint8_t>(buffer + i, i);
        cpu->m_gpr[3] = buffer + 4;
        cpu->m_gpr[4] = buffer;
        cpu->m_gpr[5] = 2; // two words, not two bytes
        cpu->m_gpr[6] = mode;
        Core::syscallHandler.get("DMAECopyMem")(*cpu);
        const auto timestamp = (std::uint64_t{cpu->m_gpr[3]} << 32) | cpu->m_gpr[4];
        EXPECT_GT(timestamp, previous);
        previous = timestamp;
        const unsigned group = mode ? 1u << mode : 1;
        for (unsigned i = 0; i < 8; ++i)
            EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(buffer + 4 + i), (i / group) * group + (group - 1 - i % group));
        Core::syscallHandler.get("DMAEWaitDone")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], 1u);
    }
}
