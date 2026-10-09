#include <atomic>
#include <bit>
#include <gtest/gtest.h>
#include <stdexcept>

#include "gfx/Gx2CommandStream.hpp"
#include "gfx/Gx2Replayer.hpp"
#include "gfx/LatteVsInterp.hpp"
#include "gfx/RasterWorkers.hpp"
#include "gfx/TextureFormat.hpp"
#include "gfx/TriangleCoverage.hpp"

// GX2 -> framebuffer replay: clears + present produce an RGBX frame of the clear colour.
using namespace Core::Gfx;

TEST(TriangleCoverageTest, SharedEdgesHaveOneOwnerForBothWindings)
{
    using Point = TriangleCoverage::Point;
    for (const unsigned size: {8u, 480u})
        for (const bool reverse: {false, true}) {
            Point a{0, 0}, b{double(size), 0}, c{double(size), double(size)}, d{0, double(size)};
            TriangleCoverage first(a, reverse ? c : b, reverse ? b : c);
            TriangleCoverage second(a, reverse ? d : c, reverse ? c : d);
            for (unsigned y = 0; y < size; ++y)
                for (unsigned x = 0; x < size; ++x)
                    ASSERT_EQ(unsigned(first.contains(x + 0.5, y + 0.5)) + second.contains(x + 0.5, y + 0.5), 1u);
        }
    TriangleCoverage first({854, 0}, {0.00003, 0}, {0.00003, 480});
    TriangleCoverage second({854, 0}, {0.00003, 480}, {854, 480});
    for (unsigned y = 0; y < 480; ++y)
        for (unsigned x = 0; x < 854; ++x)
            ASSERT_EQ(unsigned(first.contains(x + 0.5, y + 0.5)) + second.contains(x + 0.5, y + 0.5), 1u);
    EXPECT_FALSE(first.contains(-0.5, 100.5));
    EXPECT_FALSE(second.contains(854.5, 100.5));
    EXPECT_FALSE(TriangleCoverage({0, 0}, {1, 1}, {2, 2}).contains(0.5, 0.5));
}

TEST(TextureFormatTest, R32FloatPreservesRangePrecisionAndSelectors)
{
    for (const float value: {-2.0f, 0.0f, 0.123456f, 1.0f, 123.5f}) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        std::array<std::uint8_t, 4> bytes{};
        for (unsigned i = 0; i < 4; ++i)
            bytes[i] = bits >> (i * 8);
        EXPECT_EQ(decodeR32Float(bytes.data(), 0x00010203), (std::array<float, 4>{value, 0, 0, 1}));
        EXPECT_EQ(decodeR32Float(bytes.data(), 0x00000504), (std::array<float, 4>{value, value, 1, 0}));
    }
}

TEST(LatteProgramTest, SharedDecoderPreservesGroupsLiteralsAndSourceModifiers)
{
    const std::vector<std::uint32_t> words{2,
                                           (8u << 26) | (2u << 18),
                                           8u << 15,
                                           (0x28u << 23) | (1u << 21) | 0x688u,
                                           253u | (1u << 12),
                                           (0x19u << 7) | 17u | (7u << 21),
                                           0x800000FDu | (1u << 10),
                                           (0x19u << 7) | 16u | (8u << 21),
                                           std::bit_cast<std::uint32_t>(0.25f),
                                           std::bit_cast<std::uint32_t>(0.75f)};
    const auto decoded = Latte::decodeProgram(words);
    ASSERT_TRUE(decoded->valid);
    EXPECT_EQ(decoded->words, words);
    EXPECT_EQ(decoded->registerCount, 9u);
    const auto &groups = decoded->clauses.at((std::uint64_t(2) << 32) | 3);
    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].size(), 2u);
    EXPECT_TRUE(groups[0][0].src[0].neg);
    EXPECT_TRUE(groups[0][0].src[0].abs);
    EXPECT_FALSE(groups[0][0].scalarSlot);
    EXPECT_TRUE(groups[0][1].scalarSlot);
    EXPECT_EQ(groups[0][1].literal[1], 0.75f);
    EXPECT_NE(Latte::decodeProgram(words).get(), decoded.get());
    const auto output = LatteVsInterp::runPixel(*decoded, {}, nullptr, 0, {});
    ASSERT_TRUE(output.colorValid);
    EXPECT_EQ(output.color, (std::array<float, 4>{0.75f, 0, 0, 0}));
    auto truncated = words;
    truncated.pop_back();
    EXPECT_FALSE(Latte::decodeProgram(truncated)->valid);
    auto unterminated = words;
    unterminated[6] &= 0x7FFFFFFFu;
    EXPECT_FALSE(Latte::decodeProgram(unterminated)->valid);
}

