#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <gtest/gtest.h>
#include <limits>

#include "VulkanFragmentProbe.hpp"
#include "gfx/LatteShaderLowering.hpp"
#include "gfx/LatteVsInterp.hpp"
#include "gfx/SpirvCompiler.hpp"
#include "gfx/TextureSampler.hpp"
#include "gfx/VulkanRasterBackend.hpp"

using namespace Core::Gfx::Latte;

TEST(ColorTargetTest, PackedHdrRoundTripsFiniteValuesAndRoundsTiesToEven)
{
    using namespace Core::Gfx;
    EXPECT_EQ(colorFormat(0x816), ColorFormat::R11G11B10Float);
    EXPECT_EQ(colorElementBytes(ColorFormat::R11G11B10Float), 4u);
    for (unsigned fractionBits: {5u, 6u}) {
        SCOPED_TRACE(fractionBits);
        for (unsigned bits = 0; bits < (31u << fractionBits); ++bits)
            ASSERT_EQ(encodeUnsignedFloat(decodeUnsignedFloat(bits, fractionBits), fractionBits), bits);
        const auto quantum = std::ldexp(1.f, -14 - int(fractionBits));
        EXPECT_EQ(encodeUnsignedFloat(quantum * .5f, fractionBits), 0u);
        EXPECT_EQ(encodeUnsignedFloat(quantum * 1.5f, fractionBits), 2u);
        EXPECT_EQ(encodeUnsignedFloat(1.f + std::ldexp(1.f, -int(fractionBits) - 1), fractionBits), 15u << fractionBits);
        EXPECT_EQ(encodeUnsignedFloat(-1.f, fractionBits), 0u);
        EXPECT_EQ(encodeUnsignedFloat(-std::numeric_limits<float>::infinity(), fractionBits), 0u);
        EXPECT_EQ(encodeUnsignedFloat(std::numeric_limits<float>::infinity(), fractionBits), 31u << fractionBits);
        EXPECT_TRUE(std::isnan(decodeUnsignedFloat(encodeUnsignedFloat(std::numeric_limits<float>::quiet_NaN(), fractionBits), fractionBits)));
    }
    std::array<std::uint8_t, 4> bytes{};
    writeFloatColor(bytes.data(), {2.f, 4.f, .5f, 0.f}, 15, ColorFormat::R11G11B10Float);
    EXPECT_EQ(readFloatColor(bytes.data(), ColorFormat::R11G11B10Float), (std::array{2.f, 4.f, .5f, 1.f}));
    writeFloatColor(bytes.data(), {8.f, -1.f, 16.f, .25f}, 10, ColorFormat::R11G11B10Float);
    EXPECT_EQ(readFloatColor(bytes.data(), ColorFormat::R11G11B10Float), (std::array{2.f, 0.f, .5f, 1.f}));
    writeFloatColor(bytes.data(), {8.f, 9.f, 16.f, .25f}, 8, ColorFormat::R11G11B10Float);
    EXPECT_EQ(readFloatColor(bytes.data(), ColorFormat::R11G11B10Float), (std::array{2.f, 0.f, .5f, 1.f}));
}

TEST(ColorTargetTest, HalfFloatTargetsRoundTripAndRoundTiesToEven)
{
    using namespace Core::Gfx;
    EXPECT_EQ(colorFormat(0x820), ColorFormat::RGBA16Float);
    EXPECT_EQ(colorElementBytes(ColorFormat::RGBA16Float), 8u);
    for (unsigned bits = 0; bits < 0x10000; ++bits) {
        const auto value = decodeHalf(std::uint16_t(bits));
        if ((bits & 0x7C00) == 0x7C00 && (bits & 0x3FF))
            ASSERT_TRUE(std::isnan(value)) << bits;
        else
            ASSERT_EQ(encodeHalf(value), bits) << bits;
    }
    EXPECT_EQ(encodeHalf(65504.f), 0x7BFFu);
    EXPECT_EQ(encodeHalf(65520.f), 0x7C00u);
    EXPECT_EQ(encodeHalf(-1e9f), 0xFC00u);
    EXPECT_EQ(encodeHalf(1.f + std::ldexp(1.f, -11)), 0x3C00u);     // tie to even
    EXPECT_EQ(encodeHalf(1.f + 3 * std::ldexp(1.f, -11)), 0x3C02u); // tie to even
    EXPECT_EQ(encodeHalf(std::ldexp(1.f, -25)), 0u);                // half of the smallest subnormal
    EXPECT_EQ(encodeHalf(3 * std::ldexp(1.f, -25)), 2u);
    EXPECT_TRUE(std::isnan(decodeHalf(encodeHalf(std::numeric_limits<float>::quiet_NaN()))));
    std::array<std::uint8_t, 8> bytes{};
    writeFloatColor(bytes.data(), {2.f, -4.f, .5f, 1024.f}, 15, ColorFormat::RGBA16Float);
    EXPECT_EQ(readFloatColor(bytes.data(), ColorFormat::RGBA16Float), (std::array{2.f, -4.f, .5f, 1024.f}));
    writeFloatColor(bytes.data(), {8.f, 9.f, 16.f, .25f}, 5, ColorFormat::RGBA16Float);
    EXPECT_EQ(readFloatColor(bytes.data(), ColorFormat::RGBA16Float), (std::array{8.f, -4.f, 16.f, 1024.f}));
}

TEST(LatteTextureTest, TexelOffsetsReachReferenceFetcherInTexels)
{
    using namespace Core::Gfx;
    // SAMPLE_LZ with OFFSET_X=2, OFFSET_Y=-3, OFFSET_Z=1 half-texel units (MK8's BC3 encoder).
    const auto program = decodeProgram({2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x13u, 0x30000000u | (0x688u << 9),
                                        (1u << 23) | (2u << 26) | 2u | (0x1Du << 5) | (1u << 10), 0});
    const std::array<std::array<float, 4>, 1> inputs{{{.25f, .5f, 0, 0}}};
    unsigned calls = 0;
    const auto result = LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {}, {}, {},
                                                [&](const LatteVsInterp::TextureRequest &request, std::array<float, 4> &color) {
                                                    ++calls;
                                                    EXPECT_EQ(request.offset, (std::array{1.f, -1.5f, .5f}));
                                                    EXPECT_EQ(request.coords[0], .25f);
                                                    color = {.125f, .25f, .5f, 1};
                                                    return true;
                                                });
    ASSERT_TRUE(result.colorValid);
    EXPECT_EQ(result.color, (std::array<float, 4>{.125f, .25f, .5f, 1}));
    EXPECT_EQ(calls, 1u);
    // The plain sampler callback cannot apply offsets, so it must not be used for them.
    EXPECT_FALSE(LatteVsInterp::runPixel(*program, inputs, nullptr, 0, [](unsigned, unsigned, const std::array<float, 4> &, std::array<float, 4> &) {
                     return true;
                 }).colorValid);
}

