#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

#include "gfx/LatteShaderLowering.hpp"
#include "gfx/PipelineManifest.hpp"
#include "gfx/VulkanRasterBackend.hpp"
#include "ui/PreloadScreen.hpp"
#include "utils/PrivateDirectory.hpp"

using namespace Core::Gfx;

namespace {
    struct TemporaryDirectory {
            std::filesystem::path path;
            TemporaryDirectory()
            {
                char name[] = "/tmp/wemu-manifest-test-XXXXXX";
                const auto *result = ::mkdtemp(name);
                if (!result)
                    throw std::runtime_error("Cannot create temporary directory");
                path = result;
            }
            ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    };

    const std::vector<std::uint32_t> pixelWords{0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    // POS0 exports R1 (bank 3, vector 16) and PARAM0 exports R2.
    const std::vector<std::uint32_t> vertexWords{4u | (3u << 22) | (1u << 30),
                                                 (8u << 26) | (1u << 2),
                                                 60u | (1u << 13) | (1u << 15),
                                                 (Latte::CF_EXP << 23) | 0x688u,
                                                 (2u << 13) | (2u << 15),
                                                 (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                                 0,
                                                 0,
                                                 0x80000000u | 128u,
                                                 (Latte::OP2_MOV << 7) | 16u | (1u << 21)};

    PipelineRecipe recipe(const std::shared_ptr<const Latte::ShaderOrigin> &vertex, const std::shared_ptr<const Latte::ShaderOrigin> &fragment,
                          std::uint32_t tag)
    {
        return {vertex, fragment, tag & 1, {1, tag, 3}};
    }
} // namespace

TEST(PipelineManifestTest, CachedShadersRecordOriginsThatLowerToTheSameSource)
{
    Latte::FragmentShaderCache fragments;
    const std::vector<Latte::TextureType> types{Latte::TextureType::Cube, Latte::TextureType::TwoD};
    const auto position = fragments.get(pixelWords, types, false, 0, 0);
    ASSERT_TRUE(position->origin);
    EXPECT_EQ(position->origin->words, pixelWords);
    EXPECT_EQ(position->origin->resourceTypes, std::vector{Latte::TextureType::Cube});
    EXPECT_EQ(position->origin->positionRegister, 0u);
    const auto again = Latte::lowerOrigin(position->origin);
    EXPECT_EQ(again->source, position->source);
    EXPECT_EQ(again->sourceHash, position->sourceHash);
    EXPECT_EQ(again->origin, position->origin);

    Latte::VertexShaderCache vertices;
    Latte::UniformBufferWindows windows{};
    windows[3] = 64;
    for (const auto &shader: {vertices.get(vertexWords), vertices.get(vertexWords, 0, windows)}) {
        ASSERT_TRUE(*shader) << shader->error;
        ASSERT_TRUE(shader->origin && shader->origin->vertex);
        const auto lowered = Latte::lowerOrigin(shader->origin);
        EXPECT_EQ(lowered->source, shader->source);
        EXPECT_EQ(lowered->sourceHash, shader->sourceHash);
    }
    EXPECT_FALSE(Latte::lowerOrigin(nullptr));
}

TEST(PipelineManifestTest, SerializationSharesProgramsAndOriginsAndRoundTripsRecipes)
{
    Latte::FragmentShaderCache fragments;
    Latte::VertexShaderCache vertices;
    const auto pixel = fragments.get(pixelWords)->origin;
    const auto colorZero = fragments.get(pixelWords, {}, true, 2)->origin;
    const auto vertex = vertices.get(vertexWords)->origin;
    const std::vector<PipelineRecipe> recipes{recipe(vertex, pixel, 1), recipe(nullptr, pixel, 2), recipe(vertex, colorZero, 3),
                                              recipe(std::make_shared<const Latte::ShaderOrigin>(*vertex), pixel, 4)};
    const auto bytes = PipelineManifest::serialize(recipes);
    const auto parsed = PipelineManifest::parse(bytes);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(*parsed, recipes);
    EXPECT_EQ((*parsed)[0].vertex, (*parsed)[3].vertex); // equal content becomes one origin
    EXPECT_EQ((*parsed)[0].fragment, (*parsed)[1].fragment);
    EXPECT_FALSE((*parsed)[1].vertex);
    EXPECT_TRUE((*parsed)[2].fragment->onlyColorZero);
    EXPECT_EQ((*parsed)[2].fragment->colorExport, 2u);
    // Two programs are stored once each although four origins reference them.
    EXPECT_LT(bytes.size(), 200u + 4 * 22 * 4 + (pixelWords.size() + vertexWords.size()) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 7) {
        auto corrupt = bytes;
        corrupt[i] ^= 0x5A;
        EXPECT_FALSE(PipelineManifest::parse(corrupt)) << i;
    }
    EXPECT_FALSE(PipelineManifest::parse(std::span(bytes).first(bytes.size() - 4)));
    EXPECT_FALSE(PipelineManifest::parse({}));
}

TEST(PipelineManifestTest, AddRejectsDuplicatesAndInconsistentStagesThenPersistsAcrossSessions)
{
    TemporaryDirectory root;
    Latte::FragmentShaderCache fragments;
    Latte::VertexShaderCache vertices;
    const auto pixel = fragments.get(pixelWords)->origin;
    const auto vertex = vertices.get(vertexWords)->origin;
    {
        PipelineManifest manifest(root.path.string(), "title.manifest");
        ASSERT_TRUE(manifest.enabled());
        EXPECT_TRUE(manifest.load().empty());
        EXPECT_TRUE(manifest.add(recipe(vertex, pixel, 1)));
        EXPECT_FALSE(manifest.add(recipe(std::make_shared<const Latte::ShaderOrigin>(*vertex), pixel, 1)));
        EXPECT_FALSE(manifest.add(recipe(pixel, pixel, 2)));
        EXPECT_FALSE(manifest.add(recipe(vertex, vertex, 3)));
        EXPECT_FALSE(manifest.add(recipe(vertex, nullptr, 4)));
        EXPECT_TRUE(manifest.add(recipe(nullptr, pixel, 5)));
        EXPECT_EQ(manifest.size(), 2u);
        EXPECT_TRUE(manifest.finish());
    }
    PipelineManifest next(root.path.string(), "title.manifest");
    const auto loaded = next.load();
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0], recipe(vertex, pixel, 1));
    EXPECT_EQ(loaded[1], recipe(nullptr, pixel, 5));
    EXPECT_FALSE(next.add(recipe(vertex, pixel, 1)));
    EXPECT_TRUE(next.finish());