TEST(RasterWorkersTest, CoversEveryRowOnceAcrossRepeatedBatches)
{
    for (unsigned workers: {1u, 2u, 4u, 16u}) {
        RasterWorkers pool(workers);
        for (unsigned rows: {0u, 1u, 3u, 31u, 720u}) {
            std::vector<std::atomic<unsigned>> hits(rows);
            for (unsigned repeat = 0; repeat < 20; ++repeat)
                pool.run(rows, [&](unsigned first, unsigned end) {
                    for (auto row = first; row < end; ++row)
                        ++hits.at(row);
                });
            for (const auto &hit: hits)
                EXPECT_EQ(hit.load(), 20u);
        }
    }
}

TEST(RasterWorkersTest, WaitsForAllWorkersAndCanRecoverFromException)
{
    RasterWorkers pool(4);
    std::atomic<unsigned> finished{};
    EXPECT_THROW(pool.run(4,
                          [&](unsigned first, unsigned) {
                              if (first == 2)
                                  throw std::runtime_error("test failure");
                              ++finished;
                          }),
                 std::runtime_error);
    EXPECT_EQ(finished.load(), 3u);
    pool.run(4, [&](unsigned, unsigned) { ++finished; });
    EXPECT_EQ(finished.load(), 7u);
}

TEST(TextureSamplerTest, PointAddressingModesAndSeparateAxes)
{
    TextureSampler sampler;
    const auto fetch = [](unsigned x, unsigned y) { return std::array<float, 4>{float(x), float(y), 0, 1}; };
    const std::array<float, 8> expected{3, 0, 0, 0, 0, 0, -1, 0};
    sampler.customBorder = {-1, -1, -1, -1};
    for (unsigned mode = 0; mode < 8; mode++) {
        sampler.regs[0] = mode | (2u << 3) | (3u << 22);
        EXPECT_EQ(sampler.sample(4, 4, -0.125f, 2.0f, fetch)[0], expected[mode]) << mode;
        EXPECT_EQ(sampler.sample(4, 4, 0.625f, 2.0f, fetch)[1], 3) << mode;
    }
    sampler.regs[0] = 1 | (2u << 3);
    EXPECT_EQ(sampler.sample(4, 1, 1.125f, 0.5f, fetch)[0], 3);
    EXPECT_EQ(sampler.sample(4, 1, 2.125f, 0.5f, fetch)[0], 0);
}

TEST(TextureSamplerTest, BilinearTexelCentersWrapAndClamp)
{
    TextureSampler sampler;
    const auto fetch = [](unsigned x, unsigned y) { return std::array<float, 4>{float(x), float(y), 0, 1}; };
    sampler.regs[0] = (1u << 9);
    EXPECT_EQ(sampler.sample(2, 2, 0.25f, 0.75f, fetch), (std::array<float, 4>{0, 1, 0, 1}));
    EXPECT_EQ(sampler.sample(2, 2, 0.5f, 0.5f, fetch), (std::array<float, 4>{0.5f, 0.5f, 0, 1}));
    EXPECT_FLOAT_EQ(sampler.sample(2, 2, 0, 0.25f, fetch)[0], 0.5f);
    sampler.regs[0] |= 2 | (2u << 3);
    EXPECT_FLOAT_EQ(sampler.sample(2, 2, -2, 0.25f, fetch)[0], 0);
    EXPECT_FLOAT_EQ(sampler.sample(2, 2, 2, 0.25f, fetch)[0], 1);
}