TEST(VulkanPointsTest, PackedHdrBlendsPreservesPaddingAndSamplesResidentAndForeignImages)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    std::array<RasterDraw::Vertex, 2> points{{{1.5f, .5f, {}}, {1.5f, .5f, {}}}};
    for (auto &v: points)
        v.inputs[0] = {2.f, .5f, .25f, .125f};
    std::vector<std::uint8_t> target(6 * 2 * 4);
    for (unsigned i = 0; i < 12; ++i)
        writeFloatColor(target.data() + i * 4, {.5f, .25f, .75f, 0.f}, 15, ColorFormat::R11G11B10Float);
    RasterDraw draw{shader, points, {}, {}, target, 4, 2, 6, {1, 0, 2, 2}, 5, {true, 6, 1, 0, 1, 1, 0, {}}};
    draw.topology = RasterDraw::Topology::Points;
    draw.colorFormat = ColorFormat::R11G11B10Float;
    VulkanRasterBackend backend(true, true);
    auto first = backend.renderDeferred(draw);
    ASSERT_TRUE(first) << backend.lastError();
    ASSERT_TRUE(first->readback);
    draw.renderedTarget = first->readback;
    auto second = backend.renderDeferred(draw);
    ASSERT_TRUE(second) << backend.lastError();
    second->resolve();
    for (unsigned i = 0; i < 12; ++i)
        EXPECT_EQ(readFloatColor(second->rgba.data() + i * 4, draw.colorFormat),
                  (i == 1 ? std::array{8.5f, .25f, 1.75f, 1.f} : std::array{.5f, .25f, .75f, 1.f}));
    const auto sampled = lowerFragmentShader(*decodeProgram(
            {2, 1u << 23, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x13, 0x30000000u | (1u << 12) | (2u << 15) | (3u << 18), 1u << 23, 0}));
    ASSERT_TRUE(sampled) << sampled.error;
    RasterDraw::Texture texture{sampled.textures[0], 4, 2, {}, [](unsigned, unsigned) { return std::array<float, 4>{}; }};
    texture.rendered = first->readback;
    texture.renderedFormat = draw.colorFormat;
    texture.unorm8Pitch = 6;
    texture.unorm8Map = 0x02010005;
    std::array<RasterDraw::Vertex, 1> one{{{.5f, .5f, {}}}};
    one[0].inputs[0] = {.375f, .25f, 0, 1};
    std::vector<std::uint8_t> output(16);
    RasterDraw sampleDraw{sampled, one, {}, std::span{&texture, 1}, output, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
    sampleDraw.topology = RasterDraw::Topology::Points;
    sampleDraw.colorFormat = ColorFormat::RGBA32Float;
    auto result = backend.render(sampleDraw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(readFloatColor(result->rgba.data()), (std::array{1.25f, .25f, 4.5f, 1.f}));
    VulkanRasterBackend foreign(true, true);
    result = foreign.render(sampleDraw);
    ASSERT_TRUE(result) << foreign.lastError();
    EXPECT_EQ(readFloatColor(result->rgba.data()), (std::array{1.25f, .25f, 4.5f, 1.f}));
    // Explicit CPU snapshots and malformed pitches exercise the upload fallback.
    texture.r11g11b10Bytes = first->readback->resolve();
    texture.r11g11b10Pitch = 6;
    texture.rendered.reset();
    result = backend.render(sampleDraw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(readFloatColor(result->rgba.data()), (std::array{1.25f, .25f, 4.5f, 1.f}));
    texture.r11g11b10Pitch = 3;
    EXPECT_FALSE(backend.render(sampleDraw));
}

TEST(LatteLoweringTest, UnsupportedExportsIdentifyTheirEncoding)
{
    const auto shader = lowerFragmentShader(*decodeProgram({61, (0x28u << 23) | (1u << 21) | 0x688u}));
    EXPECT_FALSE(shader);
    EXPECT_NE(shader.error.find("cf=0 type=0 base=61 repeated=false"), std::string::npos);
    EXPECT_NE(shader.error.find("words=0000003D,14200688"), std::string::npos);
}

TEST(LatteReferenceTest, FullParameterAndPixelRegisterRangeIsPreserved)
{
    std::array<std::array<float, 4>, 32> attributes{};
    attributes[31] = {2.f, .25f, .5f, 1.f};
    const auto vertex = Core::Gfx::LatteVsInterp::run(
            {60u | (1u << 13) | (1u << 15), (CF_EXP << 23) | 0x688u, 31u | (2u << 13) | (32u << 15), (CF_EXP_DONE << 23) | (1u << 21) | 0x688u},
            attributes, nullptr, 0);
    ASSERT_TRUE(vertex.valid);
    ASSERT_TRUE(vertex.paramValid[31]);
    EXPECT_EQ(vertex.params[31], attributes[31]);
    const auto pixel = decodeProgram({31u << 15, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u});
    EXPECT_EQ(Core::Gfx::LatteVsInterp::runPixel(*pixel, attributes, nullptr, 0, {}).color, attributes[31]);
    std::array<std::array<float, 4>, 33> tooMany{};
    EXPECT_FALSE(Core::Gfx::LatteVsInterp::runPixel(*pixel, tooMany, nullptr, 0, {}).colorValid);
}

TEST(LatteReferenceTest, DirectExportMetadataIncludesBurstsAndBoundsRegisterInitialization)
{
    const auto exportWord = (CF_EXP_DONE << 23) | (1u << 21) | 0x688u;
    for (const auto [base, burst, expected]: {std::array{0u, 0u, 1u}, std::array{0u, 7u, 255u}, std::array{5u, 7u, 224u}, std::array{61u, 0u, 0u}}) {
        const auto program = decodeProgram({base | (127u << 15), exportWord | (burst << 17)});
        EXPECT_EQ(program->colorExportMask, expected);
        EXPECT_EQ(program->registerCount, 128u);
    }
    EXPECT_EQ(decodeProgram({1u << 22, exportWord})->colorExportMask, 0u);
    EXPECT_EQ(decodeProgram({0, (exportWord & ~0xFFFu) | 0xFFFu})->colorExportMask, 0u);
    EXPECT_EQ(decodeProgram({2u << 13, exportWord})->colorExportMask, 0u);
}

TEST(VulkanRasterTest, InactiveBurstExportsRequireCurrentTargetProofAndSeparateCacheEntries)
{
    using namespace Core::Gfx;
    const unsigned exportWord = (CF_EXP_DONE << 23) | (1u << 21) | (1u << 17) | 0x688u;
    const std::vector<unsigned> words{0, exportWord};
    FragmentShaderCache cache;
    const auto allTargets = cache.get(words);
    ASSERT_FALSE(*allTargets);
    const auto shader = cache.get(words, {}, true);
    ASSERT_TRUE(*shader) << shader->error;
    EXPECT_TRUE(shader->requiresOnlyColorZero);
    EXPECT_NE(shader, allTargets);
    EXPECT_EQ(cache.get(words), allTargets);
    const auto conditional = lowerFragmentShader(*decodeProgram({1, 0x0Au << 23, 0, exportWord}), {}, true);
    EXPECT_FALSE(conditional);
    EXPECT_NE(conditional.error.find("unconditional"), std::string::npos);
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    std::array<RasterDraw::Vertex, 1> points{{{.5f, .5f, {}}}};
    points[0].inputs[0] = {.25f, .4f, 1.f, 1.f};
    points[0].inputs[1] = {1.f, 0.f, 0.f, 1.f};
    std::vector<std::uint8_t> target(4);
    RasterDraw draw{*shader, points, {}, {}, target, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
    draw.topology = RasterDraw::Topology::Points;
    VulkanRasterBackend backend(true, true);
    const auto result = backend.render(draw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(result->rgba, (std::vector<std::uint8_t>{64, 102, 255, 255}));
    const auto submissions = backend.queueSubmissions();
    draw.enabledColorTargets = 3;
    EXPECT_FALSE(backend.render(draw));
    EXPECT_EQ(backend.queueSubmissions(), submissions);
    draw.enabledColorTargets = 1;
    const auto restored = backend.render(draw);
    ASSERT_TRUE(restored) << backend.lastError();
    EXPECT_EQ(restored->rgba, result->rgba);
}

TEST(LatteLoweringTest, ColorOnlyEffectAnalysisIgnoresUnusedReadsButRejectsObservableEffects)
{
    const std::vector<unsigned> words{2,          (CF_TEX << 23) | (1u << 10),
                                      0,          (CF_EXP_DONE << 23) | (1u << 21) | (7u << 17) | 0x688u,
                                      0x0E,       0x001FFE00,
                                      0x6DB00000, 0,
                                      0x11,       0xF01D1004,
                                      0x68868000, 0};
    FragmentShaderCache cache;
    ASSERT_TRUE(*cache.get(words, {}, true)); // Cube-index and explicit-LOD instructions now lower.
    EXPECT_TRUE(cache.hasUnconditionalColorOnlyEffects(words));
    EXPECT_TRUE(cache.hasUnconditionalColorOnlyEffects(words));
    const auto rejected = decodeProgram(words);
    EXPECT_FALSE(Core::Gfx::LatteVsInterp::rejectsEveryPixel(*rejected));
    for (float value: {0.f, .5f, 1.f}) {
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0].fill(value);
        const auto result = Core::Gfx::LatteVsInterp::runPixel(*rejected, inputs, nullptr, 0, {});
        EXPECT_FALSE(result.colorValid || result.depthValid || result.discarded);
    }
    auto effect = words;
    effect[4] = 0x02; // Unknown/memory texture operations cannot be eliminated.
    EXPECT_FALSE(cache.hasUnconditionalColorOnlyEffects(effect));
    effect = words;
    effect[2] = 61;
    effect[3] = (CF_EXP_DONE << 23) | (1u << 21) | 0xFF8u;
    EXPECT_FALSE(cache.hasUnconditionalColorOnlyEffects(effect));
    effect = words;
    effect.resize(8);
    EXPECT_FALSE(cache.hasUnconditionalColorOnlyEffects(effect));
    EXPECT_FALSE(cache.hasUnconditionalColorOnlyEffects(
            {2, 8u << 26, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 1u << 31, (0x2Cu << 7) | 16u})); // Kill/unknown ALU.
    EXPECT_FALSE(cache.hasUnconditionalColorOnlyEffects({0, (0x3Au << 23) | (1u << 21)})); // Memory export.
    std::vector<unsigned> balanced{4,
                                   9u << 26,
                                   5,
                                   CF_TEX << 23,
                                   7,
                                   10u << 26,
                                   0,
                                   (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                   0x80000000u | SRC_0 | (SRC_1 << 13),
                                   (OP2_PRED_SETGT << 7) | 12u,
                                   0x0E,
                                   0x001FFE00,
                                   0x6DB00000,
                                   0,
                                   0x80000000u,
                                   OP2_NOP << 7};
    EXPECT_TRUE(cache.hasUnconditionalColorOnlyEffects(balanced));
    const auto conditionalRead = decodeProgram(balanced);
    EXPECT_FALSE(Core::Gfx::LatteVsInterp::rejectsEveryPixel(*conditionalRead));
    VulkanFragmentProbe::Inputs inputs{};
    inputs[0] = {.25f, .5f, .75f, 1.f};
    const auto result = Core::Gfx::LatteVsInterp::runPixel(*conditionalRead, inputs, nullptr, 0, {});
    ASSERT_TRUE(result.colorValid);
    EXPECT_EQ(result.color, inputs[0]); // The inactive unsupported read must remain bypassable.
    balanced[1] = balanced[5] = 8u << 26; // A root execute-mask update is not restored.
    EXPECT_FALSE(cache.hasUnconditionalColorOnlyEffects(balanced));
}

TEST(VulkanVertexTest, SixteenAttributesAndPixelInputsMapHighParameterExports)
{
    using namespace Core::Gfx;
    const auto vs = lowerVertexShader(*decodeProgram(
            {60u | (1u << 13) | (9u << 15), (CF_EXP << 23) | 0x688u, 31u | (2u << 13) | (16u << 15), (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    const auto ps = lowerFragmentShader(*decodeProgram({15u << 15, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(vs) << vs.error;
    ASSERT_TRUE(ps) << ps.error;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    std::array<RasterDraw::Vertex, 1> points{};
    points[0].inputs[8] = {0, 0, 0, 1};
    points[0].inputs[15] = {2.f, .25f, .5f, 1.f};
    std::vector<std::uint8_t> bytes(16);
    RasterDraw draw{ps, points, {}, {}, bytes, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
    draw.topology = RasterDraw::Topology::Points;
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.vertexStage.shader = std::make_shared<VertexShader>(vs);
    draw.vertexStage.viewport = {0, 0, 1, 1, 0, 1};
    draw.vertexStage.inputParams[15] = 31;
    draw.vertexStage.inputCount = 16;
    VulkanRasterBackend backend(true, true);
    const auto result = backend.render(draw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(readFloatColor(result->rgba.data()), points[0].inputs[15]);
    // The screen-space adapter carries the same high pixel input independently
    // of the optional translated vertex stage.
    draw.vertexStage = {};
    points[0].x = points[0].y = .5f;
    const auto adapter = backend.render(draw);
    ASSERT_TRUE(adapter) << backend.lastError();
    EXPECT_EQ(readFloatColor(adapter->rgba.data()), points[0].inputs[15]);
}

TEST(LatteReferenceTest, VertexInputsReachAllThirtyTwoSemanticRegisters)
{
    std::array<std::array<float, 4>, 32> attributes{};
    for (unsigned i = 0; i < attributes.size(); ++i)
        attributes[i] = {float(i) + .25f, float(i) + .5f, float(i) + .75f, 1.f};
    for (unsigned reg: {1u, 5u, 9u, 32u}) {
        const std::vector<std::uint32_t> words{60u | (1u << 13) | (reg << 15), (CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
        const auto result = Core::Gfx::LatteVsInterp::run(words, attributes, nullptr, 0);
        ASSERT_TRUE(result.valid);
        EXPECT_EQ(result.pos, attributes[reg - 1]);
    }
    std::array<std::array<float, 4>, 33> tooMany{};
    EXPECT_FALSE(Core::Gfx::LatteVsInterp::run({60u | (1u << 13), (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}, tooMany, nullptr, 0).valid);
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
    for (std::uint32_t i = 0; i < 2 * FragmentShaderCache::maxEntries; ++i) {
        EXPECT_FALSE(*cache.get({i}));
        EXPECT_LE(cache.size(), FragmentShaderCache::maxEntries);
    }
    EXPECT_EQ(failed->error, error);
    EXPECT_NE(failed, cache.get({}));
    EXPECT_EQ(cache.hits(), 1u);
}

namespace {
    constexpr auto probeVertexSource = R"(#version 450
layout(push_constant) uniform Parameters { vec4 value[4]; } p;
layout(location=0) out vec4 inputs[16];
void main() {
    vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    for(int i=0;i<16;++i) inputs[i]=i<4 ? p.value[i] : vec4(0.0);
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

    std::vector<std::uint32_t> isolatedWholeQuadProgram()
    {
        return {5,
                9u << 26,
                4,
                (0x0Au << 23) | 1u,
                6,
                (8u << 26) | (1u << 30),
                7,
                10u << 26,
                3u << 15,
                (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                0x80000000u | (1u << 13),
                (OP2_PRED_SETGT << 7) | 12u,
                0x80000000u | SRC_1,
                (OP2_MOV << 7) | 16u | (4u << 21),
                0x80000000u | 4u,
                (OP2_MOV << 7) | 16u | (3u << 21)};
    }
} // namespace

TEST(LatteLoweringTest, IsolatedWholeQuadAluMatchesReferenceAndVulkan)
{
    const auto program = decodeProgram(isolatedWholeQuadProgram());
    for (bool condition: {false, true}) {
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0][0] = condition ? 2.f : 0.f;
        inputs[1][0] = 1.f;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
        ASSERT_TRUE(reference.colorValid);
        EXPECT_EQ(reference.color, (std::array{condition ? 1.f : 0.f, 0.f, 0.f, 0.f}));
        ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
    }
    if (!std::getenv("WEMU_TEST_VULKAN"))
        return;
    FragmentShader vertex;
    vertex.source = R"(#version 450
layout(location=0) out vec4 inputs[16];
void main() {
    vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    for(int i=0;i<16;++i) inputs[i]=vec4(0.0);
    inputs[0].x=gl_Position.x;
}
)";
    std::vector<std::uint32_t> vs, ps;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(lowerFragmentShader(*program), &ps));
    ASSERT_FALSE(vs.empty());
    ASSERT_FALSE(ps.empty());
    VulkanFragmentProbe probe;
    probe.initialize(vs, ps, {});
    const auto pixels = probe.draw({});
    for (unsigned i = 0; i < pixels.size(); ++i)
        EXPECT_EQ(pixels[i], (std::array{i % 4 >= 2 ? 1.f : 0.f, 0.f, 0.f, 0.f}));
}

TEST(LatteLoweringTest, WholeQuadValuesCannotEscapeThroughExportsOrRestoredAlu)
{
    auto words = isolatedWholeQuadProgram();
    words[8] = 4u << 15;
    auto shader = lowerFragmentShader(*decodeProgram(words));
    EXPECT_FALSE(shader);
    EXPECT_TRUE(shader.error.starts_with("whole-quad value escapes masked region:"));
    // Restoring then pushing again must not conceal an escaped register or PV/PS.
    for (unsigned selector: {4u, unsigned(SRC_PV), unsigned(SRC_PS)}) {
        for (bool scalar: {false, true}) {
            words = {7,
                     9u << 26,
                     8,
                     (8u << 26) | (1u << 30),
                     0,
                     (0x0Eu << 23) | 1u,
                     0,
                     0x0Bu << 23,
                     9,
                     10u << 26,
                     3u << 15,
                     (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                     0,
                     CF_RETURN << 23,
                     0x80000000u | (1u << 13),
                     (OP2_PRED_SETGT << 7) | 12u,
                     0x80000000u | SRC_1,
                     (OP2_MOV << 7) | 16u | (4u << 21),
                     0x80000000u | selector,
                     (OP2_MOV << 7) | 16u | (3u << 21)};
            if (scalar)
                words[17] = (OP2_RECIP_IEEE << 7) | 16u | (4u << 21);
            if ((selector == SRC_PS) != scalar)
                continue;
            shader = lowerFragmentShader(*decodeProgram(words));
            EXPECT_FALSE(shader) << selector << ' ' << scalar;
            EXPECT_TRUE(shader.error.starts_with("whole-quad value escapes masked region:"));
        }
    }
}

TEST(LatteLoweringTest, WholeQuadElseCanExposeTemporaryValues)
{
    const auto program =
            decodeProgram({6, 9u << 26, 7, (8u << 26) | (1u << 30), 4, (0x0Du << 23) | 1u, 8, 10u << 26, 3u << 15,
                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0, CF_RETURN << 23, 0x80000000u | (1u << 13), (OP2_PRED_SETGT << 7) | 12u,
                           0x80000000u | SRC_1, (OP2_MOV << 7) | 16u | (4u << 21), 0x80000000u | 4u, (OP2_MOV << 7) | 16u | (3u << 21)});
    const auto shader = lowerFragmentShader(*program);
    EXPECT_FALSE(shader);
    EXPECT_TRUE(shader.error.starts_with("whole-quad value escapes masked region:"));
    auto aluElse = program->words;
    aluElse[4] = 9;
    aluElse[5] = 15u << 26;
    aluElse.insert(aluElse.end(), {0x80000000u | SRC_0, OP2_NOP << 7});
    const auto combined = lowerFragmentShader(*decodeProgram(aluElse));
    EXPECT_FALSE(combined);
    EXPECT_TRUE(combined.error.starts_with("whole-quad value escapes masked region:"));
}

TEST(LatteLoweringTest, WholeQuadMaskUpdatesAndUnscopedClausesRemainFallback)
{
    auto words = isolatedWholeQuadProgram();
    words[13] = (OP2_PRED_SETGT << 7) | 12u;
    const auto shader = lowerFragmentShader(*decodeProgram(words));
    EXPECT_FALSE(shader);
    EXPECT_EQ(shader.error, "whole-quad ALU mask updates require quad execution");
    EXPECT_FALSE(lowerFragmentShader(
            *decodeProgram({2, (8u << 26) | (1u << 30), 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u | SRC_1, (OP2_MOV << 7) | 16u})));
    words = isolatedWholeQuadProgram();
    words[5] = (10u << 26) | (1u << 30);
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(words)));
}

TEST(LatteLoweringTest, WholeQuadImplicitSamplingRequiresRuntimeResourceProof)
{
    const auto program = decodeProgram({6,
                                        9u << 26,
                                        7,
                                        (8u << 26) | (1u << 30),
                                        9,
                                        CF_TEX << 23,
                                        8,
                                        10u << 26,
                                        3u << 15,
                                        (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                        0,
                                        CF_RETURN << 23,
                                        0x80000000u | (1u << 13),
                                        (OP2_PRED_SETGT << 7) | 12u,
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (4u << 21),
                                        0x80000000u | 4u,
                                        (OP2_MOV << 7) | 16u | (3u << 21),
                                        0x10u | (4u << 16),
                                        5u | (0x688u << 9) | 0x30000000u,
                                        0x688u << 20,
                                        0});
    const auto shader = lowerFragmentShader(*program);
    ASSERT_TRUE(shader) << shader.error;
    EXPECT_TRUE(shader.requiresDerivativeFreeSampling);
    ASSERT_EQ(shader.textures.size(), 1u);
    EXPECT_TRUE(shader.textures[0].implicitLod);
    validateSpirv(shader);
    auto explicitWords = program->words;
    explicitWords[18] = (explicitWords[18] & ~31u) | 0x13u;
    const auto explicitShader = lowerFragmentShader(*decodeProgram(explicitWords));
    ASSERT_TRUE(explicitShader) << explicitShader.error;
    EXPECT_FALSE(explicitShader.requiresDerivativeFreeSampling);
    EXPECT_FALSE(explicitShader.textures[0].implicitLod);
}

TEST(VulkanRasterBackendTest, WholeQuadImplicitSamplingAcceptsOnlyDerivativeFreeResources)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto program = decodeProgram({6,
                                        9u << 26,
                                        7,
                                        (8u << 26) | (1u << 30),
                                        9,
                                        CF_TEX << 23,
                                        8,
                                        10u << 26,
                                        3u << 15,
                                        (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                        0,
                                        CF_RETURN << 23,
                                        0x80000000u | (1u << 13),
                                        (OP2_PRED_SETGT << 7) | 12u,
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (4u << 21),
                                        0x80000000u | 5u,
                                        (OP2_MOV << 7) | 16u | (3u << 21),
                                        0x10u | (4u << 16),
                                        5u | (0x688u << 9) | 0x30000000u,
                                        0x688u << 20,
                                        0});
    const auto shader = lowerFragmentShader(*program);
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_TRUE(shader.requiresDerivativeFreeSampling);
    std::vector<RasterDraw::Vertex> vertices;
    for (const auto xy:
         {std::array{0.f, 0.f}, std::array{4.f, 0.f}, std::array{4.f, 4.f}, std::array{0.f, 0.f}, std::array{4.f, 4.f}, std::array{0.f, 4.f}}) {
        RasterDraw::Vertex vertex{xy[0], xy[1], {}};
        vertex.inputs[0][0] = xy[0] / 2.f - 1.f;
        vertices.push_back(vertex);
    }
    std::array<std::uint8_t, 64> target{};
    const std::array color{64.f / 255.f, .5f, .75f, 1.f};
    RasterDraw::Texture texture{shader.textures[0], 2, 2, {}, [&](unsigned, unsigned) { return color; }};
    texture.sourceMipCount = 1;
    RasterDraw draw{shader, vertices, {}, {&texture, 1}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend;
    for (unsigned filter: {0u, 1u}) {
        texture.sampler.regs[0] = 2u | (2u << 3) | (filter << 9) | (filter << 12);
        const auto output = backend.render(draw);
        ASSERT_TRUE(output) << backend.lastError();
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x) {
                std::array<std::array<float, 4>, 4> inputs{};
                inputs[0][0] = (float(x) + .5f) / 2.f - 1.f;
                const auto expected = LatteVsInterp::runPixel(*program, inputs, nullptr, 0,
                                                              [&](unsigned, unsigned, const std::array<float, 4> &, std::array<float, 4> &rgba) {
                                                                  rgba = color;
                                                                  return true;
                                                              });
                ASSERT_TRUE(expected.colorValid);
                for (unsigned c = 0; c < 4; ++c)
                    EXPECT_EQ(output->rgba[(y * 4 + x) * 4 + c], std::lround(expected.color[c] * 255.f));
            }
    }
    const auto validSampler = texture.sampler;
    const auto validSubmissions = backend.queueSubmissions();
    texture.binding.implicitLod = false; // The compiled shader owns this requirement.
    for (unsigned mipCount: {0u, 2u, 8u}) {
        texture.sourceMipCount = mipCount;
        EXPECT_FALSE(backend.render(draw));
    }
    texture.sourceMipCount = 1;
    texture.sourceFirstMip = 1;
    EXPECT_FALSE(backend.render(draw));
    texture.sourceFirstMip = 0;
    texture.sampler.regs[0] ^= 1u << 12; // Min and mag now disagree.
    EXPECT_FALSE(backend.render(draw));
    texture.sampler = validSampler;
    texture.sampler.regs[0] |= 1u << 19; // Anisotropic filtering consumes derivatives.
    EXPECT_FALSE(backend.render(draw));
    EXPECT_EQ(backend.queueSubmissions(), validSubmissions);
    texture.sampler = validSampler;
    ASSERT_TRUE(backend.render(draw)) << backend.lastError();
}

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

TEST(VulkanRasterBackendTest, ArrayDepthCompareSelectsLayerAndAppliesSamplerFunction)
{
    using namespace Core::Gfx;
    // SAMPLE_C with x,y,layer,reference in r0.xyzw (cascaded shadow maps).
    const auto program = decodeProgram({2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x18u, 0x30000000u | (0x688u << 9),
                                        (1u << 23) | (2u << 26) | (3u << 29), 0});
    const std::array types{TextureType::TwoDArray};
    const auto shader = lowerFragmentShader(*program, types);
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.textures.size(), 1u);
    EXPECT_EQ(shader.textures[0].type, TextureType::TwoDArray);
    EXPECT_FALSE(lowerFragmentShader(*program, std::array{TextureType::Cube}));
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    std::vector<std::uint8_t> target(4 * 4 * 4);
    const std::array<float, 2> depths{0.25f, 0.75f};
    RasterDraw::Texture texture{shader.textures[0], 1, 1, {}, [](unsigned, unsigned) { return std::array{0.f, 0.f, 0.f, 0.f}; }};
    texture.r32 = depths;
    texture.layers = 2;
    texture.sourceMipCount = 1;
    RasterDraw draw{shader, vertices, {}, {&texture, 1}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend;
    for (unsigned function: {1u, 4u, 7u})
        for (unsigned layer: {0u, 1u}) {
            SCOPED_TRACE(std::format("function={} layer={}", function, layer));
            texture.sampler.regs[0] = 2u | (2u << 3) | (function << 26);
            for (auto &vertex: vertices)
                vertex.inputs[0] = {0.5f, 0.5f, float(layer), 0.5f};
            const auto result = backend.render(draw);
            ASSERT_TRUE(result) << backend.lastError();
            const bool pass = function == 1 ? 0.5f < depths[layer] : function == 4 ? 0.5f > depths[layer] : true;
            for (unsigned c = 0; c < 4; ++c)
                EXPECT_EQ(result->rgba[c], pass ? 255 : 0);
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
    VulkanRasterBackend backend(true, true, false);
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
    EXPECT_EQ(backend.residentTextureCopies(), 0u);
    EXPECT_EQ(backend.directTextureBindings(), 1u);
    pending->resolve();
    EXPECT_EQ(pending->rgba, reference->rgba);

    // A materialized ticket permits bundle reuse. Its CPU snapshot must still
    // work after the old GPU version is overwritten.
    original->resolve();
    producer.texel = [](unsigned, unsigned) { return std::array{0.f, 1.f, 0.f, 1.f}; };
    draw.textures = {&producer, 1};
    auto newer = backend.renderDeferred(draw);
    ASSERT_TRUE(newer);
    draw.textures = {&consumer, 1};
    auto stale = backend.render(draw);
    ASSERT_TRUE(stale);
    EXPECT_EQ(materializations, 1u);
    EXPECT_EQ(backend.residentTextureCopies(), 0u);
    EXPECT_EQ(backend.directTextureBindings(), 1u);
    EXPECT_EQ(stale->rgba, reference->rgba);

    consumer.rendered = newer->readback;
    auto current = backend.render(draw);
    ASSERT_TRUE(current);
    EXPECT_EQ(backend.residentTextureCopies(), 0u);
    EXPECT_EQ(backend.directTextureBindings(), 2u);
    EXPECT_EQ(current->rgba[0], 0);
    EXPECT_EQ(current->rgba[1], 255);
    // A different device/context must use the CPU snapshot, never a foreign VkImage.
    VulkanRasterBackend foreign;
    auto imported = foreign.render(draw);
    ASSERT_TRUE(imported);
    EXPECT_EQ(imported->rgba, current->rgba);
    EXPECT_EQ(foreign.residentTextureCopies(), 0u);
    // CPU upload after a direct GPU binding must not compare against obsolete staging bytes.
    consumer.rendered.reset();
    consumer.unorm8 = original->resolve();
    auto cpuUploaded = backend.render(draw);
    ASSERT_TRUE(cpuUploaded);
    EXPECT_EQ(cpuUploaded->rgba, reference->rgba);
    auto chained = backend.renderDeferred(draw);
    ASSERT_TRUE(chained && chained->readback);
    consumer.unorm8 = {};
    const auto copiesBeforeChain = backend.residentTextureCopies();
    const auto directBeforeChain = backend.directTextureBindings();
    for (unsigned i = 0; i < 8; ++i) {
        consumer.rendered = chained->readback;
        chained = backend.renderDeferred(draw); // Live source rotates to a different bundle.
        ASSERT_TRUE(chained && chained->readback);
    }
    chained->resolve();
    EXPECT_EQ(chained->rgba, reference->rgba);
    EXPECT_EQ(backend.residentTextureCopies(), copiesBeforeChain);
    EXPECT_EQ(backend.directTextureBindings(), directBeforeChain + 8);

    // A resolved source can reuse its own bundle. Preserve self-feedback with
    // a GPU snapshot copy even though a CPU snapshot is also available.
    VulkanRasterBackend feedbackBackend(true, true, false);
    consumer.rendered.reset();
    consumer.unorm8 = original->resolve();
    chained = feedbackBackend.renderDeferred(draw);
    ASSERT_TRUE(chained && chained->readback);
    consumer.unorm8 = {};
    for (unsigned i = 0; i < 8; ++i) {
        chained->readback->resolve();
        consumer.rendered = chained->readback;
        chained = feedbackBackend.renderDeferred(draw);
        ASSERT_TRUE(chained && chained->readback);
    }
    chained->resolve();
    EXPECT_EQ(chained->rgba, reference->rgba);
    EXPECT_EQ(feedbackBackend.residentTextureCopies(), 8u);
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
    VulkanRasterBackend backend(true, true, false), referenceBackend;
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
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    EXPECT_EQ(output->rgba, expected); // In-range coordinates also work with clamp-to-border.
    for (auto &vertex: vertices)
        vertex.inputs[0][0] += 2.f;
    output = backend.render(draw);
    ASSERT_TRUE(output) << backend.lastError();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            for (unsigned c = 0; c < 4; ++c)
                EXPECT_EQ(output->rgba[(y * 6 + x) * 4 + c], 0u);
    for (auto &vertex: vertices)
        vertex.inputs[0][0] -= 2.f;
    texture.sampler.regs[0] = 7; // Mirror-once-to-border remains unsupported.
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

TEST(VulkanRasterBackendTest, R32ByteSnapshotsPreservePitchesMappingAndInvalidation)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(
            *decodeProgram({2, CF_TEX << 23, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x10u, 0x30000000u | (0x688u << 9), 1u << 23, 0}));
    ASSERT_TRUE(shader) << shader.error;
    std::array<RasterDraw::Vertex, 6> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}, {4, 0, {}}, {4, 4, {}}, {0, 4, {}}}};
    for (auto &v: vertices)
        v.inputs[0] = {v.x / 4.f + .0625f, v.y / 4.f + .0625f, 0, 1};
    std::vector<std::uint8_t> target(4 * 4 * 4, 17);
    std::array<float, 16> values{};
    for (unsigned i = 0; i < values.size(); ++i)
        values[i] = (float(i) - 3.f) / 11.f;
    values[0] = -0.f;
    RasterDraw::Texture texture{shader.textures[0], 4, 4, {}, [](unsigned, unsigned) -> std::array<float, 4> {
                                    throw std::runtime_error("R32 snapshot must bypass per-texel callback");
                                }};
    RasterDraw draw{shader, vertices, {}, {&texture, 1}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend;
    for (unsigned filter: {0u, 1u})
        for (unsigned map: {0x00010203u, 0x00000504u, 0x03020005u})
            for (unsigned pitch: {4u, 7u}) {
                texture.sampler.regs[0] = filter << 9;
                texture.r32Map = map;
                texture.r32 = values;
                texture.r32Bytes = {};
                const auto reference = backend.render(draw);
                ASSERT_TRUE(reference) << backend.lastError();
                // Intentionally unaligned source, with poisoned row padding.
                std::vector<std::uint8_t> bytes(1 + pitch * 4 * 4, 0xCD);
                const auto put = [&](unsigned x, unsigned y, float value) {
                    const auto bits = std::bit_cast<std::uint32_t>(value);
                    for (unsigned b = 0; b < 4; ++b)
                        bytes[1 + (y * pitch + x) * 4 + b] = std::uint8_t(bits >> (b * 8));
                };
                for (unsigned y = 0; y < 4; ++y)
                    for (unsigned x = 0; x < 4; ++x)
                        put(x, y, values[y * 4 + x]);
                texture.r32 = {};
                texture.r32Bytes = std::span<const std::uint8_t>(bytes).subspan(1);
                texture.r32Pitch = pitch;
                auto result = backend.render(draw);
                ASSERT_TRUE(result) << backend.lastError();
                EXPECT_EQ(result->rgba, reference->rgba);
                const auto retained = backend.retainedTextureUploads();
                ASSERT_TRUE(backend.render(draw));
                EXPECT_EQ(backend.retainedTextureUploads(), retained + 1);
                if (pitch > 4) {
                    bytes[1 + 4 * 4] ^= 255;
                    result = backend.render(draw);
                    ASSERT_TRUE(result) << backend.lastError();
                    EXPECT_EQ(result->rgba, reference->rgba);
                    EXPECT_EQ(backend.retainedTextureUploads(), retained + 2);
                }
                put(3, 2, .125f);
                const auto uploads = backend.retainedTextureUploads();
                result = backend.render(draw);
                ASSERT_TRUE(result) << backend.lastError();
                EXPECT_EQ(backend.retainedTextureUploads(), uploads);
                auto modified = values;
                modified[2 * 4 + 3] = .125f;
                texture.r32Bytes = {};
                texture.r32 = modified;
                const auto changedReference = backend.render(draw);
                ASSERT_TRUE(changedReference) << backend.lastError();
                EXPECT_EQ(result->rgba, changedReference->rgba);
                texture.r32 = {};
                texture.r32Bytes = std::span<const std::uint8_t>(bytes).subspan(1);
                texture.r32Pitch = 3;
                EXPECT_FALSE(backend.render(draw));
                texture.r32Pitch = pitch;
                texture.r32Bytes = texture.r32Bytes.first(texture.r32Bytes.size() - 1);
                EXPECT_FALSE(backend.render(draw));
                texture.r32Bytes = std::span<const std::uint8_t>(bytes).subspan(1);
                texture.r32 = values;
                EXPECT_FALSE(backend.render(draw));
                texture.r32 = {};
            }
}

TEST(VulkanRasterBackendTest, R32ByteArraySnapshotsPreserveLayerRows)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const std::array types{TextureType::TwoDArray};
    const auto shader = lowerFragmentShader(*decodeProgram({2, CF_TEX << 23, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x10u,
                                                            0x30000000u | (0x688u << 9), (1u << 23) | (2u << 26), 0}),
                                            types);
    ASSERT_TRUE(shader) << shader.error;
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    std::vector<std::uint8_t> target(4 * 4 * 4);
    std::array<float, 6> values{.125f, .25f, .5f, .625f, .75f, .875f};
    std::vector<std::uint8_t> bytes(3 * 2 * 2 * 4, 0xCD);
    for (unsigned i = 0; i < values.size(); ++i) {
        const auto bits = std::bit_cast<std::uint32_t>(values[i]);
        for (unsigned b = 0; b < 4; ++b)
            bytes[i * 8 + b] = std::uint8_t(bits >> (b * 8));
    }
    RasterDraw::Texture texture{shader.textures[0], 1, 2, {}, [](unsigned, unsigned) { return std::array{0.f, 0.f, 0.f, 0.f}; }};
    texture.layers = 3;
    RasterDraw draw{shader, vertices, {}, {&texture, 1}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend;
    for (unsigned filter: {0u, 1u})
        for (float layer: {0.f, .5f, 1.f, 2.f, 8.f}) {
            texture.sampler.regs[0] = 2u | (2u << 3) | (filter << 9);
            for (auto &v: vertices)
                v.inputs[0] = {.5f, .75f, layer, 0};
            texture.r32Bytes = {};
            texture.r32 = values;
            const auto reference = backend.render(draw);
            ASSERT_TRUE(reference) << backend.lastError();
            texture.r32 = {};
            texture.r32Bytes = bytes;
            texture.r32Pitch = 2;
            const auto result = backend.render(draw);
            ASSERT_TRUE(result) << backend.lastError();
            EXPECT_EQ(result->rgba, reference->rgba);
        }
}

TEST(LatteLoweringTest, EmitsScalarForwardingAndExactLiteralBits)
{
    auto words = forwardingProgram();
    words[1] = (8u << 26) | (4u << 18); // four instructions and one literal slot
    const auto shader = lowerFragmentShader(*decodeProgram(words));
    ASSERT_TRUE(shader) << shader.error;
    EXPECT_NE(shader.source.find("logClamped(reg0[0])"), std::string::npos);
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
    EXPECT_LT(shader.source.find("precise float t1 = reg0[0]"), shader.source.find("reg0[0]=t0"));
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
    EXPECT_EQ(shader.source.find("reg0[3]=sampled"), std::string::npos);
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
    words[0] |= 3u << 30;
    reject(words); // loop-indexed uniform window
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
         {std::pair{OP2_RECIP_IEEE, "reg0[0] != 0.0 ? 1.0 / reg0[0] : 0.0"},
          std::pair{OP2_RECIPSQRT_IEEE, "reg0[0] > 0.0 ? inversesqrt(reg0[0]) : 0.0"}, std::pair{OP2_LOG_IEEE, "logReference(reg0[0])"}}) {
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
layout(location=0) out vec4 inputs[16];
void main() {
    vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    for(int i=0;i<16;++i) inputs[i]=vec4(0.0);
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

TEST(LatteLoweringTest, AluElseAfterPreservesParentAndRunsBothBranchBodies)
{
    const auto program = decodeProgram({6,
                                        9u << 26,
                                        7,
                                        9u << 26,
                                        8,
                                        15u << 26,
                                        9,
                                        11u << 26,
                                        10,
                                        8u << 26,
                                        3u << 15,
                                        (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                        0x80000000u | (SRC_0 << 13),
                                        (OP2_PRED_SETGT << 7) | 4u,
                                        0x80000000u | (1u << 10) | (SRC_0 << 13),
                                        (OP2_PRED_SETGT << 7) | 4u,
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (3u << 21),
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29),
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (3u << 21) | (2u << 29)});
    for (bool parent: {false, true})
        for (bool branch: {false, true}) {
            VulkanFragmentProbe::Inputs inputs{};
            inputs[0][0] = parent ? 1.f : -1.f;
            inputs[0][1] = branch ? 1.f : -1.f;
            const auto out = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
            ASSERT_TRUE(out.colorValid);
            EXPECT_EQ(out.color, (std::array{parent && branch ? 1.f : 0.f, parent && !branch ? 1.f : 0.f, 1.f, 0.f}));
            ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, out.color));
        }
    const auto invalid = decodeProgram({2, 15u << 26, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u | SRC_1, (OP2_MOV << 7) | 16u});
    EXPECT_FALSE(lowerFragmentShader(*invalid));
    EXPECT_FALSE(Core::Gfx::LatteVsInterp::runPixel(*invalid, {}, nullptr, 0, {}).colorValid);
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

TEST(LatteDepthTest, DepthAndColorExportsMatchInterpreterAndRejectStencilExports)
{
    using namespace Core::Gfx;
    for (const bool color: {false, true}) {
        std::vector<std::uint32_t> words;
        if (color)
            words = {0, (0x27u << 23) | 0x688u};
        words.insert(words.end(), {61, (0x28u << 23) | (1u << 21) | 0xFF8u});
        const auto program = decodeProgram(words);
        const auto shader = lowerFragmentShader(*program);
        ASSERT_TRUE(shader) << shader.error;
        EXPECT_TRUE(shader.writesDepth);
        EXPECT_EQ(shader.exportsColor, color);
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0] = {.375f, .25f, .5f, 1.f};
        const auto reference = LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
        ASSERT_TRUE(reference.depthValid);
        EXPECT_FLOAT_EQ(reference.depth, .375f);
        EXPECT_EQ(reference.colorValid, color);
        validateSpirv(shader);
    }
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram({61, (0x28u << 23) | (1u << 21) | 0xFC0u}))) << "Stencil exports need separate state";
}

TEST(VulkanDepthTest, CompareFunctionsWriteMaskQuantizationAndReadOnlyPreserveDepth)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (0x28u << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader);
    std::array<RasterDraw::Vertex, 6> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}, {4, 0, {}}, {4, 4, {}}, {0, 4, {}}}};
    for (auto &v: vertices)
        v.inputs[0] = {1, 0, 0, 1};
    std::vector<std::uint8_t> target(4 * 4 * 4, 17);
    std::vector<float> initial(6 * 5, .5f);
    // The depth image is larger than the colour framebuffer, with padded rows.
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend;
    for (const auto format: {DepthFormat::Float32, DepthFormat::UNorm16, DepthFormat::UNorm24})
        for (unsigned function = 0; function < 8; ++function)
            for (const bool write: {false, true})
                for (const float incoming: {.25f, .5f, .75f}) {
                    SCOPED_TRACE(unsigned(format));
                    SCOPED_TRACE(function);
                    SCOPED_TRACE(write);
                    SCOPED_TRACE(incoming);
                    const auto stored = quantizeDepth(.5f, format);
                    std::fill(initial.begin(), initial.end(), stored);
                    for (auto &v: vertices)
                        v.z = incoming;
                    draw.depth = {true, write, function, format, initial, 4, 5, 6, {}};
                    const auto result = backend.render(draw);
                    ASSERT_TRUE(result) << backend.lastError();
                    const bool passed = depthCompare(clampDepth(incoming), stored, function);
                    EXPECT_EQ(result->rgba[0], passed ? 255 : 17);
                    if (write) {
                        ASSERT_EQ(result->depth.size(), initial.size());
                        EXPECT_NEAR(result->depth[0], passed ? quantizeDepth(incoming, format) : stored,
                                    format == DepthFormat::UNorm16 ? 1.f / 65535.f : 1.e-7f);
                        EXPECT_FLOAT_EQ(result->depth[4], stored) << "Row padding must survive";
                        EXPECT_FLOAT_EQ(result->depth[24], stored) << "Rows outside framebuffer must survive";
                    } else
                        EXPECT_TRUE(result->depth.empty());
                }
}

TEST(VulkanDepthTest, DepthOnlyExportAndDeferredChainsKeepSnapshotsAcrossReuse)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({61, (0x28u << 23) | (1u << 21) | 0xFF8u}));
    ASSERT_TRUE(shader) << shader.error;
    std::array<RasterDraw::Vertex, 6> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}, {4, 0, {}}, {4, 4, {}}, {0, 4, {}}}};
    std::vector<std::uint8_t> target(64, 17);
    std::vector<float> initial(24, 1.f);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    draw.depth = {true, true, 1, DepthFormat::Float32, initial, 4, 4, 6, {}};
    auto backend = std::make_unique<VulkanRasterBackend>(true, true);
    std::vector<std::shared_ptr<RasterReadback>> snapshots;
    for (const float value: {.75f, .25f, .5f, .125f}) {
        for (auto &v: vertices) {
            v.z = .9f;
            v.inputs[0][0] = value;
        }
        auto result = backend->renderDeferred(draw);
        ASSERT_TRUE(result) << backend->lastError();
        ASSERT_TRUE(result->depthReadback);
        snapshots.push_back(result->depthReadback);
        draw.depth.rendered = result->depthReadback;
        draw.renderedTarget = result->readback;
    }
    EXPECT_GE(backend->completedDraws(), 4u);
    backend.reset(); // Live tickets own the context/resources.
    const float expected[]{.75f, .25f, .25f, .125f};
    for (unsigned i = 0; i < snapshots.size(); ++i) {
        const auto &bytes = snapshots[i]->resolve();
        float value{};
        std::memcpy(&value, bytes.data(), sizeof(value));
        EXPECT_FLOAT_EQ(value, expected[i]);
        std::memcpy(&value, bytes.data() + 4 * sizeof(float), sizeof(value));
        EXPECT_FLOAT_EQ(value, 1.f);
        EXPECT_EQ(&snapshots[i]->resolve(), &bytes);
    }
}

TEST(VulkanDepthTest, EmptyScissorsPreserveDepthAndInvalidInputsFallBack)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (0x28u << 23) | (1u << 21) | 0x688u}));
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    std::vector<std::uint8_t> target(64, 17);
    std::vector<float> depth(16, .75f);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 0, 4}, 15, {}};
    draw.depth = {true, true, 1, DepthFormat::Float32, depth, 4, 4, 4, {}};
    VulkanRasterBackend backend;
    auto result = backend.render(draw);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->depth, depth);
    EXPECT_EQ(result->rgba, target);
    draw.depth.pitch = 3;
    EXPECT_FALSE(backend.render(draw));
    draw.depth.pitch = 4;
    draw.depth.compare = 8;
    EXPECT_FALSE(backend.render(draw));
    draw.depth.compare = 1;
    vertices[0].reciprocalW = -1;
    EXPECT_FALSE(backend.render(draw));
}

