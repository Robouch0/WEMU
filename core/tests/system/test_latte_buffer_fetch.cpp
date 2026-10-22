#include <bit>
#include <cstdlib>
#include <gtest/gtest.h>

#include "gfx/ColorTarget.hpp"
#include "gfx/LatteShaderLowering.hpp"
#include "gfx/LatteVsInterp.hpp"
#include "gfx/UniformBufferFetch.hpp"
#include "gfx/VulkanRasterBackend.hpp"

using namespace Core::Gfx;
namespace {
    std::vector<unsigned> paletteProgram(unsigned clause = Latte::CF_TEX, bool scalarIndex = false)
    {
        using namespace Latte;
        std::vector<unsigned> words{4,
                                    (8u << 26) | (1u << 18),
                                    6,
                                    clause << 23,
                                    60u | (1u << 13) | (2u << 15),
                                    (CF_EXP << 23) | 0x688u,
                                    (2u << 13) | (2u << 15),
                                    (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                    0x80000001u | (253u << 13),
                                    (OP2_MULLO_INT << 7) | 16u,
                                    3,
                                    0,
                                    0x3C008140,
                                    0x002D1002,
                                    0x00080000,
                                    0x0DEADFEC};
        if (scalarIndex) {
            // A vector W move leaves X free. MULLO still executes in T and
            // must publish PS for the following integer palette-row offset.
            words[1] = (8u << 26) | (3u << 18);
            words[2] = 8;
            words.insert(words.begin() + 8, {249u | (248u << 13), (OP2_MOV << 7) | 16u | (3u << 21) | (3u << 29)});
            words.insert(words.begin() + 14, {0x80000000u | SRC_PS | (250u << 13), (OP2_ADD_INT << 7) | 16u});
        }
        return words;
    }
} // namespace

TEST(LatteBufferTest, IndexedPaletteFetchMatchesNativeVertexExecution)
{
    std::array<std::array<float, 4>, 12> palette{};
    for (unsigned index = 0; index < 4; ++index)
        palette[index * 3] = palette[index * 3 + 1] = {-.75f + float(index) * .5f, 0, .25f, 1};
    const LatteVsInterp::KcacheFetch fetch = [&](unsigned bank, unsigned index, unsigned channel) {
        EXPECT_EQ(bank, 1u);
        return index < palette.size() ? palette[index][channel] : 0.f;
    };
    for (unsigned scenario = 0; scenario < 12; ++scenario) {
        const unsigned offset = (scenario / 3) % 2;
        auto words = paletteProgram(1 + scenario % 3, offset != 0);
        if (scenario >= 6)
            words[offset ? 11 : 9] ^= (Latte::OP2_MULLO_INT ^ Latte::OP2_MULLO_UINT) << 7;
        const auto program = Latte::decodeProgram(words);
        ASSERT_TRUE(program->valid);
        auto shader = std::make_shared<const Latte::VertexShader>(Latte::lowerVertexShader(*program));
        ASSERT_TRUE(*shader) << shader->error;
        ASSERT_EQ(shader->uniformBuffers.size(), 1u);
        EXPECT_EQ(shader->uniformBuffers[0].bank, 1u);
        EXPECT_LE(shader->uniforms.size(), 512u);
        std::array<RasterDraw::Vertex, 4> vertices{};
        for (unsigned index = 0; index < 4; ++index) {
            vertices[index].inputs[0][0] = std::bit_cast<float>(index);
            const auto result = LatteVsInterp::run(words, std::span(vertices[index].inputs).first(1), nullptr, 0, fetch);
            ASSERT_TRUE(result.valid);
            EXPECT_EQ(result.pos, palette[index * 3 + offset]);
            EXPECT_EQ(result.params[0], result.pos);
        }
        if (!std::getenv("WEMU_TEST_VULKAN"))
            continue;
        std::vector<float> uniforms;
        for (const auto &ref: shader->uniforms)
            for (unsigned channel = 0; channel < 4; ++channel)
                uniforms.push_back(fetch(ref.bank, ref.index, channel));
        const auto fragment = Latte::lowerFragmentShader(*Latte::decodeProgram({0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
        std::vector<std::uint8_t> target(4 * 16);
        RasterDraw draw{fragment, vertices, {}, {}, target, 4, 1, 4, {0, 0, 4, 1}, 15, {}};
        draw.colorFormat = ColorFormat::RGBA32Float;
        draw.topology = RasterDraw::Topology::Points;
        draw.vertexStage.shader = shader;
        draw.vertexStage.uniforms = uniforms;
        draw.vertexStage.inputCount = 1;
        draw.vertexStage.inputParams[0] = 0;
        draw.vertexStage.viewport = {0, 0, 4, 1, 0, 1};
        VulkanRasterBackend backend;
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        for (unsigned index = 0; index < 4; ++index)
            EXPECT_EQ(readFloatColor(result->rgba.data() + index * 16), palette[index * 3 + offset]);
        palette[offset][2] = .75f;
        for (unsigned slot = 0; slot < shader->uniforms.size(); ++slot)
            for (unsigned channel = 0; channel < 4; ++channel)
                uniforms[slot * 4 + channel] = fetch(shader->uniforms[slot].bank, shader->uniforms[slot].index, channel);
        const auto refreshed = backend.render(draw);
        ASSERT_TRUE(refreshed) << backend.lastError();
        EXPECT_EQ(readFloatColor(refreshed->rgba.data()), palette[offset]);
    }
}

TEST(LatteBufferTest, SizedWindowsReachPaletteRowsBeyondTheDefaultBudget)
{
    const auto program = Latte::decodeProgram(paletteProgram());
    const auto shared = Latte::lowerVertexShader(*program);
    ASSERT_TRUE(shared) << shared.error;
    ASSERT_EQ(shared.uniformBuffers.size(), 1u);
    EXPECT_LT(shared.uniformBuffers[0].count, 610u);
    EXPECT_FALSE(Latte::lowerVertexShader(*program, {}, Latte::kUniformVectors + 64));
    auto shader = std::make_shared<const Latte::VertexShader>(Latte::lowerVertexShader(*program, {}, 640));
    ASSERT_TRUE(*shader) << shader->error;
    ASSERT_EQ(shader->uniformBuffers.size(), 1u);
    EXPECT_EQ(shader->uniformBuffers[0].count, 640u);
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP() << "Set WEMU_TEST_VULKAN=1";
    // The program reads palette row 3 * index: indices 200..203 select rows 600..609.
    std::array<std::array<float, 4>, 640> palette{};
    for (unsigned index = 0; index < 4; ++index)
        palette[(200 + index) * 3] = {-.75f + float(index) * .5f, 0, .25f, 1};
    std::array<RasterDraw::Vertex, 4> vertices{};
    for (unsigned index = 0; index < 4; ++index)
        vertices[index].inputs[0][0] = std::bit_cast<float>(200 + index);
    std::vector<float> uniforms;
    for (const auto &ref: shader->uniforms)
        for (unsigned channel = 0; channel < 4; ++channel)
            uniforms.push_back(ref.bank == 1 && ref.index < palette.size() ? palette[ref.index][channel] : 0.f);
    const auto fragment = Latte::lowerFragmentShader(*Latte::decodeProgram({0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    std::vector<std::uint8_t> target(4 * 16);
    RasterDraw draw{fragment, vertices, {}, {}, target, 4, 1, 4, {0, 0, 4, 1}, 15, {}};
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.topology = RasterDraw::Topology::Points;
    draw.vertexStage.shader = shader;
    draw.vertexStage.uniforms = uniforms;
    draw.vertexStage.inputCount = 1;
    draw.vertexStage.inputParams[0] = 0;
    draw.vertexStage.viewport = {0, 0, 4, 1, 0, 1};
    VulkanRasterBackend backend;
    const auto result = backend.render(draw);
    ASSERT_TRUE(result) << backend.lastError();
    for (unsigned index = 0; index < 4; ++index)
        EXPECT_EQ(readFloatColor(result->rgba.data() + index * 16), palette[(200 + index) * 3]) << index;
}

TEST(LatteBufferTest, IndependentlySizedWindowsPreserveUnequalPalettesAndCacheIdentity)
{
    using namespace Latte;
    const auto fetchWord = [](unsigned bank, unsigned channel) {
        return (2u << 5) | ((128u + bank) << 8) | (1u << 16) | (channel << 24) | (15u << 26);
    };
    const std::vector<unsigned> words{
            4u | (3u << 22) | (1u << 30),
            8u << 26,
            5,
            (CF_VTX << 23) | (1u << 10),
            60u | (1u << 13) | (2u << 15),
            (CF_EXP << 23) | 0x688u,
            (2u << 13) | (3u << 15),
            (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
            0x80000080u,
            (OP2_MOV << 7) | 16u,
            fetchWord(10, 0),
            0x002D1002,
            0x00080000,
            0,
            fetchWord(11, 1),
            0x002D1003,
            0x00080000,
            0,
    };
    const auto program = decodeProgram(words);
    ASSERT_TRUE(program->valid);
    EXPECT_FALSE(lowerVertexShader(*program, {}, 1024)); // Two maximum-sized windows plus a static reference exceed the budget.
    UniformBufferWindows windows{};
    windows[10] = 384;
    windows[11] = 1024;
    VertexShaderCache cache;
    const auto shader = cache.get(words, 0, windows);
    ASSERT_TRUE(*shader) << shader->error;
    EXPECT_EQ(cache.get(words, 0, windows), shader);
    ASSERT_EQ(shader->uniformBuffers.size(), 2u);
    EXPECT_EQ(shader->uniformBuffers[0].count, 384u);
    EXPECT_EQ(shader->uniformBuffers[1].count, 1024u);
    EXPECT_EQ(shader->uniforms.size(), 1409u);
    auto different = windows;
    different[11] = 960;
    EXPECT_NE(cache.get(words, 0, different), shader);
    different[11] = kUniformVectors;
    EXPECT_FALSE(lowerVertexShader(*program, {}, 0, different));
    const std::array<float, 4> position{-.5f, 0, .25f, 1}, color{.25f, .5f, .75f, 1};
    const LatteVsInterp::KcacheFetch fetch = [&](unsigned bank, unsigned index, unsigned channel) {
        return bank == 10 && index == 383 ? position[channel] : bank == 11 && index == 1023 ? color[channel] : 0.f;
    };
    std::array<RasterDraw::Vertex, 1> vertices{};
    vertices[0].inputs[0] = {std::bit_cast<float>(383u), std::bit_cast<float>(1023u), 0, 0};
    const auto reference = LatteVsInterp::runVertex(*program, std::span(vertices[0].inputs).first(1), nullptr, 0, fetch);
    ASSERT_TRUE(reference.valid);
    EXPECT_EQ(reference.pos, position);
    EXPECT_EQ(reference.params[0], color);
    if (!std::getenv("WEMU_TEST_VULKAN"))
        return;
    std::vector<float> uniforms;
    for (const auto &ref: shader->uniforms)
        for (unsigned channel = 0; channel < 4; ++channel)
            uniforms.push_back(fetch(ref.bank, ref.index, channel));
    const auto fragment = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    std::vector<std::uint8_t> target(2 * 16);
    RasterDraw draw{fragment, vertices, {}, {}, target, 2, 1, 2, {0, 0, 2, 1}, 15, {}};
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.topology = RasterDraw::Topology::Points;
    draw.vertexStage = {shader, {}, uniforms, {0, -1, -1, -1}, {0, 0, 2, 1, 0, 1}};
    VulkanRasterBackend backend;
    const auto result = backend.render(draw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(readFloatColor(result->rgba.data()), color);
}

TEST(LatteBufferTest, UniformFetchRejectsUnsupportedFlagsAndPreservesMaskedChannels)
{
    auto words = paletteProgram();
    const std::array<std::array<float, 4>, 1> attrs{{{std::bit_cast<float>(1u), 0, 0, 0}}};
    unsigned calls = 0;
    const LatteVsInterp::KcacheFetch fetch = [&](unsigned bank, unsigned index, unsigned channel) {
        ++calls;
        EXPECT_EQ(bank, 1u);
        EXPECT_EQ(index, 4u); // index*3 plus one aligned byte offset
        return float(channel + 1);
    };
    words[14] |= 16;
    words[13] = 2u | (1u << 21) | (2u << 9) | (1u << 12) | (0u << 15) | (7u << 18);
    const auto result = LatteVsInterp::run(words, attrs, nullptr, 0, fetch);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.pos, (std::array<float, 4>{3, 2, 1, 0}));
    EXPECT_EQ(calls, 4u);
    for (const auto [word, bit]: {std::pair{12u, 1u << 23}, std::pair{13u, 1u << 7}, std::pair{14u, 1u << 16}, std::pair{14u, 1u}}) {
        auto invalid = words;
        invalid[word] ^= bit;
        EXPECT_FALSE(Latte::lowerVertexShader(*Latte::decodeProgram(invalid)));
        EXPECT_FALSE(LatteVsInterp::run(invalid, attrs, nullptr, 0, fetch).valid);
    }
}

TEST(VulkanRasterTest, GuestCullModesMatchFramebufferWindingAndSeparatePipelines)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto shader = Latte::lowerFragmentShader(*Latte::decodeProgram({0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader);
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {2, 0, {}}, {0, 2, {}}}};
    for (auto &vertex: vertices)
        vertex.inputs[0] = {1, 0, 0, 1};
    std::vector<std::uint8_t> target(16);
    RasterDraw draw{shader, vertices, {}, {}, target, 2, 2, 2, {0, 0, 2, 2}, 15, {}};
    VulkanRasterBackend backend;
    for (bool reverse: {false, true}) {
        if (reverse)
            std::swap(vertices[1], vertices[2]);
        for (bool clockwise: {false, true})
            for (unsigned cull = 0; cull < 4; ++cull) {
                SCOPED_TRACE(reverse);
                SCOPED_TRACE(clockwise);
                SCOPED_TRACE(cull);
                draw.frontFaceClockwise = clockwise;
                draw.cullFront = cull & 1;
                draw.cullBack = cull & 2;
                const auto result = backend.render(draw);
                ASSERT_TRUE(result) << backend.lastError();
                const bool front = !reverse == clockwise;
                EXPECT_EQ(result->rgba[0], (cull & (front ? 1u : 2u)) ? 0 : 255);
            }
    }
}

TEST(VulkanRasterTest, CulledFullscreenTrianglesPreservePreviousTarget)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto shader = Latte::lowerFragmentShader(*Latte::decodeProgram({0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader);
    const std::array<std::array<float, 2>, 6> xy{{{0, 0}, {2, 0}, {2, 2}, {0, 0}, {2, 2}, {0, 2}}};
    std::array<RasterDraw::Vertex, 6> vertices{};
    for (unsigned index = 0; index < 6; ++index) {
        vertices[index].x = xy[index][0];
        vertices[index].y = xy[index][1];
        vertices[index].inputs[0] = {1, 0, 0, 1};
    }
    const std::vector<std::uint8_t> target{0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};
    RasterDraw draw{shader, vertices, {}, {}, target, 2, 2, 2, {0, 0, 2, 2}, 15, {}};
    VulkanRasterBackend backend;
    for (unsigned cull: {3u, 2u, 0u, 1u, 3u}) {
        draw.cullFront = cull & 1;
        draw.cullBack = cull & 2;
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        if (draw.cullBack)
            EXPECT_EQ(result->rgba, target);
        else
            for (unsigned pixel = 0; pixel < 4; ++pixel) {
                EXPECT_EQ(result->rgba[pixel * 4], 255u);
                EXPECT_EQ(result->rgba[pixel * 4 + 1], 0u);
            }
    }
}

TEST(LatteBufferTest, LowIntegerMultiplyWrapsWithoutSignedOverflow)
{
    for (const auto op: {Latte::OP2_MULLO_INT, Latte::OP2_MULLO_UINT}) {
        const std::vector<unsigned> words{
                2, 8u << 26, 0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u | (1u << 13), (op << 7) | 16u};
        const auto program = Latte::decodeProgram(words);
        ASSERT_TRUE(program->valid);
        EXPECT_TRUE(Latte::lowerFragmentShader(*program));
        for (const auto [a, b]:
             {std::pair{0xFFFFFFFFu, 3u}, std::pair{0x80000000u, 2u}, std::pair{0xFFFFFFFFu, 0xFFFFFFFFu}, std::pair{123456u, 654321u}}) {
            const std::array<std::array<float, 4>, 2> inputs{{{std::bit_cast<float>(a), 0, 0, 0}, {std::bit_cast<float>(b), 0, 0, 0}}};
            const auto output = LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
            ASSERT_TRUE(output.colorValid);
            EXPECT_EQ(std::bit_cast<unsigned>(output.color[0]), unsigned(std::uint64_t(a) * b));
        }
    }
}