TEST(TextureSamplerTest, BorderColorsAndHalfBorderFiltering)
{
    TextureSampler sampler;
    const auto white = [](unsigned, unsigned) { return std::array<float, 4>{1, 1, 1, 1}; };
    sampler.regs[0] = 4 | (2u << 3) | (1u << 9);
    EXPECT_EQ(sampler.sample(2, 1, -5, 0.5f, white), (std::array<float, 4>{0.5f, 0.5f, 0.5f, 0.5f}));
    sampler.regs[0] = 6 | (2u << 3) | (1u << 9);
    EXPECT_EQ(sampler.sample(2, 1, -5, 0.5f, white), (std::array<float, 4>{}));
    sampler.regs[0] |= 1u << 22;
    EXPECT_EQ(sampler.sample(2, 1, -5, 0.5f, white), (std::array<float, 4>{0, 0, 0, 1}));
    sampler.regs[0] = 6 | (2u << 22);
    EXPECT_EQ(sampler.sample(2, 1, -5, 0.5f, white), (std::array<float, 4>{1, 1, 1, 1}));
    sampler.regs[0] = 6 | (3u << 22);
    sampler.customBorder = {0.1f, 0.2f, 0.3f, 0.4f};
    EXPECT_EQ(sampler.sample(2, 1, -5, 0.5f, white), sampler.customBorder);
    EXPECT_EQ(sampler.sample(2, 1, std::numeric_limits<float>::max(), 0.5f, white), sampler.customBorder);
    EXPECT_EQ(sampler.sample(2, 1, std::numeric_limits<float>::quiet_NaN(), 0.5f, white), sampler.customBorder);
    EXPECT_EQ(sampler.sample(0, 1, 0.5f, 0.5f, white), sampler.customBorder);
}

TEST(TextureSamplerTest, GatherRedFootprintOrderAndFilterIndependence)
{
    TextureSampler sampler;
    const auto fetch = [](unsigned x, unsigned y) { return std::array<float, 4>{float(1 + x + 2 * y), 20, 30, 40}; };
    for (unsigned filter = 0; filter < 8; ++filter) {
        sampler.regs[0] = filter << 9;
        EXPECT_EQ(sampler.gather(2, 2, 0.5f, 0.5f, fetch), (std::array<float, 4>{3, 4, 2, 1}));
        EXPECT_EQ(sampler.gather(2, 2, 0.3f, 0.7f, fetch), (std::array<float, 4>{3, 4, 2, 1}));
        EXPECT_EQ(sampler.gather(2, 2, 0, 0, fetch), (std::array<float, 4>{2, 1, 3, 4}));
    }
    sampler.regs[0] = 2 | (2u << 3);
    EXPECT_EQ(sampler.gather(2, 2, 0, 0, fetch), (std::array<float, 4>{1, 1, 1, 1}));
    sampler.regs[0] = 1 | (1u << 3);
    EXPECT_EQ(sampler.gather(2, 2, 1.5f, -0.5f, fetch), (std::array<float, 4>{2, 1, 3, 4}));
    EXPECT_EQ(sampler.gather(1, 1, 0.5f, 0.5f, fetch), (std::array<float, 4>{1, 1, 1, 1}));
}

TEST(TextureSamplerTest, GatherUsesRedBorderPerTapAndRejectsInvalidCoordinates)
{
    TextureSampler sampler;
    sampler.regs[0] = 6 | (6u << 3) | (3u << 22);
    sampler.customBorder = {-1, -2, -3, -4};
    unsigned calls = 0;
    const auto fetch = [&](unsigned x, unsigned y) {
        ++calls;
        EXPECT_LT(x, 2u);
        EXPECT_LT(y, 2u);
        return std::array<float, 4>{float(1 + x + 2 * y), 20, 30, 40};
    };
    EXPECT_EQ(sampler.gather(2, 2, 0, 0, fetch), (std::array<float, 4>{-1, 1, -1, -1}));
    EXPECT_EQ(calls, 1u);
    for (float invalid: {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::max()}) {
        EXPECT_EQ(sampler.gather(2, 2, invalid, 0.5f, fetch), (std::array<float, 4>{-1, -1, -1, -1}));
        EXPECT_EQ(sampler.gather(2, 2, 0.5f, invalid, fetch), (std::array<float, 4>{-1, -1, -1, -1}));
    }
    EXPECT_EQ(sampler.gather(0, 2, 0.5f, 0.5f, fetch), (std::array<float, 4>{-1, -1, -1, -1}));
    EXPECT_EQ(sampler.gather(2, 0, 0.5f, 0.5f, fetch), (std::array<float, 4>{-1, -1, -1, -1}));
    EXPECT_EQ(calls, 1u);
}