TEST(VulkanDepthTest, PerspectiveVaryingsAndWindowDepthUseTheirOwnInterpolation)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (0x28u << 23) | (1u << 21) | 0x688u}));
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    vertices[0].z = .1f;
    vertices[1].z = .5f;
    vertices[2].z = .9f;
    vertices[0].reciprocalW = 1.f;
    vertices[1].reciprocalW = .5f;
    vertices[2].reciprocalW = .25f;
    vertices[1].inputs[0][0] = 1.f;
    std::vector<std::uint8_t> target(64);
    std::vector<float> depth(16, 1.f);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    draw.depth = {true, true, 7, DepthFormat::Float32, depth, 4, 4, 4, {}};
    VulkanRasterBackend backend;
    const auto result = backend.render(draw);
    ASSERT_TRUE(result) << backend.lastError();
    const auto offset = 1 * 4 + 1;
    // Screen barycentrics (.25,.375,.375): varyings divide by interpolated reciprocal W;
    // window depth interpolates linearly, independently of that perspective correction.
    EXPECT_NEAR(int(result->rgba[offset * 4]), 90, 1);
    EXPECT_NEAR(result->depth[offset], .55f, 1.e-6f);
}

TEST(VulkanDepthTest, FixedPointQuantizationMatchesCanonicalFloatBitsAcrossBoundaries)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({61, (0x28u << 23) | (1u << 21) | 0xFF8u}));
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    std::vector<std::uint8_t> target(64);
    std::vector<float> depth(16, 1.f);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 4, 4}, 0, {}};
    VulkanRasterBackend backend;
    std::vector<float> values{0.f,
                              1.f,
                              .25f,
                              .5f,
                              .75f,
                              std::nextafter(.5f, 0.f),
                              std::nextafter(.5f, 1.f),
                              std::nextafter(.75f, 0.f),
                              std::nextafter(.75f, 1.f),
                              std::numeric_limits<float>::denorm_min()};
    std::uint32_t state = 1234567;
    for (unsigned i = 0; i < 96; ++i) {
        state = state * 1664525u + 1013904223u;
        values.push_back(float(state >> 8) / 16777216.f);
    }
    for (const auto format: {DepthFormat::UNorm16, DepthFormat::UNorm24}) {
        draw.depth = {true, true, 7, format, depth, 4, 4, 4, {}};
        for (const float value: values) {
            SCOPED_TRACE(unsigned(format));
            SCOPED_TRACE(value);
            for (auto &v: vertices)
                v.inputs[0][0] = value;
            auto result = backend.render(draw);
            ASSERT_TRUE(result) << backend.lastError();
            EXPECT_EQ(std::bit_cast<std::uint32_t>(result->depth[0]), std::bit_cast<std::uint32_t>(quantizeDepth(value, format)));
        }
    }
}

