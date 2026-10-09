#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>

#include "VulkanFragmentProbe.hpp"
#include "gfx/LatteShaderLowering.hpp"
#include "gfx/LatteVsInterp.hpp"
#include "gfx/SpirvCompiler.hpp"
#include "gfx/TextureSampler.hpp"
#include "gfx/VulkanRasterBackend.hpp"

using namespace Core::Gfx::Latte;

TEST(LatteLoweringTest, UnsupportedExportsIdentifyTheirEncoding)
{
    const auto shader = lowerFragmentShader(*decodeProgram({61, (0x28u << 23) | (1u << 21) | 0x688u}));
    EXPECT_FALSE(shader);
    EXPECT_NE(shader.error.find("cf=0 type=0 base=61 repeated=false"), std::string::npos);
    EXPECT_NE(shader.error.find("words=0000003D,14200688"), std::string::npos);
}

TEST(LatteLoweringCacheTest, ReusesExactProgramsAndNormalizesDefaultTypes)
{
    FragmentShaderCache cache;
    const std::vector<std::uint32_t> words{0, (0x28u << 23) | (1u << 21) | 0x688u};
    const auto first = cache.get(words);
    ASSERT_TRUE(*first) << first->error;
    const std::vector<TextureType> defaults(16, TextureType::TwoD);
    EXPECT_EQ(first, cache.get(words, defaults));
    auto types = defaults;
    types[0] = TextureType::TwoDArray;
    const auto specialized = cache.get(words, types);
    EXPECT_NE(first, specialized);
    types.resize(1);
    EXPECT_EQ(specialized, cache.get(words, types));
    auto changed = words;
    changed[0] = 1;
    EXPECT_NE(first, cache.get(changed));
    EXPECT_EQ(cache.hits(), 2u);
    EXPECT_EQ(cache.misses(), 3u);
}

TEST(LatteLoweringCacheTest, CachesFailuresAndKeepsResultsAliveAfterEviction)
{
    FragmentShaderCache cache;
    const auto failed = cache.get({});
    EXPECT_FALSE(*failed);
    EXPECT_EQ(failed, cache.get({}));
    const auto error = failed->error;
    for (std::uint32_t i = 0; i < 256; ++i) {
        EXPECT_FALSE(*cache.get({i}));
        EXPECT_LE(cache.size(), 128u);
    }
    EXPECT_EQ(failed->error, error);
    EXPECT_NE(failed, cache.get({}));
    EXPECT_EQ(cache.hits(), 1u);
}

namespace {
    constexpr auto probeVertexSource = R"(#version 450
layout(push_constant) uniform Parameters { vec4 value[4]; } p;
layout(location=0) out vec4 inputs[4];
void main() {
    vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    for(int i=0;i<4;++i) inputs[i]=p.value[i];
}
)";

    void validateSpirv(const FragmentShader &shader, std::vector<std::uint32_t> *code = nullptr, const char *stage = "frag")
    {
        if (!Core::Gfx::SpirvCompiler::available())
            GTEST_SKIP() << "glslangValidator and spirv-val are required for SPIR-V validation";
        static Core::Gfx::SpirvCompiler compiler;
        const auto module = compiler.compile(std::string_view(stage) == "vert" ? Core::Gfx::ShaderStage::Vertex : Core::Gfx::ShaderStage::Fragment,
                                             shader.source);
        ASSERT_TRUE(*module) << module->error;
        if (code)
            *code = module->words;
    }

    void compareNativeControlFlow(const Program &program, const VulkanFragmentProbe::Inputs &inputs, const std::array<float, 4> &expected)
    {
        const auto fragment = lowerFragmentShader(program);
        ASSERT_TRUE(fragment) << fragment.error;
        if (!std::getenv("WEMU_TEST_VULKAN"))
            return;
        FragmentShader vertex;
        vertex.source = probeVertexSource;
        std::vector<std::uint32_t> vs, ps;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
        ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
        ASSERT_FALSE(vs.empty());
        ASSERT_FALSE(ps.empty());
        VulkanFragmentProbe probe;
        probe.initialize(vs, ps, {});
        for (const auto &pixel: probe.draw(inputs))
            EXPECT_EQ(pixel, expected);
    }

    std::vector<std::uint32_t> forwardingProgram()
    {
        return {2,
                (8u << 26) | (3u << 18),
                0,
                (0x28u << 23) | (1u << 21) | 0x688u,
                0x80000000u,
                0x62u << 7, // LOG_CLAMPED PS, R0.x
                0x800000FFu | (0xFCu << 13),
                (2u << 7) | 16u, // MUL R0.x, PS, 0.5
                0x80000000u,
                (0x61u << 7) | 16u, // EXP R0.x, R0.x
                0x800000FDu,
                (0x19u << 7) | 16u | (1u << 29), // literal R0.y
                std::bit_cast<std::uint32_t>(0.75f),
                0};
    }
} // namespace

TEST(SpirvCompilerTest, RejectsInvalidRequestsWithoutTools)
{
    Core::Gfx::SpirvCompiler compiler;
    for (const auto &source: {std::string{}, std::string("abc\0def", 7), std::string(4 * 1024 * 1024 + 1, ' ')}) {
        const auto module = compiler.compile(Core::Gfx::ShaderStage::Fragment, source);
        EXPECT_FALSE(*module);
        EXPECT_FALSE(module->error.empty());
        EXPECT_TRUE(module->words.empty());
    }
    EXPECT_FALSE(*compiler.compile(static_cast<Core::Gfx::ShaderStage>(100), "void main(){}"));
    EXPECT_EQ(compiler.cachedModules(), 0u);
}

TEST(SpirvCompilerTest, CachesValidatedModulesBySourceAndStageAndRecoversAfterFailure)
{
    using namespace Core::Gfx;
    if (!SpirvCompiler::available())
        GTEST_SKIP() << "Shader tools unavailable";
    SpirvCompiler compiler;
    const std::string source = "#version 450\nvoid main() {}\n";
    const auto vertex = compiler.compile(ShaderStage::Vertex, source);
    ASSERT_TRUE(*vertex) << vertex->error;
    EXPECT_EQ(compiler.compile(ShaderStage::Vertex, source), vertex);
    const auto fragment = compiler.compile(ShaderStage::Fragment, source);
    ASSERT_TRUE(*fragment) << fragment->error;
    EXPECT_NE(fragment, vertex);
    EXPECT_NE(fragment->words, vertex->words);
    EXPECT_EQ(compiler.cachedModules(), 2u);
    const auto failure = compiler.compile(ShaderStage::Fragment, "#version 450\nthis is not GLSL;");
    EXPECT_FALSE(*failure);
    EXPECT_FALSE(failure->error.empty());
    EXPECT_TRUE(failure->words.empty());
    EXPECT_EQ(compiler.cachedModules(), 2u);
    const auto changed = compiler.compile(ShaderStage::Fragment, source + "// changed\n");
    ASSERT_TRUE(*changed) << changed->error;
    EXPECT_NE(changed, fragment);
    EXPECT_EQ(compiler.cachedModules(), 3u);
}

TEST(VulkanRasterBackendTest, ArrayTexturesSelectLayersAndRefreshPaddedSnapshots)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto program =
            decodeProgram({2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9), (1u << 23) | (2u << 26), 0});
    const std::array types{TextureType::TwoDArray};
    const auto shader = lowerFragmentShader(*program, types);
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.textures.size(), 1u);
    EXPECT_EQ(shader.textures[0].type, TextureType::TwoDArray);
    const auto flat = lowerFragmentShader(*program);
    EXPECT_NE(flat.source, shader.source);
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    std::vector<std::uint8_t> target(4 * 4 * 4);
    // Two rows per layer, one active texel and one padding texel per row.
    std::vector<std::uint8_t> pixels(3 * 2 * 2 * 4, 177);
    const auto fillLayer = [&](unsigned layer, std::array<std::uint8_t, 4> color) {
        for (unsigned y = 0; y < 2; ++y)
            std::copy(color.begin(), color.end(), pixels.begin() + (layer * 4 + y * 2) * 4);
    };
    fillLayer(0, {255, 0, 0, 255});
    fillLayer(1, {0, 255, 0, 255});
    fillLayer(2, {0, 0, 255, 255});
    RasterDraw::Texture texture{shader.textures[0], 1, 2, {}, [](unsigned, unsigned) { return std::array{0.f, 0.f, 0.f, 0.f}; }};
    texture.unorm8 = pixels;
    texture.unorm8Pitch = 2;
    texture.layers = 3;
    RasterDraw draw{shader, vertices, {}, {&texture, 1}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend;
    for (unsigned filter: {0u, 1u}) {
        texture.sampler.regs[0] = 2u | (2u << 3) | (filter << 9);
        for (float layer: {-2.f, 0.f, 0.49f, std::nextafter(0.5f, 0.f), 0.5f, 0.51f, 1.f, 1.49f, 1.5f, 1.51f, 2.f, 8.f}) {
            SCOPED_TRACE(layer);
            for (auto &vertex: vertices)
                vertex.inputs[0] = {0.5f, 0.5f, layer, 0};
            const auto result = backend.render(draw);
            ASSERT_TRUE(result) << backend.lastError();
            const auto index = unsigned(std::clamp(std::floor(double(layer) + 0.5), 0.0, 2.0));
            for (unsigned c = 0; c < 4; ++c)
                EXPECT_EQ(result->rgba[c], c == index || c == 3 ? 255 : 0);
        }
    }
    fillLayer(2, {43, 87, 129, 255});
    auto refreshed = backend.render(draw);
    ASSERT_TRUE(refreshed) << backend.lastError();
    EXPECT_EQ(refreshed->rgba[0], 43);
    EXPECT_EQ(refreshed->rgba[1], 87);
    EXPECT_EQ(refreshed->rgba[2], 129);
    texture.layers = 1; // An array view must remain an array even with one layer.
    auto single = backend.render(draw);
    ASSERT_TRUE(single) << backend.lastError();
    EXPECT_EQ(single->rgba[0], 255);
    texture.layers = 0;
    EXPECT_FALSE(backend.render(draw));
    texture.layers = 4; // Snapshot lacks the fourth layer.
    EXPECT_FALSE(backend.render(draw));
    texture.layers = 3;
    texture.binding.type = TextureType::TwoD;
    EXPECT_FALSE(backend.render(draw));
    texture.binding.type = TextureType::TwoDArray;
    texture.width = 2;
    texture.layers = 2;
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned c = 0; c < 4; ++c)
            pixels[i * 4 + c] = (i * 37 + c * 61) % 256;
    for (unsigned filter: {0u, 1u})
        for (unsigned mode = 0; mode < 3; ++mode) {
            texture.sampler.regs[0] = mode | (((mode + 1) % 3) << 3) | (filter << 9);
            for (float u: {-1.23f, -0.01f, 0.f, 0.2371f, 0.5f, 0.7913f, 1.f, 1.37f}) {
                const float v = 0.371f - u;
                for (auto &vertex: vertices)
                    vertex.inputs[0] = {u, v, 1, 0};
                const auto expected = texture.sampler.sample(2, 2, u, v, [&](unsigned x, unsigned y) {
                    std::array<float, 4> color;
                    for (unsigned c = 0; c < 4; ++c)
                        color[c] = pixels[(4 + y * 2 + x) * 4 + c] / 255.f;
                    return color;
                });
                const auto sampled = backend.render(draw);
                ASSERT_TRUE(sampled) << backend.lastError();
                for (unsigned c = 0; c < 4; ++c)
                    EXPECT_NEAR(sampled->rgba[c], std::round(expected[c] * 255.f), 1) << u << "," << v;
            }
        }
    texture.sampler.regs[0] = 3u << 22;
    texture.sampler.customBorder = {0.2f, 0.4f, 0.6f, 0.8f};
    for (unsigned coordinate = 0; coordinate < 3; ++coordinate)
        for (float invalid:
             {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
            for (auto &vertex: vertices) {
                vertex.inputs[0] = {0.5f, 0.5f, 1, 0};
                vertex.inputs[0][coordinate] = invalid;
            }
            const auto sampled = backend.render(draw);
            ASSERT_TRUE(sampled) << backend.lastError();
            for (unsigned c = 0; c < 4; ++c)
                EXPECT_EQ(sampled->rgba[c], (c + 1) * 51);
        }
}

