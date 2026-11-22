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

TEST(LatteStreamOutTest, ComputePaletteFetchPreservesMasksRawWordsAndChangedInputs)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    auto words = paletteProgram();
    words[4] = 0;
    words[5] = (0x21u << 23) | (1u << 12);
    words[6] = 1u | (2u << 15);
    words[7] = (0x20u << 23) | (1u << 21) | (5u << 12) | 3u;
    const auto program = Latte::decodeProgram(words);
    Latte::UniformBufferWindows windows{};
    windows[1] = 1024;
    const auto shader = Latte::lowerStreamOutShader(*program, {24, 4, 0, 0}, windows);
    ASSERT_TRUE(shader) << shader.error;
    EXPECT_EQ(shader.streamBufferMask, 3u);
    VulkanRasterBackend backend;
    for (unsigned count: {1u, 65u, 131u, 3u}) {
        SCOPED_TRACE(count);
        std::array<std::array<float, 4>, 1024> palette{};
        for (unsigned i = 0; i < palette.size(); ++i)
            palette[i] = {float(i) / 8.f, float(i) / 16.f, -float(i) / 32.f, 1.f};
        for (unsigned pass = 0; pass < 2; ++pass) {
            SCOPED_TRACE(pass);
            if (pass)
                for (auto &value: palette)
                    value[0] += .5f;
            const LatteVsInterp::KcacheFetch constants = [&](unsigned bank, unsigned index, unsigned channel) {
                return bank == 1 && index < palette.size() ? palette[index][channel] : 0.f;
            };
            std::vector<std::array<float, 4>> attributes(count);
            for (unsigned i = 0; i < count; ++i)
                attributes[i][0] = std::bit_cast<float>(200u + i + pass);
            std::vector<float> uniforms;
            for (const auto &ref: shader.uniforms)
                for (unsigned c = 0; c < 4; ++c)
                    uniforms.push_back(constants(ref.bank, ref.index, c));
            std::array<std::vector<std::uint8_t>, 4> buffers;
            buffers[0].assign(count * 24, 0xCD);
            buffers[1].assign(count * 4, 0xAB);
            auto expected = buffers;
            for (unsigned i = 0; i < count; ++i) {
                const auto result = LatteVsInterp::runVertex(*program, std::span(attributes).subspan(i, 1), nullptr, 0, constants, {}, 37 + i);
                ASSERT_TRUE(result.completed);
                for (const auto &write: result.streamWrites)
                    for (unsigned c = 0; c < 4; ++c)
                        if (write.mask & (1u << c)) {
                            const auto offset = i * shader.streamStrides[write.buffer] + (write.offset + c) * 4;
                            const auto bits = std::bit_cast<unsigned>(write.value[c]);
                            for (unsigned b = 0; b < 4; ++b)
                                expected[write.buffer][offset + b] = bits >> (b * 8);
                        }
            }
            StreamOutDraw draw{shader, attributes, count, 37, 1, {}, uniforms, {}};
            for (unsigned i = 0; i < 4; ++i)
                draw.buffers[i] = buffers[i];
            const auto result = backend.streamOut(draw);
            ASSERT_TRUE(result) << backend.lastError();
            EXPECT_EQ(result->usedMask, 3u);
            EXPECT_EQ(result->buffers, expected);
            EXPECT_NE(buffers, expected); // Borrowed outputs remain unchanged.
            draw.buffers[0] = std::span(buffers[0]).first(buffers[0].size() - 1);
            EXPECT_FALSE(backend.streamOut(draw));
            EXPECT_EQ(buffers[0], std::vector<std::uint8_t>(count * 24, 0xCD));
        }
    }
}