TEST(VulkanDepthTest, FixedPointNormalizationPreservesEveryRepresentableInteger)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    std::array<RasterDraw::Vertex, 1> point{{{.5f, .5f, {}}}};
    std::vector<std::uint8_t> color(4, 19);
    // A NEVER depth test leaves every uploaded sample untouched by rasterization,
    // while depth writes still execute the real GPU canonicalization pass.
    for (const auto format: {DepthFormat::UNorm16, DepthFormat::UNorm24}) {
        const unsigned width = format == DepthFormat::UNorm16 ? 256 : 4096;
        const unsigned maximum = format == DepthFormat::UNorm16 ? 65535 : 16777215;
        std::vector<float> depth(std::size_t(width) * width);
        for (unsigned value = 0; value <= maximum; ++value)
            depth[value] = float(double(value) / maximum);
        RasterDraw draw{shader, point, {}, {}, color, 1, 1, 1, {0, 0, 1, 1}, 0, {}};
        draw.topology = RasterDraw::Topology::Points;
        draw.depth = {true, true, 0, format, depth, width, width, width, {}};
        VulkanRasterBackend backend;
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        ASSERT_EQ(result->depth.size(), depth.size());
        EXPECT_EQ(result->rgba, color);
        for (unsigned value = 0; value <= maximum; ++value)
            if (std::bit_cast<unsigned>(result->depth[value]) != std::bit_cast<unsigned>(depth[value])) {
                ADD_FAILURE() << "Depth format " << unsigned(format) << " changed integer " << value << '/' << maximum;
                break;
            }
    }
}

TEST(VulkanDepthTest, SmallerDepthAttachmentClipsToCommonFramebufferExtent)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (0x28u << 23) | (1u << 21) | 0x688u}));
    std::array<RasterDraw::Vertex, 6> vertices{{{0, 0, {}}, {8, 0, {}}, {0, 8, {}}, {8, 0, {}}, {8, 8, {}}, {0, 8, {}}}};
    for (auto &v: vertices) {
        v.z = .25f;
        v.inputs[0] = {1, 0, 0, 1};
    }
    std::vector<std::uint8_t> target(8 * 8 * 4, 17);
    std::vector<float> depth(4 * 4, 1.f);
    RasterDraw draw{shader, vertices, {}, {}, target, 8, 8, 8, {0, 0, 8, 8}, 15, {}};
    draw.depth = {true, true, 1, DepthFormat::Float32, depth, 4, 4, 4, {}};
    VulkanRasterBackend backend;
    const auto result = backend.render(draw);
    ASSERT_TRUE(result) << backend.lastError();
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            EXPECT_EQ(result->rgba[(y * 8 + x) * 4], x < 4 && y < 4 ? 255 : 17);
    for (auto value: result->depth)
        EXPECT_FLOAT_EQ(value, .25f);
}

TEST(VulkanDepthTest, MaskedDepthExportsDiscardAndMixedOutputMasksFallBack)
{
    using namespace Core::Gfx;
    const std::vector<std::uint32_t> words{
            4, 9u << 26, 61, (0x27u << 23) | 0xFF8u, 0, (0x0Eu << 23) | 1, 0, 1u << 21, 0x80000000u | (1u << 13), (OP2_PRED_SETGT << 7) | 12u};
    const auto program = decodeProgram(words);
    const auto shader = lowerFragmentShader(*program);
    ASSERT_TRUE(shader) << shader.error;
    validateSpirv(shader);
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    std::vector<std::uint8_t> target(64, 17);
    std::vector<float> depth(16, 1.f);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 4, 4}, 0, {}};
    draw.depth = {true, true, 7, DepthFormat::Float32, depth, 4, 4, 4, {}};
    VulkanRasterBackend backend;
    for (const float value: {.25f, .75f}) {
        for (auto &v: vertices) {
            v.inputs[0] = {value, 0, 0, 1};
            v.inputs[1] = {.5f, 0, 0, 1};
        }
        const auto reference = LatteVsInterp::runPixel(*program, vertices[0].inputs, nullptr, 0, {});
        EXPECT_EQ(reference.discarded, value < .5f);
        EXPECT_EQ(reference.depthValid, value > .5f);
        if (std::getenv("WEMU_TEST_VULKAN") && SpirvCompiler::available()) {
            auto result = backend.render(draw);
            ASSERT_TRUE(result) << backend.lastError();
            EXPECT_FLOAT_EQ(result->depth[0], value > .5f ? value : 1.f);
            EXPECT_EQ(result->rgba, target);
        }
    }
    auto mixed = words;
    mixed[7] = (0x28u << 23) | (1u << 21) | 0x688u;
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(mixed))) << "Conditional colour/depth masks are not a single-output native draw";
}

TEST(VulkanPointsTest, FloatPointsBlendWithoutClampingAndPreserveMasksPaddingAndVersions)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    std::array<RasterDraw::Vertex, 4> points{{{1.5f, .5f, {}}, {1.5f, .5f, {}}, {3.5f, 1.5f, {}}, {-.5f, .5f, {}}}};
    for (auto &v: points)
        v.inputs[0] = {2.f, -.5f, .25f, 1.f};
    std::vector<std::uint8_t> target(6 * 2 * 16);
    for (unsigned i = 0; i < 12; ++i)
        writeFloatColor(target.data() + i * 16, {.5f, .25f, .75f, .125f}, 15);
    RasterDraw draw{shader, points, {}, {}, target, 4, 2, 6, {1, 0, 2, 2}, 7, {true, 1, 1, 0, 1, 1, 0, {}}};
    draw.topology = RasterDraw::Topology::Points;
    draw.colorFormat = ColorFormat::RGBA32Float;
    VulkanRasterBackend backend(true, true);
    auto first = backend.renderDeferred(draw);
    ASSERT_TRUE(first) << backend.lastError();
    ASSERT_TRUE(first->readback);
    draw.renderedTarget = first->readback;
    auto second = backend.renderDeferred(draw);
    ASSERT_TRUE(second) << backend.lastError();
    first->resolve();
    second->resolve();
    for (unsigned i = 0; i < 12; ++i) {
        const auto a = readFloatColor(first->rgba.data() + i * 16), b = readFloatColor(second->rgba.data() + i * 16);
        EXPECT_EQ(a, (i == 1 ? std::array{4.5f, -.75f, 1.25f, .125f} : std::array{.5f, .25f, .75f, .125f}));
        EXPECT_EQ(b, (i == 1 ? std::array{8.5f, -1.75f, 1.75f, .125f} : std::array{.5f, .25f, .75f, .125f}));
    }
    // The float image also remains a typed texture, with component remapping.
    const auto sampled = lowerFragmentShader(*decodeProgram(
            {2, 1u << 23, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x13, 0x30000000u | (1u << 12) | (2u << 15) | (3u << 18), 1u << 23, 0}));
    ASSERT_TRUE(sampled) << sampled.error;
    RasterDraw::Texture texture{sampled.textures[0], 4, 2, {}, [](unsigned, unsigned) { return std::array<float, 4>{}; }};
    texture.rendered = draw.renderedTarget;
    texture.renderedFormat = ColorFormat::RGBA32Float;
    texture.unorm8Pitch = 6;
    texture.unorm8Map = 0x02010003;
    std::array<RasterDraw::Vertex, 1> one{{{.5f, .5f, {}}}};
    one[0].inputs[0] = {.375f, .25f, 0, 1};
    std::vector<std::uint8_t> output(16);
    RasterDraw sampleDraw{sampled, one, {}, std::span{&texture, 1}, output, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
    sampleDraw.topology = RasterDraw::Topology::Points;
    sampleDraw.colorFormat = ColorFormat::RGBA32Float;
    const auto result = backend.render(sampleDraw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(readFloatColor(result->rgba.data()), (std::array{1.25f, -.75f, 4.5f, .125f}));
}

TEST(LatteLoweringTest, FragmentKcacheWindowsUseBanksAndOffsetsWhileConstantFileStaysIndependent)
{
    using namespace Core::Gfx;
    const std::vector<std::uint32_t> words{2u | (3u << 22) | (7u << 26) | (1u << 30),
                                           (8u << 26) | (3u << 18) | (1u << 2) | (2u << 10) | 2u,
                                           0,
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                           128u,
                                           (OP2_MOV << 7) | 16u,
                                           162u | (1u << 10),
                                           (OP2_MOV << 7) | 16u | (1u << 29),
                                           256u | (2u << 10),
                                           (OP2_MOV << 7) | 16u | (2u << 29),
                                           0x80000000u | 128u | (3u << 10),
                                           (OP2_MOV << 7) | 16u | (3u << 29)};
    const auto program = decodeProgram(words);
    const auto shader = lowerFragmentShader(*program);
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.uniforms.size(), 2u);
    EXPECT_EQ(shader.uniforms[0], (FragmentShader::UniformReference{3, 16}));
    EXPECT_EQ(shader.uniforms[1], (FragmentShader::UniformReference{7, 34}));
    std::array<float, 1024> constants{};
    constants[2] = .75f;
    std::array<std::array<float, 4>, 4> inputs{};
    const auto fetch = [](unsigned bank, unsigned index, unsigned channel) {
        if (bank == 3 && index == 16)
            return channel == 3 ? 1.f : .25f;
        if (bank == 7 && index == 34)
            return .5f;
        return -10.f;
    };
    const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, constants.data(), 256, {}, fetch);
    ASSERT_TRUE(reference.colorValid);
    EXPECT_EQ(reference.color, (std::array{.25f, .5f, .75f, 1.f}));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(shader));
    if (!std::getenv("WEMU_TEST_VULKAN"))
        return;
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {2, 0, {}}, {0, 2, {}}}};
    std::array<std::uint8_t, 4> target{};
    std::array<float, 8> uniforms{.25f, 0, 0, 1, 0, .5f, 0, 0};
    RasterDraw draw{shader, vertices, constants, {}, target, 1, 1, 1, {0, 0, 1, 1}, 15, {}};
    draw.uniforms = uniforms;
    VulkanRasterBackend backend;
    const auto first = backend.render(draw);
    ASSERT_TRUE(first) << backend.lastError();
    for (unsigned c = 0; c < 4; ++c)
        EXPECT_LE(std::abs(int(first->rgba[c]) - std::array{64, 128, 191, 255}[c]), 1);
    uniforms[0] = .5f;
    uniforms[5] = .25f;
    const auto changed = backend.render(draw);
    ASSERT_TRUE(changed) << backend.lastError();
    for (unsigned c = 0; c < 4; ++c)
        EXPECT_LE(std::abs(int(changed->rgba[c]) - std::array{128, 64, 191, 255}[c]), 1);
    draw.uniforms = {};
    EXPECT_FALSE(backend.render(draw));
}

TEST(LatteVertexTest, UniformWindowsAndExplicitTextureSamplingCompileWithSharedAlu)
{
    // R1.x takes bank 3, vector 16; POS0 exports R1 and PARAM0 exports R2.
    const std::vector<std::uint32_t> words{4u | (3u << 22) | (1u << 30),
                                           (8u << 26) | (1u << 2),
                                           60u | (1u << 13) | (1u << 15),
                                           (CF_EXP << 23) | 0x688u,
                                           (2u << 13) | (2u << 15),
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                           0,
                                           0,
                                           0x80000000u | 128u,
                                           (OP2_MOV << 7) | 16u | (1u << 21)};
    const auto shader = lowerVertexShader(*decodeProgram(words));
    ASSERT_TRUE(shader) << shader.error;
    ASSERT_EQ(shader.uniforms.size(), 1u);
    EXPECT_EQ(shader.uniforms[0], (FragmentShader::UniformReference{3, 16}));
    EXPECT_EQ(shader.parameterMask, 1u);
    ASSERT_NO_FATAL_FAILURE(validateSpirv(shader, nullptr, "vert"));
    auto invalid = words;
    invalid[1] |= 1u << 30;
    EXPECT_FALSE(lowerVertexShader(*decodeProgram(invalid)));
    VertexShaderCache cache;
    EXPECT_EQ(cache.get(words), cache.get(words));
}