TEST(RasterReadbackTest, MaterializesOnceAndRejectsIncompleteResults)
{
    unsigned calls = 0;
    Core::Gfx::RasterReadback readback(4, [&] {
        ++calls;
        return std::vector<std::uint8_t>{1, 2, 3, 4};
    });
    EXPECT_EQ(readback.size(), 4u);
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(readback.resolve(), (std::vector<std::uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(readback.resolve(), (std::vector<std::uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(calls, 1u);
    unsigned failures = 0;
    Core::Gfx::RasterReadback incomplete(4, [&] {
        ++failures;
        return std::vector<std::uint8_t>{1};
    });
    EXPECT_THROW(incomplete.resolve(), std::runtime_error);
    EXPECT_THROW(incomplete.resolve(), std::runtime_error);
    EXPECT_EQ(failures, 1u);
}

TEST(VulkanRasterBackendTest, DeferredReadbackOwnsInputsAndSurvivesReuseAndBackendDestruction)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto checkLayout = [](unsigned pitch) {
        SCOPED_TRACE(pitch);
        std::optional<RasterResult> survivor;
        std::vector<std::uint8_t> expected;
        {
            const auto shader = lowerFragmentShader(
                    *decodeProgram({2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9), 1u << 23, 0}));
            ASSERT_TRUE(shader) << shader.error;
            std::vector<RasterDraw::Vertex> vertices;
            for (const auto xy: {std::array{0.f, 0.f}, std::array{4.f, 0.f}, std::array{0.f, 4.f}}) {
                RasterDraw::Vertex vertex{xy[0], xy[1], {}};
                vertex.inputs[0] = {0, 0, 0, 1};
                vertices.push_back(vertex);
            }
            std::vector<std::uint8_t> target(pitch * 4 * 4, 123);
            RasterDraw::Texture texture{shader.textures[0], 1, 1, {}, [](unsigned, unsigned) { return std::array{1.f, 0.f, 0.f, 1.f}; }};
            RasterDraw draw{shader, vertices, {}, {&texture, 1}, target, 4, 4, pitch, {0, 0, 4, 4}, 15, {}};
            VulkanRasterBackend backend(true, true);
            auto reference = backend.render(draw);
            ASSERT_TRUE(reference) << backend.lastError();
            auto first = backend.renderDeferred(draw);
            ASSERT_TRUE(first) << backend.lastError();
            ASSERT_TRUE(first->readback);
            EXPECT_TRUE(first->rgba.empty());
            std::fill(target.begin(), target.end(), 77);
            survivor = backend.renderDeferred(draw); // reuse must preserve the first result
            ASSERT_TRUE(survivor) << backend.lastError();
            first->resolve();
            EXPECT_EQ(first->rgba, reference->rgba);
            VulkanRasterBackend other;
            auto secondReference = other.render(draw);
            ASSERT_TRUE(secondReference) << other.lastError();
            expected = secondReference->rgba;
            std::vector<RasterResult> queued;
            std::vector<std::vector<std::uint8_t>> references;
            for (unsigned i = 0; i < 34; ++i) {
                draw.channelMask = i % 16;
                draw.scissor = {int(i / 16), 0, 4, 4};
                auto referenceDraw = other.render(draw);
                ASSERT_TRUE(referenceDraw) << other.lastError();
                references.push_back(referenceDraw->rgba);
                auto pending = backend.renderDeferred(draw);
                ASSERT_TRUE(pending) << backend.lastError();
                queued.push_back(std::move(*pending));
            }
            // More than thirty-two resource keys forces eviction while tickets remain live.
            for (std::size_t i = queued.size(); i-- > 0;) {
                queued[i].resolve();
                EXPECT_EQ(queued[i].rgba, references[i]);
            }
            draw.channelMask = 15;
            draw.scissor = {0, 0, 4, 4};
            // Discarding the result must not permit staging/command reuse while the
            // submitted draw is still pending, even though no ticket remains alive.
            for (unsigned i = 0; i < 8; ++i) {
                const auto discarded = backend.renderDeferred(draw);
                ASSERT_TRUE(discarded) << backend.lastError();
            }
            survivor = backend.renderDeferred(draw);
            ASSERT_TRUE(survivor) << backend.lastError();
        }
        ASSERT_TRUE(survivor->readback);
        survivor->resolve();
        EXPECT_EQ(survivor->rgba, expected);
        survivor->resolve();
        EXPECT_EQ(survivor->rgba, expected);
    };
    checkLayout(4);
    checkLayout(6);
}

TEST(VulkanRasterBackendTest, RenderedTexturesUseGpuVersionsAndMaterializeStaleOrForeignImages)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(
            *decodeProgram({2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9), 1u << 23, 0}));
    ASSERT_TRUE(shader);
    std::vector<RasterDraw::Vertex> vertices;
    for (const auto xy: {std::array{0.f, 0.f}, std::array{4.f, 0.f}, std::array{0.f, 4.f}}) {
        RasterDraw::Vertex vertex{xy[0], xy[1], {}};
        vertex.inputs[0] = {0, 0, 0, 1};
        vertices.push_back(vertex);
    }
    std::vector<std::uint8_t> target(6 * 4 * 4, 123);
    RasterDraw::Texture producer{shader.textures[0], 1, 1, {}, [](unsigned, unsigned) { return std::array{1.f, 0.f, 0.f, 1.f}; }};
    RasterDraw draw{shader, vertices, {}, {&producer, 1}, target, 4, 4, 6, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend(true, true);
    auto reference = backend.render(draw);
    ASSERT_TRUE(reference);
    auto first = backend.renderDeferred(draw);
    ASSERT_TRUE(first && first->readback && first->readback->image());
    unsigned materializations = 0;
    auto original = first->readback;
    auto wrapped = std::make_shared<RasterReadback>(
            original->size(),
            [&] {
                ++materializations;
                return original->resolve();
            },
            original->image());
    RasterDraw::Texture consumer{shader.textures[0], 4, 4, {}, [](unsigned, unsigned) -> std::array<float, 4> {
                                     throw std::runtime_error("Unexpected rendered texture callback");
                                 }};
    consumer.unorm8Pitch = 6;
    consumer.rendered = wrapped;
    draw.textures = {&consumer, 1};
    auto pending = backend.renderDeferred(draw);
    ASSERT_TRUE(pending);
    EXPECT_EQ(materializations, 0u);
    EXPECT_EQ(backend.residentTextureCopies(), 1u);
    pending->resolve();
    EXPECT_EQ(pending->rgba, reference->rgba);

    // Reusing the producer bundle overwrites its GPU image but preserves old tickets.
    producer.texel = [](unsigned, unsigned) { return std::array{0.f, 1.f, 0.f, 1.f}; };
    draw.textures = {&producer, 1};
    auto newer = backend.renderDeferred(draw);
    ASSERT_TRUE(newer);
    draw.textures = {&consumer, 1};
    auto stale = backend.render(draw);
    ASSERT_TRUE(stale);
    EXPECT_EQ(materializations, 1u);
    EXPECT_EQ(backend.residentTextureCopies(), 1u);
    EXPECT_EQ(stale->rgba, reference->rgba);

    consumer.rendered = newer->readback;
    auto current = backend.render(draw);
    ASSERT_TRUE(current);
    EXPECT_EQ(backend.residentTextureCopies(), 2u);
    EXPECT_EQ(current->rgba[0], 0);
    EXPECT_EQ(current->rgba[1], 255);
    // A different device/context must use the CPU snapshot, never a foreign VkImage.
    VulkanRasterBackend foreign;
    auto imported = foreign.render(draw);
    ASSERT_TRUE(imported);
    EXPECT_EQ(imported->rgba, current->rgba);
    EXPECT_EQ(foreign.residentTextureCopies(), 0u);
    // CPU upload after a GPU copy must not compare against obsolete staging bytes.
    consumer.rendered.reset();
    consumer.unorm8 = original->resolve();
    auto cpuUploaded = backend.render(draw);
    ASSERT_TRUE(cpuUploaded);
    EXPECT_EQ(cpuUploaded->rgba, reference->rgba);
    auto chained = backend.renderDeferred(draw);
    ASSERT_TRUE(chained && chained->readback);
    consumer.unorm8 = {};
    const auto copiesBeforeChain = backend.residentTextureCopies();
    for (unsigned i = 0; i < 8; ++i) {
        consumer.rendered = chained->readback;
        chained = backend.renderDeferred(draw); // source and destination share a bundle
        ASSERT_TRUE(chained && chained->readback);
    }
    chained->resolve();
    EXPECT_EQ(chained->rgba, reference->rgba);
    EXPECT_EQ(backend.residentTextureCopies(), copiesBeforeChain + 8);
}

TEST(VulkanRasterBackendTest, ResidentTargetsPreserveUntouchedPixelsAndFallbackSnapshots)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(
            *decodeProgram({2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9), 1u << 23, 0}));
    ASSERT_TRUE(shader);
    std::vector<RasterDraw::Vertex> vertices;
    for (const auto xy: {std::array{0.f, 0.f}, std::array{4.f, 0.f}, std::array{0.f, 4.f}}) {
        RasterDraw::Vertex vertex{xy[0], xy[1], {}};
        vertex.inputs[0] = {0, 0, 0, 1};
        vertices.push_back(vertex);
    }
    std::vector<std::uint8_t> target(4 * 4 * 4, 17);
    RasterDraw::Texture texture{shader.textures[0], 1, 1, {}, [](unsigned, unsigned) { return std::array{1.f, 0.f, 0.f, 1.f}; }};
    RasterDraw draw{shader, vertices, {}, {&texture, 1}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend(true, true), referenceBackend;
    const auto firstReference = referenceBackend.render(draw);
    ASSERT_TRUE(firstReference);
    auto first = backend.renderDeferred(draw);
    ASSERT_TRUE(first && first->readback);
    EXPECT_EQ(backend.readbackTransfers(), 0u);
    auto original = first->readback;
    unsigned reads = 0;
    auto wrapped = std::make_shared<RasterReadback>(
            original->size(),
            [&] {
                ++reads;
                return original->resolve();
            },
            original->image());
    texture.texel = [](unsigned, unsigned) { return std::array{0.f, 1.f, 0.f, 1.f}; };
    draw.scissor = {0, 0, 1, 1};
    draw.channelMask = 2;
    auto referenceDraw = draw;
    referenceDraw.target = firstReference->rgba;
    const auto expected = referenceBackend.render(referenceDraw);
    ASSERT_TRUE(expected);
    std::fill(target.begin(), target.end(), 99); // Must not replace the rendered pre-draw target.
    draw.renderedTarget = wrapped;
    auto chained = backend.renderDeferred(draw);
    ASSERT_TRUE(chained);
    EXPECT_EQ(reads, 0u);
    EXPECT_EQ(backend.residentTargetCopies(), 1u);
    EXPECT_EQ(backend.readbackTransfers(), 0u);
    for (unsigned i = 0; i < 8; ++i) {
        draw.renderedTarget = chained->readback;
        chained = backend.renderDeferred(draw);
        ASSERT_TRUE(chained && chained->readback);
    }
    EXPECT_EQ(backend.readbackTransfers(), 0u);
    chained->resolve();
    EXPECT_EQ(backend.readbackTransfers(), 1u);
    chained->resolve();
    EXPECT_EQ(backend.readbackTransfers(), 1u);
    EXPECT_EQ(chained->rgba, expected->rgba);
    EXPECT_EQ(backend.residentTargetCopies(), 9u);
    EXPECT_EQ(reads, 0u);

    draw.renderedTarget = wrapped;
    auto foreign = referenceBackend.render(draw);
    ASSERT_TRUE(foreign);
    EXPECT_EQ(foreign->rgba, expected->rgba);
    EXPECT_EQ(reads, 1u);
    draw.channelMask = 0;
    auto noOp = backend.render(draw);
    ASSERT_TRUE(noOp);
    EXPECT_EQ(noOp->rgba, firstReference->rgba);
    draw.renderedTarget.reset();
    draw.channelMask = 15;
    draw.scissor = {0, 0, 4, 4};
    draw.pitch = 6;
    target.assign(6 * 4 * 4, 123);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 16; x < 24; ++x)
            target[y * 24 + x] = std::uint8_t(y * 24 + x);
    draw.target = target;
    const auto paddedReference = referenceBackend.render(draw);
    const auto transfersBeforePadding = backend.readbackTransfers();
    auto padded = backend.renderDeferred(draw);
    ASSERT_TRUE(paddedReference && padded && padded->readback);
    EXPECT_EQ(backend.readbackTransfers(), transfersBeforePadding);
    draw.renderedTarget = padded->readback;
    std::fill(target.begin(), target.end(), 45);
    auto paddedResult = backend.renderDeferred(draw);
    ASSERT_TRUE(paddedResult && paddedResult->readback);
    EXPECT_EQ(backend.readbackTransfers(), transfersBeforePadding);
    EXPECT_EQ(backend.residentTargetCopies(), 10u);
    paddedResult->resolve();
    EXPECT_EQ(backend.readbackTransfers(), transfersBeforePadding + 1);
    EXPECT_EQ(paddedResult->rgba, paddedReference->rgba);
    draw.renderedTarget.reset();
    auto overwritten = backend.render(draw); // Preserve the still-held padded source before reuse.
    ASSERT_TRUE(overwritten);
    padded->resolve();
    EXPECT_EQ(padded->rgba, paddedReference->rgba);
    auto overwrittenReference = referenceBackend.render(draw);
    ASSERT_TRUE(overwrittenReference);
    EXPECT_EQ(overwritten->rgba, overwrittenReference->rgba);
}