static Gx2Command clearColor(double r, double g, double b, double a)
{
    Gx2Command c;
    c.type = Gx2Cmd::ClearColor;
    c.fpr[0] = r;
    c.fpr[1] = g;
    c.fpr[2] = b;
    c.fpr[3] = a;
    return c;
}

TEST(Gx2ReplayTest, ClearThenPresentFillsFramebuffer)
{
    Gx2CommandStream stream;
    stream.push(clearColor(0.25, 0.5, 0.75, 1.0));
    stream.push(Gx2Command{Gx2Cmd::SwapScanBuffers, {}, {}});

    Gx2Replayer replayer;
    replayer.replay(stream);

    EXPECT_EQ(replayer.presentCount(), 1u);
    const auto &fb = replayer.framebuffer();
    ASSERT_EQ(fb.size(), static_cast<std::size_t>(Gx2Replayer::kWidth) * Gx2Replayer::kHeight * 4);

    // 0.25/0.5/0.75 -> 64/128/191, X=255; checked at first and last pixel.
    EXPECT_EQ(fb[0], 64);
    EXPECT_EQ(fb[1], 128);
    EXPECT_EQ(fb[2], 191);
    EXPECT_EQ(fb[3], 255);
    const std::size_t last = fb.size() - 4;
    EXPECT_EQ(fb[last + 0], 64);
    EXPECT_EQ(fb[last + 1], 128);
    EXPECT_EQ(fb[last + 2], 191);
}

TEST(Gx2ReplayTest, LatestClearColorWins)
{
    Gx2CommandStream stream;
    stream.push(clearColor(1.0, 0.0, 0.0, 1.0)); // red
    stream.push(clearColor(0.0, 1.0, 0.0, 1.0)); // then green
    stream.push(Gx2Command{Gx2Cmd::SwapScanBuffers, {}, {}});

    Gx2Replayer replayer;
    replayer.replay(stream);

    const auto &fb = replayer.framebuffer();
    EXPECT_EQ(fb[0], 0); // R
    EXPECT_EQ(fb[1], 255); // G
    EXPECT_EQ(fb[2], 0); // B
}

TEST(Gx2ReplayTest, NoPresentNoFrame)
{
    Gx2CommandStream stream;
    stream.push(clearColor(0.5, 0.5, 0.5, 1.0));

    Gx2Replayer replayer;
    replayer.replay(stream);

    EXPECT_EQ(replayer.presentCount(), 0u);
    EXPECT_TRUE(replayer.framebuffer().empty());
}

TEST(LatteVsTest, ScalarSlotDoesNotOverwriteVectorForwarding)
{
    // Four MOVs fill XYZW, a fifth MOV targets Y through the scalar slot.
    // The next group must see PV.y=2 and PS=9, while the GPR write still lands.
    std::vector<std::uint32_t> program(4);
    auto mov = [&](unsigned src, unsigned srcChan, unsigned dst, unsigned dstChan, bool last) {
        program.push_back(src | (srcChan << 10) | (248u << 13) | (unsigned(last) << 31));
        program.push_back((0x19u << 7) | 16u | (dst << 21) | (dstChan << 29));
    };
    for (unsigned c = 0; c < 4; c++)
        mov(1, c, 3, c, false);
    mov(2, 0, 4, 1, true);
    mov(254, 1, 5, 0, false);
    mov(255, 0, 5, 1, false);
    mov(4, 1, 5, 2, true);
    program[0] = 2;
    program[1] = (8u << 26) | (7u << 18);
    program[2] = 60u | (1u << 13) | (5u << 15);
    program[3] = (0x28u << 23) | (1u << 21) | (1u << 3) | (2u << 6) | (5u << 9);
    std::array<std::array<float, 4>, 4> attributes{};
    attributes[0] = {1, 2, 3, 4};
    attributes[1] = {9, 0, 0, 1};
    const auto out = LatteVsInterp::run(program, attributes, nullptr, 0);
    ASSERT_TRUE(out.valid);
    EXPECT_EQ(out.pos, (std::array<float, 4>{2, 9, 9, 1}));
}