TEST(VulkanPointsTest, VertexTexturesConsumeResidentDepthWithoutHostReadback)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto fragment = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    const std::vector<std::uint32_t> words{3,
                                           1u << 23,
                                           60u | (1u << 13) | (1u << 15),
                                           (CF_EXP << 23) | 0x688u,
                                           (2u << 13) | (3u << 15),
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                           0x13u | (2u << 16),
                                           0x30000000u | 3u | (1u << 12) | (2u << 15) | (3u << 18),
                                           1u << 23,
                                           0};
    auto vertex = std::make_shared<const VertexShader>(lowerVertexShader(*decodeProgram(words)));
    ASSERT_TRUE(*vertex) << vertex->error;
    ASSERT_EQ(vertex->textures.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(validateSpirv(*vertex, nullptr, "vert"));
    std::array<RasterDraw::Vertex, 2> points{{{.5f, .5f, {}}, {1.5f, .5f, {}}}};
    points[0].z = .25f;
    points[1].z = .75f;
    std::vector<float> depths(3, .9f);
    std::vector<std::uint8_t> bytes(3 * 16);
    RasterDraw depthDraw{fragment, points, {}, {}, bytes, 2, 1, 3, {0, 0, 2, 1}, 0, {}};
    depthDraw.topology = RasterDraw::Topology::Points;
    depthDraw.colorFormat = ColorFormat::RGBA32Float;
    depthDraw.depth = {true, true, 7, DepthFormat::Float32, depths, 2, 1, 3, {}};
    VulkanRasterBackend backend(true, true);
    auto source = backend.renderDeferred(depthDraw);
    ASSERT_TRUE(source) << backend.lastError();
    ASSERT_TRUE(source->depthReadback);
    unsigned resolutions{};
    auto observed = std::make_shared<RasterReadback>(
            source->depthReadback->size(),
            [&, ticket = source->depthReadback] {
                ++resolutions;
                return ticket->resolve();
            },
            source->depthReadback->image());
    auto binding = vertex->textures[0];
    binding.binding += 17;
    RasterDraw::Texture texture{binding, 2, 1, {}, [](unsigned, unsigned) -> std::array<float, 4> { throw std::runtime_error("CPU texture fetch"); }};
    texture.rendered = observed;
    texture.renderedDepth = true;
    texture.unorm8Pitch = 3;
    texture.unorm8Map = 0x00000505;
    for (unsigned i = 0; i < 2; ++i) {
        points[i].inputs[0] = {i ? .5f : -.5f, 0, 0, 1};
        points[i].inputs[1] = {i ? .75f : .25f, .5f, 0, 1};
    }
    RasterDraw draw{fragment, points, {}, std::span{&texture, 1}, bytes, 2, 1, 3, {0, 0, 2, 1}, 15, {}};
    draw.topology = RasterDraw::Topology::Points;
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.vertexStage = {vertex, {}, {}, {0, -1, -1, -1}, {0, 0, 2, 1, 0, 1}};
    auto result = backend.renderDeferred(draw);
    ASSERT_TRUE(result) << backend.lastError();
    EXPECT_EQ(resolutions, 0u);
    EXPECT_EQ(backend.readbackTransfers(), 0u);
    result->resolve();
    EXPECT_EQ(readFloatColor(result->rgba.data()), (std::array{.25f, .25f, 1.f, 1.f}));
    EXPECT_EQ(readFloatColor(result->rgba.data() + 16), (std::array{.75f, .75f, 1.f, 1.f}));
    EXPECT_EQ(resolutions, 0u);
    std::array<RasterDraw::Vertex, 3> triangle{};
    triangle[0].inputs[0] = {-1, 1, 0, 1};
    triangle[1].inputs[0] = {-1, -3, 0, 1};
    triangle[2].inputs[0] = {3, 1, 0, 1};
    for (auto &v: triangle)
        v.inputs[1] = {.25f, .5f, 0, 1};
    draw.vertices = triangle;
    draw.topology = RasterDraw::Topology::Triangles;
    auto triangles = backend.render(draw);
    ASSERT_TRUE(triangles) << backend.lastError();
    EXPECT_EQ(readFloatColor(triangles->rgba.data()), (std::array{.25f, .25f, 1.f, 1.f}));
    EXPECT_EQ(readFloatColor(triangles->rgba.data() + 16), (std::array{.25f, .25f, 1.f, 1.f}));
    EXPECT_EQ(resolutions, 0u);
    auto repeated = backend.render(draw);
    ASSERT_TRUE(repeated) << backend.lastError();
    EXPECT_EQ(repeated->rgba, triangles->rgba);
    EXPECT_EQ(resolutions, 0u);
    source->resolveDepth();
    EXPECT_EQ(source->depth, (std::vector<float>{.25f, .75f, .9f}));
}

TEST(LatteLoweringTest, DotReductionPreservesOrderedProductsPredicationAndScale)
{
    for (const auto op: {OP2_DOT4, OP2_DOT4_IEEE})
        for (unsigned predicate: {0u, 2u, 3u})
            for (unsigned scale: {0u, 1u, 2u, 3u}) {
                std::vector<std::uint32_t> words{2, (8u << 26) | (3u << 18), 2u << 15, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
                for (unsigned c = 0; c < 4; ++c) {
                    words.push_back((c << 10) | (1u << 13) | (c << 23) | (predicate << 29) | (c == 3 ? 0x80000000u : 0));
                    words.push_back((unsigned(op) << 7) | 16u | (scale << 5) | (2u << 21) | (c << 29));
                }
                const auto program = decodeProgram(words);
                VulkanFragmentProbe::Inputs inputs{};
                inputs[0] = {2, -4, .5f, 1};
                inputs[1] = {3, 2, 8, 1};
                const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
                const float expected = predicate == 2 ? 0.f : 3.f * (scale == 0 ? 1.f : scale == 1 ? 2.f : scale == 2 ? 4.f : .5f);
                ASSERT_TRUE(reference.colorValid);
                EXPECT_EQ(reference.color, (std::array{expected, expected, expected, expected}));
                ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
                auto incomplete = words;
                incomplete[1] = (8u << 26) | (2u << 18);
                incomplete[8] |= 0x80000000u;
                EXPECT_FALSE(lowerFragmentShader(*decodeProgram(incomplete)));
            }
}

TEST(LatteLoweringTest, CubeReductionMatchesFacesTiesPredicationAndScale)
{
    struct Case {
            std::array<float, 4> direction, expected;
    };
    constexpr Case cases[]{{{4, 2, 1, 0}, {-2, -1, 8, 0}},  {{-4, 2, 1, 0}, {-2, 1, -8, 1}}, {{1, 4, 2, 0}, {2, 1, 8, 2}},
                           {{1, -4, 2, 0}, {-2, 1, -8, 3}}, {{1, 2, 4, 0}, {-2, 1, 8, 4}},   {{1, 2, -4, 0}, {-2, -1, -8, 5}},
                           {{2, 2, 2, 0}, {-2, 2, 4, 4}},   {{2, 2, 1, 0}, {1, 2, 4, 2}}};
    constexpr unsigned swizzles[2][4]{{2, 2, 0, 1}, {1, 0, 2, 2}};
    for (const auto &test: cases)
        for (unsigned predicate: {0u, 2u, 3u}) {
            const unsigned scale = predicate == 3 ? 3 : 0;
            std::vector<unsigned> words{2, (8u << 26) | (3u << 18), 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
            // Overwrite the source register, checking simultaneous group reads.
            for (unsigned lane = 0; lane < 4; ++lane) {
                words.push_back((swizzles[0][lane] << 10) | (swizzles[1][lane] << 23) | (predicate << 29) | (lane == 3 ? 1u << 31 : 0));
                words.push_back((OP2_CUBE << 7) | 16u | (scale << 5) | (lane << 29));
            }
            const auto program = decodeProgram(words);
            VulkanFragmentProbe::Inputs inputs{};
            inputs[0] = test.direction;
            const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
            auto expected = predicate == 2 ? test.direction : test.expected;
            if (predicate == 3)
                for (auto &value: expected)
                    value *= .5f;
            ASSERT_TRUE(reference.colorValid);
            EXPECT_EQ(reference.color, expected);
            ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, expected));
            auto invalid = words;
            invalid[4] ^= 1u << 10; // Unsupported source swizzle must retain fallback.
            EXPECT_FALSE(lowerFragmentShader(*decodeProgram(invalid)));
            EXPECT_FALSE(Core::Gfx::LatteVsInterp::runPixel(*decodeProgram(invalid), inputs, nullptr, 0, {}).colorValid);
        }
}

TEST(LatteLoweringTest, CubeReductionPreservesPreviousVectorAndIndependentScalarResults)
{
    std::vector<unsigned> words{2,
                                (8u << 26) | (10u << 18),
                                2u << 15,
                                (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                1u | (2u << 10) | (1u << 31),
                                (OP2_MOV << 7) | (2u << 29)};
    constexpr unsigned swizzles[2][4]{{2, 2, 0, 1}, {1, 0, 2, 2}};
    constexpr unsigned registers[]{0, 2, SRC_PV}, channels[]{3, 1, 2};
    for (unsigned lane = 0; lane < 4; ++lane) {
        const auto a = swizzles[0][lane], b = swizzles[1][lane];
        words.push_back(registers[a] | (channels[a] << 10) | (registers[b] << 13) | (channels[b] << 23));
        words.push_back((OP2_CUBE << 7) | (lane << 29)); // GPR write disabled; PV still receives every component.
    }
    words.push_back(SRC_1 | (1u << 31));
    words.push_back((OP2_MOV << 7) | 16u | (3u << 21));
    for (unsigned lane = 0; lane < 4; ++lane) {
        words.push_back(SRC_PV | (lane << 10));
        words.push_back((OP2_MOV << 7) | 16u | (2u << 21) | (lane << 29));
    }
    words.push_back(SRC_PS | (1u << 31));
    words.push_back((OP2_MOV << 7) | 16u | (2u << 21));
    const auto program = decodeProgram(words);
    VulkanFragmentProbe::Inputs inputs{};
    inputs[0] = {0, 0, 0, 4};
    inputs[1] = {0, 0, 1, 0};
    inputs[2] = {0, 2, 0, 0};
    const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
    ASSERT_TRUE(reference.colorValid);
    EXPECT_EQ(reference.color, (std::array{1.f, -1.f, 8.f, 0.f}));
    ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
}

TEST(LatteLoweringTest, Dx10ComparisonMasksMatchInterpreterForOrderedAndNanInputs)
{
    for (const auto op: {OP2_SETE_DX10, OP2_SETGT_DX10, OP2_SETGE_DX10, OP2_SETNE_DX10})
        for (const auto a: {-1.f, 0.f, 1.f, std::numeric_limits<float>::quiet_NaN()}) {
            const std::vector<std::uint32_t> words{2,
                                                   (8u << 26) | (1u << 18),
                                                   3u << 15,
                                                   (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                                   0x80000000u | (1u << 13),
                                                   (unsigned(op) << 7) | 16u | (2u << 21),
                                                   0x80000000u | 2u | (SRC_0 << 13),
                                                   SRC_1 | (OP3_CNDE_INT << 13) | (3u << 21)};
            VulkanFragmentProbe::Inputs inputs{};
            inputs[0][0] = a;
            const bool condition = op == OP2_SETE_DX10 ? a == 0.f : op == OP2_SETGT_DX10 ? a > 0.f : op == OP2_SETGE_DX10 ? a >= 0.f : a != 0.f;
            const auto program = decodeProgram(words);
            const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
            ASSERT_TRUE(reference.colorValid);
            EXPECT_EQ(reference.color, (std::array{condition ? 1.f : 0.f, 0.f, 0.f, 0.f}));
            ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
        }
}

TEST(VulkanPointsTest, FloatBlendEquationsAndVertexUniformWindowsPreserveHdrValues)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto fragment = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    const std::vector<std::uint32_t> words{4u | (3u << 22) | (1u << 30),
                                           (8u << 26) | (1u << 2),
                                           60u | (1u << 13) | (1u << 15),
                                           (CF_EXP << 23) | 0x688u,
                                           (2u << 13) | (2u << 15),
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                           0,
                                           0,
                                           0x80000000u | 128u,
                                           (OP2_MOV << 7) | 16u | (1u << 21)};
    auto vertex = std::make_shared<const VertexShader>(lowerVertexShader(*decodeProgram(words)));
    ASSERT_TRUE(*vertex) << vertex->error;
    std::array<RasterDraw::Vertex, 1> points{{{0, 0, {}}}};
    points[0].inputs[0] = {3, 0, 0, 1}; // kcache supplies -.5 instead of the offscreen X
    points[0].inputs[1] = {2, -.5f, .25f, 2};
    const std::array<float, 4> uniforms{-.5f, 0, 0, 0};
    std::vector<std::uint8_t> bytes(2 * 16);
    for (unsigned i = 0; i < 2; ++i)
        writeFloatColor(bytes.data() + i * 16, {.5f, .25f, .75f, 1}, 15);
    RasterDraw draw{fragment, points, {}, {}, bytes, 2, 1, 2, {0, 0, 2, 1}, 15, {true, 1, 1, 0, 1, 1, 0, {}}};
    draw.topology = RasterDraw::Topology::Points;
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.vertexStage = {vertex, {}, uniforms, {0, -1, -1, -1}, {0, 0, 2, 1, 0, 1}};
    VulkanRasterBackend backend;
    const std::array<std::array<float, 4>, 5> expected{
            {{2.5f, -.25f, 1.f, 3.f}, {1.5f, -.75f, -.5f, 1.f}, {.5f, -.5f, .25f, 1.f}, {2.f, .25f, .75f, 2.f}, {-1.5f, .75f, .5f, -1.f}}};
    for (unsigned operation = 0; operation < 5; ++operation) {
        draw.blend.colorOperation = draw.blend.alphaOperation = operation;
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        EXPECT_EQ(readFloatColor(result->rgba.data()), expected[operation]);
        EXPECT_EQ(readFloatColor(result->rgba.data() + 16), (std::array{.5f, .25f, .75f, 1.f}));
    }
}

TEST(VulkanRasterBackendTest, DynamicViewportAndScissorReusePipelinesAcrossTargetSizes)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    VulkanRasterBackend backend;
    for (const auto dimensions: {std::array{4u, 4u}, std::array{8u, 3u}, std::array{3u, 7u}, std::array{4u, 4u}}) {
        const auto width = dimensions[0], height = dimensions[1], pitch = width + 2;
        std::vector<std::uint8_t> target(pitch * height * 4, 19);
        std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {float(width * 2), 0, {}}, {0, float(height * 2), {}}}};
        for (auto &vertex: vertices)
            vertex.inputs[0] = {1, 0, 0, 1};
        RasterDraw draw{shader, vertices, {}, {}, target, width, height, pitch, {1, 1, int(width) - 1, int(height) - 1}, 15, {}};
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < pitch; ++x) {
                const auto offset = (y * pitch + x) * 4;
                const auto expected =
                        x >= 1 && x < width && y >= 1 ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{19, 19, 19, 19};
                EXPECT_EQ((std::array{result->rgba[offset], result->rgba[offset + 1], result->rgba[offset + 2], result->rgba[offset + 3]}), expected);
            }
    }
    EXPECT_EQ(backend.pipelineCreations(), 1u);
}

TEST(VulkanRasterBackendTest, ColdScissorVariantsKeepHotResourcesAndOlderSnapshotsAlive)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {8, 0, {}}, {0, 8, {}}}};
    for (auto &v: vertices)
        v.inputs[0] = {1, 0, 0, 1};
    std::vector<std::uint8_t> target(6 * 4 * 4, 19);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 6, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend(true, true, false);
    auto first = backend.renderDeferred(draw);
    ASSERT_TRUE(first) << backend.lastError();
    for (int variant = 1; variant <= 70; ++variant) {
        draw.scissor = {-variant, 0, variant + 4, 4};
        ASSERT_TRUE(backend.renderDeferred(draw)) << backend.lastError();
        draw.scissor = {0, 0, 4, 4};
        ASSERT_TRUE(backend.renderDeferred(draw)) << backend.lastError();
    }
    // One extra bundle preserves the first live snapshot. Bundles still executing on the GPU may
    // add up to the live-version limit (8) instead of stalling, depending on GPU timing.
    EXPECT_GE(backend.reusedDraws(), 131u);
    EXPECT_LE(backend.reusedDraws(), 139u);
    EXPECT_EQ(backend.pipelineCreations(), 1u); // Scissor variants share dynamic pipeline state.
    first->resolve();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 6; ++x) {
            const auto offset = (y * 6 + x) * 4;
            EXPECT_EQ((std::array{first->rgba[offset], first->rgba[offset + 1], first->rgba[offset + 2], first->rgba[offset + 3]}),
                      (x < 4 ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{19, 19, 19, 19}));
        }
}