TEST(VulkanRasterBackendTest, PreservesTargetMasksScissorPaddingAndFeedback)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP() << "Set WEMU_TEST_VULKAN=1";
    if (!SpirvCompiler::available())
        GTEST_SKIP() << "Shader tools unavailable";
    const auto shader = lowerFragmentShader(
            *decodeProgram({2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9), 1u << 23, 0}));
    ASSERT_TRUE(shader) << shader.error;
    std::vector<RasterDraw::Vertex> vertices;
    for (const auto xy:
         {std::array{0.f, 0.f}, std::array{4.f, 0.f}, std::array{4.f, 4.f}, std::array{0.f, 0.f}, std::array{4.f, 4.f}, std::array{0.f, 4.f}}) {
        RasterDraw::Vertex vertex{xy[0], xy[1], {}};
        vertex.inputs[0] = {xy[0] / 4, xy[1] / 4, 0, 1};
        vertices.push_back(vertex);
    }
    std::vector<std::uint8_t> target(6 * 4 * 4, 123);
    const std::array<std::array<float, 4>, 4> colors{{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}}};
    RasterDraw::Texture texture{shader.textures[0], 2, 2, {}, [&](unsigned x, unsigned y) { return colors[y * 2 + x]; }};
    std::array<float, 1024> constants{};
    RasterDraw draw{shader, vertices, constants, {&texture, 1}, target, 4, 4, 6, {1, 1, 2, 2}, 5, {}};
    VulkanRasterBackend backend;
    auto output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    auto expected = target;
    for (unsigned y = 1; y < 3; ++y)
        for (unsigned x = 1; x < 3; ++x) {
            const auto &color = colors[(y / 2) * 2 + x / 2];
            expected[(y * 6 + x) * 4] = std::uint8_t(color[0] * 255);
            expected[(y * 6 + x) * 4 + 2] = std::uint8_t(color[2] * 255);
        }
    EXPECT_EQ(output->rgba, expected);
    EXPECT_EQ(target, std::vector<std::uint8_t>(target.size(), 123)); // caller commits, not backend
    auto retainedInput = output->rgba;
    draw.target = retainedInput;
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    EXPECT_EQ(output->rgba, retainedInput);
    EXPECT_EQ(backend.retainedTargetDraws(), 1u);
    // Padding is not part of the GPU image but must follow the caller's bytes.
    retainedInput[4 * 4] = 77;
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    EXPECT_EQ(output->rgba, retainedInput);
    EXPECT_EQ(backend.retainedTargetDraws(), 2u);
    // A CPU/software write to an active pixel invalidates retention, even when
    // outside this draw's scissor. Reupload must preserve that modification.
    retainedInput[0] = 33;
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    EXPECT_EQ(output->rgba, retainedInput);
    EXPECT_EQ(backend.retainedTargetDraws(), 2u);
    draw.target = target;
    draw.channelMask = 15;
    draw.blend = {true, 4, 5, 0, 0, 1, 0, {}};
    texture.texel = [](unsigned, unsigned) { return std::array{1.0f, 0.0f, 0.0f, 0.5f}; };
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            const auto off = (y * 6 + x) * 4;
            if (x >= 1 && x < 3 && y >= 1 && y < 3) {
                EXPECT_NEAR(output->rgba[off], 189, 2);
                EXPECT_NEAR(output->rgba[off + 1], 61, 2);
                EXPECT_NEAR(output->rgba[off + 2], 61, 2);
                EXPECT_EQ(output->rgba[off + 3], 123);
            } else {
                for (unsigned c = 0; c < 4; ++c)
                    EXPECT_EQ(output->rgba[off + c], 123);
            }
        }
    draw.blend = {};
    draw.channelMask = 5;
    texture.texel = [&](unsigned x, unsigned y) { return colors[y * 2 + x]; };
    const auto snapshot = output->rgba;
    texture.width = texture.height = 4;
    texture.texel = [&](unsigned x, unsigned y) {
        std::array<float, 4> pixel{};
        for (unsigned c = 0; c < 4; ++c)
            pixel[c] = snapshot[(y * 6 + x) * 4 + c] / 255.0f;
        return pixel;
    };
    for (auto &vertex: vertices)
        vertex.inputs[0][0] = 1.0f - vertex.inputs[0][0];
    draw.target = snapshot;
    draw.scissor = {0, 0, 4, 4};
    draw.channelMask = 15;
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    expected = snapshot;
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            for (unsigned c = 0; c < 4; ++c)
                expected[(y * 6 + x) * 4 + c] = snapshot[(y * 6 + 3 - x) * 4 + c];
    EXPECT_EQ(output->rgba, expected);
    for (const auto scissor: {std::array{-1, -1, 2, 2}, std::array{3, 3, 5, 5}, std::array{5, 5, 2, 2}}) {
        draw.scissor = scissor;
        auto clippedExpected = snapshot;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                if (x >= scissor[0] && y >= scissor[1] && x < scissor[0] + scissor[2] && y < scissor[1] + scissor[3])
                    for (unsigned c = 0; c < 4; ++c)
                        clippedExpected[(y * 6 + x) * 4 + c] = snapshot[(y * 6 + 3 - x) * 4 + c];
        output = backend.render(draw);
        ASSERT_TRUE(output) << backend.lastError();
        EXPECT_EQ(output->rgba, clippedExpected);
        // Reuse must compare the whole reconstructed target, not stale pixels
        // outside the transferred region, and must preserve row padding.
        output = backend.render(draw);
        ASSERT_TRUE(output) << backend.lastError();
        EXPECT_EQ(output->rgba, clippedExpected);
    }
    draw.scissor = {0, 0, 4, 4};
    draw.blend = {true, 15, 1, 0, 0, 1, 0, {}}; // dual-source requires a second shader output
    EXPECT_FALSE(backend.render(draw));
    EXPECT_FALSE(backend.lastError().empty());
    draw.blend = {};
    texture.sampler.regs[0] = 6;
    EXPECT_FALSE(backend.render(draw));
    texture.sampler.regs[0] = 0;
    texture.unorm8 = snapshot;
    texture.unorm8Pitch = 6;
    texture.texel = [](unsigned, unsigned) -> std::array<float, 4> { throw std::runtime_error("Unexpected slow texture decoding"); };
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    EXPECT_EQ(output->rgba, expected);
    EXPECT_TRUE(std::equal(snapshot.begin(), snapshot.end(), draw.target.begin(), draw.target.end()));
    const auto retained = backend.retainedTextureUploads();
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    EXPECT_EQ(output->rgba, expected);
    EXPECT_EQ(backend.retainedTextureUploads(), retained + 1);
    {
        auto modified = snapshot;
        modified[4 * 4] ^= 0xFF; // Row padding is not uploaded or sampled.
        texture.unorm8 = modified;
        output = backend.render(draw);
        ASSERT_TRUE(output) << backend.lastError();
        EXPECT_EQ(output->rgba, expected);
        EXPECT_EQ(backend.retainedTextureUploads(), retained + 2);
        modified[0] ^= 0x7F; // Active texel mutation must invalidate the retained image.
        output = backend.render(draw);
        ASSERT_TRUE(output) << backend.lastError();
        auto changed = expected;
        changed[3 * 4] = modified[0];
        EXPECT_EQ(output->rgba, changed);
        EXPECT_EQ(backend.retainedTextureUploads(), retained + 2);
    }
    texture.unorm8 = snapshot;
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    EXPECT_EQ(output->rgba, expected);
    EXPECT_EQ(backend.retainedTextureUploads(), retained + 2);
    texture.unorm8Map = 0x02010005;
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            const auto off = (y * 6 + x) * 4;
            std::swap(expected[off], expected[off + 2]);
            expected[off + 3] = 255;
        }
    EXPECT_EQ(output->rgba, expected);
    VulkanRasterBackend noTextureReuse(false);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        const auto uncached = noTextureReuse.render(draw);
        ASSERT_TRUE(uncached) << noTextureReuse.lastError();
        EXPECT_EQ(uncached->rgba, output->rgba);
        EXPECT_EQ(noTextureReuse.retainedTextureUploads(), 0u);
    }
    texture.unorm8 = {};
    texture.unorm8Map = 0x00010203;
    // New snapshots with the same GPU shape must refresh cached resources,
    // including different source pitches and storage that expires after render.
    for (unsigned pitch: {4u, 7u, 6u}) {
        std::vector<std::uint8_t> bytes(pitch * 4 * 4, 0xCD);
        std::vector<float> floats(4 * 4 * 4);
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    const auto value = std::uint8_t((x * 37 + y * 61 + c * 29 + pitch * 13) & 255);
                    bytes[(y * pitch + x) * 4 + c] = value;
                    floats[(y * 4 + x) * 4 + c] = value / 255.0f;
                }
        texture.texel = [&](unsigned x, unsigned y) {
            std::array<float, 4> value;
            std::copy_n(floats.data() + (y * 4 + x) * 4, 4, value.data());
            return value;
        };
        const auto reference = backend.render(draw);
        ASSERT_TRUE(reference) << backend.lastError();
        texture.texel = [](unsigned, unsigned) -> std::array<float, 4> { throw std::runtime_error("Unexpected bulk snapshot callback"); };
        texture.unorm8 = bytes;
        texture.unorm8Pitch = pitch;
        output = backend.render(draw);
        ASSERT_TRUE(output) << backend.lastError();
        EXPECT_EQ(output->rgba, reference->rgba);
        texture.unorm8 = {};
        texture.rgba32 = floats;
        output = backend.render(draw);
        ASSERT_TRUE(output) << backend.lastError();
        EXPECT_EQ(output->rgba, reference->rgba);
        texture.rgba32 = {};
    }
    for (auto &vertex: vertices) {
        vertex.inputs[0][0] += 0.0625f;
        vertex.inputs[0][1] += 0.0625f;
    }
    std::array<float, 16> red{};
    for (const unsigned channels: {1u, 2u})
        for (const unsigned pitch: {4u, 7u})
            for (const unsigned filter: {0u, 1u})
                for (const unsigned map: {0x00010203u, 0x02010504u, 0x03060700u}) {
                    std::vector<std::uint8_t> bytes(pitch * 4 * channels, 0xCD);
                    for (unsigned y = 0; y < 4; ++y)
                        for (unsigned x = 0; x < 4; ++x)
                            for (unsigned c = 0; c < channels; ++c)
                                bytes[(y * pitch + x) * channels + c] = (x * 37 + y * 61 + c * 29) & 255;
                    texture.unorm8 = {};
                    texture.sampler.regs[0] = filter << 9;
                    texture.texel = [&](unsigned x, unsigned y) {
                        const auto offset = (y * pitch + x) * channels;
                        const float r = bytes[offset] / 255.f;
                        const std::array<float, 4> raw{r, channels == 1 ? r : bytes[offset + 1] / 255.f, channels == 1 ? r : 0.f, 1.f};
                        std::array<float, 4> value{};
                        for (unsigned c = 0; c < 4; ++c) {
                            const auto selector = (map >> (24 - c * 8)) & 7;
                            value[c] = selector < 4 ? raw[selector] : selector == 5 ? 1.f : 0.f;
                        }
                        return value;
                    };
                    const auto reference = backend.render(draw);
                    ASSERT_TRUE(reference) << backend.lastError();
                    texture.unorm8 = bytes;
                    texture.unorm8Pitch = pitch;
                    texture.unorm8Channels = channels;
                    texture.unorm8Map = map;
                    texture.texel = [](unsigned, unsigned) -> std::array<float, 4> {
                        throw std::runtime_error("Unexpected narrow texture callback");
                    };
                    for (unsigned repeat = 0; repeat < 2; ++repeat) {
                        output = backend.render(draw);
                        ASSERT_TRUE(output) << backend.lastError();
                        ASSERT_EQ(output->rgba.size(), reference->rgba.size());
                        for (unsigned i = 0; i < output->rgba.size(); ++i)
                            EXPECT_LE(std::abs(int(output->rgba[i]) - int(reference->rgba[i])), 1);
                    }
                    const auto reused = backend.retainedTextureUploads();
                    if (pitch > 4) {
                        bytes[4 * channels] ^= 255;
                        ASSERT_TRUE(backend.render(draw));
                        EXPECT_EQ(backend.retainedTextureUploads(), reused + 1);
                    }
                    texture.unorm8 = std::span<const std::uint8_t>(bytes).first(bytes.size() - 1);
                    EXPECT_FALSE(backend.render(draw));
                    texture.unorm8 = bytes;
                    texture.unorm8Channels = 3;
                    EXPECT_FALSE(backend.render(draw));
                    texture.unorm8 = {};
                }
    texture.unorm8Channels = 4;
    for (unsigned i = 0; i < red.size(); ++i)
        red[i] = float(i) / 8.0f - 0.5f;
    for (const unsigned filter: {0u, 1u})
        for (const unsigned map: {0x00010203u, 0x00000504u, 0x03020005u}) {
            texture.sampler.regs[0] = filter << 9;
            texture.r32Map = map;
            texture.r32 = {};
            texture.texel = [&](unsigned x, unsigned y) {
                const std::array<float, 4> raw{red[y * 4 + x], 0, 0, 1};
                std::array<float, 4> value{};
                for (unsigned c = 0; c < 4; ++c) {
                    const auto selector = (map >> (24 - c * 8)) & 7;
                    value[c] = selector < 4 ? raw[selector] : selector == 5 ? 1.0f : 0.0f;
                }
                return value;
            };
            const auto reference = backend.render(draw);
            ASSERT_TRUE(reference) << backend.lastError();
            texture.r32 = red;
            texture.texel = [](unsigned, unsigned) -> std::array<float, 4> { throw std::runtime_error("Unexpected R32 callback"); };
            for (unsigned repeat = 0; repeat < 2; ++repeat) {
                output = backend.render(draw);
                ASSERT_TRUE(output) << backend.lastError();
                EXPECT_EQ(output->rgba, reference->rgba);
            }
        }
    texture.r32 = std::span<const float>(red).first(15);
    EXPECT_FALSE(backend.render(draw));
    texture.r32 = red;
    texture.unorm8 = snapshot;
    EXPECT_FALSE(backend.render(draw));
}

