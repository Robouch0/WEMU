// Fetch shaders loaded from a title's shader archive skip GX2InitFetchShaderEx, so
// GX2SetFetchShader rebuilds the vertex attribute layout from the VTX_FETCH
// microcode. Every vertex a retail title draws goes through this decode.

#include <array>
#include <bit>
#include <cstdint>
#include <vector>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "gfx/Gx2CommandStream.hpp"
#include "gfx/LatteProgram.hpp"
#include "hle/Gx2.hpp"

namespace {
    // One VTX_FETCH instruction as the decoder reads it.
    struct Fetch {
            std::uint32_t location; // semantic id
            std::uint32_t buffer; // 0-15, or 160+ for the alias range
            std::uint32_t dataFormat; // Latte FMT_*
            std::uint32_t numFormat; // 0 norm, 1 int, 2 scaled
            bool isSigned;
            std::uint32_t offset;
            std::uint32_t endian;
            std::uint32_t fetchType; // 0 per vertex, 1 per instance
            std::array<std::uint32_t, 4> select; // destination swizzle x, y, z, w
    };

    // Microcode: one CF_VTX clause (EOP) at word 0 whose fetches start at word 4.
    std::vector<std::uint32_t> fetchProgram(const std::vector<Fetch> &fetches)
    {
        using namespace Core::Gfx::Latte;
        const auto count = static_cast<std::uint32_t>(fetches.size()) - 1;
        std::vector<std::uint32_t> words{2, (CF_VTX << 23) | (1u << 21) | ((count & 7) << 10) | ((count & 8) << 16), 0, 0};
        for (const auto &f: fetches) {
            words.push_back(1u | (f.fetchType << 5) | (f.buffer << 8)); // semantic fetch
            words.push_back(f.location | (f.select[0] << 9) | (f.select[1] << 12) | (f.select[2] << 15) | (f.select[3] << 18) | (f.dataFormat << 22) |
                            (f.numFormat << 28) | (std::uint32_t(f.isSigned) << 30));
            words.push_back(f.offset | (f.endian << 16));
            words.push_back(0);
        }
        return words;
    }
} // namespace

class Gx2FetchShaderTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterGx2Functions();
            Core::Gfx::gx2Stream().clear();
        }

        // Writes a GX2FetchShader header at `header` pointing at little-endian microcode at `program`,
        // records GX2SetFetchShader and returns the layout attached to the command.
        std::vector<std::uint32_t> setFetchShader(std::uint32_t header, std::uint32_t program, const std::vector<std::uint32_t> &words)
        {
            cpu->m_memory.write<std::uint32_t>(header + 8, static_cast<std::uint32_t>(words.size() * 4));
            cpu->m_memory.write<std::uint32_t>(header + 0xC, program);
            for (std::size_t i = 0; i < words.size(); ++i)
                cpu->m_memory.write<std::uint32_t>(program + static_cast<std::uint32_t>(i * 4), std::byteswap(words[i]));
            cpu->m_gpr[3] = header;
            Core::syscallHandler.get("GX2SetFetchShader")(*cpu);
            const auto &stream = Core::Gfx::gx2Stream();
            EXPECT_FALSE(stream.empty());
            if (stream.empty())
                return {};
            const auto &payload = stream.commands().back().payload;
            return {payload.begin(), payload.end()};
        }
};

TEST_F(Gx2FetchShaderTest, AttributeFormatsBuffersOffsetsAndSwizzlesAreDecoded)
{
    const auto layout = setFetchShader(0x28500000, 0x28501000,
                                       fetchProgram({
                                               // position: FMT_32_32_32_FLOAT from buffer 0, byte-swapped 8-in-32
                                               {0, 0, 0x30, 2, false, 0, 2, 0, {0, 1, 2, 5}},
                                               // colour: FMT_8_8_8_8 unorm, per instance, buffer alias 161 -> 1
                                               {1, 161, 0x1A, 0, false, 12, 0, 1, {0, 1, 2, 3}},
                                               // bone index: FMT_16 signed integer, only x used
                                               {2, 2, 0x05, 1, true, 4, 1, 0, {0, 7, 7, 7}},
                                       }));
    // Eight words per attribute: location, buffer, offset, format, type, divisor, mask, endian.
    const std::vector<std::uint32_t> expected{
            0, 0, 0,  0x811, 0, 0, 0x00010205, 2, // 32_32_32 | float
            1, 1, 12, 0x00A, 1, 0, 0x00010203, 0, // 8_8_8_8 normalised, per instance
            2, 2, 4,  0x302, 0, 0, 0x00070707, 1, // 16 | integer | signed
    };
    EXPECT_EQ(layout, expected);
}