TEST(VulkanRasterBackendTest, DirectAliasesPreserveTargetCopiesSwizzlesAndDeferredVersions)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto producer = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(producer) << producer.error;
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    for (auto &v: vertices)
        v.inputs[0] = {.25f, .5f, .75f, 1.f};
    std::vector<std::uint8_t> target(3 * 2 * 16);
    for (unsigned y = 0; y < 2; ++y)
        writeFloatColor(target.data() + (y * 3 + 2) * 16, {-7.f, -8.f, -9.f, -10.f}, 15);
    RasterDraw make{producer, vertices, {}, {}, target, 2, 2, 3, {0, 0, 2, 2}, 15, {}};
    make.colorFormat = ColorFormat::RGBA32Float;
    VulkanRasterBackend backend(true, true, false);
    auto source = backend.renderDeferred(make);
    ASSERT_TRUE(source && source->readback) << backend.lastError();
    unsigned materializations{};
    auto original = source->readback;
    auto wrapped = std::make_shared<RasterReadback>(
            original->size(),
            [&] {
                ++materializations;
                return original->resolve();
            },
            original->image());
    Latte::FragmentShader shader;
    shader.source = R"(#version 450
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D first;
layout(set=0,binding=2) uniform sampler2D second;
void main() { color=textureLod(first,vec2(.25),0.0)+textureLod(second,vec2(.25),0.0); }
)";
    shader.textures = {{0, 0, 1}, {0, 1, 2}};
    std::array<RasterDraw::Texture, 2> textures;
    for (unsigned i = 0; i < 2; ++i) {
        textures[i].binding = shader.textures[i];
        textures[i].width = textures[i].height = 2;
        textures[i].unorm8Pitch = 3;
        textures[i].renderedFormat = ColorFormat::RGBA32Float;
        textures[i].rendered = wrapped;
        textures[i].texel = [](unsigned, unsigned) -> std::array<float, 4> { throw std::runtime_error("Unexpected CPU sampling"); };
    }
    textures[1].unorm8Map = 0x02000105;
    std::array<RasterDraw::Vertex, 1> point{{{.5f, .5f, {}}}};
    RasterDraw draw{shader, point, {}, textures, target, 2, 2, 3, {0, 0, 2, 2}, 15, {}};
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.topology = RasterDraw::Topology::Points;
    draw.renderedTarget = wrapped; // Same source is copied to the destination and sampled twice.
    auto output = backend.renderDeferred(draw);
    ASSERT_TRUE(output && output->readback) << backend.lastError();
    EXPECT_EQ(materializations, 0u);
    EXPECT_EQ(backend.readbackTransfers(), 0u);
    EXPECT_EQ(backend.residentTextureCopies(), 0u);
    EXPECT_EQ(backend.directTextureBindings(), 2u);
    EXPECT_EQ(backend.directTextureBytes(), 2u * 2u * 2u * 16u);
    output->resolve();
    EXPECT_EQ(readFloatColor(output->rgba.data()), (std::array{1.f, .75f, 1.25f, 2.f}));
    EXPECT_EQ(readFloatColor(output->rgba.data() + 16), (std::array{.25f, .5f, .75f, 1.f}));
    for (unsigned y = 0; y < 2; ++y)
        EXPECT_EQ(readFloatColor(output->rgba.data() + (y * 3 + 2) * 16), (std::array{-7.f, -8.f, -9.f, -10.f}));

    // Switch the same bundle to CPU snapshots after it started with direct images.
    const auto &bytes = original->resolve();
    for (auto &texture: textures) {
        texture.rendered.reset();
        texture.rgba32Bytes = bytes;
        texture.rgba32Pitch = 3;
    }
    draw.renderedTarget.reset();
    auto cpu = backend.render(draw);
    ASSERT_TRUE(cpu) << backend.lastError();
    EXPECT_EQ(readFloatColor(cpu->rgba.data()), (std::array{1.f, .75f, 1.25f, 2.f}));
    EXPECT_EQ(backend.directTextureBindings(), 2u);

    // A subsequent producer overwrite preserves the old deferred result.
    for (auto &v: vertices)
        v.inputs[0] = {2.f, 3.f, 4.f, 5.f};
    ASSERT_TRUE(backend.renderDeferred(make)) << backend.lastError();
    EXPECT_EQ(readFloatColor(original->resolve().data()), (std::array{.25f, .5f, .75f, 1.f}));
    EXPECT_EQ(readFloatColor(output->rgba.data()), (std::array{1.f, .75f, 1.25f, 2.f}));
}

TEST(LatteLoweringTest, UnconditionalColorProofRejectsExecutionMasksAndDepthExports)
{
    const auto plain = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(plain);
    EXPECT_TRUE(plain.alwaysWritesColor);
    for (unsigned flags: {0u, 4u, 8u, 12u}) {
        const auto shader = lowerFragmentShader(
                *decodeProgram({2, 8u << 26, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u, (OP2_PRED_SETGT << 7) | flags}));
        ASSERT_TRUE(shader) << shader.error;
        EXPECT_EQ(shader.alwaysWritesColor, !(flags & 4));
    }
    const auto depth = lowerFragmentShader(*decodeProgram({61, (CF_EXP_DONE << 23) | (1u << 21) | 0xFF8u}));
    ASSERT_TRUE(depth) << depth.error;
    EXPECT_FALSE(depth.alwaysWritesColor);
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram({})).alwaysWritesColor);
}

TEST(VulkanRasterBackendTest, FullOverwriteAvoidsInitialTransfersOnlyWithCompleteCoverageAndUnconditionalWrites)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader && shader.alwaysWritesColor);
    std::array<RasterDraw::Vertex, 6> vertices{{{0, 0, {}}, {4, 0, {}}, {4, 3, {}}, {0, 0, {}}, {4, 3, {}}, {0, 3, {}}}};
    for (auto &v: vertices)
        v.inputs[0] = {.25f, .5f, .75f, 1.f};
    std::vector<std::uint8_t> target(6 * 3 * 4, 41);
    for (unsigned y = 0; y < 3; ++y)
        std::fill(target.begin() + (y * 6 + 4) * 4, target.begin() + (y + 1) * 6 * 4, 251);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 3, 6, {0, 0, 4, 3}, 15, {}};
    VulkanRasterBackend backend(true, true, false);
    shader.alwaysWritesColor = false;
    auto reference = backend.render(draw);
    ASSERT_TRUE(reference) << backend.lastError();
    const auto uploads = backend.targetUploadTransfers();
    shader.alwaysWritesColor = true;
    auto first = backend.renderDeferred(draw);
    ASSERT_TRUE(first && first->readback) << backend.lastError();
    EXPECT_EQ(backend.targetUploadTransfers(), uploads);
    EXPECT_EQ(backend.targetCopyTransfers(), 0u);
    EXPECT_EQ(backend.fullOverwriteDraws(), 1u);
    draw.renderedTarget = first->readback;
    auto second = backend.renderDeferred(draw);
    ASSERT_TRUE(second && second->readback) << backend.lastError();
    EXPECT_EQ(backend.targetCopyTransfers(), 0u);
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);
    first->resolve();
    second->resolve();
    EXPECT_EQ(first->rgba, reference->rgba);
    EXPECT_EQ(second->rgba, reference->rgba);
    draw.renderedTarget.reset();
    draw.channelMask = 5;
    ASSERT_TRUE(backend.render(draw));
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);
    draw.channelMask = 15;
    draw.blend = {true, 1, 1, 0, 1, 1, 0, {}};
    ASSERT_TRUE(backend.render(draw));
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);
    draw.blend = {};
    draw.scissor = {0, 0, 3, 3};
    auto clipped = backend.render(draw);
    ASSERT_TRUE(clipped);
    EXPECT_EQ(clipped->rgba[3 * 4], 41);
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);
    draw.scissor = {0, 0, 4, 3};
    vertices[5] = vertices[4]; // bounding box covers the target, but the triangle pair does not.
    auto hole = backend.render(draw);
    ASSERT_TRUE(hole);
    EXPECT_EQ(hole->rgba[(2 * 6) * 4], 41);
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);
    vertices[5] = {0, 3, {}};
    vertices[5].inputs[0] = {.25f, .5f, .75f, 1.f};
    for (auto &v: vertices)
        v.reciprocalW = .5f;
    ASSERT_TRUE(backend.render(draw));
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);
    for (auto &v: vertices) {
        v.reciprocalW = 1.f;
        v.z = .5f;
    }
    std::vector<float> depths(6 * 3, 0.f);
    draw.depth = {true, false, 3, DepthFormat::Float32, depths, 4, 3, 6, {}};
    auto rejected = backend.render(draw);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->rgba, target);
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);
    draw.depth = {};
    shader =
            lowerFragmentShader(*decodeProgram({2, 8u << 26, 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u, (OP2_PRED_SETGT << 7) | 4u}));
    ASSERT_TRUE(shader && !shader.alwaysWritesColor);
    auto masked = backend.render(draw);
    ASSERT_TRUE(masked);
    EXPECT_EQ(masked->rgba, target);
    EXPECT_EQ(backend.fullOverwriteDraws(), 2u);

    shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    std::vector<std::uint8_t> hdr(6 * 3 * 16);
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 6; ++x)
            writeFloatColor(hdr.data() + (y * 6 + x) * 16, {-7.f, -8.f, -9.f, -10.f}, 15);
    for (auto &v: vertices)
        v.inputs[0] = {2.f, 3.f, -1.f, .5f};
    draw.colorFormat = ColorFormat::RGBA32Float;
    draw.target = hdr;
    const auto oldUploads = backend.targetUploadTransfers();
    auto floating = backend.render(draw);
    ASSERT_TRUE(floating);
    EXPECT_EQ(backend.targetUploadTransfers(), oldUploads);
    EXPECT_EQ(backend.fullOverwriteDraws(), 3u);
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 6; ++x)
            EXPECT_EQ(readFloatColor(floating->rgba.data() + (y * 6 + x) * 16),
                      (x < 4 ? std::array{2.f, 3.f, -1.f, .5f} : std::array{-7.f, -8.f, -9.f, -10.f}));
}

TEST(VulkanRasterBackendTest, IndependentLiveColorAndDepthVersionsStayResidentUntilRead)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    for (const auto format: {ColorFormat::RGBA8, ColorFormat::RGBA32Float}) {
        std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {8, 0, {}}, {0, 8, {}}}};
        std::vector<std::uint8_t> target(6 * 4 * colorElementBytes(format), 19);
        std::vector<float> depth(6 * 4, .9f);
        RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 6, {0, 0, 4, 4}, 15, {}};
        draw.colorFormat = format;
        draw.depth = {true, true, 7, DepthFormat::Float32, depth, 4, 4, 6, {}};
        auto backend = std::make_unique<VulkanRasterBackend>(true, true);
        std::vector<RasterResult> snapshots;
        for (unsigned i = 1; i <= 8; ++i) {
            for (auto &vertex: vertices) {
                vertex.inputs[0] = {float(i) / 16, .25f, .5f, 1};
                vertex.z = float(i) / 16;
            }
            auto result = backend->renderDeferred(draw);
            ASSERT_TRUE(result) << backend->lastError();
            snapshots.push_back(std::move(*result));
        }
        EXPECT_EQ(backend->readbackTransfers(), 0u);
        backend.reset(); // Tickets keep the Vulkan device and their versions alive.
        for (unsigned i = 0; i < snapshots.size(); ++i) {
            auto &result = snapshots[i];
            result.resolve();
            result.resolveDepth();
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 6; ++x) {
                    const auto offset = (y * 6 + x) * colorElementBytes(format);
                    if (x < 4) {
                        if (format == ColorFormat::RGBA8)
                            EXPECT_NEAR(result.rgba[offset], std::round(float(i + 1) * 255 / 16), 1);
                        else
                            EXPECT_EQ(readFloatColor(result.rgba.data() + offset), (std::array{float(i + 1) / 16, .25f, .5f, 1.f}));
                        EXPECT_FLOAT_EQ(result.depth[y * 6 + x], float(i + 1) / 16);
                    } else {
                        for (unsigned b = 0; b < colorElementBytes(format); ++b)
                            EXPECT_EQ(result.rgba[offset + b], 19);
                        EXPECT_FLOAT_EQ(result.depth[y * 6 + x], .9f);
                    }
                }
        }
    }
}

TEST(VulkanRasterBackendTest, LiveVersionRotationRemainsBoundedAndDiscardedTicketsReuseWithoutReadback)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader) << shader.error;
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {8, 0, {}}, {0, 8, {}}}};
    std::vector<std::uint8_t> target(64, 19);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend(true, true, false);
    std::vector<RasterResult> snapshots;
    for (unsigned i = 0; i < 390; ++i) {
        for (auto &vertex: vertices)
            vertex.inputs[0] = {float(i) / 512, 0, 0, 1};
        auto result = backend.renderDeferred(draw);
        ASSERT_TRUE(result) << backend.lastError();
        snapshots.push_back(std::move(*result));
    }
    EXPECT_EQ(backend.readbackTransfers(), 382u); // Eight live versions; older tickets materialize before bundle reuse.
    EXPECT_EQ(backend.reusedDraws(), 382u);
    for (unsigned i = 0; i < snapshots.size(); ++i) {
        snapshots[i].resolve();
        EXPECT_NEAR(snapshots[i].rgba[0], std::round(float(i) * 255 / 512), 1);
    }
    snapshots.clear();
    const auto transfers = backend.readbackTransfers();
    for (unsigned i = 0; i < 8; ++i)
        ASSERT_TRUE(backend.renderDeferred(draw)) << backend.lastError();
    EXPECT_EQ(backend.readbackTransfers(), transfers);
    EXPECT_GE(backend.reusedDraws(), 7u);
}

TEST(VulkanRasterBackendTest, QueuedDrawsShareFenceAndReadbacksPreserveTheirVersions)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto *setting = std::getenv("WEMU_NATIVE_BATCH_SUBMITS");
    const bool batched = !setting || std::strtoul(setting, nullptr, 10) > 3;
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    ASSERT_TRUE(shader);
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {8, 0, {}}, {0, 8, {}}}};
    std::vector<std::uint8_t> target(64, 19);
    RasterDraw draw{shader, vertices, {}, {}, target, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    VulkanRasterBackend backend(true, true, false);
    std::vector<RasterResult> snapshots;
    for (unsigned i = 1; i <= 3; ++i) {
        for (auto &vertex: vertices)
            vertex.inputs[0] = {float(i) / 255, 0, 0, 1};
        auto result = backend.renderDeferred(draw);
        ASSERT_TRUE(result) << backend.lastError();
        snapshots.push_back(std::move(*result));
    }
    EXPECT_EQ(backend.queueSubmissions(), batched ? 0u : 3u);
    snapshots.back().resolve(); // Flush all producers plus this ticket's copy.
    EXPECT_EQ(backend.submittedCommandBuffers(), 4u);
    EXPECT_EQ(backend.queueSubmissions(), batched ? 1u : 4u);
    EXPECT_EQ(backend.submissionWaits(), 1u);
    for (unsigned i = 0; i < snapshots.size(); ++i) {
        snapshots[i].resolve();
        EXPECT_EQ(snapshots[i].rgba[0], i + 1);
    }
    EXPECT_EQ(backend.submittedCommandBuffers(), 6u);
    EXPECT_EQ(backend.queueSubmissions(), batched ? 3u : 6u);
    EXPECT_EQ(backend.submissionWaits(), 3u);
}

TEST(LatteLoweringTest, FloatIntegerConversionsPreserveBitsAcrossBoundariesOnReferenceAndVulkan)
{
    const std::array<std::pair<float, std::uint32_t>, 19> signedCases{{{0.f, 0},
                                                                       {-0.f, 0},
                                                                       {.75f, 0},
                                                                       {-.75f, 0},
                                                                       {1.75f, 1},
                                                                       {-1.75f, 0xFFFFFFFF},
                                                                       {2147483520.f, 0x7FFFFF80},
                                                                       {-2147483648.f, 0x80000000},
                                                                       {2147483648.f, 0x80000000},
                                                                       {4294967040.f, 0xFFFFFF00},
                                                                       {4294967296.f, 0},
                                                                       {-4294967040.f, 256},
                                                                       {4294967808.f, 512},
                                                                       {-4294967808.f, 0xFFFFFE00},
                                                                       {0x1p55f, 0},
                                                                       {std::numeric_limits<float>::denorm_min(), 0},
                                                                       {std::numeric_limits<float>::quiet_NaN(), 0},
                                                                       {std::numeric_limits<float>::infinity(), 0x7FFFFFFF},
                                                                       {-std::numeric_limits<float>::infinity(), 0x80000000}}};
    for (const auto opcode: {OP2_FLT_TO_INT, OP2_FLT_TO_UINT}) {
        const auto program =
                decodeProgram({2, (8u << 26) | (2u << 18), 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | 256u,
                               (unsigned(opcode) << 7) | 16u | (3u << 21), 0x80000000u | 3u | (257u << 13), (OP2_XOR_INT << 7) | 16u | (3u << 21),
                               0x80000000u | 3u | (SRC_1 << 13), SRC_0 | (OP3_CNDE_INT << 13) | (3u << 21)});
        const auto fragment = lowerFragmentShader(*program);
        ASSERT_TRUE(fragment) << fragment.error;
        FragmentShader vertex;
        vertex.source = probeVertexSource;
        std::vector<std::uint32_t> vs, ps;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
        ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
        for (const auto &[input, signedExpected]: signedCases) {
            const auto expected = opcode == OP2_FLT_TO_INT          ? signedExpected
                                  : std::isnan(input) || input <= 0 ? 0u
                                  : input >= 4294967296.f           ? 0xFFFFFFFFu
                                                                    : std::uint32_t(input);
            std::array<float, 1024> constants{};
            constants[0] = input;
            constants[4] = std::bit_cast<float>(expected);
            const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), constants.size(), {});
            ASSERT_TRUE(reference.colorValid);
            for (float c: reference.color)
                EXPECT_EQ(c, 1.f) << input << " opcode=" << opcode;
            if (std::getenv("WEMU_TEST_VULKAN")) {
                VulkanFragmentProbe probe;
                probe.initialize(vs, ps, constants);
                for (const auto &pixel: probe.draw({}))
                    for (float c: pixel)
                        EXPECT_EQ(c, 1.f) << input << " opcode=" << opcode;
            }
        }
        // Both stages use the same ALU lowerer; exercise the vertex export too.
        const auto convertedVertex =
                lowerVertexShader(*decodeProgram({2, 8u << 26, 60u | (1u << 13) | (3u << 15), (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u,
                                                  (unsigned(opcode) << 7) | 16u | (3u << 21)}));
        ASSERT_TRUE(convertedVertex) << convertedVertex.error;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(convertedVertex, nullptr, "vert"));
    }
}