TEST(LatteLoweringTest, EmitsScalarForwardingAndExactLiteralBits)
{
    auto words = forwardingProgram();
    words[1] = (8u << 26) | (4u << 18); // four instructions and one literal slot
    const auto shader = lowerFragmentShader(*decodeProgram(words));
    ASSERT_TRUE(shader) << shader.error;
    EXPECT_NE(shader.source.find("logClamped(r[0][0])"), std::string::npos);
    EXPECT_NE(shader.source.find("ps=t0;"), std::string::npos);
    EXPECT_NE(shader.source.find("uintBitsToFloat(1061158912u)"), std::string::npos);
    EXPECT_TRUE(shader.textures.empty());
    validateSpirv(shader);
}

TEST(VulkanRasterBackendTest, BoundedReadbackMatchesFullTransferForMovingGeometry)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (0x28u << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    VulkanRasterBackend backend;
    std::vector<std::uint8_t> target(35 * 32 * 4, 123);
    std::array<float, 1024> constants{};
    for (float shift: {-1e30f, -20.f, -8.f, -0.49f, 0.f, 0.49f, 20.f, 50.f, 1e30f}) {
        SCOPED_TRACE(shift);
        std::vector<RasterDraw::Vertex> vertices;
        for (const auto xy: {std::array{8.f, 9.f}, std::array{13.f, 9.f}, std::array{13.f, 15.f}, std::array{8.f, 9.f}, std::array{13.f, 15.f},
                             std::array{8.f, 15.f}}) {
            RasterDraw::Vertex vertex{xy[0] + shift, xy[1] + shift, {}};
            vertex.inputs[0] = {1, 0.25f, 0.5f, 1};
            vertices.push_back(vertex);
        }
        RasterDraw draw{shader, vertices, constants, {}, target, 32, 32, 35, {0, 0, 32, 32}, 15, {}};
        const auto bounded = backend.render(draw);
        ASSERT_TRUE(bounded) << backend.lastError();
        // Degenerate triangles add no fragments but expand the conservative bounds
        // to force a full transfer, giving an independent readback-path comparison.
        for (const auto xy: {std::array{0.f, 0.f}, std::array{32.f, 32.f}})
            for (unsigned i = 0; i < 3; ++i)
                vertices.push_back({xy[0], xy[1], {}});
        draw.vertices = vertices;
        const auto full = backend.render(draw);
        ASSERT_TRUE(full) << backend.lastError();
        EXPECT_EQ(bounded->rgba, full->rgba);
    }
}