    const auto file = root.path / "title.manifest";
    auto bytes = *Utils::PrivateDirectory(root.path.string()).read("title.manifest", PipelineManifest::maxFileBytes);
    bytes[bytes.size() / 2] ^= 1;
    std::filesystem::remove(file);
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    ASSERT_EQ(::chmod(file.c_str(), 0600), 0);
    EXPECT_TRUE(PipelineManifest(root.path.string(), "title.manifest").load().empty());
    EXPECT_FALSE(PipelineManifest("", "title.manifest").enabled());
    EXPECT_FALSE(PipelineManifest(root.path.string(), "").enabled());
}

TEST(PipelineManifestTest, FilenameFollowsTheExecutableContents)
{
    TemporaryDirectory root;
    const auto write = [&](const char *name, const char *bytes) {
        std::ofstream(root.path / name, std::ios::binary) << bytes;
        return root.path / name;
    };
    const auto first = PipelineManifest::filenameFor(write("Turbo.rpx", "code"));
    EXPECT_TRUE(first.starts_with("Turbo-") && first.ends_with(".manifest")) << first;
    EXPECT_EQ(PipelineManifest::filenameFor(write("copy.rpx", "code")).substr(5), first.substr(6));
    EXPECT_NE(PipelineManifest::filenameFor(write("Turbo.rpx", "update")), first);
    EXPECT_EQ(PipelineManifest::filenameFor(write("we ird/..rpx", "x").parent_path() / "missing.rpx"), "");
}