TEST(LattePsTest, SelectsTextureThroughAluAndModulatesInterpolatedColor)
{
    std::vector<std::uint32_t> words(8);
    words[0] = 4;
    words[1] = (1u << 23) | (1u << 10); // two TEX instructions
    for (unsigned i = 0; i < 2; i++) {
        words.push_back(0x10u | ((i ? 7u : 3u) << 8)); // SAMPLE R0.xy
        words.push_back((4u + i) | (1u << 12) | (2u << 15) | (3u << 18) | 0x30000000u);
        words.push_back((1u << 23) | (4u << 26) | (5u << 29));
        words.push_back(0);
    }
    words[2] = words.size() / 2;
    words[3] = (8u << 26) | (7u << 18);
    for (unsigned c = 0; c < 4; c++) {
        // R6 = c0.x == 0 ? R4 : R5, component-wise.
        words.push_back(256u | (4u << 13) | (c << 23) | (unsigned(c == 3) << 31));
        words.push_back(5u | (c << 10) | (0x18u << 13) | (6u << 21) | (c << 29));
    }
    for (unsigned c = 0; c < 4; c++) {
        words.push_back(6u | (c << 10) | (1u << 13) | (c << 23) | (unsigned(c == 3) << 31));
        words.push_back((2u << 7) | 16u | (6u << 21) | (c << 29)); // MUL_IEEE
    }
    words[4] = 6u << 15;
    words[5] = (0x28u << 23) | (1u << 21) | 0x688u;
    const auto program = LatteVsInterp::compile(words);
    std::array<std::array<float, 4>, 4> inputs{};
    inputs[0] = {0.25f, 0.75f, 0, 1};
    inputs[1] = {0.5f, 0.25f, 1, 0.8f};
    const std::array<float, 4> a{1, 0.2f, 0.3f, 1}, b{0.1f, 0.7f, 0.4f, 0.6f};
    unsigned samples = 0;
    const auto sampler = [&](unsigned unit, unsigned, const std::array<float, 4> &coords, std::array<float, 4> &rgba) {
        EXPECT_EQ(coords, inputs[0]);
        EXPECT_TRUE(unit == 3 || unit == 7);
        rgba = unit == 3 ? a : b;
        ++samples;
        return true;
    };
    for (float selection: {0.0f, 1.0f}) {
        const float constants[4]{selection, 0, 0, 0};
        const auto out = LatteVsInterp::runPixel(*program, inputs, constants, 1, sampler);
        ASSERT_TRUE(out.colorValid);
        for (unsigned c = 0; c < 4; c++)
            EXPECT_NEAR(out.color[c], (selection == 0 ? a[c] : b[c]) * inputs[1][c], 1e-6);
    }
    EXPECT_EQ(samples, 4u);
}

TEST(LattePsTest, RejectsUnsupportedAluInsteadOfExportingZero)
{
    const std::vector<std::uint32_t> words{2, 8u << 26, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u, (0x7Eu << 7) | 16u};
    const auto program = LatteVsInterp::compile(words);
    const auto out = LatteVsInterp::runPixel(*program, {}, nullptr, 0, {});
    EXPECT_FALSE(out.colorValid);
}