TEST(LatteStreamOutTest, DeferredPassesOwnInputsBatchSubmissionsAndOutliveBackend)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    auto words = paletteProgram();
    words[4] = 0;
    words[5] = (0x21u << 23) | (1u << 12);
    words[6] = 1u | (2u << 15);
    words[7] = (0x20u << 23) | (1u << 21) | (5u << 12) | 3u;
    const auto program = Latte::decodeProgram(words);
    const auto shader = Latte::lowerStreamOutShader(*program, {24, 4, 0, 0});
    ASSERT_TRUE(shader) << shader.error;
    auto backend = std::make_unique<VulkanRasterBackend>();
    std::vector<std::shared_ptr<StreamOutReadback>> tickets;
    std::vector<StreamOutResult> expected;
    for (unsigned pass = 0; pass < 24; ++pass) {
        const unsigned count = std::array{1u, 65u, 129u, 2049u}[pass % 4];
        std::vector<std::array<float, 4>> attributes(count);
        for (unsigned i = 0; i < count; ++i)
            attributes[i][0] = std::bit_cast<float>(i % 128);
        const LatteVsInterp::KcacheFetch constants = [pass](unsigned, unsigned index, unsigned channel) {
            return float(pass * 16 + index * 4 + channel) / 8;
        };
        std::vector<float> uniforms;
        for (const auto &ref: shader.uniforms)
            for (unsigned c = 0; c < 4; ++c)
                uniforms.push_back(constants(ref.bank, ref.index, c));
        StreamOutResult reference;
        reference.usedMask = shader.streamBufferMask;
        reference.buffers[0].assign(count * 24, std::uint8_t(0x80 + pass));
        reference.buffers[1].assign(count * 4, 0xAB);
        StreamOutDraw draw{shader, attributes, count, 37 + pass, 1, {}, uniforms, {}};
        for (unsigned i = 0; i < 4; ++i)
            draw.buffers[i] = reference.buffers[i];
        auto ticket = backend->streamOutDeferred(draw);
        ASSERT_TRUE(ticket) << backend->lastError();
        tickets.push_back(std::move(ticket));
        for (unsigned i = 0; i < count; ++i) {
            const auto result = LatteVsInterp::runVertex(*program, std::span(attributes).subspan(i, 1), nullptr, 0, constants, {}, draw.first + i);
            ASSERT_TRUE(result.completed);
            for (const auto &write: result.streamWrites)
                for (unsigned c = 0; c < 4; ++c)
                    if (write.mask & (1u << c)) {
                        const auto offset = i * shader.streamStrides[write.buffer] + (write.offset + c) * 4;
                        const auto bits = std::bit_cast<unsigned>(write.value[c]);
                        for (unsigned b = 0; b < 4; ++b)
                            reference.buffers[write.buffer][offset + b] = bits >> (b * 8);
                    }
        }
        expected.push_back(std::move(reference));
        // Every borrowed vector goes away before any output is resolved.
        std::ranges::fill(attributes, std::array<float, 4>{});
        std::ranges::fill(uniforms, -999.f);
        if (pass == 11) {
            EXPECT_EQ(backend->submissionWaits(), 0u);
            for (unsigned i = unsigned(tickets.size()); i-- > 1;) {
                EXPECT_EQ(tickets[i]->resolve().buffers, expected[i].buffers);
                EXPECT_EQ(tickets[i]->resolve().usedMask, expected[i].usedMask);
            }
            const auto *setting = std::getenv("WEMU_NATIVE_BATCH_SUBMITS");
            if (!setting || std::strtoul(setting, nullptr, 10) > 1)
                EXPECT_LT(backend->submissionWaits(), tickets.size());
            // Later passes reuse these slots, while resolved snapshots stay immutable.
        }
    }
    backend.reset(); // Also drains the last unsubmitted batch; unresolved tickets retain their buffers/context.
    EXPECT_EQ(tickets[0]->resolve().buffers, expected[0].buffers);
    for (unsigned i = 0; i < tickets.size(); ++i) {
        EXPECT_EQ(tickets[i]->resolve().buffers, expected[i].buffers);
        EXPECT_EQ(tickets[i]->resolve().usedMask, expected[i].usedMask);
    }
}