TEST_F(Gx2FetchShaderTest, EveryLatteVertexFormatMapsToItsGx2AttribFormat)
{
    // FMT_* -> GX2_ATTRIB_FORMAT type, float formats flagged 0x800.
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> formats{
            {0x01, 0x000}, {0x02, 0x001}, {0x05, 0x002}, {0x06, 0x803}, {0x07, 0x004}, {0x0D, 0x005}, {0x0E, 0x806},
            {0x0F, 0x007}, {0x10, 0x808}, {0x19, 0x00B}, {0x1A, 0x00A}, {0x1D, 0x00C}, {0x1E, 0x80D}, {0x1F, 0x00E},
            {0x20, 0x80F}, {0x2F, 0x010}, {0x30, 0x811}, {0x22, 0x012}, {0x23, 0x813},
    };
    std::uint32_t program = 0x28510000;
    for (const auto &[dataFormat, gx2Format]: formats) {
        const auto layout = setFetchShader(0x28508000, program, fetchProgram({{0, 0, dataFormat, 0, false, 0, 0, 0, {0, 1, 2, 3}}}));
        ASSERT_EQ(layout.size(), 8u) << std::hex << dataFormat;
        EXPECT_EQ(layout[3], gx2Format) << "FMT 0x" << std::hex << dataFormat;
        program += 0x100; // decoded layouts are cached per program
    }
    // Scaled (numFormat 2) integer formats are flagged like floats.
    const auto scaled = setFetchShader(0x28508000, program, fetchProgram({{0, 0, 0x1A, 2, false, 0, 0, 0, {0, 1, 2, 3}}}));
    ASSERT_EQ(scaled.size(), 8u);
    EXPECT_EQ(scaled[3], 0x80Au);
}

TEST_F(Gx2FetchShaderTest, UndecodableMicrocodeYieldsNoLayoutInsteadOfAGuess)
{
    // An unknown data format would give the vertex shader garbage: the whole layout is dropped.
    EXPECT_TRUE(setFetchShader(0x28520000, 0x28521000, fetchProgram({{0, 0, 0x3F, 0, false, 0, 0, 0, {0, 1, 2, 3}}})).empty());

    // A clause that points past the end of the program.
    auto truncated = fetchProgram({{0, 0, 0x30, 0, false, 0, 0, 0, {0, 1, 2, 3}}});
    truncated[0] = 40;
    EXPECT_TRUE(setFetchShader(0x28522000, 0x28523000, truncated).empty());

    // A size outside 16 bytes .. 64 KiB is not a fetch shader.
    EXPECT_TRUE(setFetchShader(0x28524000, 0x28525000, {0, 0}).empty());
}

TEST_F(Gx2FetchShaderTest, NewMicrocodeAtAReusedAddressIsDecodedAgain)
{
    // Titles free shader archives between scenes; the next one can load at the same address.
    constexpr std::uint32_t Header = 0x28530000, Program = 0x28531000;
    const auto first = setFetchShader(Header, Program, fetchProgram({{0, 0, 0x30, 0, false, 0, 0, 0, {0, 1, 2, 3}}}));
    ASSERT_EQ(first.size(), 8u);
    EXPECT_EQ(first[3], 0x811u);

    const auto second = setFetchShader(Header, Program,
                                       fetchProgram({{3, 1, 0x1A, 0, false, 8, 0, 0, {0, 1, 2, 3}}, {4, 2, 0x10, 0, false, 0, 0, 0, {0, 1, 7, 7}}}));
    ASSERT_EQ(second.size(), 16u) << "the old single-attribute layout was reused";
    EXPECT_EQ(second[0], 3u);
    EXPECT_EQ(second[3], 0x00Au);
    EXPECT_EQ(second[8 + 3], 0x808u);

    const auto again = setFetchShader(Header, Program,
                                      fetchProgram({{3, 1, 0x1A, 0, false, 8, 0, 0, {0, 1, 2, 3}}, {4, 2, 0x10, 0, false, 0, 0, 0, {0, 1, 7, 7}}}));
    EXPECT_EQ(again, second) << "unchanged microcode keeps its layout";
}