TEST(LatteLoweringTest, PopIgnoresAddressAndRestoresMaskedExecution)
{
    for (const auto address: {0u, 7u, 0xFFFFFFFFu}) {
        const std::vector<std::uint32_t> words{5,
                                               9u << 26,
                                               6,
                                               8u << 26,
                                               address,
                                               (0x0Eu << 23) | 1u,
                                               7,
                                               8u << 26,
                                               3u << 15,
                                               (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                               0x80000000u | (SRC_0 << 13),
                                               (OP2_PRED_SETGT << 7) | 12u,
                                               0x80000000u | SRC_1,
                                               (OP2_MOV << 7) | 16u | (3u << 21),
                                               0x80000000u | SRC_HALF,
                                               (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29)};
        const auto program = decodeProgram(words);
        for (const auto input: {-2.f, 0.f, 2.f}) {
            VulkanFragmentProbe::Inputs inputs{};
            inputs[0][0] = input;
            const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
            ASSERT_TRUE(reference.colorValid);
            EXPECT_EQ(reference.color, (std::array{input > 0 ? 1.f : 0.f, .5f, 0.f, 0.f}));
            ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
        }
        auto vertexWords = words;
        vertexWords[8] = 60u | (1u << 13) | (3u << 15);
        const auto vertex = lowerVertexShader(*decodeProgram(vertexWords));
        ASSERT_TRUE(vertex) << vertex.error;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, nullptr, "vert"));
    }
}

TEST(LatteLoweringTest, TruncAndTrigonometryPreserveVectorAndScalarForwarding)
{
    FragmentShader probeVertex;
    probeVertex.source = probeVertexSource;
    std::vector<std::uint32_t> vs;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(probeVertex, &vs, "vert"));
    for (const auto opcode: {OP2_TRUNC, OP2_SIN, OP2_COS}) {
        for (const bool writeRegister: {false, true}) {
            const auto forwarding = opcode == OP2_TRUNC ? SRC_PV : SRC_PS;
            const std::vector<std::uint32_t> words{2,
                                                   (8u << 26) | (1u << 18),
                                                   3u << 15,
                                                   (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                                   0x80000000u | 256u,
                                                   (unsigned(opcode) << 7) | (writeRegister ? 16u : 0u) | (3u << 21),
                                                   0x80000000u | forwarding,
                                                   (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29)};
            const auto program = decodeProgram(words);
            const auto fragment = lowerFragmentShader(*program);
            ASSERT_TRUE(fragment) << fragment.error;
            std::vector<std::uint32_t> ps;
            ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
            auto vertexWords = words;
            vertexWords[2] = 60u | (1u << 13) | (3u << 15);
            const auto vertex = lowerVertexShader(*decodeProgram(vertexWords));
            ASSERT_TRUE(vertex) << vertex.error;
            ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, nullptr, "vert"));
            std::array<float, 1024> constants{};
            const std::array values{-2.75f, -1.f, -.5f, -.25f, -.125f, -0.f, 0.f, .125f, .25f, .5f, 1.f, 2.75f};
            for (float value: values) {
                // Hardware trig inputs have a bounded domain. Exercise ordinary
                // normalized turns without asserting host behavior outside it.
                if (opcode != OP2_TRUNC && std::abs(value) > .5f)
                    continue;
                SCOPED_TRACE(std::format("opcode={} input={}", unsigned(opcode), value));
                constants[0] = value;
                const float expected = opcode == OP2_TRUNC ? std::trunc(value)
                                       : opcode == OP2_SIN ? std::sin(value / .1591549367f)
                                                           : std::cos(value / .1591549367f);
                const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), constants.size(), {});
                ASSERT_TRUE(reference.colorValid);
                EXPECT_EQ(reference.color, (std::array{writeRegister ? expected : 0.f, expected, 0.f, 0.f}));
                const auto vertexReference = Core::Gfx::LatteVsInterp::run(vertexWords, {}, constants.data(), constants.size());
                ASSERT_TRUE(vertexReference.valid);
                EXPECT_EQ(vertexReference.pos, reference.color);
                if (std::getenv("WEMU_TEST_VULKAN")) {
                    VulkanFragmentProbe probe;
                    probe.initialize(vs, ps, constants);
                    for (const auto &pixel: probe.draw({})) {
                        for (unsigned channel = 0; channel < 4; ++channel)
                            EXPECT_NEAR(pixel[channel], reference.color[channel], 2e-6f);
                        if (opcode == OP2_TRUNC && expected == 0.f)
                            EXPECT_EQ(std::signbit(pixel[1]), std::signbit(expected));
                    }
                }
            }
        }
    }
}

TEST(LatteLoweringTest, TruncPreservesLargeAndExceptionalValuesOnReferenceAndVulkan)
{
    const auto program =
            decodeProgram({2, 8u << 26, 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | 256u, (OP2_TRUNC << 7) | 16u | (3u << 21)});
    const auto fragment = lowerFragmentShader(*program);
    ASSERT_TRUE(fragment) << fragment.error;
    FragmentShader probeVertex;
    probeVertex.source = probeVertexSource;
    std::vector<std::uint32_t> vs, ps;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(probeVertex, &vs, "vert"));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
    for (float input: {2147483648.f, -2147483648.f, std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        std::array<float, 1024> constants{};
        constants[0] = input;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), constants.size(), {});
        ASSERT_TRUE(reference.colorValid);
        const auto check = [&](float output) {
            if (std::isnan(input))
                EXPECT_TRUE(std::isnan(output));
            else
                EXPECT_EQ(output, input);
        };
        check(reference.color[0]);
        if (std::getenv("WEMU_TEST_VULKAN")) {
            VulkanFragmentProbe probe;
            probe.initialize(vs, ps, constants);
            for (const auto &pixel: probe.draw({}))
                check(pixel[0]);
        }
    }
}

TEST(LatteLoweringTest, TruncConsumesScalarResultWithoutRegisterWrites)
{
    const auto program = decodeProgram({2, (8u << 26) | (2u << 18), 3u << 15, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x80000000u, OP2_SIN << 7,
                                        0x80000000u | SRC_PS, OP2_TRUNC << 7, 0x80000000u | SRC_PV, (OP2_MOV << 7) | 16u | (3u << 21)});
    for (float value: {-.25f, -.125f, .125f, .25f}) {
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0][0] = value;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
        ASSERT_TRUE(reference.colorValid);
        const float expected = std::trunc(std::sin(value / .1591549367f));
        EXPECT_EQ(reference.color, (std::array{expected, 0.f, 0.f, 0.f}));
        ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
    }
}

TEST(LatteLoweringTest, AddressFloorAndTruncHaveDistinctHardwareOpcodes)
{
    EXPECT_EQ(unsigned(OP2_TRUNC), 0x11u);
    EXPECT_EQ(unsigned(OP2_MOVA_FLOOR), 0x16u);
    // Use literal ISA encodings so a mistaken shared enum cannot bless both backends.
    const auto program = decodeProgram({2, (8u << 26) | (1u << 18), 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | 256u, 0x16u << 7,
                                        0x80000000u | 272u | (1u << 9), (0x19u << 7) | 16u | (3u << 21)});
    const auto fragment = lowerFragmentShader(*program);
    ASSERT_TRUE(fragment) << fragment.error;
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vs, ps;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
    std::array<float, 1024> constants{};
    for (unsigned i = 1; i < 256; ++i)
        constants[i * 4] = float(i) / 256.f;
    for (float index: {-16.f, -2.75f, -0.f, 0.f, 1.75f, 239.f, -17.f, 240.f, 255.f, 256.f, -257.f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()}) {
        SCOPED_TRACE(index);
        constants[0] = index;
        const int address = !std::isfinite(index) || index < -256.f || index >= 256.f ? -256 : int(std::floor(index));
        const int selected = 16 + address;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), 256, {});
        ASSERT_TRUE(reference.colorValid);
        if (selected < 0 || selected >= 256)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(reference.color[0]), 0x7FFFFFFFu);
        else
            EXPECT_EQ(reference.color[0], constants[unsigned(selected) * 4]);
        if (std::getenv("WEMU_TEST_VULKAN")) {
            VulkanFragmentProbe probe;
            probe.initialize(vs, ps, constants);
            for (const auto &pixel: probe.draw({})) {
                if (std::isnan(reference.color[0]))
                    EXPECT_TRUE(std::isnan(pixel[0]));
                else
                    EXPECT_EQ(pixel[0], reference.color[0]);
            }
        }
    }
}

TEST(LatteLoweringTest, AddressComponentsRequirePriorGroupsAndResetEveryClause)
{
    for (unsigned component = 0; component < 4; ++component) {
        auto words = std::vector<std::uint32_t>{2,
                                                (8u << 26) | (1u << 18),
                                                3u << 15,
                                                (CF_EXP_DONE << 23) | (1u << 21),
                                                0x80000000u | 256u,
                                                (OP2_MOVA_INT << 7) | (component << 29),
                                                0x80000000u | 260u | (1u << 9) | (component << 26),
                                                (OP2_MOV << 7) | 16u | (3u << 21)};
        const auto program = decodeProgram(words);
        const auto fragment = lowerFragmentShader(*program);
        ASSERT_TRUE(fragment) << fragment.error;
        std::array<float, 1024> constants{};
        constants[0] = std::bit_cast<float>(std::int32_t(-2));
        constants[8] = .75f;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), 256, {});
        ASSERT_TRUE(reference.colorValid);
        EXPECT_EQ(reference.color[0], .75f);
        ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment));
        auto vertexWords = words;
        vertexWords[2] = 60u | (1u << 13) | (3u << 15);
        const auto vertex = lowerVertexShader(*decodeProgram(vertexWords));
        ASSERT_TRUE(vertex) << vertex.error;
        ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, nullptr, "vert"));
        const auto vertexReference = Core::Gfx::LatteVsInterp::run(vertexWords, {}, constants.data(), 256);
        ASSERT_TRUE(vertexReference.valid);
        EXPECT_EQ(vertexReference.pos[0], .75f);
        words[6] = (words[6] & ~(7u << 26)) | (4u << 26);
        EXPECT_FALSE(lowerFragmentShader(*decodeProgram(words))); // LOOP needs loop state.
        EXPECT_FALSE(Core::Gfx::LatteVsInterp::runPixel(*decodeProgram(words), {}, constants.data(), 256, {}).colorValid);
    }
    for (const auto &words:
         {// Reading an unwritten AR is not the same as an implicit zero offset.
          std::vector<std::uint32_t>{2, 8u << 26, 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | 256u | (1u << 9),
                                     (OP2_MOV << 7) | 16u | (3u << 21)},
          // A MOVA and relative read cannot share their simultaneous ALU group.
          std::vector<std::uint32_t>{2, (8u << 26) | (1u << 18), 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 256u, OP2_MOVA_FLOOR << 7,
                                     0x80000000u | 256u | (1u << 9), (OP2_MOV << 7) | 16u | (3u << 21) | (1u << 29)},
          // AR is clause local, even if the previous clause wrote it.
          std::vector<std::uint32_t>{3, 8u << 26, 4, 8u << 26, 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | SRC_1, OP2_MOVA_FLOOR << 7,
                                     0x80000000u | 256u | (1u << 9), (OP2_MOV << 7) | 16u | (3u << 21)}}) {
        const auto program = decodeProgram(words);
        EXPECT_FALSE(lowerFragmentShader(*program));
        EXPECT_FALSE(Core::Gfx::LatteVsInterp::runPixel(*program, {}, nullptr, 0, {}).colorValid);
    }
}

TEST(LatteLoweringTest, WholeQuadDeadWritesCanBeOverwrittenAfterReconvergence)
{
    const std::vector<std::uint32_t> words{5,
                                           9u << 26,
                                           6,
                                           (8u << 26) | (1u << 30),
                                           7,
                                           10u << 26,
                                           8,
                                           8u << 26,
                                           4u << 15,
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                           0x80000000u | (1u << 13),
                                           (OP2_PRED_SETGT << 7) | 12u,
                                           0x80000000u | SRC_1,
                                           (OP2_MOV << 7) | 16u | (4u << 21),
                                           0x80000000u | SRC_0,
                                           (OP2_MOV << 7) | 16u | (3u << 21),
                                           0x80000000u | SRC_HALF,
                                           (OP2_MOV << 7) | 16u | (4u << 21)};
    const auto program = decodeProgram(words);
    for (float input: {-1.f, 1.f}) {
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0][0] = input;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
        ASSERT_TRUE(reference.colorValid);
        EXPECT_EQ(reference.color, (std::array{.5f, 0.f, 0.f, 0.f}));
        ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
    }
    auto selfRead = words;
    selfRead[16] = 0x80000000u | 4u;
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(selfRead)));
    auto predicated = words;
    predicated[16] |= 3u << 29;
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(predicated)));
    auto partial = words;
    partial[13] |= 1u << 29; // Whole-quad Y survives the ordinary X overwrite.
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(partial)));
}

TEST(LatteLoweringTest, MaskedVertexExportsPreserveEarlierComponentsAndEmptyExports)
{
    const std::vector<std::uint32_t> words{60u | (1u << 13) | (1u << 15),
                                           (CF_EXP << 23) | 0x688u,
                                           60u | (1u << 13) | (2u << 15),
                                           (CF_EXP_DONE << 23) | 0xFC7u, // Only Y, from R2.x.
                                           (2u << 13) | (1u << 15),
                                           (CF_EXP << 23) | 0x688u,
                                           (2u << 13) | (2u << 15),
                                           (CF_EXP << 23) | 0x5FFu, // Only W, from R2.z.
                                           (1u << 13) | 60u,
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0xFFFu};
    const auto vertex = lowerVertexShader(*decodeProgram(words));
    ASSERT_TRUE(vertex) << vertex.error;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, nullptr, "vert"));
    VulkanFragmentProbe::Inputs attributes{};
    attributes[0] = {-.5f, .5f, .25f, 1.f};
    attributes[1] = {-.25f, .75f, 2.f, 3.f};
    const auto reference = Core::Gfx::LatteVsInterp::run(words, attributes, nullptr, 0);
    ASSERT_TRUE(reference.valid);
    EXPECT_EQ(reference.pos, (std::array{-.5f, -.25f, .25f, 1.f}));
    ASSERT_TRUE(reference.paramValid[0]);
    EXPECT_EQ(reference.params[0], (std::array{-.5f, .5f, .25f, 2.f}));
    EXPECT_EQ(vertex.parameterMask, 1u);
    if (std::getenv("WEMU_TEST_VULKAN")) {
        using namespace Core::Gfx;
        const auto fragment = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
        std::array<RasterDraw::Vertex, 2> points{};
        for (unsigned i = 0; i < points.size(); ++i) {
            points[i].inputs[0] = {i ? .5f : -.5f, 0.f, .25f, 1.f};
            points[i].inputs[1] = {0.f, 0.f, .75f, 0.f};
        }
        std::array<std::uint8_t, 32> target{};
        RasterDraw draw{fragment, points, {}, {}, target, 2, 1, 2, {0, 0, 2, 1}, 15, {}};
        draw.topology = RasterDraw::Topology::Points;
        draw.colorFormat = ColorFormat::RGBA32Float;
        draw.vertexStage = {std::make_shared<VertexShader>(vertex), {}, {}, {0, -1, -1, -1}, {0, 0, 2, 1, 0, 1}};
        VulkanRasterBackend backend;
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        EXPECT_EQ(readFloatColor(result->rgba.data()), (std::array{-.5f, 0.f, .25f, .75f}));
        EXPECT_EQ(readFloatColor(result->rgba.data() + 16), (std::array{.5f, 0.f, .25f, .75f}));
    }
    auto invalid = words;
    invalid[1] = (invalid[1] & ~7u) | 6u;
    EXPECT_FALSE(lowerVertexShader(*decodeProgram(invalid)));
}

