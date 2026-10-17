#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/H264.hpp"
#include "video/H264Decoder.hpp"

namespace {
    std::vector<std::vector<std::uint8_t>> packets(const char *name = "reordered.h264")
    {
        std::ifstream file(std::filesystem::path(__FILE__).parent_path() / "data" / name, std::ios::binary);
        std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        std::vector<std::size_t> offsets;
        // The generated fixture explicitly emits a four-byte Annex-B AUD before each access unit.
        for (std::size_t i = 0; i + 4 < data.size(); ++i)
            if (!data[i] && !data[i + 1] && !data[i + 2] && data[i + 3] == 1 && (data[i + 4] & 31) == 9)
                offsets.push_back(i);
        offsets.push_back(data.size());
        std::vector<std::vector<std::uint8_t>> result;
        for (std::size_t i = 1; i < offsets.size(); ++i)
            result.emplace_back(data.begin() + offsets[i - 1], data.begin() + offsets[i]);
        return result;
    }
} // namespace

TEST(H264DecoderTest, DelayedFramesRetainTimestampAndGuestBuffer)
{
    const auto input = packets();
    ASSERT_EQ(input.size(), 7u);
    Core::Video::H264Decoder decoder;
    std::vector<Core::Video::DecodedFrame> frames;
    for (std::size_t i = 0; i < input.size(); ++i) {
        auto decoded = decoder.decode(input[i], double(i) + 0.125, 0x28000000 + i * 0x10000);
        if (i == 0)
            EXPECT_TRUE(decoded.empty());
        for (auto &frame: decoded)
            frames.push_back(std::move(frame));
    }
    EXPECT_LT(frames.size(), input.size());
    auto delayed = decoder.drain();
    for (auto &frame: delayed)
        frames.push_back(std::move(frame));
    ASSERT_EQ(frames.size(), 7u);
    const std::array<unsigned, 7> displayOrder{0, 2, 3, 1, 5, 6, 4};
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto &frame = frames[i];
        EXPECT_DOUBLE_EQ(frame.timestamp, displayOrder[i] + 0.125);
        EXPECT_EQ(frame.guestBuffer, 0x28000000 + displayOrder[i] * 0x10000);
        EXPECT_EQ(frame.width, 64u);
        EXPECT_EQ(frame.height, 48u);
        EXPECT_EQ(frame.stride, 256u);
        ASSERT_EQ(frame.nv12.size(), 256u * 48u * 3 / 2);
        EXPECT_NE(frame.nv12[0], frame.nv12[32]);
        EXPECT_NE(frame.nv12[256 * 48], frame.nv12[256 * 48 + 1]);
        EXPECT_EQ(frame.nv12[64], 0u);
    }
    decoder.reset();
    EXPECT_TRUE(decoder.decode(input[0], 8.125, 0x29000000).empty());
    auto restarted = decoder.drain();
    ASSERT_EQ(restarted.size(), 1u);
    EXPECT_DOUBLE_EQ(restarted[0].timestamp, 8.125);
}

TEST(H264DecoderTest, ImageSizeFromHeadersAndMalformedInput)
{
    auto input = packets();
    ASSERT_EQ(input.size(), 7u);
    auto header = input[0];
    for (std::size_t i = 0; i + 3 < header.size(); ++i) {
        if (!header[i] && !header[i + 1] && header[i + 2] == 1 && (header[i + 3] & 31) == 5) {
            header.resize(i);
            break;
        }
    }
    std::uint32_t width = 123, height = 456;
    EXPECT_TRUE(Core::Video::H264Decoder::imageSize(header, width, height));
    EXPECT_EQ(width, 64u);
    EXPECT_EQ(height, 48u);
    const std::array<std::uint8_t, 5> broken{0, 0, 1, 0x67, 0};
    EXPECT_FALSE(Core::Video::H264Decoder::imageSize(broken, width, height));
    EXPECT_EQ(width, 64u);
    EXPECT_EQ(height, 48u);
}

TEST(H264DecoderTest, PreservesCodedDimensionsAndSeparateCropFields)
{
    const auto input = packets("cropped.h264");
    ASSERT_EQ(input.size(), 1u);
    std::uint32_t width{}, height{};
    ASSERT_TRUE(Core::Video::H264Decoder::imageSize(input[0], width, height));
    EXPECT_EQ(width, 64u);
    EXPECT_EQ(height, 64u);
    Core::Video::H264Decoder decoder;
    auto frames = decoder.decode(input[0], 12.25, 0x28000000);
    auto tail = decoder.drain();
    for (auto &frame: tail)
        frames.push_back(std::move(frame));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].height, 64u);
    EXPECT_EQ(frames[0].cropTop, 0u);
    EXPECT_EQ(frames[0].cropBottom, 14u);
    EXPECT_EQ(frames[0].nv12.size(), 256u * 64u * 3 / 2);
}