TEST(PipelineManifestTest, PreloadedPipelineServesTheRecordedDrawWithoutCreatingAnother)
{
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    TemporaryDirectory root;
    std::array<RasterDraw::Vertex, 1> points{{{.5f, .5f, {}}}};
    points[0].inputs[0] = {.25f, .75f, 1.f, 1.f};
    std::vector<std::uint8_t> target(4);
    const auto draw = [&](VulkanRasterBackend &backend, const Latte::FragmentShader &shader) {
        RasterDraw raster{shader, points, {}, {}, target, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
        raster.topology = RasterDraw::Topology::Points;
        const auto result = backend.render(raster);
        EXPECT_TRUE(result) << backend.lastError();
        return result ? std::vector<float>(result->rgba.begin(), result->rgba.end()) : std::vector<float>{};
    };
    auto manifest = std::make_shared<PipelineManifest>(root.path.string(), "title.manifest");
    std::vector<float> recorded;
    {
        Latte::FragmentShaderCache fragments;
        VulkanRasterBackend backend(true, true);
        backend.setPipelineManifest(manifest);
        recorded = draw(backend, *fragments.get(pixelWords, {}, false, 0, 0));
        EXPECT_EQ(backend.pipelineCreations(), 1u);
    }
    ASSERT_EQ(manifest->size(), 1u);
    ASSERT_TRUE(manifest->finish());
    const auto recipes = PipelineManifest(root.path.string(), "title.manifest").load();
    ASSERT_EQ(recipes.size(), 1u);

    Latte::FragmentShaderCache fragments;
    VulkanRasterBackend backend(true, true);
    std::vector<std::pair<std::size_t, std::size_t>> progress;
    const auto created = backend.preload(
            recipes, [&](const auto &origin) { return fragments.get(origin->words, origin->resourceTypes, false, 0, origin->positionRegister); },
            [&](std::size_t done, std::size_t total) {
                progress.emplace_back(done, total);
                return true;
            });
    EXPECT_EQ(created, 1u);
    EXPECT_EQ(backend.pipelineCreations(), 1u);
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.back().first, progress.back().second);
    EXPECT_EQ(draw(backend, *fragments.get(pixelWords, {}, false, 0, 0)), recorded);
    EXPECT_EQ(backend.pipelineCreations(), 1u);
    EXPECT_EQ(backend.preload(recipes, {}, {}), 0u);
}

TEST(PreloadScreenTest, ShowsTheCountAndABarProportionalToProgress)
{
    using namespace Core::UI;
    const auto lit = [](const std::vector<std::uint8_t> &frame, unsigned y, unsigned x0, unsigned x1) {
        unsigned count = 0;
        for (unsigned x = x0; x < x1; ++x)
            count += frame[(std::size_t(y) * preloadScreenWidth + x) * 4] == 0xE0;
        return count;
    };
    const auto half = preloadScreen(50, 100);
    ASSERT_EQ(half.size(), std::size_t(preloadScreenWidth) * preloadScreenHeight * 4);
    EXPECT_EQ(lit(half, 436, 0, preloadScreenWidth), 320u);
    EXPECT_GT(lit(half, 300, 0, preloadScreenWidth), 50u); // "Preparing shaders"
    EXPECT_EQ(lit(half, 10, 0, preloadScreenWidth), 0u);
    EXPECT_EQ(lit(preloadScreen(7, 7), 436, 0, preloadScreenWidth), 640u);
    EXPECT_EQ(lit(preloadScreen(9, 7), 436, 0, preloadScreenWidth), 640u);
    EXPECT_EQ(lit(preloadScreen(0, 7), 436, 0, preloadScreenWidth), 0u);
}