TEST(LatteLoweringTest, VulkanArithmeticMatchesSoftwareReference)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP() << "Set WEMU_TEST_VULKAN=1 to run headless Vulkan readback comparisons";
#if !defined(WEMU_GLSLANG_VALIDATOR) || !defined(WEMU_SPIRV_VAL)
    GTEST_SKIP() << "Shader compiler and SPIR-V validator required";
#else
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vertexCode;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vertexCode, "vert"));
    ASSERT_FALSE(vertexCode.empty());
    auto forwarding = forwardingProgram();
    forwarding[1] = (8u << 26) | (4u << 18);
    std::vector<std::vector<std::uint32_t>> programs{forwarding};
    // Non-default read-cycle schedules must preserve operand order, including OP3.
    programs.push_back({2, 8u << 26, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u | (1u << 23), (OP3_MULADD << 13) | (1u << 18) | (2u << 10)});
    programs.push_back({2, 8u << 26, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u, (OP2_EXP_IEEE << 7) | 16u | (3u << 18)});
    for (auto op:
         {OP2_MOV, OP2_ADD, OP2_MUL, OP2_MIN, OP2_MAX, OP2_FLOOR, OP2_FRACT, OP2_RECIP_IEEE, OP2_RECIPSQRT_IEEE, OP2_LOG_IEEE, OP2_EXP_IEEE}) {
        programs.push_back({2, 8u << 26, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u | (256u << 13), (std::uint32_t(op) << 7) | 16u});
    }
    // R0.y must observe the old R0.x, not the MOV committed in the same group.
    programs.push_back({2, (8u << 26) | (1u << 18), 0, (0x28u << 23) | (1u << 21) | 0x688u, 249u, (0x19u << 7) | 16u, 0x80000000u,
                        (0x19u << 7) | 16u | (1u << 29)});
    std::array<float, 1024> constants{};
    constants[0] = 0.375f;
    for (unsigned index = 0; index < programs.size(); ++index) {
        SCOPED_TRACE(index);
        const auto program = decodeProgram(programs[index]);
        const auto lowered = lowerFragmentShader(*program);
        ASSERT_TRUE(lowered) << lowered.error;
        std::vector<std::uint32_t> fragmentCode;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(lowered, &fragmentCode));
        ASSERT_FALSE(fragmentCode.empty());
        VulkanFragmentProbe probe;
        probe.initialize(vertexCode, fragmentCode, constants);
        RecordProperty("vulkan_device", probe.deviceName);
        for (float value: {-3.5f, -0.0f, 0.0f, 0.125f, 0.75f, 1.0f, 4.5f}) {
            SCOPED_TRACE(value);
            VulkanFragmentProbe::Inputs inputs{};
            inputs[0] = {value, 0.25f, 0.5f, 1.0f};
            const auto expected = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, constants.data(), constants.size(), {});
            ASSERT_TRUE(expected.colorValid);
            const auto actual = probe.draw(inputs);
            for (unsigned pixel = 0; pixel < actual.size(); ++pixel) {
                SCOPED_TRACE(pixel);
                for (unsigned c = 0; c < 4; ++c) {
                    const float reference = expected.color[c];
                    if (std::isnan(reference))
                        EXPECT_TRUE(std::isnan(actual[pixel][c]));
                    else if (std::isinf(reference))
                        EXPECT_EQ(actual[pixel][c], reference);
                    else
                        EXPECT_NEAR(actual[pixel][c], reference, 2e-5f * std::max(1.0f, std::abs(reference)));
                }
            }
        }
    }
#endif
}

TEST(LatteLoweringTest, VulkanIntegerMasksAndConditionalSelectionPreserveBits)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vertexCode;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vertexCode, "vert"));
    if (vertexCode.empty())
        GTEST_SKIP();
    std::vector<std::uint32_t> words{2, (8u << 26) | (7u << 18), 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    for (unsigned c = 0; c < 4; ++c) {
        const auto src = 256u + c;
        words.insert(words.end(), {0x80000000u | src | (src << 13) | (1u << 23), (OP2_AND_INT << 7) | 16u | (c << 29),
                                   0x80000000u | (c << 10) | (SRC_1 << 13), (OP3_CNDE_INT << 13) | SRC_0 | (c << 29)});
    }
    const auto program = decodeProgram(words);
    const auto lowered = lowerFragmentShader(*program);
    ASSERT_TRUE(lowered) << lowered.error;
    std::vector<std::uint32_t> fragmentCode;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(lowered, &fragmentCode));
    ASSERT_FALSE(fragmentCode.empty());
    const std::array<std::array<std::uint32_t, 2>, 12> cases{{{0, 0xFFFFFFFFu},
                                                              {1, 1},
                                                              {0x80000000u, 0xFFFFFFFFu},
                                                              {0x7FC00000u, 0xFFFFFFFFu},
                                                              {0xFFFFFFFFu, 0},
                                                              {0xAAAAAAAAu, 0x55555555u},
                                                              {0xAAAAAAAAu, 0x80000000u},
                                                              {0x7F800000u, 0x7F800000u},
                                                              {0x00000001u, 0xFFFFFFFEu},
                                                              {0x007FFFFFu, 1},
                                                              {0xFF800000u, 0x80000000u},
                                                              {0x7FC00000u, 1}}};
    for (unsigned base = 0; base < cases.size(); base += 4) {
        SCOPED_TRACE(base);
        std::array<float, 1024> constants{};
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned s = 0; s < 2; ++s)
                constants[c * 4 + s] = std::bit_cast<float>(cases[base + c][s]);
        VulkanFragmentProbe probe;
        probe.initialize(vertexCode, fragmentCode, constants);
        VulkanFragmentProbe::Inputs inputs{};
        const auto expected = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, constants.data(), constants.size(), {});
        ASSERT_TRUE(expected.colorValid);
        for (const auto &pixel: probe.draw(inputs))
            for (unsigned c = 0; c < 4; ++c) {
                EXPECT_FLOAT_EQ(expected.color[c], (cases[base + c][0] & cases[base + c][1]) == 0 ? 1.f : 0.f);
                EXPECT_FLOAT_EQ(pixel[c], expected.color[c]);
            }
    }
}

TEST(LatteLoweringTest, VulkanMinMaxMatchReferenceSpecialValues)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vertexCode;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vertexCode, "vert"));
    if (vertexCode.empty())
        GTEST_SKIP();
    const float nan = std::bit_cast<float>(0x7FC00000u), inf = std::bit_cast<float>(0x7F800000u);
    const std::array<std::array<float, 2>, 12> cases{{{nan, 0.25f},
                                                      {0.75f, nan},
                                                      {nan, nan},
                                                      {-inf, inf},
                                                      {inf, -inf},
                                                      {0.f, -0.f},
                                                      {-0.f, 0.f},
                                                      {-3.f, 2.f},
                                                      {0.5f, 0.5f},
                                                      {-2.f, -3.f},
                                                      {inf, inf},
                                                      {-inf, -inf}}};
    for (auto op: {OP2_MIN, OP2_MAX, OP2_MIN_DX10, OP2_MAX_DX10}) {
        std::vector<std::uint32_t> words{2, (8u << 26) | (3u << 18), 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
        for (unsigned c = 0; c < 4; ++c) {
            const auto src = 256u + c;
            words.insert(words.end(), {0x80000000u | src | (src << 13) | (1u << 23), (std::uint32_t(op) << 7) | 16u | (c << 29)});
        }
        const auto program = decodeProgram(words);
        const auto lowered = lowerFragmentShader(*program);
        ASSERT_TRUE(lowered) << lowered.error;
        std::vector<std::uint32_t> fragmentCode;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(lowered, &fragmentCode));
        ASSERT_FALSE(fragmentCode.empty());
        for (unsigned base = 0; base < cases.size(); base += 4) {
            SCOPED_TRACE(base);
            SCOPED_TRACE(op);
            std::array<float, 1024> constants{};
            for (unsigned c = 0; c < 4; ++c)
                std::copy(cases[base + c].begin(), cases[base + c].end(), constants.begin() + c * 4);
            VulkanFragmentProbe probe;
            probe.initialize(vertexCode, fragmentCode, constants);
            VulkanFragmentProbe::Inputs inputs{};
            const auto expected = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, constants.data(), constants.size(), {});
            ASSERT_TRUE(expected.colorValid);
            for (const auto &pixel: probe.draw(inputs))
                for (unsigned c = 0; c < 4; ++c) {
                    if (std::isnan(expected.color[c]))
                        EXPECT_TRUE(std::isnan(pixel[c]));
                    else
                        EXPECT_FLOAT_EQ(pixel[c], expected.color[c]);
                }
        }
    }
}

TEST(LatteLoweringTest, ReferenceRoundEvenAndDx10MaskAreDefined)
{
    const auto shader = [](unsigned op) {
        return decodeProgram(
                {2, 8u << 26, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u | 256u | (256u << 13) | (1u << 23), (op << 7) | 16u});
    };
    const auto round = shader(OP2_RNDNE), compare = shader(OP2_SETGT_DX10);
    const float nan = std::bit_cast<float>(0x7FC00000u), inf = std::bit_cast<float>(0x7F800000u);
    const std::array<std::array<float, 2>, 16> rounds{{{-3.5f, -4},
                                                       {-2.5f, -2},
                                                       {-1.5f, -2},
                                                       {-0.5f, -0.0f},
                                                       {0.5f, 0},
                                                       {1.5f, 2},
                                                       {2.5f, 2},
                                                       {3.5f, 4},
                                                       {-0.0f, -0.0f},
                                                       {0, 0},
                                                       {0.75f, 1},
                                                       {-0.25f, -0.0f},
                                                       {8388608.f, 8388608.f},
                                                       {inf, inf},
                                                       {-inf, -inf},
                                                       {nan, nan}}};
    struct RestoreRounding {
            int mode = std::fegetround();
            ~RestoreRounding() { std::fesetround(mode); }
    } restore;
    for (int mode: {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        for (const auto &values: rounds) {
            const std::array<float, 4> constants{values[0], 0, 0, 0};
            const auto out = Core::Gfx::LatteVsInterp::runPixel(*round, {}, constants.data(), 1, {});
            ASSERT_TRUE(out.colorValid);
            if (std::isnan(values[1]))
                EXPECT_TRUE(std::isnan(out.color[0]));
            else
                EXPECT_EQ(std::bit_cast<std::uint32_t>(out.color[0]), std::bit_cast<std::uint32_t>(values[1]));
        }
    }
    for (const auto &values:
         std::array<std::array<float, 2>, 8>{{{2, 1}, {1, 2}, {1, 1}, {-0.f, 0.f}, {nan, 1}, {1, nan}, {inf, -inf}, {-inf, inf}}}) {
        const std::array<float, 4> constants{values[0], values[1], 0, 0};
        const auto out = Core::Gfx::LatteVsInterp::runPixel(*compare, {}, constants.data(), 1, {});
        ASSERT_TRUE(out.colorValid);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(out.color[0]), values[0] > values[1] ? 0xFFFFFFFFu : 0u);
    }
}