class H264Test : public InstructionTest {};

TEST_F(H264Test, GuestDecoderOutputsNv12AndReturnsSynchronouslyToCaller)
{
    RegisterH264Functions();
    cpu->m_scheduler = Core::Scheduler{};
    cpu->m_scheduler.bootstrap(*cpu);
    const auto caller = cpu->m_scheduler.currentHandle();
    constexpr std::uint32_t context = 0x10010000, inputAddress = 0x10020000;
    constexpr std::uint32_t callback = 0x02000900, originalStack = 0xC0FFF000;
    auto call = [&](const char *name, std::initializer_list<std::uint32_t> args) {
        unsigned reg = 3;
        for (auto arg: args)
            cpu->m_gpr[reg++] = arg;
        cpu->m_hle_redirected = false;
        Core::syscallHandler.get(name)(*cpu);
        return cpu->m_gpr[3];
    };
    EXPECT_EQ(call("H264DECInitParam", {256, context}), 0u);
    EXPECT_EQ(call("H264DECSetParam_FPTR_OUTPUT", {context, callback}), 0u);
    cpu->m_memory.write<std::uint32_t>(inputAddress, 0x12345678);
    EXPECT_EQ(call("H264DECSetParam_USER_MEMORY", {context, inputAddress}), 0u);
    EXPECT_EQ(call("H264DECOpen", {context}), 0u);
    EXPECT_EQ(call("H264DECBegin", {context}), 0u);
    cpu->m_gpr[1] = originalStack;
    cpu->m_lr = 0x4000;
    std::vector<unsigned> displayed;
    auto inspectCallback = [&](std::uint32_t result) {
        ASSERT_EQ(cpu->m_scheduler.currentHandle(), caller);
        ASSERT_EQ(cpu->m_nextPc, callback - 0x02000000);
        EXPECT_EQ(cpu->m_lr, Core::H264::callbackSentinel - 0x02000000);
        const auto descriptor = cpu->m_gpr[3];
        // Exercise an actual PPC callee prologue, including a parameter spill into the caller area.
        const std::array<std::uint32_t, 4> prologue{0x9421FFE0, 0x7C0802A6, 0x90010024, 0x90610028};
        for (auto instruction: prologue)
            cpu->executeInstruction(EncodedInstruction(instruction));
        const auto count = cpu->m_memory.read<std::uint32_t>(descriptor);
        const auto pointers = cpu->m_memory.read<std::uint32_t>(descriptor + 4);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(descriptor + 8), 0x12345678u);
        ASSERT_GE(count, 1u);
        ASSERT_LE(count, 7u);
        for (unsigned i = 0; i < count; ++i) {
            const auto record = cpu->m_memory.read<std::uint32_t>(pointers + i * 4);
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(record), 100u);
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(record + 0x10), 64u);
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(record + 0x14), 48u);
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(record + 0x18), 256u);
            const auto buffer = cpu->m_memory.read<std::uint32_t>(record + 0x44);
            const auto index = (buffer - 0x28000000) / 0x10000;
            displayed.push_back(index);
            EXPECT_DOUBLE_EQ(std::bit_cast<double>(cpu->m_memory.read<std::uint64_t>(record + 8)), index + 0.125);
            EXPECT_NE(cpu->m_memory.read<std::uint8_t>(buffer), cpu->m_memory.read<std::uint8_t>(buffer + 32));
            EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(buffer + 256 * 48 * 3 / 2), 0xABu);
        }
        const std::array<std::uint32_t, 4> epilogue{0x80010024, 0x7C0803A6, 0x38210020, 0x4E800020};
        for (auto instruction: epilogue)
            cpu->executeInstruction(EncodedInstruction(instruction));
        EXPECT_EQ(cpu->m_nextPc, Core::H264::callbackSentinel - 0x02000000);
        cpu->m_gpr[3] = 0xBAD; // guest callback's own return value is not H264DECExecute's result
        Core::H264::onCallbackReturn(*cpu);
        EXPECT_EQ(cpu->m_gpr[1], originalStack);
        EXPECT_EQ(cpu->m_lr, 0x4000u);
        EXPECT_EQ(cpu->m_nextPc, 0x4000u);
        EXPECT_EQ(cpu->m_gpr[3], result);
    };
    const auto input = packets();
    ASSERT_EQ(input.size(), 7u);
    for (unsigned i = 0; i < input.size(); ++i) {
        std::memcpy(cpu->m_memory.hostPtr(inputAddress), input[i].data(), input[i].size());
        const auto output = 0x28000000 + i * 0x10000;
        cpu->m_memory.write<std::uint8_t>(output + 256 * 48 * 3 / 2, 0xAB);
        cpu->m_fpr[1] = i + 0.125;
        EXPECT_EQ(call("H264DECSetBitstream", {context, inputAddress, static_cast<std::uint32_t>(input[i].size())}), 0u);
        call("H264DECExecute", {context, output});
        if (i < 5) {
            EXPECT_FALSE(cpu->m_hle_redirected);
            EXPECT_EQ(cpu->m_gpr[3], 0xE4u);
        }
        if (cpu->m_hle_redirected)
            inspectCallback(0xE4);
    }
    call("H264DECEnd", {context});
    ASSERT_TRUE(cpu->m_hle_redirected);
    inspectCallback(0);
    EXPECT_EQ(displayed, (std::vector<unsigned>{0, 2, 3, 1, 5, 6, 4}));
    EXPECT_EQ(call("H264DECExecute", {context, 0x28000000}), 0x1010000u);
    EXPECT_EQ(call("H264DECBegin", {context}), 0u);
    std::memcpy(cpu->m_memory.hostPtr(inputAddress), input[1].data(), input[1].size());
    EXPECT_EQ(call("H264DECSetBitstream", {context, inputAddress, static_cast<std::uint32_t>(input[1].size())}), 0u);
    EXPECT_EQ(call("H264DECExecute", {context, 0x28000000}), 0x400u);
    std::memcpy(cpu->m_memory.hostPtr(inputAddress), input[0].data(), input[0].size());
    EXPECT_EQ(call("H264DECSetBitstream", {context, inputAddress, static_cast<std::uint32_t>(input[0].size())}), 0u);
    EXPECT_EQ(call("H264DECExecute", {context, 0x28000000}), 0xE4u);
    EXPECT_EQ(call("H264DECClose", {context}), 0u);
    EXPECT_EQ(call("H264DECBegin", {context}), 0x1010000u);
}