TEST(LatteStreamOutTest, PreservesSpecialFloatBitsAndWrappedSystemIndices)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto program =
            Latte::decodeProgram({1u << 15, (0x20u << 23) | (15u << 12) | 3u, 0, (0x21u << 23) | (1u << 12), 0, (0x22u << 23) | (1u << 21) | 3u});
    const auto shader = Latte::lowerStreamOutShader(*program, {16, 4, 8, 0});
    ASSERT_TRUE(shader) << shader.error;
    std::vector<std::array<float, 4>> attributes(67);
    const std::array<unsigned, 4> bits{0x80000000u, 0x7F800000u, 0x7FC01234u, 0x00000123u};
    for (auto &attribute: attributes)
        for (unsigned c = 0; c < 4; ++c)
            attribute[c] = std::bit_cast<float>(bits[c]);
    std::vector<std::uint8_t> first(attributes.size() * 16, 0xCD), second(attributes.size() * 4, 0xAB), masked(attributes.size() * 8, 0xEF);
    StreamOutDraw draw{
            shader, attributes, unsigned(attributes.size()), 0xFFFFFFF0u, 1, {}, {}, {std::span(first), std::span(second), std::span(masked), {}}};
    VulkanRasterBackend backend;
    const auto result = backend.streamOut(draw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(result->usedMask, 7u);
    EXPECT_EQ(result->buffers[2], masked);
    for (unsigned i = 0; i < attributes.size(); ++i) {
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned b = 0; b < 4; ++b)
                EXPECT_EQ(result->buffers[0][i * 16 + c * 4 + b], std::uint8_t(bits[c] >> (b * 8)));
        for (unsigned b = 0; b < 4; ++b)
            EXPECT_EQ(result->buffers[1][i * 4 + b], std::uint8_t((draw.first + i) >> (b * 8)));
    }
    EXPECT_EQ(first, std::vector<std::uint8_t>(attributes.size() * 16, 0xCD));
    draw.inputCount = 17;
    EXPECT_FALSE(backend.streamOut(draw));
    draw.inputCount = 1;
    draw.count = 0;
    EXPECT_FALSE(backend.streamOut(draw));
}

TEST(LatteStreamOutTest, RejectsUnsafeExportsTextureSamplingAndControlMasks)
{
    using namespace Latte;
    const std::vector<unsigned> words{1u << 15, (0x20u << 23) | (1u << 21) | (5u << 12) | 3u};
    EXPECT_TRUE(lowerStreamOutShader(*decodeProgram(words), {16, 0, 0, 0}));
    EXPECT_FALSE(lowerStreamOutShader(*decodeProgram(words), {8, 0, 0, 0}));
    EXPECT_FALSE(lowerStreamOutShader(*decodeProgram(words), {15, 0, 0, 0}));
    for (unsigned bit: {22u, 23u}) {
        auto invalid = words;
        invalid[0] |= 1u << bit;
        EXPECT_FALSE(lowerStreamOutShader(*decodeProgram(invalid), {16, 0, 0, 0}));
    }
    for (unsigned bit: {2u, 17u}) {
        auto invalid = words;
        invalid[1] |= 1u << bit;
        EXPECT_FALSE(lowerStreamOutShader(*decodeProgram(invalid), {16, 0, 0, 0}));
    }
    const std::vector<unsigned> masked{
            2, 9u << 26, 1u << 15, (0x20u << 23) | (1u << 21) | (5u << 12) | 3u, 0x80000000u | SRC_1, (OP2_MOV << 7) | 16u};
    EXPECT_FALSE(lowerStreamOutShader(*decodeProgram(masked), {16, 0, 0, 0}));
    auto sampled = paletteProgram();
    sampled[12] = (sampled[12] & ~31u) | 0x13u;
    sampled[6] = 2u << 15;
    sampled[7] = (0x20u << 23) | (1u << 21) | (15u << 12) | 3u;
    EXPECT_FALSE(lowerStreamOutShader(*decodeProgram(sampled), {16, 0, 0, 0}));
}

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