TEST(LattePsTest, Fetch4UsesGatherCallbackAndPreservesMaskedDestination)
{
    // Read R0.yx, gather resource 3/sampler 2 into R0, preserving W.
    const std::vector<std::uint32_t> words{2,
                                           1u << 23,
                                           0,
                                           (0x28u << 23) | (1u << 21) | 0x688u,
                                           0x0Fu | (3u << 8),
                                           0xF0000000u | (2u << 9) | (1u << 12) | (7u << 18),
                                           (2u << 15) | (1u << 20) | (4u << 26) | (5u << 29),
                                           0};
    std::array<std::array<float, 4>, 4> inputs{};
    inputs[0] = {0.25f, 0.75f, 0, 0.9f};
    unsigned samples = 0, gathers = 0;
    const LatteVsInterp::TextureSample sample = [&](unsigned, unsigned, const auto &, auto &) {
        ++samples;
        return false;
    };
    const LatteVsInterp::TextureGather gather = [&](unsigned resource, unsigned sampler, const auto &coords, auto &rgba) {
        ++gathers;
        EXPECT_EQ(resource, 3u);
        EXPECT_EQ(sampler, 2u);
        EXPECT_EQ(coords, (std::array<float, 4>{0.75f, 0.25f, 0, 1}));
        TextureSampler state;
        rgba = state.gather(2, 2, coords[0], coords[1], [](unsigned x, unsigned y) { return std::array<float, 4>{float(1 + x + y * 2), 0, 0, 0}; });
        return true;
    };
    const auto out = LatteVsInterp::runPixel(*LatteVsInterp::compile(words), inputs, nullptr, 0, sample, {}, gather);
    ASSERT_TRUE(out.colorValid);
    EXPECT_EQ(out.color, (std::array<float, 4>{1, 3, 4, 0.9f}));
    EXPECT_EQ(samples, 0u);
    EXPECT_EQ(gathers, 1u);
    EXPECT_FALSE(LatteVsInterp::runPixel(*LatteVsInterp::compile(words), inputs, nullptr, 0, sample).colorValid);
    EXPECT_EQ(samples, 0u);
    const LatteVsInterp::TextureGather unavailable = [](unsigned, unsigned, const auto &, auto &) { return false; };
    EXPECT_FALSE(LatteVsInterp::runPixel(*LatteVsInterp::compile(words), inputs, nullptr, 0, {}, {}, unavailable).colorValid);
    for (const auto mutation: {std::pair{4u, 1u << 23}, std::pair{5u, 1u << 7}, std::pair{6u, 1u}}) {
        auto invalid = words;
        invalid[mutation.first] |= mutation.second;
        EXPECT_FALSE(LatteVsInterp::runPixel(*LatteVsInterp::compile(invalid), inputs, nullptr, 0, {}, {}, gather).colorValid);
    }
    EXPECT_EQ(gathers, 1u);
}

TEST(LattePsTest, ScalarLogExponentAndPreviousScalarForwarding)
{
    const auto shader = [](unsigned op) {
        return LatteVsInterp::compile({
                2, (8u << 26) | (1u << 18), 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u, op << 7, // scalar operation, no GPR write
                0x800000FFu, (0x19u << 7) | 16u // MOV R0.x, PS
        });
    };
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    struct Case {
            unsigned op;
            float input, expected;
    };
    const Case cases[] = {{0x61, 0, 1},
                          {0x61, -0.0f, 1},
                          {0x61, 3, 8},
                          {0x61, -2, 0.25f},
                          {0x61, inf, inf},
                          {0x61, -inf, 0},
                          {0x61, nan, nan},
                          {0x62, 1, 0},
                          {0x62, 8, 3},
                          {0x62, 0.25f, -2},
                          {0x62, 0, -std::numeric_limits<float>::max()},
                          {0x62, -0.0f, -std::numeric_limits<float>::max()},
                          {0x62, inf, inf},
                          {0x62, -1, nan},
                          {0x62, nan, nan},
                          {0x63, 1, 0},
                          {0x63, 8, 3},
                          {0x63, 0, -inf},
                          {0x63, -0.0f, -inf},
                          {0x63, -1, nan},
                          {0x63, inf, inf},
                          {0x63, nan, nan}};
    for (const auto &c: cases) {
        std::array<std::array<float, 4>, 4> inputs{};
        inputs[0] = {c.input, 7, 8, 9};
        const auto out = LatteVsInterp::runPixel(*shader(c.op), inputs, nullptr, 0, {});
        ASSERT_TRUE(out.colorValid);
        if (std::isnan(c.expected))
            EXPECT_TRUE(std::isnan(out.color[0]));
        else
            EXPECT_EQ(out.color[0], c.expected);
        EXPECT_EQ(out.color[1], 7);
    }
    // exp2(log2(x) * 0.5), through PS then a GPR, without a title shader fixture.
    const auto chain = LatteVsInterp::compile({2, (8u << 26) | (2u << 18), 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u, 0x62u << 7,
                                               0x800000FFu | (0xFCu << 13), (2u << 7) | 16u, 0x80000000u, (0x61u << 7) | 16u});
    std::array<std::array<float, 4>, 4> inputs{};
    inputs[0][0] = 0.0625f;
    const auto out = LatteVsInterp::runPixel(*chain, inputs, nullptr, 0, {});
    ASSERT_TRUE(out.colorValid);
    EXPECT_NEAR(out.color[0], 0.25f, 1e-6f);
}