TEST_F(H264Test, GuestDecoderRejectsUnmappedOrWrappedMemory)
{
    RegisterH264Functions();
    cpu->m_hle_redirected = false;
    for (const auto &pair: {std::pair{0u, 0x100u}, std::pair{0xFFFFFF00u, 0x200u}, std::pair{0x10010000u, 0u}}) {
        cpu->m_gpr[3] = pair.first;
        cpu->m_gpr[4] = pair.second;
        Core::syscallHandler.get("H264DECCheckMemSegmentation")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], 1u);
    }
    cpu->m_gpr[3] = 255;
    cpu->m_gpr[4] = 0x10010000;
    Core::syscallHandler.get("H264DECInitParam")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0x1010000u);
    cpu->m_gpr[3] = 0x10010000;
    Core::syscallHandler.get("H264DECOpen")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0x1010000u);
}

TEST_F(H264Test, MemoryRequirementUsesLevelLimits)
{
    RegisterH264Functions();
    constexpr std::uint32_t output = 0x10003000;
    struct Expected {
            std::uint32_t level, bytes;
    };
    const Expected cases[] = {{10, 0x63447},   {11, 0xE1447},   {12, 0x252447},  {20, 0x252447},  {21, 0x4A4447},
                              {22, 0x7E9447},  {30, 0x7E9447},  {31, 0x1194447}, {32, 0x1400447}, {33, 0x2000447},
                              {41, 0x2000447}, {42, 0x2200447}, {43, 0x6BD0447}, {50, 0x6BD0447}, {51, 0xB400447}};
    for (auto profile: {66u, 77u, 100u}) {
        for (const auto &item: cases) {
            cpu->m_gpr[3] = profile;
            cpu->m_gpr[4] = item.level;
            cpu->m_gpr[5] = 1280;
            cpu->m_gpr[6] = 720;
            cpu->m_gpr[7] = output;
            Core::syscallHandler.get("H264DECMemoryRequirement")(*cpu);
            EXPECT_EQ(cpu->m_gpr[3], 0u);
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output), item.bytes);
        }
    }
}

TEST_F(H264Test, InvalidMemoryParametersDoNotOverwriteOutput)
{
    RegisterH264Functions();
    constexpr std::uint32_t output = 0x10003000;
    const std::array<std::uint32_t, 5> valid{100, 41, 1280, 720, output};
    for (unsigned field = 0; field < valid.size(); ++field) {
        for (unsigned i = 0; i < valid.size(); ++i)
            cpu->m_gpr[3 + i] = valid[i];
        cpu->m_memory.write<std::uint32_t>(output, 0xCAFEBABE);
        cpu->m_gpr[3 + field] = field == 1 ? 52 : 0;
        Core::syscallHandler.get("H264DECMemoryRequirement")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], field == 0 ? 0x1080000u : 0x1010000u);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output), 0xCAFEBABEu);
    }
}
