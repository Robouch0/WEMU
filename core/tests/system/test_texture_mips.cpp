#include <gtest/gtest.h>

#include "gfx/ColorTarget.hpp"
#include "gfx/CubeSampler.hpp"
#include "gfx/LatteShaderLowering.hpp"
#include "gfx/LatteSurfaceLayout.hpp"
#include "gfx/LatteVsInterp.hpp"
#include "gfx/SpirvCompiler.hpp"
#include "gfx/TextureFormat.hpp"
#include "gfx/VolumeSampler.hpp"
#include "gfx/VulkanRasterBackend.hpp"

using namespace Core::Gfx;

namespace {
    std::vector<unsigned> explicitCubeProgram()
    {
        using namespace Latte;
        return {2,
                (CF_TEX << 23) | (1u << 10),
                3u << 15,
                (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                0x0Eu | (1u << 16),
                0x001FFE00,
                0,
                0,
                0x11,
                0xF00D1003,
                0x68800000,
                0};
    }
} // namespace

TEST(LatteTextureTest, CubeIndexAndExplicitLodReachReferenceFetcher)
{
    const auto words = explicitCubeProgram();
    const auto program = LatteVsInterp::compile(words);
    ASSERT_TRUE(program->valid);
    EXPECT_FALSE(LatteVsInterp::rejectsEveryPixel(*program));
    const std::array<std::array<float, 4>, 2> inputs{{{1.25f, 1.75f, 3, .75f}, {2.5f, 0, 0, 0}}};
    unsigned calls = 0;
    const auto result = LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {}, {}, {},
                                                [&](const LatteVsInterp::TextureRequest &request, std::array<float, 4> &color) {
                                                    ++calls;
                                                    EXPECT_EQ(request.resource, 0u);
                                                    EXPECT_EQ(request.sampler, 0u);
                                                    EXPECT_EQ(request.coords, inputs[0]);
                                                    EXPECT_EQ(request.cubeIndex, 2.5f);
                                                    EXPECT_TRUE(request.explicitLod);
                                                    color = {2, .25f, .5f, 1};
                                                    return true;
                                                });
    ASSERT_TRUE(result.colorValid);
    EXPECT_EQ(result.color, (std::array<float, 4>{2, .25f, .5f, 1}));
    EXPECT_EQ(calls, 1u);
    EXPECT_FALSE(LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {}).colorValid);
    auto invalid = words;
    invalid[6] = 6u << 20;
    EXPECT_FALSE(Latte::lowerFragmentShader(*Latte::decodeProgram(invalid)));
    EXPECT_FALSE(LatteVsInterp::runPixel(*Latte::decodeProgram(invalid), inputs, nullptr, 0, {}).colorValid);
}

TEST(LatteTextureTest, R600CubeMipLayoutsKeepPaddedSlicesAndStorageSizes)
{
    const auto base = latteSurfaceLayout(32, 32, 6, 0x816, 3, 4, 0);
    const auto mip = latteSurfaceLayout(32, 32, 6, 0x816, 3, 4, 1);
    ASSERT_TRUE(base);
    ASSERT_TRUE(mip);
    EXPECT_EQ(base->pitch, 32u);
    EXPECT_EQ(base->height, 32u);
    EXPECT_EQ(base->tileMode, 4u);
    EXPECT_EQ(base->imageSize, base->pitch * base->height * base->depth * 4u);
    EXPECT_GE(mip->depth, 6u);
    EXPECT_EQ(mip->tileMode, 2u);
    EXPECT_EQ(mip->imageSize, mip->pitch * mip->height * mip->depth * 4u);
    EXPECT_FALSE(latteSurfaceLayout(32, 16, 6, 0x816, 3, 4, 0));
    EXPECT_FALSE(latteSurfaceLayout(32, 32, 5, 0x816, 3, 4, 0));
    EXPECT_FALSE(latteSurfaceLayout(32, 32, 6, 0x816, 3, 7, 0));
    EXPECT_FALSE(latteSurfaceLayout(8192, 8192, 2048, 0x823, 5, 4, 0));
}