TEST(LatteLoweringTest, VulkanRoundEvenAndDx10SelectionMatchReference)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vertexCode;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vertexCode, "vert"));
    if (vertexCode.empty())
        GTEST_SKIP();
    const auto program =
            decodeProgram({2, (8u << 26) | (2u << 18), 0, (CF_EXP_DONE << 23) | (1u << 21) | (1u << 3) | (4u << 6) | (5u << 9), 0x80000000u | 256u,
                           (OP2_RNDNE << 7) | 16u, 0x80000000u | 256u | (256u << 13) | (1u << 23), (OP2_SETGT_DX10 << 7) | 16u | (1u << 29),
                           0x80000000u | (1u << 10) | (SRC_1 << 13), (OP3_CNDE_INT << 13) | SRC_0 | (1u << 29)});
    const auto lowered = lowerFragmentShader(*program);
    ASSERT_TRUE(lowered) << lowered.error;
    std::vector<std::uint32_t> fragmentCode;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(lowered, &fragmentCode));
    ASSERT_FALSE(fragmentCode.empty());
    const float nan = std::bit_cast<float>(0x7FC00000u), inf = std::bit_cast<float>(0x7F800000u);
    const std::array<std::array<float, 2>, 12> cases{{{-3.5f, -4},
                                                      {-2.5f, -2},
                                                      {-0.5f, 0},
                                                      {0.5f, 0},
                                                      {1.5f, 2},
                                                      {2.5f, 2},
                                                      {-0.0f, 0},
                                                      {0.75f, 1},
                                                      {nan, 1},
                                                      {1, nan},
                                                      {inf, -inf},
                                                      {-inf, inf}}};
    for (const auto &values: cases) {
        std::array<float, 1024> constants{};
        constants[0] = values[0];
        constants[1] = values[1];
        VulkanFragmentProbe probe;
        probe.initialize(vertexCode, fragmentCode, constants);
        VulkanFragmentProbe::Inputs inputs{};
        const auto expected = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, constants.data(), constants.size(), {});
        ASSERT_TRUE(expected.colorValid);
        for (const auto &pixel: probe.draw(inputs))
            for (unsigned c = 0; c < 4; ++c) {
                if (std::isnan(expected.color[c]))
                    EXPECT_TRUE(std::isnan(pixel[c]));
                else
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(pixel[c]), std::bit_cast<std::uint32_t>(expected.color[c]));
            }
    }
}

TEST(LatteLoweringTest, VulkanTexturesMatchSoftwareReference)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP() << "Set WEMU_TEST_VULKAN=1 to run headless Vulkan readback comparisons";
#if !defined(WEMU_GLSLANG_VALIDATOR) || !defined(WEMU_SPIRV_VAL)
    GTEST_SKIP() << "Shader compiler and SPIR-V validator required";
#else
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vertexCode;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vertexCode, "vert"));
    ASSERT_FALSE(vertexCode.empty());
    const std::array<std::uint8_t, 16> first{255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 255, 0};
    const std::array<std::uint8_t, 16> second{32, 64, 128, 255, 192, 32, 64, 128, 64, 192, 32, 64, 128, 64, 192, 0};
    const std::array<float, 1024> constants{};
    for (unsigned secondResource: {3u, 7u}) {
        for (bool linear: {false, true}) {
            SCOPED_TRACE(secondResource);
            SCOPED_TRACE(linear);
            // Same resource with different samplers, then distinct resources. Two
            // TEX results feed ADD; the second fetch preserves the input alpha.
            const std::vector<std::uint32_t> words{3,
                                                   (1u << 23) | (1u << 10),
                                                   7,
                                                   8u << 26,
                                                   0,
                                                   (0x28u << 23) | (1u << 21) | 0x688u,
                                                   0x13u | (3u << 8),
                                                   4u | 0x30000000u | (0x688u << 9),
                                                   (2u << 15) | (1u << 23),
                                                   0,
                                                   0x10u | (secondResource << 8) | (1u << 16),
                                                   0x30000000u | (2u << 9) | (1u << 12) | (7u << 18),
                                                   (5u << 15) | (1u << 23),
                                                   0,
                                                   0x80000000u | (4u << 13),
                                                   16u};
            const auto program = decodeProgram(words);
            const auto lowered = lowerFragmentShader(*program);
            ASSERT_TRUE(lowered) << lowered.error;
            ASSERT_EQ(lowered.textures.size(), 2u);
            ASSERT_TRUE(lowered.requiresBaseLevelOnly);
            std::vector<std::uint32_t> fragmentCode;
            ASSERT_NO_FATAL_FAILURE(validateSpirv(lowered, &fragmentCode));
            ASSERT_FALSE(fragmentCode.empty());
            std::vector<VulkanFragmentProbe::Texture> textures;
            for (const auto &binding: lowered.textures) {
                ASSERT_TRUE(binding.resource == 3 || binding.resource == 7);
                ASSERT_TRUE(binding.sampler == 2 || binding.sampler == 5);
                textures.push_back({binding.binding, binding.resource == 3 ? first : second,
                                    binding.sampler == 2 ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                    linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST});
            }
            VulkanFragmentProbe probe;
            probe.initialize(vertexCode, fragmentCode, constants, textures);
            RecordProperty("vulkan_device", probe.deviceName);
            for (const auto uv: {std::array{-0.25f, 0.25f}, std::array{0.25f, 0.75f}, std::array{0.5f, 0.5f}, std::array{0.875f, 0.125f},
                                 std::array{1.25f, -0.25f}, std::array{0.0f, 1.0f}}) {
                SCOPED_TRACE(uv[0]);
                SCOPED_TRACE(uv[1]);
                VulkanFragmentProbe::Inputs inputs{};
                inputs[0] = inputs[1] = {uv[0], uv[1], 0.0f, 0.375f};
                unsigned sampled = 0;
                const auto sample = [&](unsigned resource, unsigned sampler, const std::array<float, 4> &coords, std::array<float, 4> &rgba) {
                    ++sampled;
                    EXPECT_TRUE(resource == 3 || resource == 7);
                    EXPECT_TRUE(sampler == 2 || sampler == 5);
                    Core::Gfx::TextureSampler reference;
                    const unsigned address = sampler == 2 ? 0 : 2;
                    reference.regs[0] = address | (address << 3) | (unsigned(linear) << 9);
                    const auto &bytes = resource == 3 ? first : second;
                    rgba = reference.sample(2, 2, coords[0], coords[1], [&](unsigned x, unsigned y) {
                        std::array<float, 4> texel{};
                        for (unsigned c = 0; c < 4; ++c)
                            texel[c] = bytes[(y * 2 + x) * 4 + c] / 255.0f;
                        return texel;
                    });
                    return true;
                };
                const auto expected = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, constants.data(), constants.size(), sample);
                ASSERT_TRUE(expected.colorValid);
                ASSERT_EQ(sampled, 2u);
                EXPECT_FLOAT_EQ(expected.color[3], 0.375f);
                const auto actual = probe.draw(inputs);
                for (unsigned pixel = 0; pixel < actual.size(); ++pixel) {
                    SCOPED_TRACE(pixel);
                    for (unsigned c = 0; c < 4; ++c) {
                        // UNORM8 linear filtering can round each sample by half a channel step.
                        // This shader adds two sampled RGB values; its alpha comes from the input.
                        // Keep nearest sampling and the untouched alpha at the arithmetic tolerance.
                        constexpr float arithmeticTolerance = 2e-5f;
                        const float tolerance = linear && c < 3 ? 1.0f / 255.0f + arithmeticTolerance : arithmeticTolerance;
                        EXPECT_NEAR(actual[pixel][c], expected.color[c], tolerance);
                    }
                }
            }
        }
    }
#endif
}

TEST(LatteLoweringTest, ComputesGroupSourcesBeforeCommittingRegisters)
{
    const auto shader = lowerFragmentShader(*decodeProgram({2, (8u << 26) | (1u << 18), 0, (0x28u << 23) | (1u << 21) | 0x688u, 249u,
                                                            (0x19u << 7) | 16u, 0x80000000u, (0x19u << 7) | 16u | (1u << 29)}));
    ASSERT_TRUE(shader) << shader.error;
    EXPECT_LT(shader.source.find("precise float t1 = r[0][0]"), shader.source.find("r[0][0]=t0"));
    validateSpirv(shader);
}

TEST(LatteLoweringTest, SeparatesResourceSamplerPairsAndPreservesTextureMask)
{
    std::vector<std::uint32_t> words{2, (1u << 23) | (1u << 10), 0, (0x28u << 23) | (1u << 21) | 0x688u};
    for (unsigned sampler: {2u, 5u}) {
        words.push_back(0x10u | (3u << 8));
        words.push_back(0x30000000u | (1u << 12) | (2u << 15) | (7u << 18));
        words.push_back((sampler << 15) | (1u << 23) | (4u << 26) | (5u << 29));
        words.push_back(0);
    }
    const auto shader = lowerFragmentShader(*decodeProgram(words));
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.textures.size(), 2u);
    EXPECT_EQ(shader.textures[0].resource, 3u);
    EXPECT_EQ(shader.textures[0].sampler, 2u);
    EXPECT_EQ(shader.textures[1].sampler, 5u);
    EXPECT_TRUE(shader.requiresBaseLevelOnly);
    EXPECT_EQ(shader.source.find("r[0][3]=sampled"), std::string::npos);
    words[1] |= 1u << 22; // valid-pixel mode, no KILL/quad/derivative operations
    const auto validPixels = lowerFragmentShader(*decodeProgram(words));
    ASSERT_TRUE(validPixels) << validPixels.error;
    EXPECT_EQ(validPixels.source, shader.source);
    validateSpirv(shader);
}