TEST(LatteLoweringTest, WholeQuadConditionalPopPoisonsOnlyTheTakenEdge)
{
    const std::vector<std::uint32_t> words{6,
                                           9u << 26,
                                           7,
                                           (8u << 26) | (1u << 30),
                                           5,
                                           (0x0Au << 23) | 1u,
                                           8,
                                           10u << 26,
                                           9,
                                           8u << 26,
                                           3u << 15,
                                           (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                           0x80000000u | (1u << 13),
                                           (OP2_PRED_SETGT << 7) | 12u,
                                           0x80000000u | SRC_1,
                                           (OP2_MOV << 7) | 16u | (4u << 21),
                                           0x80000000u | 4u,
                                           (OP2_MOV << 7) | 16u | (3u << 21),
                                           0x80000000u | SRC_HALF,
                                           (OP2_MOV << 7) | 16u | (4u << 21)};
    const auto program = decodeProgram(words);
    for (float value: {-1.f, 1.f}) {
        VulkanFragmentProbe::Inputs inputs{};
        inputs[0][0] = value;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, nullptr, 0, {});
        ASSERT_TRUE(reference.colorValid);
        EXPECT_EQ(reference.color, (std::array{value > 0 ? 1.f : 0.f, 0.f, 0.f, 0.f}));
        ASSERT_NO_FATAL_FAILURE(compareNativeControlFlow(*program, inputs, reference.color));
    }
    auto escaped = words;
    escaped[10] = 4u << 15;
    // The jump skips the root overwrite. Its incoming helper value must remain
    // poisoned when the taken and fall-through paths meet at the export.
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(escaped)));
}

TEST(LatteLoweringTest, UniformWholeQuadMasksPermitLiveValuesAfterReconvergence)
{
    auto words = isolatedWholeQuadProgram();
    words[10] = 0x80000000u | 256u | (SRC_HALF << 13);
    words[8] = 4u << 15;
    const auto program = decodeProgram(words);
    const auto shader = lowerFragmentShader(*program);
    ASSERT_TRUE(shader) << shader.error;
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vs, ps;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(shader, &ps));
    for (float constant: {0.f, 1.f}) {
        std::array<float, 1024> constants{};
        constants[0] = constant;
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), 256, {});
        ASSERT_TRUE(reference.colorValid);
        EXPECT_EQ(reference.color, (std::array{constant, 0.f, 0.f, 0.f}));
        if (std::getenv("WEMU_TEST_VULKAN")) {
            VulkanFragmentProbe probe;
            probe.initialize(vs, ps, constants);
            for (const auto &pixel: probe.draw({}))
                EXPECT_EQ(pixel, reference.color);
        }
    }
    // The same control flow becomes unsafe when its gate reads interpolated data.
    words[10] = 0x80000000u | (SRC_HALF << 13);
    EXPECT_FALSE(lowerFragmentShader(*decodeProgram(words)));
}

TEST(LatteLoweringTest, PredicatedWritesUsePreGroupUniformityFacts)
{
    const auto program = decodeProgram({5,
                                        (8u << 26) | (2u << 18),
                                        8,
                                        9u << 26,
                                        9,
                                        (8u << 26) | (1u << 30),
                                        10,
                                        10u << 26,
                                        5u << 15,
                                        (CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                        0x80000000u | (SRC_0 << 13),
                                        (OP2_PRED_SETGT << 7) | 8u,
                                        SRC_1 | (SRC_0 << 13),
                                        (OP2_PRED_SETGT << 7) | 8u,
                                        0x80000000u | SRC_1 | (3u << 29),
                                        (OP2_MOV << 7) | 16u | (4u << 21),
                                        0x80000000u | 4u | (SRC_0 << 13),
                                        (OP2_PRED_SETGT << 7) | 12u,
                                        0x80000000u | SRC_1,
                                        (OP2_MOV << 7) | 16u | (5u << 21),
                                        0x80000000u | SRC_0,
                                        (OP2_MOV << 7) | 16u | (6u << 21)});
    // The constant predicate write shares a group with a MOV predicated by
    // the previous varying predicate. Its destination is still varying.
    const auto shader = lowerFragmentShader(*program);
    EXPECT_FALSE(shader);
    EXPECT_TRUE(shader.error.starts_with("whole-quad value escapes masked region:"));
}

TEST(LatteLoweringTest, SquareRootMatchesReferenceIncludingExceptionalInputs)
{
    std::vector<std::uint32_t> words{2, (8u << 26) | (3u << 18), 0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    for (unsigned channel = 0; channel < 4; ++channel) {
        words.push_back(0x80000000u | 256u | (channel << 10));
        words.push_back((OP2_SQRT_IEEE << 7) | 16u | (channel << 29));
    }
    const auto program = decodeProgram(words);
    const auto shader = lowerFragmentShader(*program);
    ASSERT_TRUE(shader) << shader.error;
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<std::uint32_t> vs, ps;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(shader, &ps));
    const std::array<std::array<float, 4>, 3> cases{{{0.f, .25f, 1.f, 4.f},
                                                     {-0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()},
                                                     {.0625f, .5625f, 9.f, -std::numeric_limits<float>::infinity()}}};
    for (const auto &values: cases) {
        std::array<float, 1024> constants{};
        std::copy(values.begin(), values.end(), constants.begin());
        const auto reference = Core::Gfx::LatteVsInterp::runPixel(*program, {}, constants.data(), 256, {});
        ASSERT_TRUE(reference.colorValid);
        for (unsigned channel = 0; channel < 4; ++channel) {
            const float expected = std::sqrt(values[channel]);
            if (std::isnan(expected))
                EXPECT_TRUE(std::isnan(reference.color[channel]));
            else
                EXPECT_EQ(std::bit_cast<unsigned>(reference.color[channel]), std::bit_cast<unsigned>(expected));
        }
        if (!std::getenv("WEMU_TEST_VULKAN"))
            continue;
        VulkanFragmentProbe probe;
        probe.initialize(vs, ps, constants);
        for (const auto &pixel: probe.draw({}))
            for (unsigned channel = 0; channel < 4; ++channel)
                if (std::isnan(reference.color[channel]))
                    EXPECT_TRUE(std::isnan(pixel[channel]));
                else
                    EXPECT_EQ(std::bit_cast<unsigned>(pixel[channel]), std::bit_cast<unsigned>(reference.color[channel]));
    }
}

TEST(VulkanDepthTest, DepthOnlyUsesItsOwnExtentAndPreservesUnusedColorVersions)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    std::array<RasterDraw::Vertex, 6> vertices{{{0, 0, {}}, {6, 0, {}}, {0, 4, {}}, {6, 0, {}}, {6, 4, {}}, {0, 4, {}}}};
    for (auto &vertex: vertices) {
        vertex.z = .25f;
        vertex.inputs[0] = {1, 0, 0, 1};
    }
    std::vector<std::uint8_t> color(3 * 2 * 4, 29);
    std::vector<float> depth(8 * 4, .875f);
    RasterDraw draw{shader, vertices, {}, {}, color, 2, 2, 3, {1, 1, 4, 2}, 0, {}};
    auto backend = std::make_unique<VulkanRasterBackend>(true, true);
    std::vector<RasterResult> snapshots;
    for (const auto format: {DepthFormat::Float32, DepthFormat::UNorm16, DepthFormat::UNorm24}) {
        const auto stored = quantizeDepth(.875f, format);
        std::fill(depth.begin(), depth.end(), stored);
        draw.depth = {true, true, 1, format, depth, 6, 4, 8, {}};
        auto result = backend->renderDeferred(draw);
        ASSERT_TRUE(result) << backend->lastError();
        ASSERT_TRUE(result->depthReadback);
        EXPECT_EQ(result->rgba, color);
        EXPECT_EQ(backend->targetUploadTransfers(), 0u);
        EXPECT_EQ(backend->targetCopyTransfers(), 0u);
        EXPECT_EQ(backend->readbackTransfers(), 0u);
        snapshots.push_back(std::move(*result));
    }
    // A live unused color version must stay lazy and retain its identity.
    draw.depth = {};
    draw.channelMask = 15;
    draw.scissor = {0, 0, 2, 2};
    auto colored = backend->renderDeferred(draw);
    ASSERT_TRUE(colored) << backend->lastError();
    ASSERT_TRUE(colored->readback);
    draw.renderedTarget = colored->readback;
    draw.channelMask = 0;
    draw.scissor = {0, 0, 6, 4};
    draw.depth = {true, true, 1, DepthFormat::Float32, depth, 6, 4, 8, {}};
    const auto reads = backend->readbackTransfers();
    auto depthOnly = backend->renderDeferred(draw);
    ASSERT_TRUE(depthOnly) << backend->lastError();
    EXPECT_EQ(depthOnly->readback, colored->readback);
    EXPECT_EQ(backend->readbackTransfers(), reads);
    backend.reset(); // Deferred depth/color tickets own independent lifetimes.
    for (unsigned index = 0; index < snapshots.size(); ++index) {
        auto &result = snapshots[index];
        result.resolveDepth();
        const auto format = DepthFormat(index);
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 8; ++x)
                EXPECT_FLOAT_EQ(result.depth[y * 8 + x], quantizeDepth(x >= 1 && x < 5 && y >= 1 && y < 3 ? .25f : .875f, format));
    }
    depthOnly->resolve();
    EXPECT_EQ(depthOnly->rgba[0], 255);
    EXPECT_EQ(depthOnly->rgba[1], 0);
    EXPECT_EQ(depthOnly->rgba[2 * 4], 29) << "Color row padding survives";
}

TEST(VulkanDepthTest, PolygonBiasUsesSlopeUnitsClampAndDynamicPipelineState)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    std::array<RasterDraw::Vertex, 3> vertices{{{0, 0, {}}, {4, 0, {}}, {0, 4, {}}}};
    vertices[0].z = .25f;
    vertices[1].z = .5f;
    vertices[2].z = .75f;
    for (auto &v: vertices)
        v.inputs[0] = {.125f, 0, 0, 1};
    std::vector<std::uint8_t> color(64, 19);
    std::vector<float> depths(16, 1);
    RasterDraw draw{shader, vertices, {}, {}, color, 4, 4, 4, {0, 0, 4, 4}, 15, {}};
    draw.depth = {true, true, 7, DepthFormat::Float32, depths, 4, 4, 4, {}};
    VulkanRasterBackend backend;
    const std::array<RasterDraw::DepthBias, 7> cases{{{false, 0, 0, 0},
                                                      {true, 4, 0, 0},
                                                      {true, -4, 0, 0},
                                                      {true, 0, .1f, 0},
                                                      {true, 0, .1f, .005f},
                                                      {true, 0, -.1f, -.005f},
                                                      {true, 0, 0, 0}}};
    for (const auto bias: cases) {
        draw.depthBias = bias;
        const auto result = backend.render(draw);
        ASSERT_TRUE(result) << backend.lastError();
        const float offset =
                bias.enabled ? polygonDepthBias({0, 4, 0}, {0, 0, 4}, {.25f, .5f, .75f}, DepthFormat::Float32, bias.constant, bias.slope, bias.clamp)
                             : 0.f;
        EXPECT_NEAR(result->depth[0], .34375f + offset, 5.e-8f);
    }
    EXPECT_EQ(backend.pipelineCreations(), 2u) << "Bias values are dynamic; enabling bias separates pipelines";
    shader = lowerFragmentShader(*decodeProgram({61, (CF_EXP_DONE << 23) | (1u << 21) | 0xFF8u}));
    draw.depthBias = {true, 4, .1f, 0};
    const auto exported = backend.render(draw);
    ASSERT_TRUE(exported) << backend.lastError();
    EXPECT_FLOAT_EQ(exported->depth[0], .125f) << "Explicit shader Z replaces biased geometric depth";
    shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    for (const auto format: {DepthFormat::UNorm16, DepthFormat::UNorm24}) {
        draw.depth.format = format;
        for (const auto bias: cases) {
            draw.depthBias = bias;
            const auto result = backend.render(draw);
            ASSERT_TRUE(result) << backend.lastError();
            const float offset =
                    bias.enabled ? polygonDepthBias({0, 4, 0}, {0, 0, 4}, {.25f, .5f, .75f}, format, bias.constant, bias.slope, bias.clamp) : 0.f;
            EXPECT_FLOAT_EQ(result->depth[0], quantizeDepth(.34375f + offset, format));
        }
    }
    EXPECT_FLOAT_EQ(polygonDepthBias({0, 4, 0}, {0, 0, 4}, {.25f, .25f, .25f}, DepthFormat::UNorm16, 65535, 0, 0), 1.f);
    EXPECT_FLOAT_EQ(polygonDepthBias({0, 4, 0}, {0, 0, 4}, {.25f, .25f, .25f}, DepthFormat::UNorm24, 16777215, 0, 0), 1.f);
}

TEST(VulkanDepthTest, SeparateVertexProgramsKeepSlopedPrepassEqualCoverage)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN") || !SpirvCompiler::available())
        GTEST_SKIP();
    const auto shader = lowerFragmentShader(*decodeProgram({0, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u}));
    const auto position = 60u | (1u << 13) | (1u << 15);
    const auto prepass =
            std::make_shared<const VertexShader>(lowerVertexShader(*decodeProgram({position, (CF_EXP_DONE << 23) | (1u << 21) | 0x688u})));
    const auto material = std::make_shared<const VertexShader>(lowerVertexShader(
            *decodeProgram({position, (CF_EXP << 23) | 0x688u, (2u << 13) | (2u << 15), (CF_EXP_DONE << 23) | (1u << 21) | 0x688u})));
    ASSERT_TRUE(*prepass) << prepass->error;
    ASSERT_TRUE(*material) << material->error;
    std::array<RasterDraw::Vertex, 3> vertices{};
    vertices[0].inputs[0] = {-.9f, .9f, -.6f, 1};
    vertices[1].inputs[0] = {1.2f, 1.2f, .5f, 1.5f};
    vertices[2].inputs[0] = {-1.8f, -1.8f, .8f, 2};
    for (auto &v: vertices)
        v.inputs[1] = {1, .5f, .25f, 1};
    std::vector<std::uint8_t> color(8 * 8 * 4, 17);
    std::vector<float> depth(8 * 8, 1);
    RasterDraw draw{shader, vertices, {}, {}, color, 8, 8, 8, {0, 0, 8, 8}, 0, {}};
    draw.vertexStage.shader = prepass;
    draw.vertexStage.viewport = {0, 0, 8, 8, 0, 1};
    draw.vertexStage.inputParams[0] = 0;
    draw.vertexStage.inputCount = 1;
    draw.depth = {true, true, 7, DepthFormat::Float32, depth, 8, 8, 8, {}};
    VulkanRasterBackend backend(true, true);
    auto first = backend.renderDeferred(draw);
    ASSERT_TRUE(first) << backend.lastError();
    draw.depth.rendered = first->depthReadback;
    draw.depth.write = false;
    draw.depth.compare = 2;
    draw.channelMask = 15;
    draw.vertexStage.shader = material;
    auto equal = backend.render(draw);
    ASSERT_TRUE(equal) << backend.lastError();
    draw.depth.compare = 7;
    auto always = backend.render(draw);
    ASSERT_TRUE(always) << backend.lastError();
    EXPECT_EQ(equal->rgba, always->rgba) << "EQUAL must preserve all pixels covered by the matching geometric prepass";
    EXPECT_NE(equal->rgba, color);
}

TEST(LatteLoweringTest, AddressRelativeGprReadsPreserveSimultaneousValuesAndBounds)
{
    // AR.x comes from c0.x in one ALU group; the following group reads
    // GPR[4+AR.x]. Both inputs and zero-initialized higher registers are visible.
    const auto program = decodeProgram({2, (8u << 26) | (1u << 18), 3u << 15, (CF_EXP_DONE << 23) | (1u << 21), 0x80000000u | 256u,
                                        OP2_MOVA_FLOOR << 7, 0x80000000u | 4u | (1u << 9), (OP2_MOV << 7) | 16u | (3u << 21)});
    const auto fragment = lowerFragmentShader(*program);
    ASSERT_TRUE(fragment) << fragment.error;
    EXPECT_NE(fragment.source.find("vec4 r[128]"), std::string::npos);
    FragmentShader vertex;
    vertex.source = probeVertexSource;
    std::vector<unsigned> vs, ps;
    ASSERT_NO_FATAL_FAILURE(validateSpirv(vertex, &vs, "vert"));
    ASSERT_NO_FATAL_FAILURE(validateSpirv(fragment, &ps));
    std::array<std::array<float, 4>, 4> inputs{};
    for (unsigned i = 0; i < inputs.size(); ++i)
        inputs[i][0] = float(i + 1) / 16.f;
    for (float index: {-5.f, -4.f, -2.1f, 0.f, 1.75f, 11.f, 12.f, 123.f, 124.f}) {
        SCOPED_TRACE(index);
        std::array<float, 1024> constants{};
        constants[0] = index;
        const int selected = 4 + int(std::floor(index));
        const auto result = Core::Gfx::LatteVsInterp::runPixel(*program, inputs, constants.data(), 256, {});
        ASSERT_TRUE(result.colorValid);
        if (selected < 0 || selected >= 128)
            EXPECT_EQ(std::bit_cast<unsigned>(result.color[0]), 0x7FFFFFFFu);
        else
            EXPECT_EQ(result.color[0], selected < 4 ? inputs[unsigned(selected)][0] : 0.f);
        if (std::getenv("WEMU_TEST_VULKAN")) {
            VulkanFragmentProbe probe;
            probe.initialize(vs, ps, constants);
            for (const auto &pixel: probe.draw(inputs))
                if (std::isnan(result.color[0]))
                    EXPECT_TRUE(std::isnan(pixel[0]));
                else
                    EXPECT_EQ(pixel[0], result.color[0]);
        }
    }
}