TEST(VulkanCubeTest, FacesArrayIndicesMipsSeamsAndRefreshMatchReference)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const std::array types{Latte::TextureType::Cube};
    const auto shader = Latte::lowerFragmentShader(*Latte::decodeProgram(explicitCubeProgram()), types);
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.textures.size(), 1u);
    EXPECT_TRUE(shader.textures[0].explicitLod);
    EXPECT_FALSE(shader.textures[0].implicitLod);
    std::vector<float> base(2 * 2 * 12 * 4), mip(12 * 4);
    for (unsigned layer = 0; layer < 12; ++layer) {
        const std::array<float, 4> color{float(layer + 1) / 16.f, .2f, .4f, 1};
        const std::array<float, 4> smaller{float(layer + 1) / 8.f, .8f, .6f, 1};
        for (unsigned pixel = 0; pixel < 4; ++pixel)
            std::copy(color.begin(), color.end(), base.begin() + (layer * 4 + pixel) * 4);
        std::copy(smaller.begin(), smaller.end(), mip.begin() + layer * 4);
    }
    std::array<RasterDraw::Texture::MipLevel, 1> mips{{{1, 1, mip}}};
    RasterDraw::Texture texture{shader.textures[0], 2, 2, {}, [](unsigned, unsigned) { return std::array<float, 4>{}; }};
    texture.rgba32 = base;
    texture.mips = mips;
    texture.layers = 12;
    texture.sourceMipCount = 2;
    texture.sampler.regs[1] = 64u << 10;
    std::array<RasterDraw::Vertex, 1> vertices{{{.5f, .5f, {}}}};
    std::vector<std::uint8_t> target(16);
    RasterDraw draw{shader, vertices, {}, std::span{&texture, 1}, target, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.topology = RasterDraw::Topology::Points;
    VulkanRasterBackend backend(true, true);
    for (unsigned face = 0; face < 6; ++face)
        for (float index: {-1.f, 0.f, .5f, 1.f, 8.f})
            for (float lod: {-4.f, 0.f, .5f, 1.f, 8.f}) {
                SCOPED_TRACE(face);
                SCOPED_TRACE(index);
                SCOPED_TRACE(lod);
                texture.sampler.regs[0] = 2u | (2u << 3) | (2u << 17);
                vertices[0].inputs[0] = {1.5f, 1.5f, float(face), lod};
                vertices[0].inputs[1] = {index, 0, 0, 0};
                const auto result = backend.render(draw);
                ASSERT_TRUE(result) << backend.lastError();
                const auto color = readFloatColor(result->rgba.data());
                const auto layer = unsigned(std::clamp(std::floor(index + .5f), 0.f, 1.f)) * 6 + face;
                const auto blend = std::clamp(lod, 0.f, 1.f);
                for (unsigned c = 0; c < 4; ++c)
                    EXPECT_NEAR(color[c], std::lerp(base[layer * 16 + c], mip[layer * 4 + c], blend), 0.0001f);
            }
    texture.sampler.regs[0] = 2u | (2u << 3) | (1u << 9) | (1u << 12) | (1u << 17);
    for (unsigned face = 0; face < 6; ++face)
        for (float s: {1.f, 1.25f, 1.5f, 2.f})
            for (float t: {1.f, 1.5f, 2.f}) {
                SCOPED_TRACE(face);
                SCOPED_TRACE(s);
                SCOPED_TRACE(t);
                vertices[0].inputs[0] = {s, t, float(face), 0};
                vertices[0].inputs[1] = {0, 0, 0, 0};
                const auto reference = sampleCube(2, s, t, face, true, [&](unsigned layer, unsigned x, unsigned y) {
                    std::array<float, 4> color{};
                    std::copy_n(base.data() + ((layer * 2 + y) * 2 + x) * 4, 4, color.begin());
                    return color;
                });
                const auto result = backend.render(draw);
                ASSERT_TRUE(result) << backend.lastError();
                const auto color = readFloatColor(result->rgba.data());
                for (unsigned c = 0; c < 4; ++c)
                    // Cube-corner filtering uses the device's finite subtexel precision.
                    EXPECT_NEAR(color[c], reference[c], .001f);
            }
    vertices[0].inputs[0] = {1.5f, 1.5f, 0, 1};
    mip[0] = 3.f;
    auto result = backend.render(draw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_FLOAT_EQ(readFloatColor(result->rgba.data())[0], 3.f);
    const auto submissions = backend.queueSubmissions();
    mips[0].rgba32 = std::span(mip).first(1);
    EXPECT_FALSE(backend.render(draw));
    EXPECT_EQ(backend.queueSubmissions(), submissions);
    mips[0].rgba32 = mip;
    texture.sourceMipCount = 3;
    EXPECT_FALSE(backend.render(draw));
    EXPECT_EQ(backend.queueSubmissions(), submissions);
    texture.sourceMipCount = 2;
    texture.layers = 5;
    EXPECT_FALSE(backend.render(draw));
    EXPECT_EQ(backend.queueSubmissions(), submissions);
}

TEST(LatteTextureTest, SelectedBurstExportsUseTheirOwnRegistersAndCacheKeys)
{
    const std::vector<unsigned> words{0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | (5u << 17) | 0x688u};
    const auto program = Latte::decodeProgram(words);
    std::array<std::array<float, 4>, 6> inputs{};
    Latte::FragmentShaderCache cache;
    std::array<std::shared_ptr<const Latte::FragmentShader>, 6> shaders;
    for (unsigned slot = 0; slot < 6; ++slot) {
        inputs[slot] = {float(slot) + .125f, .25f, .5f, 1};
        shaders[slot] = cache.get(words, {}, true, slot);
        ASSERT_TRUE(*shaders[slot]) << shaders[slot]->error;
        EXPECT_TRUE(shaders[slot]->requiresOnlyColorZero);
        const auto output = LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {}, {}, {}, {}, slot);
        ASSERT_TRUE(output.colorValid);
        EXPECT_EQ(output.color, inputs[slot]);
        if (slot)
            EXPECT_NE(shaders[slot]->source, shaders[slot - 1]->source);
    }
    EXPECT_EQ(cache.size(), 6u);
    EXPECT_EQ(cache.get(words, {}, true, 5), shaders[5]);
    EXPECT_FALSE(Latte::lowerFragmentShader(*program, {}, true, 8));
    EXPECT_FALSE(Latte::lowerFragmentShader(*program, {}, false, 1));
    if (!std::getenv("WEMU_TEST_VULKAN"))
        return;
    std::array<RasterDraw::Vertex, 1> vertices{{{.5f, .5f, {}}}};
    std::copy(inputs.begin(), inputs.end(), vertices[0].inputs.begin());
    std::vector<std::uint8_t> target(16);
    VulkanRasterBackend backend;
    for (unsigned slot = 0; slot < 6; ++slot) {
        RasterDraw draw{*shaders[slot], vertices, {}, {}, target, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
        draw.topology = RasterDraw::Topology::Points;
        draw.colorFormat = ColorFormat::RGBA32Float;
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        EXPECT_EQ(readFloatColor(result->rgba.data()), inputs[slot]);
    }
}

TEST(VulkanCubeTest, ChangingLiveVertexCountReusesCapacityWithoutDrawingStaleVertices)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto shader = Latte::lowerFragmentShader(*Latte::decodeProgram({0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    std::vector<RasterDraw::Vertex> vertices(9);
    for (unsigned index = 0; index < vertices.size(); ++index) {
        vertices[index].x = float(index % 8) + .5f;
        vertices[index].y = .5f;
        vertices[index].inputs[0] = {1, 0, 0, 1};
    }
    std::vector<std::uint8_t> target(8 * 4);
    RasterDraw draw{shader, {}, {}, {}, target, 8, 1, 8, {0, 0, 8, 1}, 15, {}};
    draw.topology = RasterDraw::Topology::Points;
    VulkanRasterBackend backend;
    unsigned reused = 0, draws = 0;
    for (const unsigned count: {5u, 7u, 6u, 8u, 9u, 5u}) {
        draw.vertices = std::span(vertices).first(count);
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        for (unsigned x = 0; x < 8; ++x) {
            EXPECT_EQ(result->rgba[x * 4], x < count ? 255 : 0);
            EXPECT_EQ(result->rgba[x * 4 + 3], x < count ? 255 : 0);
        }
        if (draws && count != 9)
            ++reused;
        EXPECT_EQ(backend.reusedDraws(), reused);
        ++draws;
    }
}

TEST(TextureFormatTest, SignedBc4PreservesNegativeEndpointsAndBothInterpolationModes)
{
    for (const auto endpoints: {std::pair{127, -127}, std::pair{-127, 127}, std::pair{-128, 64}}) {
        std::array<std::uint8_t, 8> block{};
        block[0] = std::uint8_t(endpoints.first);
        block[1] = std::uint8_t(endpoints.second);
        std::uint64_t selectors{};
        for (unsigned i = 0; i < 16; ++i)
            selectors |= std::uint64_t(i % 8) << (i * 3);
        for (unsigned i = 0; i < 6; ++i)
            block[i + 2] = std::uint8_t(selectors >> (i * 8));
        const float a = float(std::max(endpoints.first, -127)) / 127.f;
        const float b = float(std::max(endpoints.second, -127)) / 127.f;
        for (unsigned i = 0; i < 16; ++i) {
            const unsigned select = i % 8;
            const float expected = select == 0   ? a
                                   : select == 1 ? b
                                   : a > b       ? ((8 - select) * a + (select - 1) * b) / 7.f
                                   : select < 6  ? ((6 - select) * a + (select - 1) * b) / 5.f
                                   : select == 6 ? -1.f
                                                 : 1.f;
            EXPECT_NEAR(decodeBc4Snorm(block.data(), i), expected, 1.e-7f);
        }
    }
}

TEST(LatteTextureTest, VolumeFilteringUsesNormalizedZAndIndependentAddressModes)
{
    TextureSampler sampler;
    sampler.regs[0] = 2u | (2u << 3) | (2u << 6) | (1u << 9) | (1u << 12);
    const auto fetch = [](unsigned x, unsigned y, unsigned z) { return std::array<float, 4>{float(x), float(y), float(z), 1}; };
    EXPECT_EQ(sampleVolume(sampler, 2, 2, 2, .5f, .5f, .5f, fetch), (std::array<float, 4>{.5f, .5f, .5f, 1}));
    EXPECT_EQ(sampleVolume(sampler, 2, 2, 2, .25f, .75f, -1, fetch), (std::array<float, 4>{0, 1, 0, 1}));
    EXPECT_EQ(sampleVolume(sampler, 2, 2, 2, .25f, .75f, 2, fetch), (std::array<float, 4>{0, 1, 1, 1}));
    sampler.regs[0] &= ~(7u << 6); // Repeat Z without changing XY's clamp mode.
    EXPECT_EQ(sampleVolume(sampler, 2, 2, 2, .25f, .75f, 1.25f, fetch), (std::array<float, 4>{0, 1, 0, 1}));
    EXPECT_EQ(sampleVolume(sampler, 2, 2, 2, .25f, .75f, -.25f, fetch), (std::array<float, 4>{0, 1, 1, 1}));
    EXPECT_EQ(sampleVolume(sampler, 2, 2, 2, 0, 0, std::numeric_limits<float>::quiet_NaN(), fetch), sampler.border());
}

TEST(VulkanCubeTest, SharedSnapshotsKeepDuplicateBindingsVersionsAndDeviceLifetime)
{
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    using namespace Latte;
    const auto shader = lowerFragmentShader(*decodeProgram({2, (CF_TEX << 23) | (1u << 10), 1u << 15, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x13,
                                                            0x300D1001, 0x68800000, 0, 0x113, 0x300D1001, 0x68808000, 0}));
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.textures.size(), 2u);
    std::array<RasterDraw::Texture, 2> textures;
    for (unsigned i = 0; i < 2; ++i) {
        textures[i] = {shader.textures[i], 1, 1, {}, [](unsigned, unsigned) { return std::array<float, 4>{}; }};
        textures[i].unorm8Pitch = 1;
    }
    std::array<RasterDraw::Vertex, 1> vertices{{{.5f, .5f, {{{.5f, .5f, 0, 1}}}}}};
    std::array<std::uint8_t, 4> target{};
    RasterDraw draw{shader, vertices, {}, textures, target, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
    draw.topology = RasterDraw::Topology::Points;
    // Bundles keep their uploads alive after the strong cache evicts them.
    auto backend = std::make_unique<VulkanRasterBackend>(true, true, false);
    constexpr unsigned snapshots = 2052; // four more than the strong cache entry limit
    std::vector<RasterResult> results;
    std::weak_ptr<const std::vector<std::uint8_t>> firstOwner, lastOwner;
    for (unsigned i = 0; i < snapshots; ++i) {
        auto owner = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{std::uint8_t(i % 240 + 1), 10, 20, 255});
        if (!i)
            firstOwner = owner;
        lastOwner = owner;
        draw.cullFront = i == 0; // Keep the first bundle alive while the other pipeline rotates.
        for (auto &texture: textures) {
            texture.unorm8 = *owner;
            texture.snapshotOwner = owner;
        }
        auto result = backend->renderDeferred(draw);
        ASSERT_TRUE(result) << backend->lastError();
        results.push_back(std::move(*result));
    }
    // The strong cache evicted the first upload, but its other draw bundle
    // still owns it. Recover it through the weak registry across pipelines.
    auto first = firstOwner.lock();
    ASSERT_TRUE(first);
    for (auto &texture: textures) {
        texture.unorm8 = *first;
        texture.snapshotOwner = first;
    }
    auto recovered = backend->renderDeferred(draw);
    ASSERT_TRUE(recovered) << backend->lastError();
    results.push_back(std::move(*recovered));
    first.reset();
    EXPECT_EQ(backend->sharedTextureUploads(), snapshots); // Duplicate bindings and surviving uploads are shared.
    EXPECT_GE(backend->sharedTextureHits(), snapshots + 2);
    EXPECT_LE(backend->sharedTextureBytes(), (snapshots - 3) * 8u); // Strong entries plus the separately retained first image.
    backend.reset();
    textures = {};
    EXPECT_FALSE(lastOwner.expired()); // Deferred images retain the device and its shared cache.
    for (unsigned i = 0; i < results.size(); ++i) {
        results[i].resolve();
        EXPECT_EQ(results[i].rgba, (std::vector<std::uint8_t>{std::uint8_t(i == snapshots ? 1 : i % 240 + 1), 10, 20, 255}));
    }
    EXPECT_TRUE(firstOwner.expired());
    results.clear();
    EXPECT_TRUE(lastOwner.expired());
}

TEST(VulkanCubeTest, CompressedBaseLevelValidatesPayloadAndKeepsViewMapping)
{
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    using namespace Latte;
    const auto shader = lowerFragmentShader(
            *decodeProgram({2, CF_TEX << 23, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x10, 0x30000000u | (0x688u << 9), 1u << 23, 0}));
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.textures.size(), 1u);
    for (const auto format: {TextureBlockFormat::BC1, TextureBlockFormat::BC2, TextureBlockFormat::BC3}) {
        SCOPED_TRACE(unsigned(format));
        const unsigned blockBytes = format == TextureBlockFormat::BC1 ? 8 : 16;
        std::vector<std::uint8_t> blocks(4 * blockBytes);
        for (unsigned i = 0; i < blocks.size(); i += blockBytes) {
            if (blockBytes == 16)
                std::fill_n(blocks.begin() + i, 8, 255); // Opaque alpha.
            const unsigned color = i + (blockBytes == 16 ? 8 : 0);
            blocks[color] = blocks[color + 1] = 255; // White endpoint, palette index zero.
        }
        std::array<RasterDraw::Texture, 1> textures{{{shader.textures[0], 7, 5, {}, [](unsigned, unsigned) { return std::array<float, 4>{}; }}}};
        textures[0].blocks = blocks;
        textures[0].blockFormat = format;
        std::array<RasterDraw::Vertex, 1> vertices{{{.5f, .5f, {{{.5f, .5f, 0, 1}}}}}};
        std::array<std::uint8_t, 4> target{};
        RasterDraw draw{shader, vertices, {}, textures, target, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
        draw.topology = RasterDraw::Topology::Points;
        VulkanRasterBackend backend;
        auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        EXPECT_EQ(result->rgba, (std::vector<std::uint8_t>{255, 255, 255, 255}));
        textures[0].unorm8Map = 0x04050100; // ZERO, ONE, G, R.
        result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        EXPECT_EQ(result->rgba, (std::vector<std::uint8_t>{0, 255, 255, 255}));
        textures[0].blocks = std::span(blocks).first(blocks.size() - 1);
        EXPECT_FALSE(backend.render(draw)); // Partial block is never submitted.
        textures[0].blocks = blocks;
        textures[0].unorm8 = target;
        EXPECT_FALSE(backend.render(draw)); // Ambiguous representations are rejected.
        textures[0].unorm8 = {};
        textures[0].blockFormat = TextureBlockFormat::None;
        EXPECT_FALSE(backend.render(draw));
        EXPECT_EQ(backend.completedDraws(), 2u);
    }
}