TEST(LatteLoweringTest, BankReadSchedulesPreserveLoweringAndRejectReservedValues)
{
    for (unsigned kind = 0; kind < 4; ++kind) {
        const bool op3 = kind >= 2;
        const bool scalar = kind & 1;
        std::vector<std::uint32_t> words{2,
                                         8u << 26,
                                         0,
                                         (0x28u << 23) | (1u << 21) | 0x688u,
                                         0x80000000u | (1u << 23),
                                         op3 ? (OP3_MULADD << 13) | (2u << 10) : ((scalar ? OP2_EXP_IEEE : OP2_ADD) << 7) | 16u};
        if (op3 && scalar) {
            // A second X instruction occupies the scalar slot in the same group.
            words[1] |= 1u << 18;
            words.insert(words.begin() + 4, {0u, (OP2_MOV << 7) | 16u | (2u << 21)});
        }
        const auto baseline = lowerFragmentShader(*decodeProgram(words));
        ASSERT_TRUE(baseline) << baseline.error;
        for (unsigned bank = 0; bank < 8; ++bank) {
            SCOPED_TRACE(kind);
            SCOPED_TRACE(bank);
            auto variant = words;
            variant.back() |= bank << 18;
            const auto shader = lowerFragmentShader(*decodeProgram(variant));
            if (bank <= (scalar ? 3u : 5u)) {
                ASSERT_TRUE(shader) << shader.error;
                EXPECT_EQ(shader.source, baseline.source);
            } else {
                EXPECT_FALSE(shader);
                EXPECT_EQ(shader.error, "reserved ALU bank swizzle");
                EXPECT_TRUE(shader.source.empty());
            }
        }
    }
}

TEST(LatteLoweringTest, RejectsUnsupportedProgramsWithoutPartialSource)
{
    const std::vector<std::uint32_t> base{2, 8u << 26, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u, (0x19u << 7) | 16u};
    const auto reject = [](const auto &words) {
        const auto shader = lowerFragmentShader(*decodeProgram(words));
        EXPECT_FALSE(shader);
        EXPECT_FALSE(shader.error.empty());
        EXPECT_TRUE(shader.source.empty());
        EXPECT_TRUE(shader.textures.empty());
    };
    auto words = base;
    words[4] |= 1u << 9;
    reject(words); // relative source
    words = base;
    words[5] |= 1u << 28;
    reject(words); // relative destination
    words = base;
    words[5] |= 4;
    reject(words); // execution mask update
    words = base;
    words[1] = 9u << 26;
    reject(words); // unbalanced ALU_PUSH_BEFORE
    words = base;
    words[0] |= 1u << 30;
    reject(words); // uniform window
    words = base;
    words[2] |= 1u << 13;
    reject(words); // position export
    words = base;
    words[3] &= ~(1u << 21);
    reject(words); // no termination
    words = base;
    words[5] = (0x7Eu << 7) | 16u;
    reject(words);
    reject(std::vector<std::uint32_t>{});
}

TEST(LatteLoweringTest, RejectsTextureStateThatNeedsAdditionalRuntimeSemantics)
{
    const std::vector<std::uint32_t> base{2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9), 1u << 23, 0};
    const auto supported = lowerFragmentShader(*decodeProgram(base));
    ASSERT_TRUE(supported) << supported.error;
    EXPECT_FALSE(supported.requiresBaseLevelOnly);
    for (const auto [word, bit]: {std::pair{1u, 7u}, std::pair{4u, 23u}, std::pair{5u, 7u}, std::pair{6u, 0u}}) {
        auto words = base;
        words[word] |= 1u << bit;
        const auto rejected = lowerFragmentShader(*decodeProgram(words));
        EXPECT_FALSE(rejected) << "word=" << word << " bit=" << bit;
        EXPECT_TRUE(rejected.source.empty());
        EXPECT_TRUE(rejected.textures.empty());
        EXPECT_FALSE(rejected.requiresBaseLevelOnly);
    }
    auto unnormalized = base;
    unnormalized[5] &= ~(1u << 28);
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(unnormalized)));
    validateSpirv(supported);
}

TEST(LatteLoweringTest, TextureSlotPaddingDoesNotChangeSemantics)
{
    std::vector<std::uint32_t> words{2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9), 1u << 23, 0};
    const auto baseline = lowerFragmentShader(*decodeProgram(words));
    ASSERT_TRUE(baseline) << baseline.error;
    for (auto padding: {1u, 0x12345678u, 0xFFFFFFFFu}) {
        words[7] = padding;
        const auto shader = lowerFragmentShader(*decodeProgram(words));
        ASSERT_TRUE(shader) << shader.error;
        EXPECT_EQ(shader.source, baseline.source);
        ASSERT_EQ(shader.textures.size(), baseline.textures.size());
        EXPECT_EQ(shader.requiresBaseLevelOnly, baseline.requiresBaseLevelOnly);
    }
    words.pop_back();
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(words))); // slot must still exist
}

TEST(LatteLoweringTest, GatherFootprintMatchesVulkan)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP() << "Set WEMU_TEST_VULKAN=1";
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    const std::vector<std::uint32_t> words{2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x0Fu, 0xF0000000u | (0x688u << 9), (1u << 23), 0};
    const auto program = decodeProgram(words);
    const auto fragment = lowerFragmentShader(*program);
    ASSERT_TRUE(fragment) << fragment.error;
    ASSERT_TRUE(fragment.usesGather);
    ASSERT_TRUE(fragment.requiresBaseLevelOnly);
    std::vector<std::uint32_t> vs, ps;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
    ASSERT_FALSE(vs.empty());
    ASSERT_FALSE(ps.empty());
    const std::array<std::uint8_t, 16> bytes{32, 11, 12, 13, 96, 21, 22, 23, 160, 31, 32, 33, 224, 41, 42, 43};
    const std::array<float, 1024> constants{};
    for (unsigned mode: {0u, 1u, 2u, 6u}) {
        const auto address = mode == 0   ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                             : mode == 1 ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                             : mode == 2 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                         : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        for (bool linear: {false, true}) {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(linear);
            VulkanFragmentProbe probe;
            probe.initialize(vs, ps, constants, {{1, bytes, address, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST}});
            Core::Gfx::TextureSampler sampler;
            sampler.regs[0] = mode | (mode << 3) | (unsigned(linear) << 9);
            for (const auto uv:
                 {std::array{0.5f, 0.5f}, std::array{0.0f, 0.0f}, std::array{1.0f, 1.0f}, std::array{1.5f, -0.5f}, std::array{-0.125f, 0.375f},
                  std::array{0.875f, 1.125f}, std::array{1.875f, -2.125f}, std::array{std::numeric_limits<float>::quiet_NaN(), 0.5f},
                  std::array{0.5f, std::numeric_limits<float>::infinity()}}) {
                SCOPED_TRACE(uv[0]);
                SCOPED_TRACE(uv[1]);
                VulkanFragmentProbe::Inputs inputs{};
                inputs[0] = {uv[0], uv[1], 0, 0};
                const Core::Gfx::LatteVsInterp::TextureGather gather = [&](unsigned resource, unsigned unit, const auto &coord, auto &rgba) {
                    EXPECT_EQ(resource, 0u);
                    EXPECT_EQ(unit, 0u);
                    rgba = sampler.gather(2, 2, coord[0], coord[1],
                                          [&](unsigned x, unsigned y) { return std::array<float, 4>{bytes[(y * 2 + x) * 4] / 255.0f, 0, 0, 0}; });
                    return true;
                };
                const auto expected = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {}, {}, gather);
                ASSERT_TRUE(expected.colorValid);
                const auto actual = probe.draw(inputs);
                for (const auto &pixel: actual)
                    for (unsigned c = 0; c < 4; ++c)
                        EXPECT_NEAR(pixel[c], expected.color[c], 1e-6f);
            }
        }
    }
}

TEST(LatteLoweringTest, EmitsReferenceArithmeticDomainGuards)
{
    for (const auto [op, expected]:
         {std::pair{OP2_RECIP_IEEE, "r[0][0] != 0.0 ? 1.0 / r[0][0] : 0.0"},
          std::pair{OP2_RECIPSQRT_IEEE, "r[0][0] > 0.0 ? inversesqrt(r[0][0]) : 0.0"}, std::pair{OP2_LOG_IEEE, "logReference(r[0][0])"}}) {
        const auto shader = lowerFragmentShader(
                *decodeProgram({2, 8u << 26, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x80000000u, (std::uint32_t(op) << 7) | 16u | (1u << 31)}));
        ASSERT_TRUE(shader) << shader.error;
        EXPECT_NE(shader.source.find(expected), std::string::npos);
        EXPECT_NE(shader.source.find("precise float t0 = clampReference("), std::string::npos);
        validateSpirv(shader);
    }
}

TEST(LatteLoweringTest, ReferencePredicateAndExecutionMaskAreIndependent)
{
    for (unsigned flags: {0u, 4u, 8u, 12u})
        for (bool condition: {false, true}) {
            SCOPED_TRACE(flags);
            SCOPED_TRACE(condition);
            const auto program = decodeProgram(
                    {5, 9u << 26, 6, 8u << 26, 0, (0x0Eu << 23) | 1u, 7, (8u << 26) | (1u << 18), 3u << 15, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                     0x80000000u | (1u << 13), (OP2_PRED_SETGT << 7) | 16u | (3u << 21) | flags, 0x80000000u | SRC_1,
                     (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29), 0x80000000u | SRC_1 | (3u << 29), (OP2_MOV << 7) | 16u | (3u << 21) | (2u << 29),
                     0x80000000u | SRC_1 | (2u << 29), (OP2_MOV << 7) | 16u | (3u << 21) | (3u << 29)});
            VulkanFragmentProbe::Inputs inputs{};
            inputs[0][0] = condition ? 2.f : 0.f;
            inputs[1][0] = 1.f;
            const auto out = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
            ASSERT_TRUE(out.colorValid);
            const bool pred = !(flags & 8u) || condition;
            EXPECT_EQ(out.color[0], condition ? 1.f : 0.f);
            EXPECT_EQ(out.color[1], (!(flags & 4u) || condition) ? 1.f : 0.f);
            EXPECT_EQ(out.color[2], pred ? 1.f : 0.f);
            EXPECT_EQ(out.color[3], pred ? 0.f : 1.f);
            ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, out.color));
        }
}

TEST(LatteLoweringTest, ReferenceIntegerPredicateReturnsMask)
{
    for (auto op: {OP2_PRED_SETE_INT, OP2_PRED_SETNE_INT, OP2_PRED_SETGT_INT, OP2_PRED_SETGE_INT})
        for (std::int32_t a: {-2, 0, 2})
            for (std::int32_t b: {-2, 0, 2}) {
                const auto program = decodeProgram(
                        {2, 8u << 26, 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | (1u << 13), (unsigned(op) << 7) | 16u | (3u << 21)});
                VulkanFragmentProbe::Inputs inputs{};
                inputs[0][0] = std::bit_cast<float>(a);
                inputs[1][0] = std::bit_cast<float>(b);
                const auto out = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
                ASSERT_TRUE(out.colorValid);
                const bool condition = op == OP2_PRED_SETE_INT    ? a == b
                                       : op == OP2_PRED_SETNE_INT ? a != b
                                       : op == OP2_PRED_SETGT_INT ? a > b
                                                                  : a >= b;
                for (float c: out.color)
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(c), condition ? 0xFFFFFFFFu : 0u);
            }
}