TEST(LattePsTest, SparseHighRegistersResetBetweenInvocations)
{
    auto shader = [](unsigned channels) {
        std::vector<std::uint32_t> words{2, (8u << 26) | ((channels - 1) << 18), 127u << 15, (0x28u << 23) | (1u << 21) | 0x688u};
        for (unsigned c = 0; c < channels; ++c) {
            words.push_back((c << 10) | (unsigned(c + 1 == channels) << 31));
            words.push_back((0x19u << 7) | 16u | (127u << 21) | (c << 29)); // MOV R127.c, R0.c
        }
        return LatteVsInterp::compile(words);
    };
    const auto full = shader(4), partial = shader(1);
    const auto uninitialized = LatteVsInterp::compile({63u << 15, (0x28u << 23) | (1u << 21) | 0x688u});
    for (unsigned repeat = 0; repeat < 128; ++repeat) {
        std::array<std::array<float, 4>, 4> inputs{};
        inputs[0] = {float(repeat), 2, 3, 4};
        auto output = LatteVsInterp::runPixel(*full, inputs, nullptr, 0, {});
        ASSERT_TRUE(output.colorValid);
        EXPECT_EQ(output.color, inputs[0]);
        output = LatteVsInterp::runPixel(*partial, inputs, nullptr, 0, {});
        ASSERT_TRUE(output.colorValid);
        EXPECT_EQ(output.color, (std::array<float, 4>{float(repeat), 0, 0, 0}));
        output = LatteVsInterp::runPixel(*uninitialized, inputs, nullptr, 0, {});
        ASSERT_TRUE(output.colorValid);
        EXPECT_EQ(output.color, (std::array<float, 4>{}));
    }
}

TEST(LatteVsTest, VertexTextureSampleUsesSelectorsAndPreservesMaskedChannels)
{
    std::vector<std::uint32_t> program{2,
                                       1u << 23,
                                       60u | (1u << 13) | (1u << 15),
                                       (0x28u << 23) | (1u << 21) | 0x688u,
                                       0x13u | (3u << 8) | (2u << 16),
                                       1u | (2u << 9) | (1u << 12) | (0u << 15) | (7u << 18) | 0x30000000u,
                                       (5u << 15) | (1u << 20) | (0u << 23) | (4u << 26) | (5u << 29),
                                       0};
    std::array<std::array<float, 4>, 4> attributes{};
    attributes[0] = {0, 0, 0, 7};
    attributes[1] = {0.25f, 0.75f, 0, 1};
    unsigned calls = 0;
    const auto sampler = [&](std::uint32_t resource, std::uint32_t samplerIndex, const std::array<float, 4> &coords, std::array<float, 4> &rgba) {
        calls++;
        EXPECT_EQ(resource, 3u);
        EXPECT_EQ(samplerIndex, 5u);
        EXPECT_EQ(coords, (std::array<float, 4>{0.75f, 0.25f, 0, 1}));
        rgba = {0.1f, 0.2f, 0.3f, 1};
        return true;
    };
    auto out = LatteVsInterp::run(program, attributes, nullptr, 0, {}, sampler);
    EXPECT_TRUE(out.valid);
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(out.pos, (std::array<float, 4>{0.3f, 0.2f, 0.1f, 7}));
    EXPECT_FALSE(LatteVsInterp::run(program, attributes, nullptr, 0).valid);
    program[4] = (program[4] & ~31u) | 0x10u; // implicit-LOD SAMPLE is not implemented
    EXPECT_FALSE(LatteVsInterp::run(program, attributes, nullptr, 0, {}, sampler).valid);
}