TEST(LatteLoweringTest, ReferenceInactiveTextureAndJumpUseExecutionMask)
{
    for (bool condition: {false, true}) {
        const auto program = decodeProgram({6,
                                            9u << 26,
                                            8,
                                            CF_TEX << 23,
                                            4,
                                            (0x0Au << 23) | 1u,
                                            0,
                                            (0x0Eu << 23) | 1u,
                                            7,
                                            8u << 26,
                                            3u << 15,
                                            (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                            0x80000000u | (1u << 13),
                                            (OP2_PRED_SETGT << 7) | 4u,
                                            0x80000000u | SRC_1,
                                            (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29),
                                            0x13u,
                                            3u | (7u << 12) | (7u << 15) | (7u << 18) | 0x30000000u,
                                            0x688u << 20,
                                            0});
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0][0] = condition ? 2.f : 0.f;
        inputs[1][0] = 1.f;
        unsigned samples = 0;
        const auto out = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0,
                                                            [&](unsigned, unsigned, const std::array<float, 4> &, std::array<float, 4> &rgba) {
                                                                ++samples;
                                                                rgba = {.5f, .5f, .5f, .5f};
                                                                return true;
                                                            });
        ASSERT_TRUE(out.colorValid);
        EXPECT_EQ(samples, condition ? 1u : 0u);
        EXPECT_EQ(out.color, (std::array{condition ? .5f : 0.f, 1.f, 0.f, 0.f}));
    }
}

TEST(LatteLoweringTest, ReferenceNestedInactiveElseStaysInactive)
{
    const auto program = decodeProgram({7,
                                        9u << 26,
                                        8,
                                        9u << 26,
                                        4,
                                        0x0Du << 23,
                                        9,
                                        8u << 26,
                                        0,
                                        (0x0Eu << 23) | 2u,
                                        10,
                                        8u << 26,
                                        3u << 15,
                                        (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                        0x80000000u | SRC_0 | (SRC_1 << 13),
                                        (OP2_PRED_SETGT << 7) | 4u,
                                        0x80000000u | SRC_1 | (SRC_0 << 13),
                                        (OP2_PRED_SETGT << 7) | 4u,
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (3u << 21),
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29)});
    const auto out = Core::Gfx::LatteVsInterp::runPixel(*program, {}, nullptr, 0, {});
    ASSERT_TRUE(out.colorValid);
    EXPECT_EQ(out.color, (std::array{0.f, 1.f, 0.f, 0.f}));
    ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, {}, out.color));
}

TEST(LatteLoweringTest, ForwardJumpElseAndAluPopMatchVulkan)
{
    const std::vector<std::uint32_t> words{6,
                                           9u << 26,
                                           3,
                                           0x0Au << 23,
                                           7,
                                           8u << 26,
                                           5,
                                           (0x0Du << 23) | 1u,
                                           8,
                                           10u << 26,
                                           3u << 15,
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                           0x80000000u | (1u << 13),
                                           (OP2_PRED_SETGT << 7) | 12u,
                                           0x80000000u | SRC_1,
                                           (OP2_MOV << 7) | 16u | (3u << 21),
                                           0x80000000u | SRC_1,
                                           (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29)};
    const auto program = decodeProgram(words);
    for (bool condition: {false, true}) {
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0][0] = condition ? 2.f : 0.f;
        inputs[1][0] = 1.f;
        const auto out = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
        ASSERT_TRUE(out.colorValid);
        EXPECT_EQ(out.color, (std::array{condition ? 1.f : 0.f, condition ? 0.f : 1.f, 0.f, 0.f}));
        ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, out.color));
    }
    if (std::getenv("WEMU_TEST_VULKAN")) {
        FragmentShader vertex;
        vertex.source = R"(#version 450
layout(location=0) out vec4 inputs[4];
void main() {
    vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    for(int i=0;i<4;++i) inputs[i]=vec4(0.0);
    inputs[0].x=gl_Position.x;
}
)";
        const auto fragment = lowerFragmentShader(*program);
        ASSERT_TRUE(fragment) << fragment.error;
        std::vector<std::uint32_t> vs, ps;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
        ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
        ASSERT_FALSE(vs.empty());
        ASSERT_FALSE(ps.empty());
        VulkanFragmentProbe probe;
        probe.initialize(vs, ps, {});
        const auto pixels = probe.draw({});
        for (unsigned i = 0; i < pixels.size(); ++i) {
            VulkanFragmentProbe::Inputs inputs{};
            inputs[0][0] = float(i % 4) * .5f - .75f;
            const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
            ASSERT_TRUE(reference.colorValid);
            EXPECT_EQ(pixels[i], reference.color);
        }
    }
    for (unsigned target: {0u, 1u, 5u, 6u, 100u}) {
        auto invalid = words;
        invalid[2] = target;
        EXPECT_FALSE(lowerFragmentShader(*decodeProgram(invalid))) << target;
    }
    auto invalid = words;
    invalid[1] |= 1u << 30;
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(invalid))); // whole-quad mode
    invalid = words;
    invalid[3] |= 1u << 8;
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(invalid))); // non-ACTIVE condition
}

TEST(LatteLoweringTest, IntegerArithmeticAndShiftsMatchBitsOnVulkan)
{
    const bool vulkan = std::getenv("WEMU_TEST_VULKAN");
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vs;
    if (vulkan)
        ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    for (auto op: {OP2_OR_INT, OP2_XOR_INT, OP2_ADD_INT, OP2_SUB_INT, OP2_ASHR, OP2_LSHR, OP2_LSHL}) {
        SCOPED_TRACE(op);
        // Compare raw result bits inside the shader, avoiding NaN payload changes
        // at the floating-point framebuffer boundary.
        const auto program =
                decodeProgram({2, (8u << 26) | (2u << 18), 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | 256u | (257u << 13),
                               (unsigned(op) << 7) | 16u | (3u << 21), 0x80000000u | 3u | (258u << 13), (OP2_XOR_INT << 7) | 16u | (3u << 21),
                               0x80000000u | 3u | (SRC_1 << 13), SRC_0 | (OP3_CNDE_INT << 13) | (3u << 21)});
        const auto fragment = lowerFragmentShader(*program);
        ASSERT_TRUE(fragment) << fragment.error;
        std::vector<std::uint32_t> ps;
        if (vulkan) {
            ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
            ASSERT_FALSE(vs.empty());
            ASSERT_FALSE(ps.empty());
        }
        for (auto a: {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xAAAAAAAAu})
            for (auto b: {0u, 1u, 31u, 32u, 33u, 0xFFFFFFFFu, 0x55555555u}) {
                SCOPED_TRACE(a);
                SCOPED_TRACE(b);
                const auto shift = b & 31u;
                std::uint32_t expected{};
                switch (op) {
                    case OP2_OR_INT:
                        expected = a | b;
                        break;
                    case OP2_XOR_INT:
                        expected = a ^ b;
                        break;
                    case OP2_ADD_INT:
                        expected = a + b;
                        break;
                    case OP2_SUB_INT:
                        expected = a - b;
                        break;
                    case OP2_LSHR:
                        expected = a >> shift;
                        break;
                    case OP2_LSHL:
                        expected = a << shift;
                        break;
                    case OP2_ASHR:
                        expected = a >> shift;
                        if ((a & 0x80000000u) && shift)
                            expected |= 0xFFFFFFFFu << (32u - shift);
                        break;
                    default:
                        FAIL();
                }
                std::array<float, 1024> constants{};
                constants[0] = std::bit_cast<float>(a);
                constants[4] = std::bit_cast<float>(b);
                constants[8] = std::bit_cast<float>(expected);
                const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), constants.size(), {});
                ASSERT_TRUE(reference.colorValid);
                for (float c: reference.color)
                    EXPECT_EQ(c, 1.f);
                if (vulkan) {
                    VulkanFragmentProbe probe;
                    probe.initialize(vs, ps, constants);
                    for (const auto &pixel: probe.draw({}))
                        for (float c: pixel)
                            ASSERT_EQ(c, 1.f);
                }
            }
    }
}

TEST(LatteLoweringTest, ScaledMultiplyAddMatchesReferenceAndVulkan)
{
    const bool vulkan = std::getenv("WEMU_TEST_VULKAN");
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vs;
    if (vulkan)
        ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    for (const auto [op, scale]: {std::pair{OP3_MULADD_M2, 2.f},
                                  {OP3_MULADD_M4, 4.f},
                                  {OP3_MULADD_D2, .5f},
                                  {OP3_MULADD_IEEE_M2, 2.f},
                                  {OP3_MULADD_IEEE_M4, 4.f},
                                  {OP3_MULADD_IEEE_D2, .5f}})
        for (bool clamp: {false, true}) {
            SCOPED_TRACE(op);
            SCOPED_TRACE(clamp);
            // R3.x = scale * (R0.x * R1.x + R2.x), optionally clamped after scaling.
            const auto program = decodeProgram({2, 8u << 26, 3u << 15, (0x28u << 23) | (1u << 21), 0x80000000u | (1u << 13),
                                                2u | (unsigned(op) << 13) | (3u << 21) | (unsigned(clamp) << 31)});
            const auto fragment = lowerFragmentShader(*program);
            ASSERT_TRUE(fragment) << fragment.error;
            std::vector<std::uint32_t> ps;
            if (vulkan)
                ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
            VulkanFragmentProbe probe;
            if (vulkan) {
                ASSERT_FALSE(vs.empty());
                ASSERT_FALSE(ps.empty());
                probe.initialize(vs, ps, {});
            }
            for (const auto values:
                 {std::array{.5f, .25f, .25f}, std::array{-2.f, 3.f, 1.f}, std::array{2.f, -3.f, 6.f}, std::array{8.f, .5f, -1.f}}) {
                VulkanFragmentProbe::Inputs inputs{};
                for (unsigned i = 0; i < 3; ++i)
                    inputs[i][0] = values[i];
                float expected = (values[0] * values[1] + values[2]) * scale;
                if (clamp)
                    expected = std::clamp(expected, 0.f, 1.f);
                const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
                ASSERT_TRUE(reference.colorValid);
                for (float c: reference.color)
                    EXPECT_FLOAT_EQ(c, expected);
                if (vulkan)
                    for (const auto &pixel: probe.draw(inputs))
                        for (float c: pixel)
                            EXPECT_FLOAT_EQ(c, expected);
            }
        }
}
