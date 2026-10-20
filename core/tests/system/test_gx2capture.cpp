#include <bit>
#include <cstring>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "gfx/Gx2CommandStream.hpp"
#include "gfx/Gx2Replayer.hpp"
#include "gfx/SpirvCompiler.hpp"
#include "gfx/SurfaceLayout.hpp"
#include "gfx/TextureFormat.hpp"
#include "gfx/VulkanRasterBackend.hpp"
#include "hle/Gx2.hpp"
#include "hle/Libc.hpp"

// GX2 command capture: render calls are recorded into the command stream for later Vulkan replay.
class Gx2CaptureTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterGx2Functions();
            Core::Gfx::gx2Stream().clear();
        }

        static void call(const char *name) { Core::syscallHandler.get(name)(*cpu); }

        void surface(std::uint32_t header, std::uint32_t image, std::uint32_t size, std::uint32_t width, std::uint32_t height, std::uint32_t pitch,
                     std::uint32_t tile)
        {
            for (std::uint32_t off = 0; off < 0x74; off += 4)
                cpu->m_memory.write<std::uint32_t>(header + off, 0);
            cpu->m_memory.write<std::uint32_t>(header, 1);
            cpu->m_memory.write<std::uint32_t>(header + 4, width);
            cpu->m_memory.write<std::uint32_t>(header + 8, height);
            cpu->m_memory.write<std::uint32_t>(header + 0xC, 1);
            cpu->m_memory.write<std::uint32_t>(header + 0x14, 0x1A);
            cpu->m_memory.write<std::uint32_t>(header + 0x20, size);
            cpu->m_memory.write<std::uint32_t>(header + 0x24, image);
            cpu->m_memory.write<std::uint32_t>(header + 0x30, tile);
            cpu->m_memory.write<std::uint32_t>(header + 0x3C, pitch);
        }

        void copySurface(std::uint32_t source, std::uint32_t dest)
        {
            cpu->m_gpr[3] = source;
            cpu->m_gpr[4] = cpu->m_gpr[5] = cpu->m_gpr[7] = cpu->m_gpr[8] = 0;
            cpu->m_gpr[6] = dest;
            call("GX2CopySurface");
        }
};

TEST_F(Gx2CaptureTest, SamplerInitializationAndCapturePreserveIndependentFields)
{
    constexpr std::uint32_t address = 0x28001000;
    cpu->m_gpr[3] = address;
    cpu->m_gpr[4] = 2;
    cpu->m_gpr[5] = 1;
    call("GX2InitSampler");
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(address), 2u | (2u << 3) | (2u << 6) | (1u << 9) | (1u << 12));
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(address + 4), 1023u << 10);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(address + 8), 1u << 31);
    cpu->m_gpr[4] = 3;
    call("GX2InitSamplerBorderType");
    cpu->m_gpr[4] = 6;
    cpu->m_gpr[5] = 1;
    cpu->m_gpr[6] = 2;
    call("GX2InitSamplerClamping");
    const auto expected = 6u | (1u << 3) | (2u << 6) | (1u << 9) | (1u << 12) | (3u << 22);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(address), expected);
    cpu->m_fpr[1] = 1.5;
    cpu->m_fpr[2] = 16;
    cpu->m_fpr[3] = -0.5;
    call("GX2InitSamplerLOD");
    const auto lod = 96u | (1023u << 10) | (0xFE0u << 20);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(address + 4), lod);
    cpu->m_gpr[4] = 5;
    call("GX2SetPixelSampler");
    cpu->m_gpr[3] = address;
    call("GX2SetVertexSampler");
    cpu->m_memory.write<std::uint32_t>(address, 0);
    const auto &commands = Core::Gfx::gx2Stream().commands();
    ASSERT_EQ(commands.size(), 2u);
    for (const auto &cmd: commands) {
        EXPECT_EQ(cmd.gpr[1], 5u);
        EXPECT_EQ(cmd.payload, (std::vector<std::uint32_t>{expected, lod, 1u << 31}));
    }
    EXPECT_EQ(commands[0].type, Core::Gfx::Gx2Cmd::SetPixelSampler);
    EXPECT_EQ(commands[1].type, Core::Gfx::Gx2Cmd::SetVertexSampler);
}

TEST_F(Gx2CaptureTest, RecordsRenderSequenceInOrder)
{
    using C = Core::Gfx::Gx2Cmd;

    // GX2ClearColor(colorBuffer, r, g, b, a) — buffer in r3, color in f1..f4.
    cpu->m_gpr[3] = 0x28001000;
    cpu->m_fpr[1] = 0.25;
    cpu->m_fpr[2] = 0.5;
    cpu->m_fpr[3] = 0.75;
    cpu->m_fpr[4] = 1.0;
    call("GX2ClearColor");

    call("GX2SetContextState");
    call("GX2SetPixelShader");

    // GX2DrawEx(mode, count, offset, numInstances)
    cpu->m_gpr[3] = 4; // triangles
    cpu->m_gpr[4] = 36; // count
    cpu->m_gpr[5] = 0;
    cpu->m_gpr[6] = 1;
    call("GX2DrawEx");

    call("GX2CopyColorBufferToScanBuffer");
    // (SwapScanBuffers presents + clears the stream — covered separately below.)

    const auto &s = Core::Gfx::gx2Stream();
    ASSERT_EQ(s.size(), 5u);
    EXPECT_EQ(s.drawCount(), 1u);

    const auto &cmds = s.commands();
    EXPECT_EQ(cmds[0].type, C::ClearColor);
    EXPECT_EQ(cmds[0].gpr[0], 0x28001000u); // color buffer pointer
    EXPECT_DOUBLE_EQ(cmds[0].fpr[0], 0.25); // r
    EXPECT_DOUBLE_EQ(cmds[0].fpr[3], 1.0); // a
    EXPECT_EQ(cmds[1].type, C::SetContextState);
    EXPECT_EQ(cmds[2].type, C::SetPixelShader);

    EXPECT_EQ(cmds[3].type, C::DrawEx);
    EXPECT_EQ(cmds[3].gpr[0], 4u); // mode
    EXPECT_EQ(cmds[3].gpr[1], 36u); // count

    EXPECT_EQ(cmds[4].type, C::CopyColorBufferToScanBuffer);
}

TEST_F(Gx2CaptureTest, SwapPresentsAndStartsNewFrame)
{
    call("GX2ClearColor");
    call("GX2DrawEx");
    EXPECT_EQ(Core::Gfx::gx2Stream().size(), 2u);

    // SwapScanBuffers replays + presents the frame, then clears the stream for the next frame.
    // (No renderer is attached in the unit test, so present is a guarded no-op.)
    call("GX2SwapScanBuffers");
    EXPECT_TRUE(Core::Gfx::gx2Stream().empty());
}

TEST_F(Gx2CaptureTest, NoopsAreNotCaptured)
{
    // A non-render GX2 call (state-register init) must not land in the stream.
    call("GX2SetDefaultState");
    cpu->m_gpr[3] = 0x28001000;
    call("GX2InitSampler");
    EXPECT_TRUE(Core::Gfx::gx2Stream().empty());
}

class Gx2TimingTest : public Gx2CaptureTest {
    protected:
        static constexpr std::uint64_t PERIOD = 62156250ull / 60;
        void SetUp() override
        {
            Gx2CaptureTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_hle_redirected = false;
            cpu->m_interruptsDisabled = false;
            cpu->m_scheduler.bootstrap(*cpu);
        }

        Core::ThreadContext *worker()
        {
            auto *thread = cpu->m_scheduler.create(*cpu, 0x2000, 0x02000200, 77, 0, 0xC0000000, 17);
            cpu->m_scheduler.resume(thread->osThreadPtr);
            return thread;
        }
};

TEST_F(Gx2TimingTest, VsyncSleepsUntilBoundaryAndResumesAfterCall)
{
    auto &sch = cpu->m_scheduler;
    const auto *main = sch.current();
    const auto *other = worker();
    sch.advanceTicks(37000);
    cpu->m_pc = 0x1230;
    cpu->m_lr = 0x1234;
    call("GX2WaitForVsync");
    EXPECT_EQ(sch.currentHandle(), other->osThreadPtr);
    EXPECT_EQ(cpu->m_gpr[3], 77u);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Sleeping);
    EXPECT_EQ(main->wakeTick, PERIOD);
    EXPECT_EQ(main->pc, 0x1234u);
    sch.advanceTicks(PERIOD - 37001);
    EXPECT_FALSE(sch.rescheduleAfterHle(*cpu));
    sch.advanceTicks(1);
    ASSERT_TRUE(sch.rescheduleAfterHle(*cpu));
    EXPECT_EQ(sch.currentHandle(), main->osThreadPtr);
    EXPECT_EQ(cpu->m_pc, 0x1234u);
}

TEST_F(Gx2TimingTest, VsyncWaitersShareTheSameDisplayTick)
{
    auto &sch = cpu->m_scheduler;
    const auto *main = sch.current();
    const auto *other = worker();
    call("GX2WaitForVsync");
    sch.advanceTicks(50000);
    call("GX2WaitForVsync");
    // Both threads slept; the idle scheduler advances to their common deadline.
    EXPECT_EQ(sch.now(), PERIOD);
    EXPECT_EQ(main->wakeTick, other->wakeTick);
    EXPECT_EQ(main->wakeTick, PERIOD);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Running);
    EXPECT_EQ(other->state, Core::ThreadContext::State::Ready);
}

TEST_F(Gx2TimingTest, SoleVsyncWaiterAdvancesToNextBoundary)
{
    auto &sch = cpu->m_scheduler;
    sch.advanceTicks(PERIOD);
    const auto handle = sch.currentHandle();
    call("GX2WaitForVsync");
    EXPECT_EQ(sch.now(), 2 * PERIOD);
    EXPECT_EQ(sch.currentHandle(), handle);
    EXPECT_FALSE(cpu->m_hle_redirected);
}

TEST_F(Gx2TimingTest, CompletedFlipReturnsWithoutScheduling)
{
    auto &sch = cpu->m_scheduler;
    const auto handle = sch.currentHandle();
    const auto *other = worker();
    sch.advanceTicks(12345);
    call("GX2WaitForFlip");
    EXPECT_EQ(sch.currentHandle(), handle);
    EXPECT_EQ(other->state, Core::ThreadContext::State::Ready);
    EXPECT_EQ(sch.now(), 12345u);
    EXPECT_FALSE(cpu->m_hle_redirected);
}

TEST_F(Gx2TimingTest, SwapStatusReportsFlipAndIndependentVsyncTimes)
{
    auto &sch = cpu->m_scheduler;
    sch.advanceTicks(PERIOD + 12345);
    call("GX2SwapScanBuffers");
    sch.advanceTicks(PERIOD);
    constexpr std::uint32_t out = 0x28001000;
    cpu->m_gpr[3] = out;
    cpu->m_gpr[4] = out + 4;
    cpu->m_gpr[5] = out + 8;
    cpu->m_gpr[6] = out + 16;
    call("GX2GetSwapStatus");
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(out), cpu->m_memory.read<std::uint32_t>(out + 4));
    EXPECT_EQ(cpu->m_memory.read<std::uint64_t>(out + 8), PERIOD + 12345);
    EXPECT_EQ(cpu->m_memory.read<std::uint64_t>(out + 16), 2 * PERIOD);
}

TEST_F(Gx2CaptureTest, TextureCopyHonorsPitchesAndPreservesPadding)
{
    constexpr std::uint32_t SRC = 0x28001000, DST = 0x28002000;
    surface(SRC, 0x28010000, 64, 3, 2, 4, 16);
    surface(DST, 0x28020000, 64, 3, 2, 8, 1);
    for (unsigned i = 0; i < 16; i++)
        cpu->m_memory.write<std::uint32_t>(0x28020000 + i * 4, 0xDEADBEEF);
    for (unsigned y = 0; y < 2; y++)
        for (unsigned x = 0; x < 3; x++)
            cpu->m_memory.write<std::uint32_t>(0x28010000 + (y * 4 + x) * 4, 0x12340000 + y * 3 + x);
    copySurface(SRC, DST);
    for (unsigned y = 0; y < 2; y++) {
        for (unsigned x = 0; x < 3; x++)
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28020000 + (y * 8 + x) * 4), 0x12340000 + y * 3 + x);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28020000 + (y * 8 + 3) * 4), 0xDEADBEEF);
    }
}

TEST_F(Gx2CaptureTest, SpecialLinearUploadSizeHasNoRowOrAllocationPadding)
{
    surface(0x28001000, 0, 0, 90, 106, 0, 16);
    cpu->m_gpr[3] = 0x28001000;
    call("GX2CalcSurfaceSizeAndAlignment");
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x2800103C), 90u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28001020), 90u * 106u * 4u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28001038), 1u);
}

TEST_F(Gx2CaptureTest, BaseSurfaceAllocationResolvesDefaultAndPadsTiles)
{
    constexpr unsigned S = 0x28001000;
    struct Case {
            unsigned width, height, depth, format, tile, expectedTile, pitch, size, align;
    };
    const Case cases[] = {
            {4, 4, 1, 0x1A, 0, 2, 8, 256, 256},
            {33, 17, 1, 0x1A, 0, 4, 64, 8192, 2048},
            {8, 32, 1, 0x1A, 0, 4, 32, 4096, 2048},
            {32, 8, 1, 0x1A, 0, 4, 32, 2048, 2048},
            {1024, 1024, 3, 0x34, 0, 4, 256, 0x180000, 4096},
            {26, 26, 1, 0x34, 2, 2, 8, 512, 256},
            {3, 2, 1, 1, 1, 1, 256, 512, 256},
            {3, 2, 1, 0x1A, 16, 16, 3, 24, 1},
    };
    for (const auto &c: cases) {
        SCOPED_TRACE(c.tile);
        surface(S, 0, 0, c.width, c.height, 0, c.tile);
        cpu->m_memory.write<std::uint32_t>(S, c.depth == 1 ? 1 : 5);
        cpu->m_memory.write<std::uint32_t>(S + 0xC, c.depth);
        cpu->m_memory.write<std::uint32_t>(S + 0x14, c.format);
        cpu->m_gpr[3] = S;
        call("GX2CalcSurfaceSizeAndAlignment");
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x30), c.expectedTile);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x3C), c.pitch);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x20), c.size);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x38), c.align);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x10), 1u);
        cpu->m_memory.write<std::uint32_t>(S + 0x10, 4);
        cpu->m_gpr[3] = S;
        call("GX2CalcSurfaceSizeAndAlignment");
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x30), c.expectedTile);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x20), c.size);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(S + 0x3C), c.pitch);
    }
}

TEST_F(Gx2CaptureTest, CubeMipmapAllocationIncludesPaddedFacesAndClearsUnusedOffsets)
{
    constexpr unsigned S = 0x28001000;
    surface(S, 0, 0, 32, 32, 0, 4);
    cpu->m_memory.write<unsigned>(S, 3);
    cpu->m_memory.write<unsigned>(S + 0xC, 6);
    cpu->m_memory.write<unsigned>(S + 0x10, 2);
    cpu->m_memory.write<unsigned>(S + 0x14, 0x816);
    cpu->m_memory.write<unsigned>(S + 0x34, 0xA5000300);
    cpu->m_gpr[3] = S;
    call("GX2CalcSurfaceSizeAndAlignment");
    EXPECT_EQ(cpu->m_memory.read<unsigned>(S + 0x20), 32u * 32u * 6u * 4u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(S + 0x28), 16u * 16u * 8u * 4u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(S + 0x40), 32u * 32u * 6u * 4u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(S + 0x34), 0xA5010300u);
    for (unsigned index = 1; index < 13; ++index)
        EXPECT_EQ(cpu->m_memory.read<unsigned>(S + 0x40 + index * 4), 0u);
    cpu->m_memory.write<unsigned>(S + 0x10, 1);
    cpu->m_gpr[3] = S;
    call("GX2CalcSurfaceSizeAndAlignment");
    EXPECT_EQ(cpu->m_memory.read<unsigned>(S + 0x28), 0u);
    for (unsigned index = 0; index < 13; ++index)
        EXPECT_EQ(cpu->m_memory.read<unsigned>(S + 0x40 + index * 4), 0u);
}

TEST_F(Gx2CaptureTest, DefaultAllocatedTextureCopyRoundTrip)
{
    constexpr unsigned SRC = 0x28001000, TILED = 0x28002000, DST = 0x28003000;
    surface(SRC, 0x28010000, 37 * 19 * 4, 37, 19, 37, 16);
    surface(TILED, 0x28020000, 0, 37, 19, 0, 0);
    surface(DST, 0x28030000, 37 * 19 * 4, 37, 19, 37, 16);
    cpu->m_gpr[3] = TILED;
    call("GX2CalcSurfaceSizeAndAlignment");
    for (unsigned i = 0; i < 37 * 19; ++i)
        cpu->m_memory.write<std::uint32_t>(0x28010000 + i * 4, 0xABCD0000 + i);
    copySurface(SRC, TILED);
    copySurface(TILED, DST);
    for (unsigned i = 0; i < 37 * 19; ++i)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28030000 + i * 4), 0xABCD0000 + i);
}

TEST(SurfaceLayoutTest, ThinTileAddressesFitAllocatedPlanes)
{
    using namespace Core::Gfx;
    for (unsigned bytes: {1, 2, 4, 8, 16})
        for (unsigned tile: {0, 1, 2, 4, 16}) {
            const auto layout = baseSurfaceLayout(37, 19, 3, bytes, tile, 37, 19);
            ASSERT_TRUE(layout);
            for (unsigned y = 0; y < 19; ++y)
                for (unsigned x = 0; x < 37; ++x)
                    EXPECT_LE(tiledElementOffset(x, y, layout->pitch, bytes * 8, layout->tileMode, 0x700) + bytes, layout->imageSize / 3);
        }
    EXPECT_FALSE(baseSurfaceLayout(UINT32_MAX, UINT32_MAX, UINT32_MAX, 16, 4, UINT32_MAX, UINT32_MAX));
    EXPECT_FALSE(baseSurfaceLayout(8, 8, 1, 4, 7, 8, 8));
}

TEST_F(Gx2CaptureTest, TextureCopyMacroTileRoundTrip)
{
    constexpr std::uint32_t SRC = 0x28001000, TILED = 0x28002000, DST = 0x28003000;
    surface(SRC, 0x28010000, 2048, 32, 16, 32, 16);
    surface(TILED, 0x28020000, 2048, 32, 16, 32, 4);
    surface(DST, 0x28030000, 2048, 32, 16, 32, 16);
    for (unsigned i = 0; i < 512; i++)
        cpu->m_memory.write<std::uint32_t>(0x28010000 + i * 4, 0xABCD0000 + i);
    copySurface(SRC, TILED);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28020000 + 0x300), 0xABCD0008u); // x=8, y=0: bank 1, pipe 1
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28020000 + 0x100), 0xABCD0100u); // x=0, y=8: bank 0, pipe 1
    copySurface(TILED, DST);
    for (unsigned i = 0; i < 512; i++)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28030000 + i * 4), 0xABCD0000 + i);
}

TEST_F(Gx2CaptureTest, TextureCopyRejectsShortAllocationBeforeWriting)
{
    constexpr std::uint32_t SRC = 0x28001000, DST = 0x28002000;
    surface(SRC, 0x28010000, 8, 3, 1, 3, 16);
    surface(DST, 0x28020000, 12, 3, 1, 3, 16);
    cpu->m_memory.write<std::uint32_t>(0x28020000, 0xDEADBEEF);
    copySurface(SRC, DST);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(0x28020000), 0xDEADBEEF);
}

TEST_F(Gx2CaptureTest, FetchDeclarationSurvivesCallerReuse)
{
    constexpr unsigned SHADER = 0x28001000, PROGRAM = 0x28002000, ATTRS = 0x28003000;
    const std::array<std::uint32_t, 8> attr{2, 1, 12, 0x80D, 0, 0, 0x00010405, 3};
    for (unsigned i = 0; i < attr.size(); i++)
        cpu->m_memory.write<std::uint32_t>(ATTRS + i * 4, attr[i]);
    cpu->m_gpr[3] = SHADER;
    cpu->m_gpr[4] = PROGRAM;
    cpu->m_gpr[5] = 1;
    cpu->m_gpr[6] = ATTRS;
    cpu->m_gpr[7] = cpu->m_gpr[8] = 0;
    call("GX2InitFetchShaderEx");
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(SHADER + 0xC), PROGRAM);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(SHADER + 0x10), 1u);
    cpu->m_memory.write<std::uint32_t>(ATTRS, 99);
    cpu->m_gpr[3] = SHADER;
    call("GX2SetFetchShader");
    const auto &commands = Core::Gfx::gx2Stream().commands();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].payload, (std::vector<std::uint32_t>(attr.begin(), attr.end())));
}

TEST_F(Gx2CaptureTest, VertexTextureBindingSnapshotsSurface)
{
    surface(0x28001000, 0x28020000, 1024, 16, 16, 16, 1);
    cpu->m_memory.write<std::uint32_t>(0x28001084, 0x05050500);
    cpu->m_gpr[3] = 0x28001000;
    cpu->m_gpr[4] = 3;
    call("GX2SetVertexTexture");
    cpu->m_memory.write<std::uint32_t>(0x28001024, 0);
    cpu->m_memory.write<std::uint32_t>(0x28001084, 0);
    const auto &commands = Core::Gfx::gx2Stream().commands();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].type, Core::Gfx::Gx2Cmd::SetVertexTexture);
    EXPECT_EQ(commands[0].gpr[1], 3u);
    ASSERT_EQ(commands[0].payload.size(), 0x9Cu / 4);
    EXPECT_EQ(commands[0].payload[9], 0x28020000u);
    EXPECT_EQ(commands[0].payload[0x84 / 4], 0x05050500u);
}

TEST_F(Gx2CaptureTest, DeclaredAttributesAndPixelSemanticRenderWithEveryIndexEndian)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
    constexpr unsigned VERTS = 0x28010000, INDICES = 0x28011000;
    for (unsigned i = 0; i < 0x200; i += 4) {
        cpu->m_memory.write<std::uint32_t>(VS + i, 0);
        cpu->m_memory.write<std::uint32_t>(PS + i, 0);
    }
    cpu->m_memory.write<std::uint32_t>(VS + 0x40, 2);
    cpu->m_memory.write<std::uint32_t>(VS + 0x44, 5);
    cpu->m_memory.write<std::uint32_t>(VS + 0x48, 9);
    cpu->m_memory.write<std::uint32_t>(VS + 0xC, 1);
    cpu->m_memory.write<std::uint32_t>(VS + 0x10, 0xFFFF07FF); // PARAM1 has semantic 7
    cpu->m_memory.write<std::uint32_t>(PS + 0x10, 1);
    cpu->m_memory.write<std::uint32_t>(PS + 0x14, 7); // R0 consumes semantic 7
    const std::array<std::uint32_t, 4> vs{60u | (1u << 13) | (1u << 15), (0x28u << 23) | 0x688u, 1u | (2u << 13) | (2u << 15),
                                          (0x28u << 23) | (1u << 21) | 0x688u};
    const std::array<std::uint32_t, 8> ps{2,
                                          1u << 23,
                                          0,
                                          (0x28u << 23) | (1u << 21) | 0x688u,
                                          0x10u | (3u << 8),
                                          0x30000000u | (1u << 12) | (2u << 15) | (3u << 18),
                                          (5u << 15) | (1u << 23),
                                          0};
    auto shader = [&](unsigned header, unsigned offset, unsigned ptr, const auto &words) {
        cpu->m_memory.write<std::uint32_t>(header + offset, words.size() * 4);
        cpu->m_memory.write<std::uint32_t>(header + offset + 4, ptr);
        for (unsigned i = 0; i < words.size(); i++)
            cpu->m_memory.write<std::uint32_t>(ptr + i * 4, std::byteswap(words[i]));
    };
    shader(VS, 0xD0, VSP, vs);
    shader(PS, 0xA4, PSP, ps);
    const std::array<std::uint32_t, 16> attrs{5, 1, 4, 0x811, 0, 0, 0x00010205, 3, 9, 2, 24, 0x80D, 0, 0, 0x00010405, 3};
    for (unsigned i = 0; i < attrs.size(); i++)
        cpu->m_memory.write<std::uint32_t>(0x28003000 + i * 4, attrs[i]);
    cpu->m_gpr[3] = 0x28001000;
    cpu->m_gpr[4] = 0x28002000;
    cpu->m_gpr[5] = 2;
    cpu->m_gpr[6] = 0x28003000;
    cpu->m_gpr[7] = cpu->m_gpr[8] = 0;
    call("GX2InitFetchShaderEx");
    const float vertices[4][8] = {
            {99, -1, 1, 0, 99, 99, 0, 0}, {99, 1, 1, 0, 99, 99, 1, 0}, {99, 1, -1, 0, 99, 99, 1, 1}, {99, -1, -1, 0, 99, 99, 0, 1}};
    for (unsigned v = 0; v < 4; v++)
        for (unsigned c = 0; c < 8; c++)
            cpu->m_memory.write<std::uint32_t>(VERTS + v * 32 + c * 4, std::bit_cast<std::uint32_t>(vertices[v][c]));
    surface(0x28009000, 0x28040000, 256, 8, 8, 8, 1);
    surface(0x2800A000, 0x28050000, 16, 2, 2, 2, 1);
    cpu->m_memory.write<std::uint32_t>(0x28009080, 1);
    cpu->m_memory.write<std::uint32_t>(0x2800A080, 1);
    const std::array<std::uint32_t, 4> colors{0xFF0000FF, 0x00FF00FF, 0x0000FFFF, 0xFFFF00FF};
    for (unsigned i = 0; i < colors.size(); i++)
        cpu->m_memory.write<std::uint32_t>(0x28050000 + i * 4, colors[i]);
    struct TextureCase {
            unsigned map;
            bool image;
            std::array<std::array<std::uint8_t, 4>, 4> expected;
            unsigned format{0x1A};
    };
    const TextureCase textureCases[] = {
            {0x00010203, true, {{{255, 255, 255, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}, {255, 255, 255, 255}}}, 1},
            {0x00010203, true, {{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 0, 255}, {255, 255, 0, 255}}}, 7},
            {0x01000504, true, {{{0, 255, 255, 0}, {255, 0, 255, 0}, {0, 0, 255, 0}, {255, 255, 255, 0}}}, 7},
            {0x00010203, true, {{{255, 0, 0, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}, {255, 0, 0, 255}}}, 0x80E},
            {0x00010203, true, {{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}}}},
            {0x02010003, true, {{{0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}, {0, 255, 255, 255}}}},
            {0x05050500, true, {{{255, 255, 255, 255}, {255, 255, 255, 0}, {255, 255, 255, 0}, {255, 255, 255, 255}}}},
            {0x04050405, false, {{{0, 255, 0, 255}, {0, 255, 0, 255}, {0, 255, 0, 255}, {0, 255, 0, 255}}}},
            {0x00000005, false, {{{0, 0, 0, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}}}},
    };
    for (const auto &textureCase: textureCases)
        for (const unsigned compressedTile: {0u, 1u, 2u, 4u})
            for (const unsigned samplerMode: {0u, 2u, 6u})
                for (const unsigned type: {0u, 1u, 4u, 9u}) {
                    if (textureCase.format == 0x80E && compressedTile)
                        continue;
                    SCOPED_TRACE(textureCase.map);
                    SCOPED_TRACE(textureCase.format);
                    SCOPED_TRACE(compressedTile);
                    SCOPED_TRACE(samplerMode);
                    SCOPED_TRACE(type);
                    if (textureCase.format == 1 || textureCase.format == 7) {
                        const unsigned components = textureCase.format == 1 ? 1 : 2;
                        // Include padded rows and both micro/macro-tiled source layouts.
                        const unsigned tile = compressedTile;
                        surface(0x2800A000, 0x28050000, 64 * 32 * components, 2, 2, 64, tile);
                        cpu->m_memory.write<std::uint32_t>(0x2800A014, textureCase.format);
                        for (unsigned y = 0; y < 2; ++y)
                            for (unsigned x = 0; x < 2; ++x) {
                                const auto offset = tiledElementOffset(x, y, 64, components * 8, tile, 0);
                                const auto color = colors[y * 2 + x];
                                for (unsigned c = 0; c < components; ++c)
                                    cpu->m_memory.write<std::uint8_t>(0x28050000 + offset + c, color >> (24 - c * 8));
                            }
                    } else if (compressedTile) {
                        // GX2 pitch is in compression blocks, not pixels. Cross both
                        // microtile and macro-tile rows with four solid-color quadrants.
                        surface(0x2800A000, 0x28050000, 64 * 32 * 8, 256, 128, 64, compressedTile);
                        cpu->m_memory.write<std::uint32_t>(0x2800A014, 0x31);
                        const std::uint16_t endpoints[4]{0xF800, 0x07E0, 0x001F, 0xFFE0};
                        for (unsigned y = 0; y < 32; y++)
                            for (unsigned x = 0; x < 64; x++) {
                                const auto address = 0x28050000 + tiledElementOffset(x, y, 64, 64, compressedTile, 0);
                                const auto color = endpoints[(y >= 16 ? 2 : 0) + (x >= 32 ? 1 : 0)];
                                for (unsigned b = 0; b < 8; b++)
                                    cpu->m_memory.write<std::uint8_t>(address + b, b < 4 ? color >> ((b & 1) * 8) : 0);
                            }
                    } else {
                        surface(0x2800A000, 0x28050000, 16, 2, 2, 2, 1);
                        cpu->m_memory.write<std::uint32_t>(0x2800A014, textureCase.format);
                        for (unsigned i = 0; i < colors.size(); i++)
                            cpu->m_memory.write<std::uint32_t>(0x28050000 + i * 4,
                                                               textureCase.format == 0x80E
                                                                       ? std::byteswap(std::bit_cast<std::uint32_t>(i == 0 || i == 3 ? 1.0f : 0.0f))
                                                                       : colors[i]);
                    }
                    for (unsigned v = 0; v < 4; v++)
                        cpu->m_memory.write<std::uint32_t>(VERTS + v * 32 + 24, std::bit_cast<std::uint32_t>(samplerMode ? -0.25f : vertices[v][6]));
                    cpu->m_memory.write<std::uint32_t>(0x2800A084, textureCase.map);
                    cpu->m_memory.write<std::uint32_t>(0x2800A024, textureCase.image ? 0x28050000 : 0);
                    const unsigned bytes = type == 1 || type == 9 ? 4 : 2;
                    const unsigned order[6]{0, 1, 2, 0, 2, 3};
                    for (unsigned i = 0; i < 6; i++)
                        for (unsigned b = 0; b < bytes; b++)
                            cpu->m_memory.write<std::uint8_t>(INDICES + i * bytes + b, order[i] >> ((type < 4 ? b : bytes - 1 - b) * 8));
                    gx2Stream().clear();
                    auto record = [&](const char *name, std::initializer_list<std::uint32_t> args) {
                        unsigned r = 3;
                        for (const auto arg: args)
                            cpu->m_gpr[r++] = arg;
                        call(name);
                    };
                    record("GX2SetColorBuffer", {0x28009000, 0});
                    record("GX2SetColorControl", {0xCC, 0, 0, 1});
                    cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
                    cpu->m_fpr[3] = cpu->m_fpr[4] = 8;
                    record("GX2SetViewport", {});
                    record("GX2SetFetchShader", {0x28001000});
                    record("GX2SetVertexShader", {VS});
                    record("GX2SetPixelShader", {PS});
                    record("GX2SetPixelTexture", {0x2800A000, 3});
                    record("GX2SetVertexTexture", {0x2800A000, 0}); // unrelated binding must not block native collection
                    record("GX2InitSampler", {0x2800B000, samplerMode, 0});
                    record("GX2InitSamplerBorderType", {0x2800B000, 3});
                    cpu->m_fpr[1] = 1;
                    cpu->m_fpr[2] = 0;
                    cpu->m_fpr[3] = 1;
                    cpu->m_fpr[4] = 1;
                    record("GX2SetPixelSamplerBorderColor", {5});
                    record("GX2SetPixelSampler", {0x2800B000, 5});
                    cpu->m_memory.write<std::uint32_t>(0x2800B000, 0);
                    cpu->m_memory.write<std::uint32_t>(0x2800A084, 0); // caller may reuse the descriptor before replay
                    record("GX2SetAttribBuffer", {1, 128, 32, VERTS});
                    record("GX2SetAttribBuffer", {2, 128, 32, VERTS});
                    record("GX2DrawIndexedEx", {4, 6, type, INDICES, 0, 1});
                    record("GX2CopyColorBufferToScanBuffer", {0x28009000, 1});
                    gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
                    Gx2Replayer replayer;
                    replayer.setRasterWorkerCount(1);
                    replayer.replay(gx2Stream(), &cpu->m_memory);
                    Gx2Replayer parallel;
                    parallel.setRasterWorkerCount(4, 0);
                    parallel.setRasterVerificationInterval(1);
                    parallel.replay(gx2Stream(), &cpu->m_memory);
                    EXPECT_EQ(parallel.verifiedRasterTriangles(), 2u);
                    EXPECT_EQ(parallel.framebuffer(), replayer.framebuffer());
                    const auto &fb = replayer.framebuffer();
                    ASSERT_FALSE(fb.empty());
                    auto pixel = [&](unsigned x, unsigned y) {
                        const auto off = (y * Gx2Replayer::kWidth + x) * 4;
                        return std::array<std::uint8_t, 4>{fb[off], fb[off + 1], fb[off + 2], fb[off + 3]};
                    };
                    const std::array<std::uint8_t, 4> border{255, 0, 255, 255};
                    EXPECT_EQ(pixel(1, 1), samplerMode == 6 ? border : textureCase.expected[0]);
                    EXPECT_EQ(pixel(6, 1), samplerMode == 6 ? border : textureCase.expected[samplerMode == 2 ? 0 : 1]);
                    EXPECT_EQ(pixel(1, 6), samplerMode == 6 ? border : textureCase.expected[2]);
                    EXPECT_EQ(pixel(6, 6), samplerMode == 6 ? border : textureCase.expected[samplerMode == 2 ? 2 : 3]);
                    if ((textureCase.format == 1 || textureCase.format == 7 || textureCase.format == 0x80E) && !samplerMode && !type &&
                        std::getenv("WEMU_TEST_VULKAN") && SpirvCompiler::available()) {
                        auto backend = std::make_shared<VulkanRasterBackend>();
                        Gx2Replayer native;
                        native.setRasterBackend(backend);
                        native.setNativeVerificationInterval(1);
                        native.replay(gx2Stream(), &cpu->m_memory);
                        EXPECT_EQ(backend->completedDraws(), 1u) << backend->lastError();
                        EXPECT_EQ(native.comparedNativeDraws(), 1u);
                        EXPECT_EQ(native.framebuffer(), replayer.framebuffer());
                    }
                    if (textureCase.format == 1 && textureCase.map == 0x00010203 && !compressedTile && !samplerMode && !type) {
                        auto gatherWords = ps;
                        gatherWords[4] = (gatherWords[4] & ~31u) | 0x0Fu;
                        shader(PS, 0xA4, PSP, gatherWords);
                        auto gatherStream = gx2Stream();
                        for (auto &command: gatherStream.mutableCommands())
                            if (command.type == Gx2Cmd::SetPixelTexture)
                                command.payload[4] = 1;
                        Gx2Replayer reference;
                        reference.replay(gatherStream, &cpu->m_memory);
                        const auto offset = (Gx2Replayer::kWidth + 1) * 4;
                        const auto &gathered = reference.framebuffer();
                        ASSERT_GT(gathered.size(), offset + 3);
                        EXPECT_EQ((std::array<std::uint8_t, 4>{gathered[offset], gathered[offset + 1], gathered[offset + 2], gathered[offset + 3]}),
                                  (std::array<std::uint8_t, 4>{0, 255, 0, 255}));
                        if (std::getenv("WEMU_TEST_VULKAN") && SpirvCompiler::available()) {
                            auto backend = std::make_shared<VulkanRasterBackend>();
                            Gx2Replayer native;
                            native.setRasterBackend(backend);
                            native.setNativeVerificationInterval(1);
                            native.replay(gatherStream, &cpu->m_memory);
                            EXPECT_EQ(backend->completedDraws(), 1u) << backend->lastError();
                            EXPECT_EQ(native.comparedNativeDraws(), 1u);
                            EXPECT_EQ(native.framebuffer(), gathered);
                            for (unsigned mipCount: {0u, 2u}) {
                                auto invalid = gatherStream;
                                for (auto &command: invalid.mutableCommands())
                                    if (command.type == Gx2Cmd::SetPixelTexture)
                                        command.payload[4] = mipCount;
                                auto rejectedBackend = std::make_shared<VulkanRasterBackend>();
                                Gx2Replayer rejected;
                                rejected.setRasterBackend(rejectedBackend);
                                rejected.replay(invalid, &cpu->m_memory);
                                EXPECT_EQ(rejectedBackend->completedDraws(), 0u);
                            }
                        }
                        shader(PS, 0xA4, PSP, ps);
                    }
                    if (textureCase.map == 0x00010203 && !compressedTile && !samplerMode && !type) {
                        struct BackendProbe final : RasterBackend {
                                unsigned calls{};
                                bool accept{}, malformed{}, expectBlend{}, expectSnapshot{};
                                const std::uint8_t *expectedBorrowed{};
                                std::array<float, 4> firstTexel{};
                                std::optional<RasterResult> render(const RasterDraw &draw) override
                                {
                                    ++calls;
                                    // Two triangles share the quad's four corners through indices.
                                    EXPECT_EQ(draw.vertices.size(), 4u);
                                    EXPECT_EQ(draw.indices.size(), 6u);
                                    EXPECT_EQ(draw.constants.size(), 256u);
                                    EXPECT_EQ(draw.width, 8u);
                                    EXPECT_EQ(draw.height, 8u);
                                    EXPECT_EQ(draw.channelMask, 15u);
                                    EXPECT_EQ(draw.blend.enabled, expectBlend);
                                    if (expectBlend) {
                                        EXPECT_EQ(draw.blend.alphaSource, draw.blend.colorSource);
                                        EXPECT_EQ(draw.blend.alphaDestination, draw.blend.colorDestination);
                                        EXPECT_EQ(draw.blend.alphaOperation, draw.blend.colorOperation);
                                    }
                                    EXPECT_EQ(draw.textures.size(), 1u);
                                    if (!draw.textures.empty()) {
                                        EXPECT_EQ(draw.textures[0].binding.resource, 3u);
                                        EXPECT_EQ(draw.textures[0].binding.sampler, 5u);
                                        firstTexel = draw.textures[0].texel(0, 0);
                                        if (calls == 1 && expectedBorrowed) {
                                            EXPECT_EQ(draw.textures[0].unorm8.data(), expectedBorrowed);
                                            EXPECT_EQ(draw.textures[0].unorm8Channels, 4u);
                                        }
                                        if (expectSnapshot)
                                            EXPECT_FALSE(draw.textures[0].unorm8.empty());
                                        if (!draw.textures[0].unorm8.empty()) {
                                            for (unsigned y = 0; y < draw.textures[0].height; ++y)
                                                for (unsigned x = 0; x < draw.textures[0].width; ++x) {
                                                    const auto reference = draw.textures[0].texel(x, y);
                                                    const auto &texture = draw.textures[0];
                                                    const auto channels = texture.unorm8Channels;
                                                    const auto offset = (y * texture.unorm8Pitch + x) * channels;
                                                    std::array<float, 4> raw{0, 0, 0, 1};
                                                    for (unsigned c = 0; c < channels; ++c)
                                                        raw[c] = texture.unorm8[offset + c] / 255.f;
                                                    if (channels == 1)
                                                        raw[1] = raw[2] = raw[0];
                                                    for (unsigned c = 0; c < 4; ++c) {
                                                        const auto selector = (texture.unorm8Map >> (24 - c * 8)) & 7;
                                                        EXPECT_FLOAT_EQ(selector < 4 ? raw[selector] : selector == 5 ? 1.f : 0.f, reference[c]);
                                                    }
                                                }
                                        }
                                    }
                                    if (!draw.vertices.empty()) {
                                        EXPECT_FLOAT_EQ(draw.vertices[0].inputs[0][1], 0.0f);
                                        EXPECT_FLOAT_EQ(draw.vertices[0].inputs[0][3], 1.0f);
                                    }
                                    if (!accept)
                                        return std::nullopt;
                                    RasterResult result;
                                    result.rgba.assign(draw.target.begin(), draw.target.end());
                                    for (unsigned y = 0; y < draw.height; ++y)
                                        for (unsigned x = 0; x < draw.width; ++x) {
                                            const auto off = (y * draw.pitch + x) * 4;
                                            const std::array<std::uint8_t, 4> color{9, 17, 33, 255};
                                            std::copy(color.begin(), color.end(), result.rgba.begin() + off);
                                        }
                                    if (malformed)
                                        result.rgba.pop_back();
                                    result.pixels = draw.width * draw.height;
                                    result.alphaSum = result.pixels * 255;
                                    return result;
                                }
                        };
                        auto backend = std::make_shared<BackendProbe>();
                        backend->expectSnapshot = textureCase.format == 1 || textureCase.format == 7 || textureCase.format == 0x1A;
                        if (textureCase.format == 0x1A)
                            backend->expectedBorrowed = cpu->m_memory.hostPtr(0x28050000);
                        Gx2Replayer fallback;
                        fallback.setRasterBackend(backend);
                        fallback.replay(gx2Stream(), &cpu->m_memory);
                        EXPECT_EQ(backend->calls, 1u);
                        EXPECT_EQ(backend->firstTexel,
                                  (textureCase.format == 1 ? std::array<float, 4>{1, 1, 1, 1} : std::array<float, 4>{1, 0, 0, 1}));
                        EXPECT_EQ(fallback.framebuffer(), replayer.framebuffer());
                        if (textureCase.format == 0x1A) {
                            constexpr unsigned width = 854, height = 480, image = 0x28200000, target = 0x28400000;
                            for (unsigned y = 0; y < height; ++y)
                                for (unsigned x = 0; x < width; ++x)
                                    cpu->m_memory.write<std::uint32_t>(image + (y * width + x) * 4, ((x ^ y) & 1) ? 0xFFFFFFFF : 0x000000FF);
                            auto wideStream = gx2Stream();
                            for (auto &command: wideStream.mutableCommands()) {
                                const bool texture = command.type == Gx2Cmd::SetPixelTexture;
                                if (texture || command.type == Gx2Cmd::SetColorBuffer || command.type == Gx2Cmd::CopyColorBufferToScanBuffer) {
                                    command.payload[1] = width;
                                    command.payload[2] = height;
                                    command.payload[8] = width * height * 4;
                                    command.payload[9] = texture ? image : target;
                                    command.payload[12] = 16;
                                    command.payload[15] = width;
                                } else if (command.type == Gx2Cmd::SetViewport) {
                                    command.fpr[2] = width;
                                    command.fpr[3] = height;
                                } else if (command.type == Gx2Cmd::SetPixelSampler) {
                                    command.payload[0] = 2 | (2 << 3) | (1 << 9);
                                }
                            }
                            Gx2Replayer wide;
                            wide.setRasterWorkerCount(1);
                            wide.replay(wideStream, &cpu->m_memory);
                            unsigned maxError = 0;
                            for (unsigned y = 0; y < height; ++y)
                                for (unsigned x = 0; x < width; ++x) {
                                    const int expected = ((x ^ y) & 1) ? 255 : 0;
                                    const auto offset = (y * Gx2Replayer::kWidth + x) * 4;
                                    for (unsigned c = 0; c < 3; ++c)
                                        maxError = std::max(maxError, unsigned(std::abs(int(wide.framebuffer()[offset + c]) - expected)));
                                }
                            EXPECT_LE(maxError, 1u) << "One-to-one linear sampling must not drift across texel centers";
                            Gx2Replayer wideParallel;
                            wideParallel.setRasterWorkerCount(4, 0);
                            wideParallel.replay(wideStream, &cpu->m_memory);
                            EXPECT_EQ(wideParallel.framebuffer(), wide.framebuffer());
                        }
                        std::shared_ptr<VulkanRasterBackend> nativeBackend;
                        const auto enabled = [](const char *name) {
                            const auto *value = std::getenv(name);
                            return value && std::string_view(value) == "1";
                        };
                        if (textureCase.format == 0x1A && enabled("WEMU_NATIVE_DEFER_READBACK") && enabled("WEMU_NATIVE_RESIDENT_TEXTURES") &&
                            enabled("WEMU_NATIVE_RESIDENT_TARGETS")) {
                            struct DeferredProbe final : RasterBackend {
                                    unsigned calls{}, reads{}, clipped{};
                                    bool rejectSecond{}, separateTarget{};
                                    bool supportsRenderedTextures() const override { return true; }
                                    bool supportsRenderedTargets() const override { return true; }
                                    std::optional<RasterResult> render(const RasterDraw &) override { return std::nullopt; }
                                    std::optional<RasterResult> renderDeferred(const RasterDraw &draw) override
                                    {
                                        if (draw.scissor[0] >= int(draw.width) || draw.scissor[1] >= int(draw.height)) {
                                            ++clipped;
                                            return std::nullopt;
                                        }
                                        ++calls;
                                        if (calls == 2) {
                                            EXPECT_EQ(reads, 0u) << "Unused feedback binding must not materialize the target";
                                            EXPECT_EQ(bool(draw.renderedTarget), !separateTarget);
                                            if (rejectSecond)
                                                return std::nullopt;
                                        }
                                        if (separateTarget && calls == 3) {
                                            EXPECT_EQ(reads, 0u) << "Software drawing to another target must not materialize this output";
                                            EXPECT_TRUE(draw.renderedTarget);
                                        }
                                        RasterResult result;
                                        const auto bytes = draw.target.size();
                                        result.readback = std::make_shared<RasterReadback>(bytes, [this, bytes] {
                                            ++reads;
                                            return std::vector<std::uint8_t>(bytes, 71);
                                        });
                                        return result;
                                    }
                            };
                            auto chain = gx2Stream();
                            auto &commands = chain.mutableCommands();
                            const auto drawCommand = *std::find_if(commands.begin(), commands.end(),
                                                                   [](const auto &command) { return command.type == Gx2Cmd::DrawIndexedEx; });
                            record("GX2SetPixelTexture", {0x28009000, 15}); // Shader samples slot 3, not 15.
                            const auto unusedAlias = gx2Stream().commands().back();
                            gx2Stream().mutableCommands().pop_back();
                            auto present = std::find_if(commands.begin(), commands.end(),
                                                        [](const auto &command) { return command.type == Gx2Cmd::CopyColorBufferToScanBuffer; });
                            present = commands.insert(present, unusedAlias);
                            commands.insert(present + 1, drawCommand);
                            // A degenerate draw between native draws must not force
                            // their deferred output back to the CPU.
                            constexpr unsigned emptyIndices = 0x28011200;
                            for (unsigned i = 0; i < 24; ++i)
                                cpu->m_memory.write<std::uint8_t>(emptyIndices + i, 0);
                            auto degenerate = drawCommand;
                            degenerate.gpr[3] = emptyIndices;
                            const auto firstDraw = std::find_if(commands.begin(), commands.end(),
                                                                [](const auto &command) { return command.type == Gx2Cmd::DrawIndexedEx; });
                            commands.insert(firstDraw + 1, {Gx2Command{Gx2Cmd::SetScissor, {8, 8, 8, 8}}, drawCommand,
                                                            Gx2Command{Gx2Cmd::SetScissor, {0, 0, 8, 8}}, degenerate});
                            for (const bool reject: {false, true}) {
                                auto deferred = std::make_shared<DeferredProbe>();
                                deferred->rejectSecond = reject;
                                Gx2Replayer replay;
                                replay.setRasterBackend(deferred);
                                replay.setNativeVerificationInterval(0);
                                replay.replay(chain, &cpu->m_memory);
                                EXPECT_EQ(deferred->calls, 2u);
                                EXPECT_EQ(deferred->clipped, 1u);
                                EXPECT_EQ(deferred->reads, 1u);
                                if (reject)
                                    EXPECT_EQ(replay.framebuffer(), replayer.framebuffer());
                                else
                                    EXPECT_EQ(replay.framebuffer()[0], 71);
                            }
                            // A covered fallback on B must not read unrelated A;
                            // its subsequent native draw still consumes the GPU version.
                            auto separate = gx2Stream();
                            auto &separateCommands = separate.mutableCommands();
                            const auto originalTarget = *std::find_if(separateCommands.begin(), separateCommands.end(),
                                                                      [](const auto &command) { return command.type == Gx2Cmd::SetColorBuffer; });
                            auto otherTarget = originalTarget;
                            otherTarget.payload[0x24 / 4] = 0x28092000;
                            auto separatePresent = std::find_if(separateCommands.begin(), separateCommands.end(), [](const auto &command) {
                                return command.type == Gx2Cmd::CopyColorBufferToScanBuffer;
                            });
                            separateCommands.insert(separatePresent, {otherTarget, drawCommand, originalTarget, drawCommand});
                            auto separateProbe = std::make_shared<DeferredProbe>();
                            separateProbe->rejectSecond = separateProbe->separateTarget = true;
                            Gx2Replayer separateReplay;
                            separateReplay.setRasterBackend(separateProbe);
                            separateReplay.setNativeVerificationInterval(0);
                            separateReplay.replay(separate, &cpu->m_memory);
                            EXPECT_EQ(separateProbe->calls, 3u);
                            EXPECT_EQ(separateProbe->reads, 1u);
                            EXPECT_EQ(separateReplay.framebuffer()[0], 71);
                            if (textureCase.map == 0x00010203 && !compressedTile && !samplerMode && !type) {
                                struct VerificationProbe final : RasterBackend {
                                        unsigned calls{};
                                        std::vector<std::uint8_t> firstPixels;
                                        bool supportsRenderedTargets() const override { return true; }
                                        bool supportsRenderedTextures() const override { return true; }
                                        std::optional<RasterResult> render(const RasterDraw &) override { return std::nullopt; }
                                        std::optional<RasterResult> renderDeferred(const RasterDraw &draw) override
                                        {
                                            ++calls;
                                            auto pixels = calls == 1 ? firstPixels : std::vector<std::uint8_t>(draw.target.size(), 255);
                                            if (calls == 3)
                                                EXPECT_TRUE(draw.renderedTarget);
                                            RasterResult result;
                                            result.readback = std::make_shared<RasterReadback>(pixels.size(), [pixels] { return pixels; });
                                            return result;
                                        }
                                };
                                auto verifyStream = gx2Stream();
                                auto &verifyCommands = verifyStream.mutableCommands();
                                auto whiteTexture = *std::find_if(verifyCommands.begin(), verifyCommands.end(), [](const auto &command) {
                                    return command.type == Gx2Cmd::SetPixelTexture && command.gpr[1] == 3;
                                });
                                whiteTexture.payload[0x84 / 4] = 0x05050505;
                                const auto verifyPresent = std::find_if(verifyCommands.begin(), verifyCommands.end(), [](const auto &command) {
                                    return command.type == Gx2Cmd::CopyColorBufferToScanBuffer;
                                });
                                verifyCommands.insert(verifyPresent,
                                                      {whiteTexture, drawCommand, Gx2Command{Gx2Cmd::SetScissor, {8, 8, 8, 8}}, drawCommand});
                                auto verificationProbe = std::make_shared<VerificationProbe>();
                                for (unsigned y = 0; y < 8; ++y) {
                                    const auto row = replayer.framebuffer().begin() + y * Gx2Replayer::kWidth * 4;
                                    verificationProbe->firstPixels.insert(verificationProbe->firstPixels.end(), row, row + 8 * 4);
                                }
                                Gx2Replayer verified;
                                verified.setRasterBackend(verificationProbe);
                                // Draw 0 is checked, draw 1 stays deferred, and
                                // clipped draw 2 must compare its preserved GPU input.
                                verified.setNativeVerificationInterval(2);
                                verified.replay(verifyStream, &cpu->m_memory);
                                EXPECT_EQ(verificationProbe->calls, 3u);
                                EXPECT_EQ(verified.comparedNativeDraws(), 2u);
                                EXPECT_EQ(verified.differingNativeDraws(), 0u);
                                EXPECT_EQ(verified.framebuffer()[0], 255);
                            }
                        }
                        Gx2Replayer native;
                        if (std::getenv("WEMU_TEST_VULKAN") && SpirvCompiler::available()) {
                            nativeBackend = std::make_shared<VulkanRasterBackend>();
                            native.setRasterBackend(nativeBackend);
                            native.setNativeVerificationInterval(1);
                            native.replay(gx2Stream(), &cpu->m_memory);
                            EXPECT_EQ(native.comparedNativeDraws(), 1u);
                            EXPECT_EQ(native.differingNativeDraws(), 0u);
                            EXPECT_EQ(nativeBackend->completedDraws(), 1u) << nativeBackend->lastError();
                            EXPECT_EQ(native.framebuffer(), replayer.framebuffer());
                        }
                        auto accepted = std::make_shared<BackendProbe>();
                        // Disabling separate alpha blending does not disable RGB blending.
                        auto blendStream = gx2Stream();
                        for (auto &command: blendStream.mutableCommands())
                            if (command.type == Gx2Cmd::SetColorControl)
                                command.gpr[1] = 1;
                        Gx2Command blendCommand{Gx2Cmd::SetBlendControl};
                        blendCommand.gpr = {0, 0, 1, 0, 0, 13, 14, 2}; // preserve destination; separate alpha off
                        blendStream.mutableCommands().insert(blendStream.mutableCommands().begin(), blendCommand);
                        auto blendProbe = std::make_shared<BackendProbe>();
                        blendProbe->expectBlend = true;
                        Gx2Replayer blended, blendFallback;
                        blendFallback.setRasterBackend(blendProbe);
                        blended.replay(blendStream, &cpu->m_memory);
                        blendFallback.replay(blendStream, &cpu->m_memory);
                        EXPECT_EQ(blendProbe->calls, 1u);
                        EXPECT_EQ(blendFallback.framebuffer(), blended.framebuffer());
                        EXPECT_EQ(blended.framebuffer()[0], 0);
                        if (nativeBackend) {
                            Gx2Replayer nativeBlended;
                            nativeBlended.setRasterBackend(nativeBackend);
                            nativeBlended.replay(blendStream, &cpu->m_memory);
                            EXPECT_EQ(nativeBackend->completedDraws(), 2u) << nativeBackend->lastError();
                            EXPECT_EQ(nativeBlended.framebuffer(), blended.framebuffer());
                        }
                        accepted->accept = true;
                        Gx2Replayer committed;
                        committed.setRasterBackend(accepted);
                        committed.setNativeVerificationInterval(1);
                        committed.replay(gx2Stream(), &cpu->m_memory);
                        EXPECT_EQ(committed.comparedNativeDraws(), 1u);
                        EXPECT_EQ(committed.differingNativeDraws(), 1u);
                        EXPECT_EQ(accepted->calls, 1u);
                        EXPECT_EQ(committed.framebuffer()[0], 9);
                        EXPECT_EQ(committed.framebuffer()[1], 17);
                        EXPECT_EQ(committed.framebuffer()[2], 33);
                        accepted->malformed = true;
                        EXPECT_THROW(committed.replay(gx2Stream(), &cpu->m_memory), std::runtime_error);
                        for (const unsigned interval: {0u, 2u}) {
                            Gx2Replayer sampled;
                            sampled.setRasterWorkerCount(4, 0);
                            sampled.setRasterVerificationInterval(interval);
                            sampled.replay(gx2Stream(), &cpu->m_memory);
                            EXPECT_EQ(sampled.verifiedRasterTriangles(), interval ? 1u : 0u);
                            EXPECT_EQ(sampled.framebuffer(), replayer.framebuffer());
                        }
                        // Mirror an already rendered texture. Self-feedback must read the
                        // pre-draw image, and each later draw must see the updated backing.
                        cpu->m_memory.write<std::uint32_t>(0x28009084, 0x00010203);
                        for (unsigned v = 0; v < 4; ++v)
                            cpu->m_memory.write<std::uint32_t>(VERTS + v * 32 + 24, std::bit_cast<std::uint32_t>(1.0f - vertices[v][6]));
                        surface(0x2800C000, 0x28060000, 256, 8, 8, 8, 1);
                        for (unsigned pass = 0; pass < 3; ++pass) {
                            gx2Stream().clear();
                            const unsigned target = pass == 2 ? 0x2800C000 : 0x28009000;
                            record("GX2SetColorBuffer", {target, 0});
                            record("GX2SetPixelTexture", {0x28009000, 3});
                            record("GX2DrawIndexedEx", {4, 6, type, INDICES, 0, 1});
                            record("GX2CopyColorBufferToScanBuffer", {target, 1});
                            gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
                            replayer.replay(gx2Stream(), &cpu->m_memory);
                            parallel.replay(gx2Stream(), &cpu->m_memory);
                            fallback.replay(gx2Stream(), &cpu->m_memory);
                            EXPECT_EQ(backend->calls, pass + 2);
                            // The third draw samples the second draw's unmirrored result.
                            const auto expectedBefore = textureCase.expected[pass == 1 ? 1u : 0u];
                            for (unsigned c = 0; c < 4; ++c)
                                EXPECT_FLOAT_EQ(backend->firstTexel[c], expectedBefore[c] / 255.0f);
                            EXPECT_EQ(fallback.framebuffer(), replayer.framebuffer());
                            if (nativeBackend) {
                                native.replay(gx2Stream(), &cpu->m_memory);
                                EXPECT_EQ(nativeBackend->completedDraws(), pass + 3u) << nativeBackend->lastError();
                                EXPECT_EQ(native.framebuffer(), replayer.framebuffer());
                                if (pass == 2)
                                    EXPECT_GE(nativeBackend->reusedDraws(), 2u);
                            }
                            EXPECT_EQ(parallel.framebuffer(), replayer.framebuffer());
                            const unsigned flip = pass == 1 ? 0 : 1;
                            EXPECT_EQ(pixel(1, 1), textureCase.expected[0 ^ flip]);
                            EXPECT_EQ(pixel(6, 1), textureCase.expected[1 ^ flip]);
                            EXPECT_EQ(pixel(1, 6), textureCase.expected[2 ^ flip]);
                            EXPECT_EQ(pixel(6, 6), textureCase.expected[3 ^ flip]);
                        }
                        surface(0x2800A000, 0x28050000, 48, 2, 2, 2, 16);
                        cpu->m_memory.write<std::uint32_t>(0x2800A000, 5);
                        cpu->m_memory.write<std::uint32_t>(0x2800A00C, 3);
                        cpu->m_memory.write<std::uint32_t>(0x2800A084, 0x00010203);
                        for (unsigned layer = 0; layer < 3; ++layer)
                            for (unsigned texel = 0; texel < 4; ++texel)
                                cpu->m_memory.write<std::uint32_t>(0x28050000 + layer * 16 + texel * 4, colors[layer]);
                        struct LayerCase {
                                float coordinate;
                                unsigned first, count, expected;
                        };
                        const LayerCase layers[] = {{-20, 0, 3, 0},  {0.49f, 0, 3, 0}, {0.5f, 0, 3, 1},
                                                    {1.5f, 0, 3, 2}, {20, 0, 3, 2},    {0, 1, 2, 1},
                                                    {1, 1, 2, 2},    {-20, 1, 2, 1},   {0, 3, 1, 3},
                                                    {0, 2, 2, 3},    {0, 0, 0, 3},     {std::numeric_limits<float>::quiet_NaN(), 0, 3, 3}};
                        unsigned validArrayDraws = 0;
                        for (const auto &layer: layers) {
                            // TEX selects R0.x for Z; use constant U to exercise fractional layer rounding.
                            for (unsigned v = 0; v < 4; ++v)
                                cpu->m_memory.write<std::uint32_t>(VERTS + v * 32 + 24, std::bit_cast<std::uint32_t>(layer.coordinate));
                            cpu->m_memory.write<std::uint32_t>(0x2800A07C, layer.first);
                            cpu->m_memory.write<std::uint32_t>(0x2800A080, layer.count);
                            gx2Stream().clear();
                            record("GX2SetPixelTexture", {0x2800A000, 3});
                            record("GX2DrawIndexedEx", {4, 6, type, INDICES, 0, 1});
                            record("GX2CopyColorBufferToScanBuffer", {0x2800C000, 1});
                            gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
                            replayer.replay(gx2Stream(), &cpu->m_memory);
                            fallback.replay(gx2Stream(), &cpu->m_memory);
                            const bool validView = layer.count && layer.first < 3 && layer.count <= 3 - layer.first;
                            validArrayDraws += validView;
                            EXPECT_EQ(backend->calls, 4u + validArrayDraws);
                            EXPECT_EQ(fallback.framebuffer(), replayer.framebuffer());
                            if (nativeBackend) {
                                native.replay(gx2Stream(), &cpu->m_memory);
                                EXPECT_EQ(nativeBackend->completedDraws(), 5u + validArrayDraws) << nativeBackend->lastError();
                                EXPECT_EQ(native.framebuffer(), replayer.framebuffer());
                            }
                            const std::array<std::array<std::uint8_t, 4>, 3> layerColors{{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}}};
                            EXPECT_EQ(pixel(1, 1), layer.expected == 3 ? border : layerColors[layer.expected]);
                        }
                        // A patterned macro-tiled layer catches missing slice bank rotation.
                        surface(0x2800A000, 0x28050000, 3 * 2048, 32, 16, 32, 4);
                        cpu->m_memory.write<std::uint32_t>(0x2800A000, 5);
                        cpu->m_memory.write<std::uint32_t>(0x2800A00C, 3);
                        cpu->m_memory.write<std::uint32_t>(0x2800A034, 0x300);
                        cpu->m_memory.write<std::uint32_t>(0x2800A07C, 0);
                        cpu->m_memory.write<std::uint32_t>(0x2800A080, 3);
                        for (unsigned layer = 0; layer < 3; ++layer)
                            for (unsigned y = 0; y < 16; ++y)
                                for (unsigned x = 0; x < 32; ++x) {
                                    const auto off = layer * 2048 + tiledElementOffset(x, y, 32, 32, 4, ((3 + layer * 2) & 7) << 8);
                                    cpu->m_memory.write<std::uint32_t>(0x28050000 + off, (x << 24) | (y << 16) | ((40 * layer) << 8) | 255);
                                }
                        for (unsigned v = 0; v < 4; ++v)
                            cpu->m_memory.write<std::uint32_t>(VERTS + v * 32 + 24, std::bit_cast<std::uint32_t>(vertices[v][6]));
                        // Shader programs are cached by header identity; bind a distinct constant-layer shader.
                        constexpr unsigned ARRAY_PS = 0x2800D000, ARRAY_PSP = 0x2800E000;
                        for (unsigned i = 0; i < 0x200; i += 4)
                            cpu->m_memory.write<std::uint32_t>(ARRAY_PS + i, 0);
                        cpu->m_memory.write<std::uint32_t>(ARRAY_PS + 0x10, 1);
                        cpu->m_memory.write<std::uint32_t>(ARRAY_PS + 0x14, 7);
                        auto arrayPs = ps;
                        arrayPs[6] |= 5u << 26; // TEX Z selector: constant 1
                        shader(ARRAY_PS, 0xA4, ARRAY_PSP, arrayPs);
                        gx2Stream().clear();
                        record("GX2SetPixelShader", {ARRAY_PS});
                        record("GX2SetPixelTexture", {0x2800A000, 3});
                        record("GX2DrawIndexedEx", {4, 6, type, INDICES, 0, 1});
                        record("GX2CopyColorBufferToScanBuffer", {0x2800C000, 1});
                        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
                        replayer.replay(gx2Stream(), &cpu->m_memory);
                        EXPECT_EQ(pixel(1, 1), (std::array<std::uint8_t, 4>{6, 3, 40, 255}));
                        EXPECT_EQ(pixel(6, 6), (std::array<std::uint8_t, 4>{26, 13, 40, 255}));
                        if (nativeBackend) {
                            native.replay(gx2Stream(), &cpu->m_memory);
                            EXPECT_EQ(native.framebuffer(), replayer.framebuffer());
                            EXPECT_EQ(nativeBackend->completedDraws(), 6u + validArrayDraws) << nativeBackend->lastError();
                        }
                        surface(0x2800A000, 0x28050000, 24, 4, 4, 1, 16);
                        cpu->m_memory.write<std::uint32_t>(0x2800A000, 5);
                        cpu->m_memory.write<std::uint32_t>(0x2800A00C, 3);
                        cpu->m_memory.write<std::uint32_t>(0x2800A014, 0x34);
                        cpu->m_memory.write<std::uint32_t>(0x2800A080, 1);
                        cpu->m_memory.write<std::uint32_t>(0x2800A084, 0x05050500);
                        for (unsigned layer = 0; layer < 3; ++layer)
                            for (unsigned byte = 0; byte < 8; ++byte)
                                cpu->m_memory.write<std::uint8_t>(0x28050000 + layer * 8 + byte, byte < 2 ? 40 + layer * 80 : 0);
                        for (unsigned layer = 0; layer < 4; ++layer) {
                            cpu->m_memory.write<std::uint32_t>(0x2800A07C, layer % 3);
                            // Last case must reject a wrapping allocation before accessing guest memory.
                            cpu->m_memory.write<std::uint32_t>(0x2800A024, layer == 3 ? 0xFFFFFFF0 : 0x28050000);
                            gx2Stream().clear();
                            record("GX2SetPixelTexture", {0x2800A000, 3});
                            record("GX2DrawIndexedEx", {4, 6, type, INDICES, 0, 1});
                            record("GX2CopyColorBufferToScanBuffer", {0x2800C000, 1});
                            gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
                            replayer.replay(gx2Stream(), &cpu->m_memory);
                            EXPECT_EQ(pixel(1, 1), layer == 3 ? border : (std::array<std::uint8_t, 4>{255, 255, 255, std::uint8_t(40 + layer * 80)}));
                            if (nativeBackend) {
                                native.replay(gx2Stream(), &cpu->m_memory);
                                EXPECT_EQ(native.framebuffer(), replayer.framebuffer());
                                EXPECT_EQ(nativeBackend->completedDraws(), 6u + validArrayDraws + std::min(layer + 1, 3u))
                                        << nativeBackend->lastError();
                            }
                        }
                        // Rebind an unchanged view after guest writes: decoded array caches
                        // must compare source bytes, not trust address/shape identity alone.
                        cpu->m_memory.write<std::uint32_t>(0x2800A024, 0x28050000);
                        cpu->m_memory.write<std::uint8_t>(0x28050000, 199);
                        cpu->m_memory.write<std::uint8_t>(0x28050001, 199);
                        gx2Stream().clear();
                        record("GX2SetPixelTexture", {0x2800A000, 3});
                        record("GX2DrawIndexedEx", {4, 6, type, INDICES, 0, 1});
                        record("GX2CopyColorBufferToScanBuffer", {0x2800C000, 1});
                        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
                        replayer.replay(gx2Stream(), &cpu->m_memory);
                        EXPECT_EQ(pixel(1, 1), (std::array<std::uint8_t, 4>{255, 255, 255, 199}));
                        if (nativeBackend) {
                            native.replay(gx2Stream(), &cpu->m_memory);
                            EXPECT_EQ(native.framebuffer(), replayer.framebuffer());
                            EXPECT_EQ(nativeBackend->completedDraws(), 10u + validArrayDraws) << nativeBackend->lastError();
                        }
                    }
                }
}

TEST_F(Gx2CaptureTest, DepthClearsExportsOcclusionAndSamplingMatchNative)
{
    using namespace Core::Gfx;
    constexpr unsigned colorHeader = 0x28009000, depthHeader = 0x2800A000, colorImage = 0x28040000, depthImage = 0x28050000;
    constexpr unsigned vsHeader = 0x28001000, psColorHeader = 0x28002000, psDepthHeader = 0x28003000, psSampleHeader = 0x28006000;
    constexpr unsigned vertices = 0x28010000;
    const std::vector<std::uint32_t> vs{60u | (1u << 13) | (1u << 15), (0x27u << 23) | 0x688u, (2u << 13) | (2u << 15),
                                        (0x28u << 23) | (1u << 21) | 0x688u};
    const std::vector<std::uint32_t> colorPs{0, (0x28u << 23) | (1u << 21) | 0x688u};
    const std::vector<std::uint32_t> depthPs{61, (0x28u << 23) | (1u << 21) | 0xFF8u};
    const std::vector<std::uint32_t> samplePs{2, 1u << 23, 0, (0x28u << 23) | (1u << 21) | 0x688u, 0x10u, 0x30000000u | (0x688u << 9), 1u << 23, 0};
    const auto shader = [&](unsigned header, unsigned offset, unsigned ptr, const auto &words) {
        for (unsigned off = 0; off < 0x200; off += 4)
            cpu->m_memory.write<std::uint32_t>(header + off, 0);
        cpu->m_memory.write<std::uint32_t>(header + offset, words.size() * 4);
        cpu->m_memory.write<std::uint32_t>(header + offset + 4, ptr);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<std::uint32_t>(ptr + i * 4, std::byteswap(words[i]));
    };
    shader(vsHeader, 0xD0, 0x28007000, vs);
    shader(psColorHeader, 0xA4, 0x28008000, colorPs);
    shader(psDepthHeader, 0xA4, 0x2800B000, depthPs);
    shader(psSampleHeader, 0xA4, 0x2800C000, samplePs);
    for (unsigned header: {psColorHeader, psDepthHeader, psSampleHeader})
        cpu->m_memory.write<std::uint32_t>(header + 0x10, 1);
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    const auto mesh = [&](float z, const std::array<float, 4> &value) {
        const float positions[6][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, 1}, {1, -1}, {-1, -1}};
        for (unsigned v = 0; v < 6; ++v) {
            const std::array<float, 8> data{positions[v][0], positions[v][1], z, 1, value[0], value[1], value[2], value[3]};
            for (unsigned c = 0; c < 8; ++c)
                cpu->m_memory.write<std::uint32_t>(vertices + v * 32 + c * 4, std::bit_cast<std::uint32_t>(data[c]));
        }
    };
    for (const unsigned format: {0x80Eu, 5u}) {
        SCOPED_TRACE(format);
        gx2Stream().clear();
        surface(colorHeader, colorImage, 256, 8, 8, 8, 1);
        surface(depthHeader, depthImage, 10 * 8 * (format == 5 ? 2 : 4), 8, 8, 10, 0);
        cpu->m_memory.write<std::uint32_t>(depthHeader + 0x14, format);
        cpu->m_memory.write<std::uint32_t>(depthHeader + 0x80, 1);
        cpu->m_memory.write<std::uint32_t>(depthHeader + 0x84, 0x00040505); // depth,0,1,1
        for (auto &f: cpu->m_fpr)
            f = 0;
        cpu->m_fpr[4] = 1;
        cpu->m_fpr[5] = 1;
        record("GX2ClearBuffersEx", {colorHeader, depthHeader, 0, 1});
        record("GX2SetColorBuffer", {colorHeader, 0});
        record("GX2SetDepthBuffer", {depthHeader});
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        cpu->m_fpr[1] = cpu->m_fpr[2] = cpu->m_fpr[5] = 0;
        cpu->m_fpr[3] = cpu->m_fpr[4] = 8;
        cpu->m_fpr[6] = 1;
        record("GX2SetViewport", {});
        record("GX2SetScissor", {0, 0, 8, 8});
        Gx2Command fetch{Gx2Cmd::SetFetchShader};
        fetch.payload = {0, 0, 0, 0x813, 0, 0, 0x00010203, 3, 1, 0, 16, 0x813, 0, 0, 0x00010203, 3};
        gx2Stream().push(fetch);
        record("GX2SetVertexShader", {vsHeader});
        record("GX2SetAttribBuffer", {0, 6 * 32, 32, vertices});
        record("GX2SetDepthOnlyControl", {1, 1, 1});
        record("GX2SetPixelShader", {psDepthHeader});
        record("GX2DrawEx", {4, 6, 0, 1});
        mesh(0, {.25f, 0, 0, 1});
        Gx2Replayer reference, native;
        std::shared_ptr<VulkanRasterBackend> backend;
        if (std::getenv("WEMU_TEST_VULKAN") && SpirvCompiler::available()) {
            backend = std::make_shared<VulkanRasterBackend>();
            native.setRasterBackend(backend);
            native.setNativeVerificationInterval(1);
        }
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        if (backend)
            EXPECT_EQ(backend->completedDraws(), 1u) << backend->lastError();
        if (format == 0x80Eu) {
            // Equal-depth fractional geometry must remain occluded by LESS.
            // Independently rounded weights previously made a constant .25
            // plane drift below the stored value in isolated pixels.
            gx2Stream().clear();
            record("GX2SetPixelShader", {psColorHeader});
            record("GX2DrawEx", {4, 6, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {colorHeader, 1});
            gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
            mesh(-.5f, {1, 0, 0, 1});
            for (unsigned v = 0; v < 6; ++v) {
                const float x = v == 1 || v == 3 || v == 4 ? .88711f : -.84383f;
                const float y = v == 0 || v == 1 || v == 3 ? .67123f : -.95371f;
                cpu->m_memory.write<std::uint32_t>(vertices + v * 32, std::bit_cast<std::uint32_t>(x));
                cpu->m_memory.write<std::uint32_t>(vertices + v * 32 + 4, std::bit_cast<std::uint32_t>(y));
            }
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            for (std::size_t i = 0; i < reference.framebuffer().size(); i += 4)
                EXPECT_EQ(reference.framebuffer()[i], 0);
            EXPECT_EQ(native.framebuffer(), reference.framebuffer());
        }
        // A stencil-only clear leaves depth .25; z=.5 is occluded.
        gx2Stream().clear();
        cpu->m_fpr[1] = 1;
        record("GX2ClearDepthStencilEx", {depthHeader, 0, 2});
        record("GX2SetPixelShader", {psColorHeader});
        record("GX2DrawEx", {4, 6, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {colorHeader, 1});
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        mesh(0, {1, 0, 0, 1});
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ(reference.framebuffer()[0], 0);
        EXPECT_EQ(native.framebuffer(), reference.framebuffer());
        // Depth clear makes the same triangle visible.
        gx2Stream().mutableCommands()[0].gpr[2] = 1;
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ(reference.framebuffer()[0], 255);
        EXPECT_EQ(native.framebuffer(), reference.framebuffer());
        // A near triangle updates the depth surface; a later sampler sees it.
        gx2Stream().clear();
        record("GX2SetPixelShader", {psColorHeader});
        record("GX2DrawEx", {4, 6, 0, 1});
        mesh(-.75f, {1, 0, 0, 1});
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        gx2Stream().clear();
        record("GX2SetDepthOnlyControl", {0, 0, 1});
        record("GX2SetPixelShader", {psSampleHeader});
        cpu->m_memory.write<std::uint32_t>(depthHeader + 0xC, 0); // 2D texture views may leave the unused depth field zero.
        record("GX2SetPixelTexture", {depthHeader, 0});
        record("GX2DrawEx", {4, 6, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {colorHeader, 1});
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        mesh(0, {.5f, .5f, 0, 1});
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ(reference.framebuffer()[0], 32);
        EXPECT_EQ(reference.framebuffer()[1], 0);
        EXPECT_EQ(native.framebuffer(), reference.framebuffer());
        if (backend)
            EXPECT_EQ(native.differingNativeDraws(), 0u);
    }
}

TEST_F(Gx2CaptureTest, EarlyDepthRejectsPointsAndTrianglesButPreservesDepthExports)
{
    using namespace Core::Gfx;
    constexpr unsigned color = 0x28001000, depth = 0x28002000, vs = 0x28003000, ps = 0x28004000, depthPs = 0x28005000;
    constexpr unsigned vertices = 0x28010000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    const auto shader = [&](unsigned header, unsigned offset, unsigned pointer, const std::vector<unsigned> &words) {
        for (unsigned i = 0; i < 0x200; i += 4)
            cpu->m_memory.write<unsigned>(header + i, 0);
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    };
    shader(vs, 0xD0, 0x28006000,
           {60u | (1u << 13) | (1u << 15), (Latte::CF_EXP << 23) | 0x688u, (2u << 13) | (2u << 15),
            (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u});
    shader(ps, 0xA4, 0x28007000, {0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u});
    shader(depthPs, 0xA4, 0x28008000, {61, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0xFF8u});
    for (unsigned mode: {1u, 4u}) {
        SCOPED_TRACE(mode);
        const unsigned count = mode == 1 ? 1 : 6;
        const auto mesh = [&](float z) {
            const std::array<std::array<float, 2>, 6> xy{{{-1, 1}, {1, 1}, {-1, -1}, {1, 1}, {1, -1}, {-1, -1}}};
            for (unsigned v = 0; v < count; ++v) {
                const std::array<float, 8> data{mode == 1 ? .25f : xy[v][0], mode == 1 ? -.25f : xy[v][1], z, 1, .125f, 0, 0, 1};
                for (unsigned c = 0; c < data.size(); ++c)
                    cpu->m_memory.write<unsigned>(vertices + v * 32 + c * 4, std::bit_cast<unsigned>(data[c]));
            }
        };
        gx2Stream().clear();
        surface(color, 0x28040000, 64, 4, 4, 4, 1);
        surface(depth, 0x28050000, 64, 4, 4, 4, 0);
        cpu->m_memory.write<unsigned>(depth + 0x14, 0x80E);
        for (unsigned c = 1; c <= 4; ++c)
            cpu->m_fpr[c] = 0;
        record("GX2ClearColor", {color});
        cpu->m_fpr[1] = .25;
        record("GX2ClearDepthStencilEx", {depth, 0, 1});
        record("GX2SetColorBuffer", {color, 0});
        record("GX2SetDepthBuffer", {depth});
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        record("GX2SetDepthOnlyControl", {1, 1, 1});
        cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
        cpu->m_fpr[3] = cpu->m_fpr[4] = 4;
        cpu->m_fpr[5] = 0;
        cpu->m_fpr[6] = 1;
        record("GX2SetViewport", {});
        record("GX2SetScissor", {0, 0, 4, 4});
        Gx2Command fetch{Gx2Cmd::SetFetchShader};
        fetch.payload = {0, 0, 0, 0x813, 0, 0, 0x00010203, 3, 1, 0, 16, 0x813, 0, 0, 0x00010203, 3};
        gx2Stream().push(fetch);
        record("GX2SetVertexShader", {vs});
        record("GX2SetPixelShader", {ps});
        record("GX2SetAttribBuffer", {0, count * 32, 32, vertices});
        record("GX2DrawEx", {mode, count, 0, 1});
        mesh(.5f); // Window depth .75 fails LESS against .25.
        Gx2Replayer reference, native;
        std::shared_ptr<VulkanRasterBackend> backend;
        if (std::getenv("WEMU_TEST_VULKAN")) {
            backend = std::make_shared<VulkanRasterBackend>(true, true);
            native.setRasterBackend(backend);
            native.setNativeVerificationInterval(1);
        }
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ(reference.earlyDepthRejectedFragments(), mode == 1 ? 1u : 0u);
        EXPECT_EQ(reference.earlyDepthRejectedTriangles(), mode == 1 ? 0u : 2u);
        // Disabled color makes a proven unconditional color-only shader dead.
        // Even its otherwise unsupported cube texture must not prevent the GPU
        // from rendering the original geometry and writing geometric depth.
        gx2Stream().clear();
        shader(0x2800A000, 0xA4, 0x2800C000,
               {2, (1u << 23) | (1u << 10), 0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | (7u << 17) | 0x688u, 0x0E, 0x001FFE00, 0x6DB00000, 0, 0x11,
                0x30000000u | (1u << 12) | (2u << 15) | (3u << 18), 1u << 23, 0});
        surface(0x2800B000, 0x28060000, 6 * 4 * 4 * 4, 4, 4, 4, 1);
        cpu->m_memory.write<unsigned>(0x2800B000, 3);
        cpu->m_memory.write<unsigned>(0x2800B000 + 0xC, 6);
        record("GX2SetPixelTexture", {0x2800B000, 0});
        record("GX2SetPixelShader", {0x2800A000});
        // Even with color enabled, readonly unsupported sampling cannot alter
        // depth or coverage. Reject hidden fragments before legacy sampling.
        record("GX2DrawEx", {mode, count, 0, 1});
        const auto rejectedFragments = reference.earlyDepthRejectedFragments();
        const auto rejectedTriangles = reference.earlyDepthRejectedTriangles();
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ(reference.earlyDepthRejectedFragments() - rejectedFragments, mode == 1 ? 1u : 0u);
        EXPECT_EQ(reference.earlyDepthRejectedTriangles() - rejectedTriangles, mode == 1 ? 0u : 2u);
        gx2Stream().clear();
        if (mode == 1)
            record("GX2SetColorControl", {0xCC, 0, 0, 0});
        else
            record("GX2SetTargetChannelMasks", {0, 0, 0, 0, 0, 0, 0, 0});
        record("GX2DrawEx", {mode, count, 0, 1});
        mesh(-.75f);
        const auto beforeDepthOnly = backend ? backend->completedDraws() : 0;
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        if (backend)
            EXPECT_EQ(backend->completedDraws(), beforeDepthOnly + 1) << backend->lastError();
        // A masked color export can discard and must still execute with color
        // writes disabled. Incorrect elimination would overwrite depth .125.
        gx2Stream().clear();
        shader(0x2800D000, 0xA4, 0x2800E000,
               {4, 9u << 26, 0, (Latte::CF_EXP << 23) | 0x688u, 0, (0x0Eu << 23) | 1, 0, 1u << 21, 0x80000000u | Latte::SRC_0 | (Latte::SRC_1 << 13),
                (Latte::OP2_PRED_SETGT << 7) | 12u});
        record("GX2SetPixelShader", {0x2800D000});
        record("GX2DrawEx", {mode, count, 0, 1});
        mesh(-.875f);
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        gx2Stream().clear();
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        record("GX2SetTargetChannelMasks", {15, 0, 0, 0, 0, 0, 0, 0});
        record("GX2SetPixelShader", {ps});
        record("GX2SetDepthOnlyControl", {1, 0, 2});
        record("GX2DrawEx", {mode, count, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {color, 1});
        mesh(-.75f);
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        const auto depthOnlyCenter = (2 * Gx2Replayer::kWidth + 2) * 4;
        EXPECT_EQ(reference.framebuffer()[depthOnlyCenter], 32u);
        EXPECT_EQ(native.framebuffer(), reference.framebuffer());
        gx2Stream().clear();
        cpu->m_fpr[1] = .25;
        record("GX2ClearDepthStencilEx", {depth, 0, 1});
        record("GX2SetDepthOnlyControl", {1, 1, 1});
        record("GX2SetColorControl", {0xCC, 0, 0, 0});
        record("GX2SetPixelShader", {depthPs});
        record("GX2DrawEx", {mode, count, 0, 1});
        mesh(.5f);
        const auto beforeDepthExport = reference.earlyDepthRejectedFragments();
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory); // Exported .125 passes despite geometric .75.
        EXPECT_EQ(reference.earlyDepthRejectedFragments(), beforeDepthExport);
        gx2Stream().clear();
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        record("GX2SetPixelShader", {ps});
        record("GX2SetDepthOnlyControl", {1, 0, 2});
        record("GX2DrawEx", {mode, count, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {color, 1});
        mesh(-.75f); // Window .125 equals the previous shader's depth export.
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        const auto center = (2 * Gx2Replayer::kWidth + 2) * 4;
        EXPECT_EQ(reference.framebuffer()[center], 32u);
        EXPECT_EQ(reference.framebuffer()[center + 3], 255u);
        if (mode == 4) {
            // NOTEQUAL can change the stored range in either direction. A later
            // triangle must see bounds expanded by the first triangle's writes.
            gx2Stream().clear();
            cpu->m_fpr[1] = .5;
            record("GX2ClearDepthStencilEx", {depth, 0, 1});
            record("GX2SetDepthOnlyControl", {1, 1, 5});
            record("GX2DrawEx", {4, 6, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {color, 1});
            const std::array<std::array<float, 2>, 3> xy{{{-1, 1}, {3, 1}, {-1, -3}}};
            for (unsigned v = 0; v < 6; ++v) {
                const std::array<float, 8> data{xy[v % 3][0], xy[v % 3][1], v < 3 ? -.5f : 0.f, 1, v < 3 ? 1.f : 0.f, v < 3 ? 0.f : 1.f, 0, 1};
                for (unsigned c = 0; c < data.size(); ++c)
                    cpu->m_memory.write<unsigned>(vertices + v * 32 + c * 4, std::bit_cast<unsigned>(data[c]));
            }
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            EXPECT_EQ(reference.framebuffer()[center], 0u);
            EXPECT_EQ(reference.framebuffer()[center + 1], 255u);
            EXPECT_EQ(native.framebuffer(), reference.framebuffer());
            // A depth-only shader covers the selected array layer even outside
            // an unrelated smaller color attachment. Descriptors are snapshots.
            gx2Stream().clear();
            surface(color, 0x28040000, 16, 2, 2, 2, 1);
            surface(depth, 0x28050000, 128, 4, 4, 4, 0);
            cpu->m_memory.write<unsigned>(depth, 5);
            cpu->m_memory.write<unsigned>(depth + 0xC, 2);
            cpu->m_memory.write<unsigned>(depth + 0x14, 0x80E);
            cpu->m_memory.write<unsigned>(depth + 0x78, 0);
            cpu->m_memory.write<unsigned>(depth + 0x7C, 1);
            cpu->m_fpr[1] = .75;
            record("GX2ClearDepthStencilEx", {depth, 0, 1});
            cpu->m_memory.write<unsigned>(depth + 0x78, 1);
            cpu->m_fpr[1] = .25;
            record("GX2ClearDepthStencilEx", {depth, 0, 1});
            record("GX2SetDepthBuffer", {depth});
            record("GX2SetColorBuffer", {color, 0});
            record("GX2SetDepthOnlyControl", {1, 1, 1});
            record("GX2SetPixelShader", {depthPs});
            record("GX2DrawEx", {4, 6, 0, 1});
            cpu->m_memory.write<unsigned>(depth + 0x78, 0); // Reuse the descriptor before replay.
            mesh(.5f);
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            gx2Stream().clear();
            surface(color, 0x28040000, 64, 4, 4, 4, 1);
            record("GX2SetColorBuffer", {color, 0});
            record("GX2SetPixelShader", {ps});
            record("GX2SetDepthOnlyControl", {1, 0, 2});
            record("GX2DrawEx", {4, 6, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {color, 1});
            mesh(-.75f);
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            EXPECT_EQ(reference.framebuffer()[center], 32u);
            EXPECT_EQ(reference.framebuffer()[center + 1], 0u);
            EXPECT_EQ(native.framebuffer(), reference.framebuffer());
            gx2Stream().clear();
            record("GX2SetDepthBuffer", {depth}); // Layer zero still holds .75.
            record("GX2DrawEx", {4, 6, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {color, 1});
            mesh(.5f);
            for (unsigned v = 0; v < 6; ++v) {
                cpu->m_memory.write<unsigned>(vertices + v * 32 + 16, 0);
                cpu->m_memory.write<unsigned>(vertices + v * 32 + 20, std::bit_cast<unsigned>(1.f));
            }
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            EXPECT_EQ(reference.framebuffer()[center], 0u);
            EXPECT_EQ(reference.framebuffer()[center + 1], 255u);
            EXPECT_EQ(native.framebuffer(), reference.framebuffer());
        }
    }
}

TEST_F(Gx2CaptureTest, DepthClearDescriptorsAndControlRegistersAreCapturedAtCallTime)
{
    using namespace Core::Gfx;
    constexpr unsigned color = 0x28001000, depth = 0x28002000, control = 0x28003000;
    surface(color, 0x28040000, 256, 8, 8, 8, 1);
    surface(depth, 0x28050000, 256, 8, 8, 8, 0);
    cpu->m_memory.write<std::uint32_t>(depth + 0x14, 0x80E);
    cpu->m_memory.write<unsigned>(depth + 0x74, 0);
    cpu->m_memory.write<unsigned>(depth + 0x78, 1);
    cpu->m_memory.write<unsigned>(depth + 0x7C, 1);
    cpu->m_gpr[3] = color;
    cpu->m_gpr[4] = depth;
    cpu->m_gpr[5] = 23;
    cpu->m_gpr[6] = 1;
    cpu->m_fpr[5] = .75;
    call("GX2ClearBuffersEx");
    ASSERT_EQ(gx2Stream().size(), 1u);
    const auto both = gx2Stream().commands()[0];
    ASSERT_EQ(both.payload.size(), 64u);
    EXPECT_EQ(both.payload[9], 0x28040000u);
    EXPECT_EQ(both.payload[41], 0x28050000u);
    EXPECT_EQ(both.payload[32 + 0x78 / 4], 1u);
    EXPECT_EQ(both.payload[32 + 0x7C / 4], 1u);
    EXPECT_DOUBLE_EQ(both.fpr[4], .75);
    EXPECT_EQ(both.gpr[2], 23u);
    EXPECT_EQ(both.gpr[3], 1u);
    cpu->m_gpr[3] = depth;
    cpu->m_gpr[4] = 0;
    cpu->m_gpr[5] = 1;
    cpu->m_fpr[1] = .25;
    call("GX2ClearDepthStencilEx");
    ASSERT_EQ(gx2Stream().commands()[1].payload.size(), 32u);
    cpu->m_memory.write<std::uint32_t>(depth + 0x24, 0);
    EXPECT_EQ(gx2Stream().commands()[1].payload[9], 0x28050000u);
    const unsigned word = (1u << 1) | (1u << 2) | (3u << 4);
    cpu->m_memory.write<std::uint32_t>(control, word);
    cpu->m_gpr[3] = control;
    call("GX2SetDepthStencilControlReg");
    cpu->m_memory.write<std::uint32_t>(control, 0);
    const auto state = gx2Stream().commands().back();
    EXPECT_EQ(state.type, Gx2Cmd::SetDepthStencilControl);
    EXPECT_EQ(state.gpr[0], 1u);
    EXPECT_EQ(state.gpr[1], 1u);
    EXPECT_EQ(state.gpr[2], 3u);
    EXPECT_EQ(state.gpr[3], 0u);
    EXPECT_EQ(state.payload, std::vector<std::uint32_t>{word});
}

TEST_F(Gx2CaptureTest, IndexedPointsPreserveHdrFloatTargetsThroughVertexTextureSampling)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
    constexpr unsigned FETCH = 0x28001000, ATTRS = 0x28003000, VERTS = 0x28010000, INDEX = 0x28011000;
    constexpr unsigned HDR = 0x28009000, OUT = 0x2800A000, HDR_IMAGE = 0x28040000, OUT_IMAGE = 0x28050000;
    for (unsigned i = 0; i < 0x200; i += 4) {
        cpu->m_memory.write<unsigned>(VS + i, 0);
        cpu->m_memory.write<unsigned>(PS + i, 0);
    }
    cpu->m_memory.write<unsigned>(VS + 0x40, 2);
    cpu->m_memory.write<unsigned>(VS + 0x44, 0);
    cpu->m_memory.write<unsigned>(VS + 0x48, 1);
    cpu->m_memory.write<unsigned>(VS + 0xC, 1);
    cpu->m_memory.write<unsigned>(VS + 0x10, 0xFFFFFF07);
    cpu->m_memory.write<unsigned>(PS + 0x10, 1);
    cpu->m_memory.write<unsigned>(PS + 0x14, 7);
    const auto shader = [&](unsigned header, unsigned offset, unsigned pointer, const std::vector<unsigned> &words) {
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    };
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    const std::array<unsigned, 16> attributes{0, 0, 0, 0x811, 0, 0, 0x00010205, 3, 1, 0, 12, 0x811, 0, 0, 0x00010205, 3};
    for (unsigned i = 0; i < attributes.size(); ++i)
        cpu->m_memory.write<unsigned>(ATTRS + i * 4, attributes[i]);
    record("GX2InitFetchShaderEx", {FETCH, 0x28002000, 2, ATTRS, 0, 0});
    const std::array<std::array<float, 6>, 2> vertices{{{-.25f, .5f, 0, 2.f, -.5f, .25f}, {.75f, -.5f, 0, 3.f, .5f, 2.f}}};
    for (unsigned v = 0; v < 2; ++v)
        for (unsigned c = 0; c < 6; ++c)
            cpu->m_memory.write<unsigned>(VERTS + v * 24 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
    surface(HDR, HDR_IMAGE, 6 * 2 * 16, 4, 2, 6, 1);
    cpu->m_memory.write<unsigned>(HDR + 0x14, 0x823);
    surface(OUT, OUT_IMAGE, 6 * 2 * 16, 4, 2, 6, 1);
    cpu->m_memory.write<unsigned>(OUT + 0x14, 0x823);
    for (unsigned header: {HDR, OUT}) {
        cpu->m_memory.write<unsigned>(header + 0x10, 1);
        cpu->m_memory.write<unsigned>(header + 0x80, 1);
        cpu->m_memory.write<unsigned>(header + 0x84, 0x00010203);
    }
    const std::vector<unsigned> plainVs{60u | (1u << 13) | (1u << 15), (Latte::CF_EXP << 23) | 0x688u, (2u << 13) | (2u << 15),
                                        (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    const std::vector<unsigned> plainPs{0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    for (unsigned hdrFormat: {0x823u, 0x816u})
        for (unsigned type: {0u, 1u, 4u, 9u}) {
            SCOPED_TRACE(hdrFormat);
            surface(HDR, HDR_IMAGE, (hdrFormat == 0x816 ? 2 : 1) * 6 * 2 * colorElementBytes(colorFormat(hdrFormat)), 4, 2, 6, 1);
            if (hdrFormat == 0x816) {
                cpu->m_memory.write<unsigned>(HDR, 5);
                cpu->m_memory.write<unsigned>(HDR + 0xC, 2);
                cpu->m_memory.write<unsigned>(HDR + 0x7C, 1);
            }
            cpu->m_memory.write<unsigned>(HDR + 0x14, hdrFormat);
            cpu->m_memory.write<unsigned>(HDR + 0x10, 1);
            cpu->m_memory.write<unsigned>(HDR + 0x80, 1);
            cpu->m_memory.write<unsigned>(HDR + 0x84, 0x00010203);
            SCOPED_TRACE(type);
            const unsigned bytes = type == 1 || type == 9 ? 4 : 2;
            const std::array<unsigned, 3> order{0, 0, 1};
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned b = 0; b < bytes; ++b)
                    cpu->m_memory.write<std::uint8_t>(INDEX + i * bytes + b, order[i] >> ((type < 4 ? b : bytes - 1 - b) * 8));
            shader(VS, 0xD0, VSP, plainVs);
            shader(PS, 0xA4, PSP, plainPs);
            gx2Stream().clear();
            for (unsigned c = 1; c <= 4; ++c)
                cpu->m_fpr[c] = 0;
            record("GX2ClearColor", {HDR});
            record("GX2ClearColor", {OUT});
            cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
            cpu->m_fpr[3] = 4;
            cpu->m_fpr[4] = 2;
            cpu->m_fpr[5] = 0;
            cpu->m_fpr[6] = 1;
            record("GX2SetViewport", {});
            record("GX2SetScissor", {0, 0, 4, 2});
            record("GX2SetFetchShader", {FETCH});
            record("GX2SetVertexShader", {VS});
            record("GX2SetPixelShader", {PS});
            record("GX2SetAttribBuffer", {0, 48, 24, VERTS});
            record("GX2SetColorBuffer", {HDR, 0});
            record("GX2SetColorControl", {0xCC, 1, 0, 1});
            record("GX2SetBlendControl", {0, 1, 1, 0, 1, 1, 1, 0});
            record("GX2DrawIndexedEx", {1, 3, type, INDEX, 0, 1});
            auto first = gx2Stream(); // preserve first program; replay second after swapping bytecode
            Gx2Replayer reference;
            reference.replay(first, &cpu->m_memory);
            std::shared_ptr<VulkanRasterBackend> backend;
            Gx2Replayer native;
            if (std::getenv("WEMU_TEST_VULKAN")) {
                backend = std::make_shared<VulkanRasterBackend>(true, true);
                native.setRasterBackend(backend);
                native.setNativeVerificationInterval(1);
                native.replay(first, &cpu->m_memory);
                ASSERT_EQ(backend->completedDraws(), 1u) << backend->lastError();
            }
            // Sample the actual float target in the vertex shader; scale before UNORM
            // conversion so clamping the intermediate would produce a different pixel.
            const std::vector<unsigned> texturedVs{3,
                                                   1u << 23,
                                                   60u | (1u << 13) | (1u << 15),
                                                   (Latte::CF_EXP << 23) | 0x688u,
                                                   (2u << 13) | (3u << 15),
                                                   (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                                   0x13u | (2u << 16),
                                                   0x30000000u | 3u | (1u << 12) | (2u << 15) | (3u << 18),
                                                   1u << 23,
                                                   0};
            std::vector<unsigned> scaledPs{2, (8u << 26) | (3u << 18), 0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
            for (unsigned c = 0; c < 4; ++c) {
                scaledPs.push_back((c << 10) | (128u << 13) | (c << 23) | (c == 3 ? 0x80000000u : 0));
                scaledPs.push_back((Latte::OP2_MUL << 7) | 16u | (c << 29));
            }
            for (unsigned off = 0; off < 0x200; off += 4) {
                cpu->m_memory.write<unsigned>(0x2800D000 + off, cpu->m_memory.read<unsigned>(VS + off));
                cpu->m_memory.write<unsigned>(0x2800E000 + off, cpu->m_memory.read<unsigned>(PS + off));
            }
            shader(0x2800D000, 0xD0, 0x2800F000, texturedVs);
            shader(0x2800E000, 0xA4, 0x28012000, scaledPs);
            for (unsigned v = 0; v < 2; ++v) {
                cpu->m_memory.write<unsigned>(VERTS + v * 24 + 12, std::bit_cast<unsigned>(v ? .875f : .375f));
                cpu->m_memory.write<unsigned>(VERTS + v * 24 + 16, std::bit_cast<unsigned>(v ? .75f : .25f));
            }
            gx2Stream().clear();
            record("GX2SetVertexShader", {0x2800D000});
            record("GX2SetPixelShader", {0x2800E000});
            for (unsigned off = 0; off < 0x9C; off += 4)
                cpu->m_memory.write<unsigned>(0x28016000 + off, cpu->m_memory.read<unsigned>(HDR + off));
            cpu->m_memory.write<unsigned>(0x28016000 + 0x7C, 0);
            record("GX2SetVertexTexture", {0x28016000, 0});
            record("GX2InitSampler", {0x2800B000, 2, 0});
            record("GX2SetVertexSampler", {0x2800B000, 0});
            record("GX2SetColorBuffer", {OUT, 0});
            record("GX2SetColorControl", {0xCC, 0, 0, 1});
            for (unsigned c = 0; c < 4; ++c)
                cpu->m_memory.write<unsigned>(0x2800C000 + c * 4, std::bit_cast<unsigned>(.125f));
            for (unsigned i = 0; i < 96; ++i)
                for (unsigned b = 0; b < bytes; ++b)
                    cpu->m_memory.write<std::uint8_t>(INDEX + i * bytes + b, (i == 95 ? 1u : 0u) >> ((type < 4 ? b : bytes - 1 - b) * 8));
            record("GX2SetPixelUniformReg", {0, 4, 0x2800C000});
            record("GX2DrawIndexedEx", {1, 96, type, INDEX, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {OUT, 1});
            reference.replay(gx2Stream(), &cpu->m_memory);
            const auto offset = 4;
            const auto &fb = reference.framebuffer();
            EXPECT_EQ((std::array{fb[offset], fb[offset + 1], fb[offset + 2], fb[offset + 3]}),
                      (std::array<std::uint8_t, 4>{128, 0, 16, static_cast<std::uint8_t>(hdrFormat == 0x816 ? 32 : 64)}));
            if (backend) {
                native.replay(gx2Stream(), &cpu->m_memory);
                // An array vertex texture whose storage does not overlap the target stays native.
                EXPECT_EQ(backend->completedDraws(), 2u) << backend->lastError();
                EXPECT_EQ(native.comparedNativeDraws(), 2u);
                EXPECT_EQ(native.differingNativeDraws(), 0u);
                EXPECT_EQ(native.framebuffer(), reference.framebuffer());
            }
            // Reference/native must read captured bank 3 bytes, not registers or
            // the later contents of the reused guest buffer.
            auto blockPs = scaledPs;
            blockPs[0] |= (3u << 22) | (1u << 30);
            cpu->m_memory.write<unsigned>(0x28013010, 1);
            cpu->m_memory.write<unsigned>(0x28013014, 7);
            shader(0x28013000, 0xA4, 0x28014000, blockPs);
            for (unsigned c = 0; c < 4; ++c)
                cpu->m_memory.write<unsigned>(0x28015000 + c * 4, std::byteswap(std::bit_cast<unsigned>(.25f)));
            gx2Stream().clear();
            record("GX2SetPixelShader", {0x28013000});
            record("GX2SetPixelUniformBlock", {3, 16, 0x28015000});
            for (unsigned c = 0; c < 4; ++c)
                cpu->m_memory.write<unsigned>(0x28015000 + c * 4, 0);
            record("GX2DrawIndexedEx", {1, 96, type, INDEX, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {OUT, 1});
            reference.replay(gx2Stream(), &cpu->m_memory);
            const auto &blockFb = reference.framebuffer();
            EXPECT_EQ((std::array{blockFb[offset], blockFb[offset + 1], blockFb[offset + 2], blockFb[offset + 3]}),
                      (std::array<std::uint8_t, 4>{255, 0, 32, static_cast<std::uint8_t>(hdrFormat == 0x816 ? 64 : 128)}));
            if (backend) {
                native.replay(gx2Stream(), &cpu->m_memory);
                EXPECT_EQ(backend->completedDraws(), 3u) << backend->lastError();
                EXPECT_EQ(native.comparedNativeDraws(), 3u);
                EXPECT_EQ(native.differingNativeDraws(), 0u);
                EXPECT_EQ(native.framebuffer(), reference.framebuffer());
            }
            // Restore color attributes before the next independent first pass.
            for (unsigned v = 0; v < 2; ++v)
                for (unsigned c = 3; c < 6; ++c)
                    cpu->m_memory.write<unsigned>(VERTS + v * 24 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
        }
}

TEST_F(Gx2CaptureTest, RenderedHdrArraySamplingPreservesLayersMappingAndChangedVersions)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28001000, PS = 0x28002000, VSP = 0x28003000, PSP = 0x28004000;
    constexpr unsigned HDR = 0x28005000, TEX = 0x28006000, OUT = 0x28007000, VERTS = 0x28010000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    const auto shader = [&](unsigned header, unsigned offset, unsigned pointer, const std::vector<unsigned> &words) {
        for (unsigned i = 0; i < 0x200; i += 4)
            cpu->m_memory.write<unsigned>(header + i, 0);
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    };
    shader(VS, 0xD0, VSP,
           {60u | (1u << 13) | (1u << 15), (Latte::CF_EXP << 23) | 0x688u, 2u << 13 | (2u << 15), (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u});
    cpu->m_memory.write<unsigned>(VS + 0x40, 2);
    cpu->m_memory.write<unsigned>(VS + 0x44, 0);
    cpu->m_memory.write<unsigned>(VS + 0x48, 1);
    cpu->m_memory.write<unsigned>(VS + 0xC, 1);
    cpu->m_memory.write<unsigned>(VS + 0x10, 0xFFFFFF07);
    std::vector<unsigned> ps{3,          Latte::CF_TEX << 23,
                             5,          (8u << 26) | (3u << 18),
                             1u << 15,   (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                             0x13,       1u | 0x30000000u | (0x688u << 9),
                             0x68800000, 0};
    for (unsigned c = 0; c < 4; ++c) {
        ps.push_back(1u | (c << 10) | (128u << 13) | (c << 23) | (c == 3 ? 0x80000000u : 0u));
        ps.push_back((Latte::OP2_MUL << 7) | 16u | (1u << 21) | (c << 29));
    }
    shader(PS, 0xA4, PSP, ps);
    cpu->m_memory.write<unsigned>(PS + 0x10, 1);
    cpu->m_memory.write<unsigned>(PS + 0x14, 7);
    for (unsigned format: {0x816u, 0x823u}) {
        SCOPED_TRACE(format);
        gx2Stream().clear();
        surface(HDR, 0x28040000, 3 * 4 * 2 * colorElementBytes(colorFormat(format)), 2, 2, 4, 1);
        cpu->m_memory.write<unsigned>(HDR, 5);
        cpu->m_memory.write<unsigned>(HDR + 0xC, 3);
        cpu->m_memory.write<unsigned>(HDR + 0x10, 1);
        cpu->m_memory.write<unsigned>(HDR + 0x14, format);
        cpu->m_memory.write<unsigned>(HDR + 0x7C, 1);
        for (unsigned layer = 0; layer < 3; ++layer) {
            cpu->m_memory.write<unsigned>(HDR + 0x78, layer);
            cpu->m_fpr[1] = double(1u << layer);
            cpu->m_fpr[2] = double(2u << layer);
            cpu->m_fpr[3] = double(4u << layer);
            cpu->m_fpr[4] = 1;
            record("GX2ClearColor", {HDR});
        }
        for (unsigned off = 0; off < 0x74; off += 4)
            cpu->m_memory.write<unsigned>(TEX + off, cpu->m_memory.read<unsigned>(HDR + off));
        cpu->m_memory.write<unsigned>(TEX + 0x74, 0);
        cpu->m_memory.write<unsigned>(TEX + 0x78, 1);
        cpu->m_memory.write<unsigned>(TEX + 0x7C, 1);
        cpu->m_memory.write<unsigned>(TEX + 0x80, 2);
        cpu->m_memory.write<unsigned>(TEX + 0x84, 0x02010005); // B,G,R,1
        surface(OUT, 0x28050000, 4 * 4 * 4, 4, 4, 4, 1);
        cpu->m_fpr[1] = cpu->m_fpr[2] = cpu->m_fpr[5] = 0;
        cpu->m_fpr[3] = cpu->m_fpr[4] = 4;
        cpu->m_fpr[6] = 1;
        record("GX2SetViewport", {});
        record("GX2SetScissor", {0, 0, 4, 4});
        Gx2Command fetch{Gx2Cmd::SetFetchShader};
        fetch.payload = {0, 0, 0, 0x813, 0, 0, 0x00010203, 3, 1, 0, 16, 0x813, 0, 0, 0x00010203, 3};
        gx2Stream().push(fetch);
        record("GX2SetVertexShader", {VS});
        record("GX2SetPixelShader", {PS});
        record("GX2SetAttribBuffer", {0, 3 * 32, 32, VERTS});
        record("GX2SetColorBuffer", {OUT, 0});
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        record("GX2InitSampler", {0x28008000, 2, 0});
        record("GX2SetPixelSampler", {0x28008000, 0});
        record("GX2SetPixelTexture", {TEX, 0});
        for (unsigned c = 0; c < 4; ++c)
            cpu->m_memory.write<unsigned>(0x28009000 + c * 4, std::bit_cast<unsigned>(1.f / 17));
        record("GX2SetPixelUniformReg", {0, 4, 0x28009000});
        Gx2Replayer reference, native;
        std::shared_ptr<VulkanRasterBackend> backend;
        if (std::getenv("WEMU_TEST_VULKAN")) {
            backend = std::make_shared<VulkanRasterBackend>(true, true);
            native.setRasterBackend(backend);
            native.setNativeVerificationInterval(1);
        }
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        for (unsigned pass = 0; pass < 20; ++pass) {
            SCOPED_TRACE(pass);
            gx2Stream().clear();
            if (pass == 1)
                record("GX2SetTargetChannelMasks", {7, 0, 0, 0, 0, 0, 0, 0}); // A different pipeline shares the same snapshot upload.
            if (pass == 2) {
                // A clear changes canonical rendered data without changing guest RAM.
                cpu->m_memory.write<unsigned>(HDR + 0x78, 2);
                cpu->m_fpr[1] = 1;
                cpu->m_fpr[2] = 2;
                cpu->m_fpr[3] = 4;
                cpu->m_fpr[4] = 1;
                record("GX2ClearColor", {HDR});
            }
            if (pass >= 3) {
                const auto image = pass == 19 ? 0x28040000u : 0x28080000u + (pass - 3) * 0x1000u;
                if (pass != 19) {
                    cpu->m_memory.write<unsigned>(HDR + 0x24, image);
                    record("GX2ClearColor", {HDR});
                }
                cpu->m_memory.write<unsigned>(TEX + 0x24, image);
                record("GX2SetPixelTexture", {TEX, 0});
            }
            const float positions[3][2]{{-1, 1}, {3, 1}, {-1, -3}};
            for (unsigned v = 0; v < 3; ++v) {
                const std::array data{positions[v][0], positions[v][1], 0.f, 1.f, .5f, .5f, float(pass != 0), 1.f};
                for (unsigned c = 0; c < 8; ++c)
                    cpu->m_memory.write<unsigned>(VERTS + v * 32 + c * 4, std::bit_cast<unsigned>(data[c]));
            }
            record("GX2DrawEx", {4, 3, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {OUT, 1});
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            const std::array<std::uint8_t, 4> expected = pass == 0   ? std::array<std::uint8_t, 4>{120, 60, 30, 15}
                                                         : pass == 1 ? std::array<std::uint8_t, 4>{240, 120, 60, 15}
                                                                     : std::array<std::uint8_t, 4>{60, 30, 15, 15};
            EXPECT_EQ((std::array{reference.framebuffer()[0], reference.framebuffer()[1], reference.framebuffer()[2], reference.framebuffer()[3]}),
                      expected);
            EXPECT_EQ(native.framebuffer(), reference.framebuffer());
            if (backend) {
                EXPECT_EQ(backend->completedDraws(), pass + 1u) << backend->lastError();
                EXPECT_EQ(native.differingNativeDraws(), 0u);
                // Sixteen intervening views evict the first strong mip snapshot;
                // the final draw must recover the still-uploaded immutable owner.
                EXPECT_EQ(backend->sharedTextureUploads(), pass < 2 ? 1u : pass < 19 ? pass : 18u);
                if (pass)
                    EXPECT_GE(backend->sharedTextureHits(), 1u);
            }
        }
    }
}

TEST_F(Gx2CaptureTest, ScanoutClearsOnlyMarginsAndInvalidSourcesDoNotRetainPreviousPixels)
{
    using namespace Core::Gfx;
    constexpr std::uint32_t header = 0x28001000, image = 0x28100000;
    auto *pixels = cpu->m_memory.hostPtr(image);
    ASSERT_NE(pixels, nullptr);
    const auto bytes = Gx2Replayer::kWidth * Gx2Replayer::kHeight * 4;
    std::fill_n(pixels, bytes, 73);
    Gx2Replayer replayer;
    const auto present = [&](unsigned width, unsigned height, unsigned format, std::uint32_t address) {
        gx2Stream().clear();
        surface(header, address, bytes, width, height, width, 1);
        cpu->m_memory.write<std::uint32_t>(header + 0x14, format);
        gx2Stream().push(Gx2Command{Gx2Cmd::ClearColor, {}, {0.25, 0.5, 0.75, 1}});
        cpu->m_gpr[3] = header;
        cpu->m_gpr[4] = 1;
        call("GX2CopyColorBufferToScanBuffer");
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        replayer.replay(gx2Stream(), &cpu->m_memory);
    };
    present(Gx2Replayer::kWidth, Gx2Replayer::kHeight, 0x1A, image);
    ASSERT_EQ(replayer.framebuffer().size(), bytes);
    EXPECT_EQ(replayer.framebuffer().front(), 73);
    EXPECT_EQ(replayer.framebuffer().back(), 73);
    for (const auto format: {0x1Au, 0x823u}) {
        if (format == 0x823)
            for (unsigned i = 0; i < 2 * 3; ++i)
                writeFloatColor(pixels + i * 16, {.2f, .4f, .6f, 1.f}, 15);
        present(2, 3, format, image);
        const auto &frame = replayer.framebuffer();
        const auto pixel = [&](unsigned x, unsigned y) {
            const auto index = (y * Gx2Replayer::kWidth + x) * 4;
            return std::array{frame[index], frame[index + 1], frame[index + 2], frame[index + 3]};
        };
        EXPECT_EQ(pixel(0, 0), (format == 0x1A ? std::array<std::uint8_t, 4>{73, 73, 73, 73} : std::array<std::uint8_t, 4>{51, 102, 153, 255}));
        for (const auto xy: {std::array{2u, 0u}, std::array{0u, 3u}, std::array{1279u, 719u}})
            EXPECT_EQ(pixel(xy[0], xy[1]), (std::array<std::uint8_t, 4>{64, 128, 191, 255}));
    }
    present(2, 3, 0x1A, 0); // Invalid scan source still produces the complete clear frame.
    EXPECT_EQ(replayer.framebuffer()[0], 64);
    EXPECT_EQ(replayer.framebuffer().back(), 255);
}

TEST_F(Gx2CaptureTest, GamePadScanoutDecodesFloatColorTargets)
{
    using namespace Core::Gfx;
    constexpr std::uint32_t header = 0x28001000, image = 0x28100000;
    auto *pixels = cpu->m_memory.hostPtr(image);
    ASSERT_NE(pixels, nullptr);
    Gx2Replayer replayer;
    replayer.setDrcCapture(true);
    // 2x2 sources: the left column and the right column are scaled to each half of the panel.
    for (const auto format: {0x1Au, 0x823u, 0x816u}) {
        const auto elementBytes = colorElementBytes(colorFormat(format));
        for (unsigned i = 0; i < 4; ++i) {
            const std::array<float, 4> value = i % 2 ? std::array{1.f, .5f, .25f, 1.f} : std::array{.25f, .5f, 1.f, 1.f};
            if (format == 0x1A)
                for (unsigned c = 0; c < 4; ++c)
                    pixels[i * 4 + c] = std::uint8_t(std::lround(value[c] * 255));
            else
                writeFloatColor(pixels + i * elementBytes, value, 15, colorFormat(format));
        }
        gx2Stream().clear();
        surface(header, image, 4 * elementBytes, 2, 2, 2, 1);
        cpu->m_memory.write<std::uint32_t>(header + 0x14, format);
        cpu->m_gpr[3] = header;
        cpu->m_gpr[4] = 4;
        call("GX2CopyColorBufferToScanBuffer");
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        replayer.replay(gx2Stream(), &cpu->m_memory);
        const auto &frame = replayer.drcFramebuffer();
        ASSERT_EQ(frame.size(), std::size_t(Gx2Replayer::kDrcWidth) * Gx2Replayer::kDrcHeight * 4);
        const auto pixel = [&](unsigned x, unsigned y) {
            const auto index = (y * Gx2Replayer::kDrcWidth + x) * 4;
            return std::array{frame[index], frame[index + 1], frame[index + 2], frame[index + 3]};
        };
        EXPECT_EQ(pixel(0, 0), (std::array<std::uint8_t, 4>{64, 128, 255, 255})) << std::hex << format;
        EXPECT_EQ(pixel(Gx2Replayer::kDrcWidth - 1, Gx2Replayer::kDrcHeight - 1), (std::array<std::uint8_t, 4>{255, 128, 64, 255}))
                << std::hex << format;
    }
}

TEST_F(Gx2CaptureTest, HighVertexSemanticsSurviveContextReload)
{
    using namespace Core::Gfx;
    RegisterLibcFunctions();
    for (unsigned burst: {0u, 7u})
        for (unsigned dimension: {5u, 3u}) {
            SCOPED_TRACE(burst);
            SCOPED_TRACE(dimension);
            gx2Stream().clear();
            constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
            constexpr unsigned FETCH = 0x28001000, ATTRS = 0x28003000, VERTS = 0x28010000;
            constexpr unsigned COLOR = 0x28009000, IMAGE = 0x28040000;
            for (unsigned i = 0; i < 0x200; i += 4) {
                cpu->m_memory.write<unsigned>(VS + i, 0);
                cpu->m_memory.write<unsigned>(PS + i, 0);
            }
            cpu->m_memory.write<unsigned>(VS + 0x40, 9);
            for (unsigned i = 0; i < 9; ++i)
                cpu->m_memory.write<unsigned>(VS + 0x44 + i * 4, i);
            cpu->m_memory.write<unsigned>(VS + 0xC, 3);
            cpu->m_memory.write<unsigned>(VS + 0x10, 0xFFFFFFFF);
            cpu->m_memory.write<unsigned>(VS + 0x14, 0xFFFFFFFF);
            cpu->m_memory.write<unsigned>(VS + 0x18, 0xFFFFFF07);
            cpu->m_memory.write<unsigned>(PS + 0x10, 8);
            for (unsigned i = 0; i < 8; ++i)
                cpu->m_memory.write<unsigned>(PS + 0x14 + i * 4, i == 7 ? 7 : 255);
            const std::array<unsigned, 4> vertex{60u | (1u << 13) | (9u << 15), (0x28u << 23) | 0x688u, 8u | (2u << 13) | (5u << 15),
                                                 (0x28u << 23) | (1u << 21) | 0x688u};
            const std::array<unsigned, 2> pixel{7u << 15, (0x28u << 23) | (1u << 21) | (burst << 17) | 0x688u};
            const auto shader = [&](unsigned header, unsigned offset, unsigned ptr, const auto &words) {
                cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
                cpu->m_memory.write<unsigned>(header + offset + 4, ptr);
                for (unsigned i = 0; i < words.size(); ++i)
                    cpu->m_memory.write<unsigned>(ptr + i * 4, std::byteswap(words[i]));
            };
            shader(VS, 0xD0, VSP, vertex);
            shader(PS, 0xA4, PSP, pixel);
            const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
                unsigned r = 3;
                for (auto arg: args)
                    cpu->m_gpr[r++] = arg;
                call(name);
            };
            // Only R9 contains a position; R5 supplies the red parameter export.
            const std::array<unsigned, 16> attrs{8, 0, 0, 0x813, 0, 0, 0x00010203, 3, 4, 0, 16, 0x813, 0, 0, 0x00010203, 3};
            for (unsigned i = 0; i < attrs.size(); ++i)
                cpu->m_memory.write<unsigned>(ATTRS + i * 4, attrs[i]);
            record("GX2InitFetchShaderEx", {FETCH, 0x28002000, 2, ATTRS, 0, 0});
            const std::array<std::array<float, 8>, 3> vertices{{{-1, 1, 0, 1, 1, 0, 0, 1}, {3, 1, 0, 1, 1, 0, 0, 1}, {-1, -3, 0, 1, 1, 0, 0, 1}}};
            for (unsigned v = 0; v < vertices.size(); ++v)
                for (unsigned c = 0; c < 8; ++c)
                    cpu->m_memory.write<unsigned>(VERTS + v * 32 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
            surface(COLOR, IMAGE, (dimension == 3 ? 6 : 2) * 8 * 8 * 4, 8, 8, 8, 1);
            cpu->m_memory.write<unsigned>(COLOR, dimension);
            cpu->m_memory.write<unsigned>(COLOR + 0xC, dimension == 3 ? 6 : 2);
            cpu->m_memory.write<unsigned>(COLOR + 0x78, 1);
            cpu->m_memory.write<unsigned>(COLOR + 0x7C, 1);
            record("GX2SetContextState", {0x2800B000});
            record("GX2SetColorBuffer", {COLOR, 0});
            if (dimension == 3) {
                cpu->m_memory.write<unsigned>(0x28012000, 0xCCu << 16);
                record("GX2SetColorControlReg", {0x28012000});
                cpu->m_memory.write<unsigned>(0x28012000, 1u << 4);
            } else
                record("GX2SetColorControl", {0xCC, 0, 0, 1});
            surface(0x2800F000, 0x28060000, 4 * 4 * 4, 4, 4, 4, 1);
            cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
            cpu->m_fpr[3] = cpu->m_fpr[4] = 1;
            record("GX2ClearColor", {0x2800F000});
            record("GX2SetColorBuffer", {0x2800F000, 1});
            if (dimension == 3) {
                cpu->m_memory.write<unsigned>(0x28012004, 15);
                record("GX2SetTargetChannelMasksReg", {0x28012004});
                cpu->m_memory.write<unsigned>(0x28012004, 0xF0);
            } else
                record("GX2SetTargetChannelMasks", {15, 0, 0, 0, 0, 0, 0, 0});
            cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
            cpu->m_fpr[3] = cpu->m_fpr[4] = 8;
            record("GX2SetViewport", {});
            record("GX2SetFetchShader", {FETCH});
            record("GX2SetVertexShader", {VS});
            record("GX2SetPixelShader", {PS});
            record("GX2SetAttribBuffer", {0, 96, 32, VERTS});
            record("GX2SetContextState", {0x2800C000});
            record("GX2SetColorControl", {0xCC, 0, 1, 1});
            record("GX2SetAttribBuffer", {0, 0, 0, 0});
            for (unsigned i = 0; i < 0x200; i += 4) {
                cpu->m_memory.write<unsigned>(0x2800D000 + i, cpu->m_memory.read<unsigned>(VS + i));
                cpu->m_memory.write<unsigned>(0x2800E000 + i, cpu->m_memory.read<unsigned>(PS + i));
            }
            cpu->m_memory.write<unsigned>(0x2800D000 + 0xC, 0);
            cpu->m_memory.write<unsigned>(0x2800E000 + 0x10, 1);
            cpu->m_memory.write<unsigned>(0x2800E000 + 0x14, 7);
            record("GX2SetVertexShader", {0x2800D000});
            record("GX2SetPixelShader", {0x2800E000});
            record("GX2SetContextState", {0x2800B000});
            record("GX2BeginDisplayListEx", {0x2801A000, 256, 0});
            record("GX2DrawEx", {4, 3, 0, 1});
            record("GX2EndDisplayList", {0x2801A000});
            const auto bytes = cpu->m_gpr[3];
            record("memcpy", {0x2801B000, 0x2801A000, bytes});
            record("GX2CallDisplayList", {0x2801B000, bytes});
            record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
            gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
            Gx2Replayer replay;
            replay.setRasterWorkerCount(1);
            replay.replay(gx2Stream(), &cpu->m_memory);
            if (std::getenv("WEMU_TEST_VULKAN")) {
                auto backend = std::make_shared<VulkanRasterBackend>(true, true);
                Gx2Replayer native;
                native.setRasterBackend(backend);
                native.setNativeVerificationInterval(1);
                native.replay(gx2Stream(), &cpu->m_memory);
                EXPECT_EQ(backend->completedDraws(), 1u) << backend->lastError();
                EXPECT_EQ(native.differingNativeDraws(), 0u);
                EXPECT_EQ(native.framebuffer(), replay.framebuffer());
                gx2Stream().clear();
                record("GX2CopyColorBufferToScanBuffer", {0x2800F000, 1});
                native.replay(gx2Stream(), &cpu->m_memory);
                EXPECT_EQ((std::array{native.framebuffer()[0], native.framebuffer()[1], native.framebuffer()[2], native.framebuffer()[3]}),
                          (std::array<std::uint8_t, 4>{0, 0, 255, 255}));
            }
            const auto &image = replay.framebuffer();
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x) {
                    const auto i = (y * Gx2Replayer::kWidth + x) * 4;
                    EXPECT_EQ(image[i], 255u);
                    EXPECT_EQ(image[i + 1], 0u);
                    EXPECT_EQ(image[i + 2], 0u);
                    EXPECT_EQ(image[i + 3], 255u);
                }
            gx2Stream().clear();
            record("GX2CopyColorBufferToScanBuffer", {0x2800F000, 1});
            replay.replay(gx2Stream(), &cpu->m_memory);
            EXPECT_EQ((std::array{replay.framebuffer()[0], replay.framebuffer()[1], replay.framebuffer()[2], replay.framebuffer()[3]}),
                      (std::array<std::uint8_t, 4>{0, 0, 255, 255}));
        }
}

TEST_F(Gx2CaptureTest, DirectVertexArraysAreReusedUntilGuestVertexBytesChange)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
    constexpr unsigned FETCH = 0x28001000, ATTRS = 0x28003000, VERTS = 0x28010000, INDEX = 0x28011000;
    constexpr unsigned COLOR = 0x28009000, IMAGE = 0x28040000, UNIFORM = 0x28012000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    for (unsigned i = 0; i < 0x200; i += 4) {
        cpu->m_memory.write<unsigned>(VS + i, 0);
        cpu->m_memory.write<unsigned>(PS + i, 0);
    }
    cpu->m_memory.write<unsigned>(VS + 0x40, 1);
    cpu->m_memory.write<unsigned>(VS + 0x44, 0);
    cpu->m_memory.write<unsigned>(PS + 0x10, 1);
    // Position from attribute 0, parameter 0 from c0.
    std::vector<unsigned> vertex{3,
                                 (8u << 26) | (3u << 18),
                                 60u | (1u << 13) | (1u << 15),
                                 (Latte::CF_EXP << 23) | 0x688u,
                                 (2u << 13) | (2u << 15),
                                 (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    for (unsigned c = 0; c < 4; ++c) {
        vertex.push_back(256u | (c << 10) | (c == 3 ? 0x80000000u : 0u));
        vertex.push_back((Latte::OP2_MOV << 7) | 16u | (2u << 21) | (c << 29));
    }
    const std::vector<unsigned> pixel{0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    for (const auto &[header, offset, pointer, words]: {std::tuple{VS, 0xD0u, VSP, vertex}, std::tuple{PS, 0xA4u, PSP, pixel}}) {
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    }
    const std::array<unsigned, 8> attribute{0, 0, 0, 0x813, 0, 0, 0x00010203, 3};
    for (unsigned i = 0; i < attribute.size(); ++i)
        cpu->m_memory.write<unsigned>(ATTRS + i * 4, attribute[i]);
    record("GX2InitFetchShaderEx", {FETCH, 0x28002000, 1, ATTRS, 0, 0});
    const auto writeQuad = [&](float right) {
        const std::array<std::array<float, 4>, 4> vertices{{{-1, 1, 0, 1}, {right, 1, 0, 1}, {-1, -1, 0, 1}, {right, -1, 0, 1}}};
        for (unsigned v = 0; v < vertices.size(); ++v)
            for (unsigned c = 0; c < 4; ++c)
                cpu->m_memory.write<unsigned>(VERTS + v * 16 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
    };
    // Enough indices for the conversion cache: the quad repeated twelve times.
    constexpr unsigned count = 72;
    const std::array<unsigned, 6> quad{0, 1, 2, 2, 1, 3};
    for (unsigned i = 0; i < count; ++i)
        cpu->m_memory.write<std::uint16_t>(INDEX + i * 2, quad[i % quad.size()]);
    surface(COLOR, IMAGE, 4 * 4 * 4, 4, 4, 4, 1);
    for (unsigned offset: {0x74u, 0x78u, 0x7Cu})
        cpu->m_memory.write<unsigned>(COLOR + offset, 0);
    for (unsigned c = 0; c < 4; ++c)
        cpu->m_memory.write<unsigned>(UNIFORM + c * 4, std::bit_cast<unsigned>(c == 1 || c == 3 ? 1.f : 0.f));
    Gx2Replayer reference, native;
    const auto backend = std::make_shared<VulkanRasterBackend>(true, true);
    native.setRasterBackend(backend);
    native.setNativeVerificationInterval(0);
    const auto frame = [&] {
        gx2Stream().clear();
        cpu->m_fpr[1] = cpu->m_fpr[2] = cpu->m_fpr[3] = cpu->m_fpr[4] = 0;
        record("GX2ClearColor", {COLOR});
        record("GX2SetFetchShader", {FETCH});
        record("GX2SetVertexShader", {VS});
        record("GX2SetPixelShader", {PS});
        record("GX2SetAttribBuffer", {0, 64, 16, VERTS});
        record("GX2SetColorBuffer", {COLOR, 0});
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        cpu->m_fpr[3] = cpu->m_fpr[4] = 4;
        record("GX2SetViewport", {});
        record("GX2SetVertexUniformReg", {0, 4, UNIFORM});
        record("GX2DrawIndexedEx", {4, count, 4, INDEX, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
    };
    // Only deferred direct rendering keeps the arrays on the GPU between draws.
    const auto *deferred = std::getenv("WEMU_NATIVE_DEFER_READBACK");
    const unsigned resident = deferred && std::string_view(deferred) == "1" ? 1 : 0;
    writeQuad(1);
    frame();
    ASSERT_EQ(backend->completedDraws(), 1u) << backend->lastError();
    EXPECT_EQ(native.framebuffer(), reference.framebuffer());
    // A draw is cached from its second sighting and moves to the GPU once reused.
    frame();
    EXPECT_EQ(native.directVertexReuses(), 0u);
    frame();
    EXPECT_EQ(native.directVertexReuses(), 1u);
    EXPECT_EQ(backend->residentVertexHits(), 0u);
    frame();
    EXPECT_EQ(native.directVertexReuses(), 2u);
    EXPECT_EQ(backend->residentVertexHits(), resident);
    EXPECT_EQ(native.framebuffer(), reference.framebuffer());
    // Narrowing the quad in guest memory must reach the GPU despite unchanged indices.
    writeQuad(0);
    frame();
    EXPECT_EQ(backend->completedDraws(), 5u) << backend->lastError();
    EXPECT_EQ(native.directVertexReuses(), 2u);
    EXPECT_EQ(backend->residentVertexHits(), resident);
    EXPECT_EQ(native.framebuffer(), reference.framebuffer());
    const auto &fb = native.framebuffer();
    EXPECT_NE(std::vector(fb.begin(), fb.begin() + 4), std::vector(fb.end() - 4, fb.end()));
}

TEST_F(Gx2CaptureTest, IndexedVertexReusePreservesParametersAndRefreshesEachDraw)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
    constexpr unsigned FETCH = 0x28001000, ATTRS = 0x28003000, VERTS = 0x28010000, INDEX = 0x28011000;
    constexpr unsigned COLOR = 0x28009000, IMAGE = 0x28040000, UNIFORM = 0x28012000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    for (unsigned i = 0; i < 0x200; i += 4) {
        cpu->m_memory.write<unsigned>(VS + i, 0);
        cpu->m_memory.write<unsigned>(PS + i, 0);
    }
    cpu->m_memory.write<unsigned>(VS + 0x40, 1);
    cpu->m_memory.write<unsigned>(VS + 0x44, 0);
    cpu->m_memory.write<unsigned>(PS + 0x10, 1);
    std::vector<unsigned> vertex{3,
                                 (8u << 26) | (3u << 18),
                                 60u | (1u << 13) | (1u << 15),
                                 (Latte::CF_EXP << 23) | 0x688u,
                                 (2u << 13) | (2u << 15),
                                 (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    for (unsigned c = 0; c < 4; ++c) {
        vertex.push_back(256u | (c << 10) | (c == 3 ? 0x80000000u : 0u));
        vertex.push_back((Latte::OP2_MOV << 7) | 16u | (2u << 21) | (c << 29));
    }
    const std::vector<unsigned> pixel{0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u};
    for (const auto &[header, offset, pointer, words]: {std::tuple{VS, 0xD0u, VSP, vertex}, std::tuple{PS, 0xA4u, PSP, pixel}}) {
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    }
    const std::array<unsigned, 8> attribute{0, 0, 0, 0x813, 0, 0, 0x00010203, 3};
    for (unsigned i = 0; i < attribute.size(); ++i)
        cpu->m_memory.write<unsigned>(ATTRS + i * 4, attribute[i]);
    record("GX2InitFetchShaderEx", {FETCH, 0x28002000, 1, ATTRS, 0, 0});
    const std::array<std::array<float, 4>, 4> vertices{{{-1, 1, 0, 1}, {1, 1, 0, 1}, {-1, -1, 0, 1}, {1, -1, 0, 1}}};
    for (unsigned v = 0; v < vertices.size(); ++v)
        for (unsigned c = 0; c < 4; ++c)
            cpu->m_memory.write<unsigned>(VERTS + v * 16 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
    const std::array<unsigned, 6> indices{0, 1, 2, 2, 1, 3};
    for (unsigned i = 0; i < indices.size(); ++i)
        cpu->m_memory.write<std::uint16_t>(INDEX + i * 2, indices[i]);
    surface(COLOR, IMAGE, 4 * 4 * 4, 4, 4, 4, 1);
    for (unsigned offset: {0x74u, 0x78u, 0x7Cu})
        cpu->m_memory.write<unsigned>(COLOR + offset, 0);
    Gx2Replayer cached, uncached;
    cached.setRasterWorkerCount(1);
    uncached.setRasterWorkerCount(1);
    uncached.setVertexCacheEnabled(false);
    for (unsigned pass = 0; pass < 2; ++pass) {
        gx2Stream().clear();
        record("GX2SetFetchShader", {FETCH});
        record("GX2SetVertexShader", {VS});
        record("GX2SetPixelShader", {PS});
        record("GX2SetAttribBuffer", {0, 64, 16, VERTS});
        record("GX2SetColorBuffer", {COLOR, 0});
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
        cpu->m_fpr[3] = cpu->m_fpr[4] = 4;
        record("GX2SetViewport", {});
        for (unsigned c = 0; c < 4; ++c)
            cpu->m_memory.write<unsigned>(UNIFORM + c * 4, std::bit_cast<unsigned>(c == pass || c == 3 ? 1.f : 0.f));
        record("GX2SetVertexUniformReg", {0, 4, UNIFORM});
        record("GX2DrawIndexedEx", {4, 6, 4, INDEX, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        cached.replay(gx2Stream(), &cpu->m_memory);
        uncached.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ(cached.framebuffer(), uncached.framebuffer());
        EXPECT_EQ(cached.framebuffer()[pass], 255u);
        EXPECT_EQ(cached.framebuffer()[1 - pass], 0u);
        // Triangle assembly shades each distinct index once, so the shared corners of the
        // quad never reach the per-draw vertex cache, which stays a fallback.
        EXPECT_EQ(cached.vertexShaderExecutions(), 4u * (pass + 1));
        EXPECT_EQ(cached.vertexShaderCacheHits(), 0u);
        EXPECT_EQ(uncached.vertexShaderExecutions(), 4u * (pass + 1));
    }

    // Hidden triangle meshes must preserve the target and avoid executing their
    // vertex shaders. Point primitives ignore face culling and still render.
    const auto originalFrame = cached.framebuffer();
    const auto executions = cached.vertexShaderExecutions();
    gx2Stream().clear();
    record("GX2SetCullOnlyControl", {0, 1, 1});
    for (unsigned c = 0; c < 4; ++c)
        cpu->m_memory.write<unsigned>(UNIFORM + c * 4, std::bit_cast<unsigned>(c == 2 || c == 3 ? 1.f : 0.f));
    record("GX2SetVertexUniformReg", {0, 4, UNIFORM});
    record("GX2DrawIndexedEx", {4, 6, 4, INDEX, 0, 1});
    record("GX2DrawEx", {0x13, 4, 0, 1});
    record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
    gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
    cached.replay(gx2Stream(), &cpu->m_memory);
    EXPECT_EQ(cached.vertexShaderExecutions(), executions);
    EXPECT_EQ(cached.framebuffer(), originalFrame);
    gx2Stream().clear();
    record("GX2DrawEx", {1, 1, 0, 1});
    record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
    gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
    cached.replay(gx2Stream(), &cpu->m_memory);
    EXPECT_EQ(cached.vertexShaderExecutions(), executions + 1);
    EXPECT_EQ(cached.framebuffer()[2], 255);
    EXPECT_EQ(cached.framebuffer()[1], 0);
    gx2Stream().clear();
    record("GX2SetCullOnlyControl", {0, 0, 0});
    cached.replay(gx2Stream(), &cpu->m_memory);

    // A ground triangle crosses the eye plane. Its visible half belongs below
    // the horizon; dividing the negative-W vertex first paints the upper half.
    surface(COLOR, IMAGE, 8 * 8 * 4, 8, 8, 8, 1);
    for (bool whollyBehind: {false, true}) {
        SCOPED_TRACE(whollyBehind);
        const std::array<std::array<float, 4>, 3> ground{
                {{-1, -.5f, 0, whollyBehind ? -1.f : 1.f}, {1, -.5f, 0, whollyBehind ? -1.f : 1.f}, {0, -.5f, 0, -1}}};
        for (unsigned v = 0; v < ground.size(); ++v)
            for (unsigned c = 0; c < 4; ++c)
                cpu->m_memory.write<unsigned>(VERTS + v * 16 + c * 4, std::bit_cast<unsigned>(ground[v][c]));
        gx2Stream().clear();
        record("GX2SetFetchShader", {FETCH});
        record("GX2SetVertexShader", {VS});
        record("GX2SetPixelShader", {PS});
        record("GX2SetAttribBuffer", {0, 64, 16, VERTS});
        record("GX2SetColorBuffer", {COLOR, 0});
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
        cpu->m_fpr[3] = cpu->m_fpr[4] = 1;
        record("GX2ClearColor", {COLOR});
        cpu->m_fpr[3] = cpu->m_fpr[4] = 8;
        record("GX2SetViewport", {});
        for (unsigned c = 0; c < 4; ++c)
            cpu->m_memory.write<unsigned>(UNIFORM + c * 4, std::bit_cast<unsigned>(c == 0 || c == 3 ? 1.f : 0.f));
        record("GX2SetVertexUniformReg", {0, 4, UNIFORM});
        record("GX2DrawEx", {4, 3, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        cached.replay(gx2Stream(), &cpu->m_memory);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 1; x < 7; ++x) {
                const auto offset = (y * Gx2Replayer::kWidth + x) * 4;
                EXPECT_EQ(cached.framebuffer()[offset], !whollyBehind && y >= 6 ? 255 : 0) << x << ',' << y;
                EXPECT_EQ(cached.framebuffer()[offset + 2], whollyBehind || y < 6 ? 255 : 0) << x << ',' << y;
            }
        if (std::getenv("WEMU_TEST_VULKAN")) {
            auto backend = std::make_shared<VulkanRasterBackend>(true, true);
            Gx2Replayer native;
            native.setRasterBackend(backend);
            native.setNativeVerificationInterval(1);
            native.replay(gx2Stream(), &cpu->m_memory);
            EXPECT_EQ(native.framebuffer(), cached.framebuffer());
            EXPECT_EQ(native.differingNativeDraws(), 0u);
            // Vertex programs run on the GPU, which clips the wholly hidden triangle itself.
            EXPECT_EQ(backend->completedDraws(), 1u) << backend->lastError();
        }
    }
}

TEST_F(Gx2CaptureTest, ShaderResourceQueriesReturnHeaderCounts)
{
    constexpr unsigned header = 0x28005000;
    for (unsigned resources: {0u, 0x80004B1Cu, 0xFFFFFFFFu}) {
        cpu->m_memory.write<unsigned>(header, resources);
        for (const char *name: {"GX2GetPixelShaderGPRs", "GX2GetVertexShaderGPRs", "GX2GetGeometryShaderGPRs"}) {
            cpu->m_gpr[3] = header;
            call(name);
            EXPECT_EQ(cpu->m_gpr[3], resources & 0xFF);
        }
        for (const char *name: {"GX2GetPixelShaderStackEntries", "GX2GetVertexShaderStackEntries", "GX2GetGeometryShaderStackEntries"}) {
            cpu->m_gpr[3] = header;
            call(name);
            EXPECT_EQ(cpu->m_gpr[3], (resources >> 8) & 0xFF);
        }
    }
    EXPECT_TRUE(Core::Gfx::gx2Stream().commands().empty());
}

TEST_F(Gx2CaptureTest, RecordingAdditionalListsPreservesLiveShaderBindings)
{
    using namespace Core::Gfx;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    constexpr unsigned first = 0x28001000, shader = 0x28002000;
    record("GX2BeginDisplayListEx", {first, 256, 0});
    record("GX2SetVertexShader", {shader});
    record("GX2EndDisplayList", {first});
    const auto firstSize = cpu->m_gpr[3];
    for (unsigned i = 0; i < 8193; ++i) {
        const unsigned buffer = 0x28100000 + i * 256;
        record("GX2BeginDisplayListEx", {buffer, 256, 0});
        record("GX2SetPixelShader", {shader + 0x100});
        record("GX2EndDisplayList", {buffer});
    }
    gx2Stream().clear();
    record("GX2CallDisplayList", {first, firstSize});
    ASSERT_EQ(gx2Stream().commands().size(), 1u);
    EXPECT_EQ(gx2Stream().commands()[0].type, Gx2Cmd::SetVertexShader);
    EXPECT_EQ(gx2Stream().commands()[0].gpr[0], shader);
    gx2Stream().clear();
    record("GX2BeginDisplayListEx", {first, 256, 0});
    record("GX2SetPixelShader", {shader + 0x200});
    record("GX2EndDisplayList", {first});
    record("GX2CallDisplayList", {first, cpu->m_gpr[3]});
    ASSERT_EQ(gx2Stream().commands().size(), 1u);
    EXPECT_EQ(gx2Stream().commands()[0].type, Gx2Cmd::SetPixelShader);
    EXPECT_EQ(gx2Stream().commands()[0].gpr[0], shader + 0x200);
}

TEST_F(Gx2CaptureTest, CurrentDisplayListReportsCapacityWithoutClobberingInactiveOutputs)
{
    constexpr unsigned buffer = 0x28001000, output = 0x28002000;
    const auto query = [&] {
        cpu->m_gpr[3] = output;
        cpu->m_gpr[4] = output + 4;
        call("GX2GetCurrentDisplayList");
        return cpu->m_gpr[3];
    };
    cpu->m_memory.write<unsigned>(output, 0x12345678);
    cpu->m_memory.write<unsigned>(output + 4, 0xABCDEF);
    EXPECT_EQ(query(), 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(output), 0x12345678u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(output + 4), 0xABCDEFu);
    cpu->m_gpr[3] = buffer;
    cpu->m_gpr[4] = 1024;
    call("GX2BeginDisplayListEx");
    EXPECT_EQ(query(), 1u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(output), buffer);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(output + 4), 1024u);
    call("GX2SetPixelShader");
    EXPECT_EQ(query(), 1u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(output + 4), 1024u);
    cpu->m_gpr[3] = buffer;
    call("GX2EndDisplayList");
    EXPECT_EQ(cpu->m_gpr[3], 64u); // Used bytes remain a separate EndDisplayList result.
}

TEST_F(Gx2CaptureTest, CopiedDisplayListsKeepSnapshotsAndRejectPartialOrOverwrittenCopies)
{
    using namespace Core::Gfx;
    RegisterLibcFunctions();
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned reg = 3;
        for (auto value: args)
            cpu->m_gpr[reg++] = value;
        call(name);
    };
    constexpr unsigned source = 0x28300000, copy = 0x28301000, partial = 0x28302000, shader = 0x28005000;
    record("GX2BeginDisplayListEx", {source, 256, 0});
    record("GX2SetPixelShader", {shader});
    record("GX2EndDisplayList", {source});
    const auto bytes = cpu->m_gpr[3];
    record("memcpy", {copy, source, bytes});
    record("memcpy", {partial, source, bytes - 1});
    record("GX2BeginDisplayListEx", {source, 256, 0});
    record("GX2SetVertexShader", {shader});
    record("GX2EndDisplayList", {source});
    gx2Stream().clear();
    record("GX2CallDisplayList", {copy, bytes});
    record("GX2CallDisplayList", {source, bytes});
    record("GX2CallDisplayList", {partial, bytes});
    ASSERT_EQ(gx2Stream().size(), 2u);
    EXPECT_EQ(gx2Stream().commands()[0].type, Gx2Cmd::SetPixelShader);
    EXPECT_EQ(gx2Stream().commands()[1].type, Gx2Cmd::SetVertexShader);
    gx2Stream().clear();
    record("memcpy", {copy, source, 0});
    record("GX2CallDisplayList", {copy, bytes});
    ASSERT_EQ(gx2Stream().size(), 1u);
    record("memset", {copy, 0, 4});
    gx2Stream().clear();
    record("GX2CallDisplayList", {copy, bytes});
    EXPECT_TRUE(gx2Stream().commands().empty());
    ReleaseGx2HostResources();
    record("GX2CallDisplayList", {source, bytes});
    EXPECT_TRUE(gx2Stream().commands().empty());
}

TEST_F(Gx2CaptureTest, IndependentColorAttachmentsReceiveDistinctBurstOutputs)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
    constexpr unsigned FETCH = 0x28001000, ATTRS = 0x28003000, VERTS = 0x28010000;
    constexpr unsigned COLOR = 0x28009000, SECOND = 0x2800A000, DEPTH = 0x2800B000, TEXTURE = 0x2800C000;
    constexpr unsigned SAMPLING_PS = 0x2800D000, SAMPLING_PSP = 0x2800E000, COLOR_IMAGE = 0x28040000, SECOND_IMAGE = 0x28050000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned reg = 3;
        for (const auto arg: args)
            cpu->m_gpr[reg++] = arg;
        call(name);
    };
    const std::array<unsigned, 8> attrs{0, 0, 0, 0x813, 0, 0, 0x00010203, 3};
    for (unsigned i = 0; i < attrs.size(); ++i)
        cpu->m_memory.write<unsigned>(ATTRS + i * 4, attrs[i]);
    record("GX2InitFetchShaderEx", {FETCH, 0x28002000, 1, ATTRS, 0, 0});
    const std::array<std::array<float, 4>, 3> vertices{{{-1, 1, 0, 1}, {3, 1, 0, 1}, {-1, -3, 0, 1}}};
    for (unsigned v = 0; v < vertices.size(); ++v)
        for (unsigned c = 0; c < 4; ++c)
            cpu->m_memory.write<unsigned>(VERTS + v * 16 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
    for (unsigned i = 0; i < 0x200; i += 4) {
        cpu->m_memory.write<unsigned>(VS + i, 0);
        cpu->m_memory.write<unsigned>(PS + i, 0);
    }
    cpu->m_memory.write<unsigned>(VS + 0xC, 1);
    cpu->m_memory.write<unsigned>(VS + 0x10, 0xFFFFFF00);
    cpu->m_memory.write<unsigned>(VS + 0x14, 0xFFFFFFFF);
    cpu->m_memory.write<unsigned>(VS + 0x18, 0xFFFFFFFF);
    const std::array<unsigned, 2> vertex{60u | (1u << 13) | (1u << 15), (0x28u << 23) | (1u << 21) | 0x688u};
    std::vector<unsigned> pixel{2, (8u << 26) | (7u << 18), 0, (0x28u << 23) | (1u << 21) | (1u << 17) | 0x688u};
    for (unsigned reg = 0; reg < 2; ++reg)
        for (unsigned c = 0; c < 4; ++c) {
            pixel.push_back((c == reg || c == 3 ? 249u : 248u) | (c == 3 ? 1u << 31 : 0));
            pixel.push_back(16u | (0x19u << 7) | (reg << 21) | (c << 29));
        }
    const auto shader = [&](unsigned header, unsigned offset, unsigned ptr, const auto &words) {
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, ptr);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(ptr + i * 4, std::byteswap(words[i]));
    };
    // The same outputs after an unused sample of unit 0 (TEX at word 24, ALU at word 6).
    std::vector<unsigned> sampling{12, 1u << 23, 3, (8u << 26) | (7u << 18), 0, (0x28u << 23) | (1u << 21) | (1u << 17) | 0x688u};
    sampling.insert(sampling.end(), pixel.begin() + 4, pixel.end());
    sampling.resize(24);
    sampling.insert(sampling.end(), {0x10u, 5u | (0x688u << 9) | 0xF0000000u, 1u << 23, 0u});
    for (unsigned i = 0; i < 0x200; i += 4)
        cpu->m_memory.write<unsigned>(SAMPLING_PS + i, 0);
    shader(VS, 0xD0, VSP, vertex);
    shader(PS, 0xA4, PSP, pixel);
    shader(SAMPLING_PS, 0xA4, SAMPLING_PSP, sampling);
    surface(COLOR, COLOR_IMAGE, 256, 8, 8, 8, 1);
    surface(SECOND, SECOND_IMAGE, 256, 8, 8, 8, 1);
    for (const auto header: {COLOR, SECOND}) {
        cpu->m_memory.write<unsigned>(header + 0x74, 0);
        cpu->m_memory.write<unsigned>(header + 0x78, 0);
        cpu->m_memory.write<unsigned>(header + 0x7C, 1);
    }
    surface(DEPTH, 0x28060000, 256, 8, 8, 8, 0);
    cpu->m_memory.write<unsigned>(DEPTH + 0x14, 0x80E);
    // A read-only depth test gives both separate passes the same coverage: no test, passing, failing.
    // A bound texture overlapping an attachment only blocks the split when the shader samples it.
    for (const auto [depthMode, overlap, sampled]: {std::tuple{0u, 0u, false}, {1u, 0u, false}, {2u, 0u, false}, {0u, COLOR_IMAGE, false},
                                                    {0u, SECOND_IMAGE, false}, {0u, COLOR_IMAGE, true}}) {
    SCOPED_TRACE(depthMode);
    SCOPED_TRACE(overlap);
    SCOPED_TRACE(sampled);
    gx2Stream().clear();
    for (const auto header: {COLOR, SECOND}) {
        for (unsigned c = 1; c <= 4; ++c)
            cpu->m_fpr[c] = 0;
        record("GX2ClearColor", {header});
    }
    record("GX2SetColorBuffer", {COLOR, 0});
    record("GX2SetColorBuffer", {SECOND, 1});
    record("GX2SetColorControl", {0xCC, 0, 0, 1});
    record("GX2SetTargetChannelMasks", {15, 15, 0, 0, 0, 0, 0, 0});
    if (depthMode) {
        cpu->m_fpr[1] = depthMode == 1 ? 1. : .25; // geometry lies at window depth 0.5
        record("GX2ClearDepthStencilEx", {DEPTH, 0, 1});
        record("GX2SetDepthBuffer", {DEPTH});
        record("GX2SetDepthOnlyControl", {1, 0, depthMode == 1 ? 3u : 1u}); // LEQUAL passes, LESS fails
    } else
        record("GX2SetDepthOnlyControl", {0, 0, 1});
    cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
    cpu->m_fpr[3] = cpu->m_fpr[4] = 8;
    cpu->m_fpr[5] = 0;
    cpu->m_fpr[6] = 1;
    record("GX2SetViewport", {});
    record("GX2SetFetchShader", {FETCH});
    record("GX2SetVertexShader", {VS});
    record("GX2SetPixelShader", {sampled ? SAMPLING_PS : PS});
    if (overlap) {
        surface(TEXTURE, overlap, 256, 8, 8, 8, 1);
        cpu->m_memory.write<unsigned>(TEXTURE + 0x78, 1);
        cpu->m_memory.write<unsigned>(TEXTURE + 0x80, 1);
        record("GX2SetPixelTexture", {TEXTURE, 0});
    }
    record("GX2SetAttribBuffer", {0, 48, 16, VERTS});
    record("GX2DrawEx", {4, 3, 0, 1});
    record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
    const auto captured = gx2Stream();
    for (bool native: {false, true}) {
        if (native && !std::getenv("WEMU_TEST_VULKAN"))
            continue;
        Gx2Replayer replay;
        auto backend = std::make_shared<VulkanRasterBackend>(true, true);
        if (native)
            replay.setRasterBackend(backend);
        replay.setNativeVerificationInterval(1);
        replay.replay(captured, &cpu->m_memory);
        if (sampled) {
            // The split is refused: the draw keeps the simultaneous (software) path.
            if (native)
                EXPECT_EQ(backend->completedDraws(), 0u);
            continue;
        }
        const bool drawn = depthMode != 2;
        EXPECT_EQ((std::array{replay.framebuffer()[0], replay.framebuffer()[1], replay.framebuffer()[2], replay.framebuffer()[3]}),
                  (drawn ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{}));
        if (native) {
            EXPECT_EQ(backend->completedDraws(), 2u) << backend->lastError();
            EXPECT_EQ(replay.differingNativeDraws(), 0u);
        }
        gx2Stream().clear();
        record("GX2CopyColorBufferToScanBuffer", {SECOND, 1});
        replay.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ((std::array{replay.framebuffer()[0], replay.framebuffer()[1], replay.framebuffer()[2], replay.framebuffer()[3]}),
                  (drawn ? std::array<std::uint8_t, 4>{0, 255, 0, 255} : std::array<std::uint8_t, 4>{}));
    }
    }
}

TEST_F(Gx2CaptureTest, AttributeFetchesAreBoundedByElementStartNotByteExtent)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
    constexpr unsigned FETCH = 0x28001000, ATTRS = 0x28003000, VERTS = 0x28010000, COLOR = 0x28009000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned reg = 3;
        for (const auto arg: args)
            cpu->m_gpr[reg++] = arg;
        call(name);
    };
    // 32_32_32_32 positions with W forced to 1, read from a 12-byte stride: the last vertex's
    // fourth float lies past the declared 36-byte buffer, as in MK8's course geometry.
    const std::array<unsigned, 8> attrs{0, 0, 0, 0x813, 0, 0, 0x00010205, 3};
    for (unsigned i = 0; i < attrs.size(); ++i)
        cpu->m_memory.write<unsigned>(ATTRS + i * 4, attrs[i]);
    record("GX2InitFetchShaderEx", {FETCH, 0x28002000, 1, ATTRS, 0, 0});
    const std::array<std::array<float, 3>, 3> vertices{{{-1, 1, 0}, {3, 1, 0}, {-1, -3, 0}}};
    for (unsigned v = 0; v < vertices.size(); ++v)
        for (unsigned c = 0; c < 3; ++c)
            cpu->m_memory.write<unsigned>(VERTS + v * 12 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
    cpu->m_memory.write<unsigned>(VERTS + 36, std::bit_cast<unsigned>(123.f)); // never selected
    for (unsigned i = 0; i < 0x200; i += 4) {
        cpu->m_memory.write<unsigned>(VS + i, 0);
        cpu->m_memory.write<unsigned>(PS + i, 0);
    }
    cpu->m_memory.write<unsigned>(VS + 0xC, 1);
    cpu->m_memory.write<unsigned>(VS + 0x10, 0xFFFFFF00);
    cpu->m_memory.write<unsigned>(VS + 0x14, 0xFFFFFFFF);
    cpu->m_memory.write<unsigned>(VS + 0x18, 0xFFFFFFFF);
    const std::array<unsigned, 2> vertex{60u | (1u << 13) | (1u << 15), (0x28u << 23) | (1u << 21) | 0x688u};
    // Export constant red: MOV R0, (1, 0, 0, 1).
    std::vector<unsigned> pixel{2, (8u << 26) | (3u << 18), 0, (0x28u << 23) | (1u << 21) | 0x688u};
    for (unsigned c = 0; c < 4; ++c) {
        pixel.push_back((c == 0 || c == 3 ? 249u : 248u) | (c == 3 ? 1u << 31 : 0));
        pixel.push_back(16u | (0x19u << 7) | (c << 29));
    }
    const auto shader = [&](unsigned header, unsigned offset, unsigned ptr, const auto &words) {
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, ptr);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(ptr + i * 4, std::byteswap(words[i]));
    };
    shader(VS, 0xD0, VSP, vertex);
    shader(PS, 0xA4, PSP, pixel);
    surface(COLOR, 0x28040000, 256, 8, 8, 8, 1);
    for (unsigned c = 1; c <= 4; ++c)
        cpu->m_fpr[c] = 0;
    record("GX2ClearColor", {COLOR});
    record("GX2SetColorBuffer", {COLOR, 0});
    record("GX2SetColorControl", {0xCC, 0, 0, 1});
    record("GX2SetTargetChannelMasks", {15, 0, 0, 0, 0, 0, 0, 0});
    cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
    cpu->m_fpr[3] = cpu->m_fpr[4] = 8;
    cpu->m_fpr[5] = 0;
    cpu->m_fpr[6] = 1;
    record("GX2SetViewport", {});
    record("GX2SetFetchShader", {FETCH});
    record("GX2SetVertexShader", {VS});
    record("GX2SetPixelShader", {PS});
    record("GX2SetAttribBuffer", {0, 36, 12, VERTS});
    record("GX2DrawEx", {4, 3, 0, 1});
    record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
    const auto captured = gx2Stream();
    for (bool native: {false, true}) {
        if (native && !std::getenv("WEMU_TEST_VULKAN"))
            continue;
        SCOPED_TRACE(native);
        Gx2Replayer replay;
        auto backend = std::make_shared<VulkanRasterBackend>(true, true);
        if (native)
            replay.setRasterBackend(backend);
        replay.replay(captured, &cpu->m_memory);
        EXPECT_EQ((std::array{replay.framebuffer()[0], replay.framebuffer()[1], replay.framebuffer()[2], replay.framebuffer()[3]}),
                  (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    }
}

TEST_F(Gx2CaptureTest, PolygonRegisterCapturePreservesStackArgumentsAndRoundTrips)
{
    constexpr unsigned REG = 0x28001000, STACK = 0x28002000, OUT = 0x28003000;
    cpu->m_gpr[1] = STACK;
    const std::array<unsigned, 10> args{REG, 1, 0, 1, 1, 2, 1, 1, 0, 1};
    for (unsigned index = 0; index < args.size(); ++index)
        if (index < 8)
            cpu->m_gpr[index + 3] = args[index];
        else
            cpu->m_memory.write<unsigned>(STACK + 8 + index * 4, args[index]);
    call("GX2InitPolygonControlReg");
    const auto word = 2u | 4u | (1u << 3) | (2u << 5) | (1u << 8) | (1u << 11) | (1u << 13);
    ASSERT_EQ(cpu->m_memory.read<unsigned>(REG), word);
    cpu->m_gpr[3] = REG;
    call("GX2SetPolygonControlReg");
    cpu->m_memory.write<unsigned>(REG, 0);
    ASSERT_EQ(Core::Gfx::gx2Stream().commands().back().type, Core::Gfx::Gx2Cmd::SetPolygonControl);
    EXPECT_EQ(Core::Gfx::gx2Stream().commands().back().gpr[0], word);
    cpu->m_memory.write<unsigned>(REG, word);
    cpu->m_gpr[3] = REG;
    for (unsigned index = 0; index < 9; ++index)
        if (index + 1 < 8)
            cpu->m_gpr[index + 4] = OUT + index * 4;
        else
            cpu->m_memory.write<unsigned>(STACK + 8 + (index + 1) * 4, OUT + index * 4);
    call("GX2GetPolygonControlReg");
    for (unsigned index = 0; index < 9; ++index)
        EXPECT_EQ(cpu->m_memory.read<unsigned>(OUT + index * 4), args[index + 1]);
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = 1;
    cpu->m_gpr[5] = 0;
    call("GX2SetCullOnlyControl");
    EXPECT_EQ(Core::Gfx::gx2Stream().commands().back().gpr[0], 1u);
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = 1;
    cpu->m_gpr[5] = 1;
    call("GX2SetRasterizerClipControlEx");
    const auto &clip = Core::Gfx::gx2Stream().commands().back();
    EXPECT_EQ(clip.type, Core::Gfx::Gx2Cmd::SetRasterizerClipControl);
    EXPECT_EQ((std::array{clip.gpr[0], clip.gpr[1], clip.gpr[2]}), (std::array{0u, 1u, 1u}));
}

TEST_F(Gx2CaptureTest, PolygonOffsetRegistersStoreScaledFieldsAndSnapshotValues)
{
    constexpr unsigned reg = 0x28080000, output = 0x28080100;
    const std::array<float, 5> values{7.5f, .125f, -3.25f, -.25f, .0625f};
    const std::array<unsigned, 5> fields{1, 0, 3, 2, 4};
    cpu->m_gpr[3] = reg;
    for (unsigned i = 0; i < values.size(); ++i)
        cpu->m_fpr[i + 1] = values[i];
    call("GX2InitPolygonOffsetReg");
    EXPECT_TRUE(Core::Gfx::gx2Stream().empty());
    for (unsigned i = 0; i < values.size(); ++i)
        EXPECT_FLOAT_EQ(std::bit_cast<float>(cpu->m_memory.read<unsigned>(reg + fields[i] * 4)), values[i] * ((i == 1 || i == 3) ? 16.f : 1.f));
    cpu->m_gpr[3] = reg;
    call("GX2SetPolygonOffsetReg");
    for (unsigned i = 0; i < values.size(); ++i)
        EXPECT_DOUBLE_EQ(Core::Gfx::gx2Stream().commands().back().fpr[i], values[i]);
    cpu->m_gpr[3] = reg;
    for (unsigned i = 0; i < values.size(); ++i)
        cpu->m_gpr[i + 4] = output + i * 4;
    call("GX2GetPolygonOffsetReg");
    for (unsigned i = 0; i < values.size(); ++i)
        EXPECT_FLOAT_EQ(std::bit_cast<float>(cpu->m_memory.read<unsigned>(output + i * 4)), values[i]);
    cpu->m_memory.write<unsigned>(reg, 0);
    for (unsigned i = 0; i < values.size(); ++i)
        EXPECT_DOUBLE_EQ(Core::Gfx::gx2Stream().commands().back().fpr[i], values[i]);
    call("GX2SetPolygonOffset");
    for (unsigned i = 0; i < values.size(); ++i)
        EXPECT_DOUBLE_EQ(Core::Gfx::gx2Stream().commands().back().fpr[i], values[i]);
}

TEST_F(Gx2CaptureTest, SignedBlockTexturesKeepNegativeValuesFilteringAndChangedBytes)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28001000, PS = 0x28002000, COLOR = 0x28003000, TEX = 0x28004000;
    constexpr unsigned VSP = 0x28010000, PSP = 0x28011000, VERTS = 0x28012000, IMAGE = 0x28040000, BLOCKS = 0x28080000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    const auto shader = [&](unsigned header, unsigned offset, unsigned pointer, const std::vector<unsigned> &words) {
        for (unsigned i = 0; i < 0x200; i += 4)
            cpu->m_memory.write<unsigned>(header + i, 0);
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    };
    shader(VS, 0xD0, VSP,
           {60u | (1u << 13) | (1u << 15), (Latte::CF_EXP << 23) | 0x688u, (2u << 13) | (2u << 15),
            (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u});
    cpu->m_memory.write<unsigned>(VS + 0x40, 2);
    cpu->m_memory.write<unsigned>(VS + 0x44, 0);
    cpu->m_memory.write<unsigned>(VS + 0x48, 1);
    // Remap signed samples to visible UNORM. Prematurely clamping negative
    // texels would change both the endpoint and mixed-footprint expectations.
    std::vector<unsigned> pixel{3,        Latte::CF_TEX << 23,
                                5,        (8u << 26) | (4u << 18),
                                0,        (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                0x10,     0x30000000u | (0x688u << 9),
                                1u << 23, 0};
    for (unsigned channel = 0; channel < 4; ++channel) {
        pixel.push_back((channel << 10) | (Latte::SRC_HALF << 13) | (channel == 3 ? 0x80000000u : 0u));
        pixel.push_back(Latte::SRC_LITERAL | (Latte::OP3_MULADD << 13) | (channel << 29));
    }
    pixel.insert(pixel.end(), {std::bit_cast<unsigned>(.375f), 0});
    shader(PS, 0xA4, PSP, pixel);
    cpu->m_memory.write<unsigned>(PS + 0x10, 1);
    const std::array<std::array<float, 4>, 6> corners{{{-1, 1, 0, 0}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}}};
    for (unsigned i = 0; i < corners.size(); ++i) {
        const auto &v = corners[i];
        const std::array<float, 8> data{v[0], v[1], 0, 1, v[2], v[3], 0, 1};
        for (unsigned c = 0; c < data.size(); ++c)
            cpu->m_memory.write<unsigned>(VERTS + i * 32 + c * 4, std::bit_cast<unsigned>(data[c]));
    }
    for (const unsigned format: {0x234u, 0x235u}) {
        Gx2Replayer reference, native;
        std::shared_ptr<VulkanRasterBackend> backend;
        if (std::getenv("WEMU_TEST_VULKAN")) {
            backend = std::make_shared<VulkanRasterBackend>(true, true);
            native.setRasterBackend(backend);
            native.setNativeVerificationInterval(1);
        }
        const unsigned bytes = format == 0x234 ? 8 : 16;
        surface(COLOR, IMAGE, 8 * 4 * 4, 8, 4, 8, 1);
        surface(TEX, BLOCKS, bytes * 2, 8, 4, 2, 1);
        for (const unsigned header: {COLOR, TEX}) {
            cpu->m_memory.write<unsigned>(header + 0x74, 0);
            cpu->m_memory.write<unsigned>(header + 0x78, 1);
            cpu->m_memory.write<unsigned>(header + 0x7C, 0);
            cpu->m_memory.write<unsigned>(header + 0x80, 1);
        }
        // GX2ColorBuffer has mip/first-slice/count, unlike GX2Texture's
        // mip-first/mip-count/slice-first/slice-count descriptor.
        cpu->m_memory.write<unsigned>(COLOR + 0x78, 0);
        cpu->m_memory.write<unsigned>(COLOR + 0x7C, 1);
        cpu->m_memory.write<unsigned>(TEX + 0x14, format);
        cpu->m_memory.write<unsigned>(TEX + 0x84, 0x00010405);
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (unsigned block = 0; block < 2; ++block) {
                for (unsigned i = 0; i < bytes; ++i)
                    cpu->m_memory.write<std::uint8_t>(BLOCKS + block * bytes + i, 0);
                cpu->m_memory.write<std::uint8_t>(BLOCKS + block * bytes, (block == 0) == bool(pass) ? 63 : 192); // +63 versus -64
                cpu->m_memory.write<std::uint8_t>(BLOCKS + block * bytes + 1, 0);
                if (bytes == 16) {
                    cpu->m_memory.write<std::uint8_t>(BLOCKS + block * bytes + 8, 63);
                    cpu->m_memory.write<std::uint8_t>(BLOCKS + block * bytes + 9, 192);
                }
            }
            for (unsigned i = 0; i < corners.size(); ++i)
                cpu->m_memory.write<unsigned>(VERTS + i * 32 + 16, std::bit_cast<unsigned>(corners[i][2] + (pass ? .0625f : 0.f)));
            gx2Stream().clear();
            record("GX2SetColorBuffer", {COLOR, 0});
            record("GX2SetColorControl", {0xCC, 0, 0, 1});
            cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
            cpu->m_fpr[3] = 8;
            cpu->m_fpr[4] = 4;
            record("GX2SetViewport", {});
            record("GX2SetScissor", {0, 0, 8, 4});
            Gx2Command fetch{Gx2Cmd::SetFetchShader};
            fetch.payload = {0, 0, 0, 0x813, 0, 0, 0x00010203, 3, 1, 0, 16, 0x813, 0, 0, 0x00010203, 3};
            gx2Stream().push(fetch);
            record("GX2SetVertexShader", {VS});
            record("GX2SetPixelShader", {PS});
            record("GX2SetAttribBuffer", {0, 6 * 32, 32, VERTS});
            record("GX2SetPixelTexture", {TEX, 0});
            Gx2Command sampler{Gx2Cmd::SetPixelSampler};
            sampler.gpr[1] = 0;
            sampler.payload = {pass ? (1u << 9) | (1u << 12) : 0u, 0, 0};
            gx2Stream().push(sampler);
            record("GX2DrawEx", {4, 6, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
            gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            EXPECT_EQ(reference.framebuffer()[0], pass ? 159 : 31);
            EXPECT_EQ(reference.framebuffer()[1], format == 0x235 ? 159 : 96);
            EXPECT_EQ(reference.framebuffer()[2], 96);
            EXPECT_EQ(reference.framebuffer()[3], 223);
            EXPECT_EQ(reference.framebuffer()[3 * 4], pass ? 95 : 31) << "Linear filtering mixes signed texels before shader remapping";
            EXPECT_EQ(native.framebuffer(), reference.framebuffer());
            if (backend) {
                EXPECT_EQ(backend->completedDraws(), pass + 1) << backend->lastError();
                EXPECT_EQ(native.differingNativeDraws(), 0u);
            }
        }
        // Change only the signed view mapping, leaving source bytes intact.
        // The immutable decoded floats must have a distinct cache identity.
        gx2Stream().clear();
        cpu->m_memory.write<unsigned>(TEX + 0x84, 0x04010005);
        record("GX2SetPixelTexture", {TEX, 0});
        record("GX2DrawEx", {4, 6, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ(reference.framebuffer()[0], 96);
        EXPECT_EQ(reference.framebuffer()[2], 159);
        EXPECT_EQ(native.framebuffer(), reference.framebuffer());
        if (backend) {
            EXPECT_EQ(backend->completedDraws(), 3u);
            EXPECT_EQ(native.differingNativeDraws(), 0u);
        }
    }
}

TEST_F(Gx2CaptureTest, PackedAndBlockDecodeMatchReferenceForTilingPartialBlocksAndChangedBytes)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28001000, PS = 0x28002000, COLOR = 0x28003000, TEX = 0x28004000;
    constexpr unsigned VSP = 0x28010000, PSP = 0x28011000, VERTS = 0x28012000, IMAGE = 0x28040000, BLOCKS = 0x28080000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    const auto shader = [&](unsigned header, unsigned offset, unsigned pointer, const std::vector<unsigned> &words) {
        for (unsigned i = 0; i < 0x200; i += 4)
            cpu->m_memory.write<unsigned>(header + i, 0);
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    };
    shader(VS, 0xD0, VSP,
           {60u | (1u << 13) | (1u << 15), (Latte::CF_EXP << 23) | 0x688u, (2u << 13) | (2u << 15),
            (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u});
    cpu->m_memory.write<unsigned>(VS + 0x40, 2);
    cpu->m_memory.write<unsigned>(VS + 0x44, 0);
    cpu->m_memory.write<unsigned>(VS + 0x48, 1);
    const std::vector<unsigned> pixel{2,    Latte::CF_TEX << 23,         0,        (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u,
                                      0x10, 0x30000000u | (0x688u << 9), 1u << 23, 0};
    shader(PS, 0xA4, PSP, pixel);
    cpu->m_memory.write<unsigned>(PS + 0x10, 1);
    const std::array<std::array<float, 4>, 6> corners{{{-1, 1, 0, 0}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}}};
    std::vector<std::array<unsigned, 4>> cases;
    for (const unsigned tile: {1u, 2u, 4u})
        for (const unsigned format: {8u, 0x31u, 0x32u, 0x33u, 0x34u, 0x35u})
            cases.push_back({tile, format, 7, 5});
    // A near-4K BC1 atlas should stay compressed on the native path, instead
    // of expanding to 64 MiB of RGBA or a still larger float callback upload.
    cases.push_back({4, 0x31, 4092, 4096});
    for (const auto &[tile, format, width, height]: cases) {
        SCOPED_TRACE(tile);
        SCOPED_TRACE(format);
        // Stay away from nearest-sample texel edges: interpolator rounding at
        // an exact 4K boundary can legitimately choose adjacent source texels.
        for (unsigned i = 0; i < corners.size(); ++i) {
            const auto &v = corners[i];
            const std::array<float, 8> data{v[0], v[1], 0, 1, v[2] + .25f / float(width), v[3] + .25f / float(height), 0, 1};
            for (unsigned c = 0; c < data.size(); ++c)
                cpu->m_memory.write<unsigned>(VERTS + i * 32 + c * 4, std::bit_cast<unsigned>(data[c]));
        }
        Gx2Replayer reference, native;
        std::shared_ptr<VulkanRasterBackend> backend;
        if (std::getenv("WEMU_TEST_VULKAN")) {
            backend = std::make_shared<VulkanRasterBackend>(true, true);
            native.setRasterBackend(backend);
            if (format != 0x31 && format != 0x34)
                native.setNativeVerificationInterval(1);
        }
        const unsigned bytes = format == 8 ? 2 : format == 0x31 || format == 0x34 ? 8 : 16;
        const auto layout =
                baseSurfaceLayout(format == 8 ? width : (width + 3) / 4, format == 8 ? height : (height + 3) / 4, 1, bytes, tile, width, height);
        ASSERT_TRUE(layout);
        surface(COLOR, IMAGE, 7 * 5 * 4, 7, 5, 7, 1);
        surface(TEX, BLOCKS, layout->imageSize, width, height, layout->pitch, tile);
        for (const unsigned header: {COLOR, TEX}) {
            cpu->m_memory.write<unsigned>(header + 0x74, 0);
            cpu->m_memory.write<unsigned>(header + 0x78, 1);
            cpu->m_memory.write<unsigned>(header + 0x7C, 0);
            cpu->m_memory.write<unsigned>(header + 0x80, 1);
        }
        // GX2ColorBuffer has mip/first-slice/count, unlike GX2Texture's
        // mip-first/mip-count/slice-first/slice-count descriptor.
        cpu->m_memory.write<unsigned>(COLOR + 0x78, 0);
        cpu->m_memory.write<unsigned>(COLOR + 0x7C, 1);
        cpu->m_memory.write<unsigned>(TEX + 0x14, format);
        cpu->m_memory.write<unsigned>(TEX + 0x84, 0x00010203);
        const bool large = width > 4000;
        for (unsigned pass = 0; pass < (large ? 20u : 2u); ++pass) {
            // Eighteen atlases overflow the strong CPU decode cache while their GPU
            // uploads still own the snapshots. Returning to the first unchanged
            // atlas must recover its owner instead of decoding/uploading again.
            const auto image = BLOCKS + (large && pass < 18 ? pass * 0x01000000u : 0u);
            cpu->m_memory.write<unsigned>(TEX + 0x24, image);
            if (!large || pass != 18) {
                unsigned random = 0x163AE42u + pass;
                for (unsigned i = 0; i < layout->imageSize; ++i) {
                    random = random * 1664525u + 1013904223u;
                    cpu->m_memory.write<std::uint8_t>(image + i, std::uint8_t(random >> 24));
                }
            }
            gx2Stream().clear();
            record("GX2SetColorBuffer", {COLOR, 0});
            record("GX2SetColorControl", {0xCC, 0, 0, 1});
            cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
            cpu->m_fpr[3] = 7;
            cpu->m_fpr[4] = 5;
            record("GX2SetViewport", {});
            record("GX2SetScissor", {0, 0, 7, 5});
            Gx2Command fetch{Gx2Cmd::SetFetchShader};
            fetch.payload = {0, 0, 0, 0x813, 0, 0, 0x00010203, 3, 1, 0, 16, 0x813, 0, 0, 0x00010203, 3};
            gx2Stream().push(fetch);
            record("GX2SetVertexShader", {VS});
            record("GX2SetPixelShader", {PS});
            record("GX2SetAttribBuffer", {0, 6 * 32, 32, VERTS});
            record("GX2SetPixelTexture", {TEX, 0});
            Gx2Command sampler{Gx2Cmd::SetPixelSampler};
            sampler.gpr[1] = 0;
            sampler.payload = {0, 0, 0};
            gx2Stream().push(sampler);
            record("GX2DrawEx", {4, 6, 0, 1});
            record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
            gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
            reference.replay(gx2Stream(), &cpu->m_memory);
            native.replay(gx2Stream(), &cpu->m_memory);
            if (format == 8) {
                const std::array<std::uint8_t, 2> raw{cpu->m_memory.read<std::uint8_t>(BLOCKS), cpu->m_memory.read<std::uint8_t>(BLOCKS + 1)};
                const auto expected = decodeRgb565(raw.data());
                for (unsigned channel = 0; channel < 4; ++channel)
                    EXPECT_EQ(reference.framebuffer()[channel], expected[channel]);
            }
            if (backend && (format == 0x31 || format == 0x34)) {
                // BC1-5 hardware interpolation is approximate. D3D11 section
                // 19.5.2 bounds it by absolute error + 3% of endpoint distance;
                // NVIDIA's BC1 green interpolation actually exercises this bound.
                // The reference additionally truncates expanded endpoints and
                // interpolants to bytes. Allow those two rounding steps for BC1,
                // one for BC4, while keeping alpha/untouched pixels exact.
                // https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#19.5.2
                const auto &actual = native.framebuffer(), &expected = reference.framebuffer();
                ASSERT_EQ(actual.size(), expected.size());
                for (unsigned y = 0; y < Gx2Replayer::kHeight; ++y)
                    for (unsigned x = 0; x < Gx2Replayer::kWidth; ++x) {
                        std::array<unsigned, 4> tolerance{};
                        if (x < 7 && y < 5) {
                            const auto tx = unsigned((float(x) + .5f) * float(width) / 7.f + .25f) % width;
                            const auto ty = unsigned((float(y) + .5f) * float(height) / 5.f + .25f) % height;
                            const auto block = image + tiledElementOffset(tx / 4, ty / 4, layout->pitch, bytes * 8, tile, 0);
                            if (format == 0x31) {
                                const auto a = cpu->m_memory.read<std::uint8_t>(block) | (unsigned(cpu->m_memory.read<std::uint8_t>(block + 1)) << 8);
                                const auto b =
                                        cpu->m_memory.read<std::uint8_t>(block + 2) | (unsigned(cpu->m_memory.read<std::uint8_t>(block + 3)) << 8);
                                for (unsigned c = 0; c < 3; ++c) {
                                    const unsigned shift = c == 0 ? 11 : c == 1 ? 5 : 0, mask = c == 1 ? 63 : 31;
                                    const auto distance = std::abs(int(((a >> shift) & mask) * 255 / mask) - int(((b >> shift) & mask) * 255 / mask));
                                    tolerance[c] = unsigned(std::ceil(2.0 + .03 * distance));
                                }
                            } else {
                                const auto distance =
                                        std::abs(int(cpu->m_memory.read<std::uint8_t>(block)) - int(cpu->m_memory.read<std::uint8_t>(block + 1)));
                                tolerance.fill(unsigned(std::ceil(1.0 + .03 * distance)));
                            }
                        }
                        for (unsigned c = 0; c < 4; ++c) {
                            const auto i = (std::size_t(y) * Gx2Replayer::kWidth + x) * 4 + c;
                            ASSERT_LE(unsigned(std::abs(int(actual[i]) - int(expected[i]))), tolerance[c]) << x << ',' << y << ',' << c;
                        }
                    }
            } else {
                EXPECT_EQ(native.framebuffer(), reference.framebuffer());
            }
            if (backend) {
                EXPECT_EQ(backend->completedDraws(), pass + 1) << backend->lastError();
                EXPECT_EQ(native.differingNativeDraws(), 0u);
                if (large)
                    EXPECT_EQ(backend->sharedTextureUploads(), pass < 18 ? pass + 1u : pass == 18 ? 18u : 19u);
                if (large && pass == 0)
                    EXPECT_EQ(backend->sharedTextureBytes(), std::uint64_t((width + 3) / 4) * ((height + 3) / 4) * bytes * 2);
            }
        }
    }
}

TEST_F(Gx2CaptureTest, TrustedTextureSnapshotsStillSeeGuestStoresAndWritableViews)
{
    using namespace Core::Gfx;
    if (!std::getenv("WEMU_TEST_VULKAN"))
        GTEST_SKIP();
    constexpr unsigned VS = 0x28001000, PS = 0x28002000, COLOR = 0x28003000, TEX = 0x28004000;
    constexpr unsigned VSP = 0x28010000, PSP = 0x28011000, VERTS = 0x28012000, IMAGE = 0x28040000, BLOCKS = 0x28080000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned r = 3;
        for (auto arg: args)
            cpu->m_gpr[r++] = arg;
        call(name);
    };
    const auto shader = [&](unsigned header, unsigned offset, unsigned pointer, const std::vector<unsigned> &words) {
        for (unsigned i = 0; i < 0x200; i += 4)
            cpu->m_memory.write<unsigned>(header + i, 0);
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, pointer);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(pointer + i * 4, std::byteswap(words[i]));
    };
    shader(VS, 0xD0, VSP,
           {60u | (1u << 13) | (1u << 15), (Latte::CF_EXP << 23) | 0x688u, (2u << 13) | (2u << 15),
            (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u});
    cpu->m_memory.write<unsigned>(VS + 0x40, 2);
    cpu->m_memory.write<unsigned>(VS + 0x44, 0);
    cpu->m_memory.write<unsigned>(VS + 0x48, 1);
    shader(PS, 0xA4, PSP,
           {2, Latte::CF_TEX << 23, 0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x10, 0x30000000u | (0x688u << 9), 1u << 23, 0});
    cpu->m_memory.write<unsigned>(PS + 0x10, 1);
    constexpr unsigned width = 7, height = 5;
    const std::array<std::array<float, 4>, 6> corners{{{-1, 1, 0, 0}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}}};
    for (unsigned i = 0; i < corners.size(); ++i) {
        const auto &v = corners[i];
        const std::array<float, 8> data{v[0], v[1], 0, 1, v[2] + .25f / float(width), v[3] + .25f / float(height), 0, 1};
        for (unsigned c = 0; c < data.size(); ++c)
            cpu->m_memory.write<unsigned>(VERTS + i * 32 + c * 4, std::bit_cast<unsigned>(data[c]));
    }
    // A tiled RGB565 texture goes through the decoded-texture cache.
    const auto layout = baseSurfaceLayout(width, height, 1, 2, 4, width, height);
    ASSERT_TRUE(layout);
    surface(COLOR, IMAGE, width * height * 4, width, height, width, 1);
    surface(TEX, BLOCKS, layout->imageSize, width, height, layout->pitch, 4);
    for (const unsigned header: {COLOR, TEX}) {
        cpu->m_memory.write<unsigned>(header + 0x74, 0);
        cpu->m_memory.write<unsigned>(header + 0x78, 1);
        cpu->m_memory.write<unsigned>(header + 0x7C, 0);
        cpu->m_memory.write<unsigned>(header + 0x80, 1);
    }
    cpu->m_memory.write<unsigned>(COLOR + 0x78, 0);
    cpu->m_memory.write<unsigned>(COLOR + 0x7C, 1);
    cpu->m_memory.write<unsigned>(TEX + 0x14, 8);
    cpu->m_memory.write<unsigned>(TEX + 0x84, 0x00010203);
    cpu->m_memory.write<unsigned>(TEX + 0x24, BLOCKS);
    const auto fill = [&](unsigned seed) {
        for (unsigned i = 0; i < layout->imageSize; ++i) {
            seed = seed * 1664525u + 1013904223u;
            cpu->m_memory.write<std::uint8_t>(BLOCKS + i, std::uint8_t(seed >> 24));
        }
    };
    fill(0x51u);
    Gx2Replayer reference, native;
    const auto backend = std::make_shared<VulkanRasterBackend>(true, true);
    native.setRasterBackend(backend);
    native.setNativeVerificationInterval(0);
    for (unsigned frame = 0; frame < 12; ++frame) {
        SCOPED_TRACE(frame);
        // Unchanged frames let the snapshot become trusted; then a store and a writable view
        // must each still reach the next draw.
        if (frame == 6)
            fill(0x77u);
        if (frame == 10)
            std::memset(cpu->m_memory.hostPtr(BLOCKS, layout->imageSize), 0x5A, layout->imageSize);
        gx2Stream().clear();
        record("GX2SetColorBuffer", {COLOR, 0});
        record("GX2SetColorControl", {0xCC, 0, 0, 1});
        cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
        cpu->m_fpr[3] = width;
        cpu->m_fpr[4] = height;
        record("GX2SetViewport", {});
        record("GX2SetScissor", {0, 0, width, height});
        Gx2Command fetch{Gx2Cmd::SetFetchShader};
        fetch.payload = {0, 0, 0, 0x813, 0, 0, 0x00010203, 3, 1, 0, 16, 0x813, 0, 0, 0x00010203, 3};
        gx2Stream().push(fetch);
        record("GX2SetVertexShader", {VS});
        record("GX2SetPixelShader", {PS});
        record("GX2SetAttribBuffer", {0, 6 * 32, 32, VERTS});
        record("GX2SetPixelTexture", {TEX, 0});
        Gx2Command sampler{Gx2Cmd::SetPixelSampler};
        sampler.payload = {0, 0, 0};
        gx2Stream().push(sampler);
        record("GX2DrawEx", {4, 6, 0, 1});
        record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
        gx2Stream().push(Gx2Command{Gx2Cmd::SwapScanBuffers});
        reference.replay(gx2Stream(), &cpu->m_memory);
        native.replay(gx2Stream(), &cpu->m_memory);
        ASSERT_EQ(backend->completedDraws(), frame + 1) << backend->lastError();
        EXPECT_EQ(native.framebuffer(), reference.framebuffer());
    }
}

TEST_F(Gx2CaptureTest, MovieImageDrawUsesViewportTransformUvScissorAndOrderedTarget)
{
    using namespace Core::Gfx;
    constexpr unsigned COLOR = 0x28003000, IMAGE = 0x28040000;
    surface(COLOR, IMAGE, 8 * 8 * 4, 8, 8, 8, 1);
    Gx2CommandStream stream;
    Gx2Command color{Gx2Cmd::SetColorBuffer};
    color.gpr[0] = COLOR;
    stream.push(color);
    Gx2Command clear{Gx2Cmd::ClearColor};
    clear.gpr[0] = COLOR;
    clear.fpr = {0, 0, 1, 1};
    stream.push(clear);
    Gx2Command viewport{Gx2Cmd::SetViewport};
    viewport.fpr = {0, 0, 8, 8, 0, 1};
    stream.push(viewport);
    Gx2Command scissor{Gx2Cmd::SetScissor};
    scissor.gpr = {3, 1, 4, 6};
    stream.push(scissor);
    Gx2Command image{Gx2Cmd::DrawImage};
    image.gpr = {2, 2};
    image.payload.resize(36);
    for (unsigned i = 0; i < 4; ++i) {
        image.payload[i * 5] = std::bit_cast<unsigned>(i < 2 ? .5f : 1.f);
        image.payload[16 + i * 5] = std::bit_cast<unsigned>(1.f);
    }
    // Collapse UVs to the bottom-right texel: the UV transform is independent
    // of the clip transform and must survive call-time capture.
    image.payload[16] = image.payload[21] = 0;
    image.payload[19] = image.payload[23] = std::bit_cast<unsigned>(1.f);
    const std::array<std::uint8_t, 16> pixels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 200, 50, 25, 255};
    std::memcpy(image.payload.data() + 32, pixels.data(), pixels.size());
    stream.push(image);
    Gx2Command copy{Gx2Cmd::CopyColorBufferToScanBuffer};
    copy.gpr = {COLOR, 1};
    stream.push(copy);
    stream.push(Gx2Command{Gx2Cmd::SwapScanBuffers});
    Gx2Replayer reference, native;
    std::shared_ptr<VulkanRasterBackend> backend;
    if (std::getenv("WEMU_TEST_VULKAN")) {
        backend = std::make_shared<VulkanRasterBackend>(true, true);
        native.setRasterBackend(backend);
    }
    reference.replay(stream, &cpu->m_memory);
    native.replay(stream, &cpu->m_memory);
    EXPECT_EQ(native.framebuffer(), reference.framebuffer());
    const auto &frame = reference.framebuffer();
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x) {
            const bool covered = x >= 3 && x < 6 && y >= 2 && y < 6;
            const auto offset = (y * 1280 + x) * 4;
            EXPECT_EQ(frame[offset], covered ? 200 : 0) << x << ',' << y;
            EXPECT_EQ(frame[offset + 1], covered ? 50 : 0);
            EXPECT_EQ(frame[offset + 2], covered ? 25 : 255);
            EXPECT_EQ(frame[offset + 3], 255);
        }
    if (backend)
        EXPECT_EQ(backend->completedDraws(), 1u) << backend->lastError();
}

TEST_F(Gx2CaptureTest, LinearVolumeAttachmentsKeepDistinctRenderedSlices)
{
    using namespace Core::Gfx;
    constexpr unsigned VS = 0x28005000, PS = 0x28006000, VSP = 0x28007000, PSP = 0x28008000;
    constexpr unsigned FETCH = 0x28001000, ATTRS = 0x28003000, VERTS = 0x28010000;
    constexpr unsigned COLOR = 0x28009000, SECOND = 0x2800A000;
    const auto record = [&](const char *name, std::initializer_list<unsigned> args) {
        unsigned reg = 3;
        for (const auto arg: args)
            cpu->m_gpr[reg++] = arg;
        call(name);
    };
    const std::array<unsigned, 8> attrs{0, 0, 0, 0x813, 0, 0, 0x00010203, 3};
    for (unsigned i = 0; i < attrs.size(); ++i)
        cpu->m_memory.write<unsigned>(ATTRS + i * 4, attrs[i]);
    record("GX2InitFetchShaderEx", {FETCH, 0x28002000, 1, ATTRS, 0, 0});
    const std::array<std::array<float, 4>, 3> vertices{{{-1, 1, 0, 1}, {3, 1, 0, 1}, {-1, -3, 0, 1}}};
    for (unsigned v = 0; v < vertices.size(); ++v)
        for (unsigned c = 0; c < 4; ++c)
            cpu->m_memory.write<unsigned>(VERTS + v * 16 + c * 4, std::bit_cast<unsigned>(vertices[v][c]));
    for (unsigned i = 0; i < 0x200; i += 4) {
        cpu->m_memory.write<unsigned>(VS + i, 0);
        cpu->m_memory.write<unsigned>(PS + i, 0);
    }
    cpu->m_memory.write<unsigned>(VS + 0xC, 1);
    cpu->m_memory.write<unsigned>(VS + 0x10, 0xFFFFFF00);
    cpu->m_memory.write<unsigned>(VS + 0x14, 0xFFFFFFFF);
    cpu->m_memory.write<unsigned>(VS + 0x18, 0xFFFFFFFF);
    const std::array<unsigned, 2> vertex{60u | (1u << 13) | (1u << 15), (0x28u << 23) | (1u << 21) | 0x688u};
    std::vector<unsigned> pixel{2, (8u << 26) | (7u << 18), 0, (0x28u << 23) | (1u << 21) | (1u << 17) | 0x688u};
    for (unsigned reg = 0; reg < 2; ++reg)
        for (unsigned c = 0; c < 4; ++c) {
            pixel.push_back((c == reg || c == 3 ? 249u : 248u) | (c == 3 ? 1u << 31 : 0));
            pixel.push_back(16u | (0x19u << 7) | (reg << 21) | (c << 29));
        }
    const auto shader = [&](unsigned header, unsigned offset, unsigned ptr, const auto &words) {
        cpu->m_memory.write<unsigned>(header + offset, words.size() * 4);
        cpu->m_memory.write<unsigned>(header + offset + 4, ptr);
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<unsigned>(ptr + i * 4, std::byteswap(words[i]));
    };
    shader(VS, 0xD0, VSP, vertex);
    shader(PS, 0xA4, PSP, pixel);
    surface(COLOR, 0x28040000, 256, 8, 8, 8, 1);
    surface(SECOND, 0x28040000, 512, 8, 8, 8, 1);
    cpu->m_memory.write<unsigned>(COLOR + 0x20, 512);
    for (const auto header: {COLOR, SECOND}) {
        cpu->m_memory.write<unsigned>(header, 2);
        cpu->m_memory.write<unsigned>(header + 0xC, 2);
    }
    for (const auto header: {COLOR, SECOND}) {
        cpu->m_memory.write<unsigned>(header + 0x74, 0);
        cpu->m_memory.write<unsigned>(header + 0x78, 0);
        cpu->m_memory.write<unsigned>(header + 0x7C, 1);
    }
    cpu->m_memory.write<unsigned>(SECOND + 0x78, 1);
    record("GX2SetColorBuffer", {COLOR, 0});
    record("GX2SetColorBuffer", {SECOND, 1});
    record("GX2SetColorControl", {0xCC, 0, 0, 1});
    record("GX2SetTargetChannelMasks", {15, 15, 0, 0, 0, 0, 0, 0});
    cpu->m_fpr[1] = cpu->m_fpr[2] = 0;
    cpu->m_fpr[3] = cpu->m_fpr[4] = 8;
    record("GX2SetViewport", {});
    record("GX2SetFetchShader", {FETCH});
    record("GX2SetVertexShader", {VS});
    record("GX2SetPixelShader", {PS});
    record("GX2SetAttribBuffer", {0, 48, 16, VERTS});
    record("GX2DrawEx", {4, 3, 0, 1});
    record("GX2CopyColorBufferToScanBuffer", {COLOR, 1});
    const auto captured = gx2Stream();
    for (bool native: {false, true}) {
        if (native && !std::getenv("WEMU_TEST_VULKAN"))
            continue;
        Gx2Replayer replay;
        auto backend = std::make_shared<VulkanRasterBackend>(true, true);
        if (native)
            replay.setRasterBackend(backend);
        replay.setNativeVerificationInterval(1);
        replay.replay(captured, &cpu->m_memory);
        EXPECT_EQ((std::array{replay.framebuffer()[0], replay.framebuffer()[1], replay.framebuffer()[2], replay.framebuffer()[3]}),
                  (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
        if (native) {
            EXPECT_EQ(backend->completedDraws(), 2u) << backend->lastError();
            EXPECT_EQ(replay.differingNativeDraws(), 0u);
        }
        gx2Stream().clear();
        record("GX2CopyColorBufferToScanBuffer", {SECOND, 1});
        replay.replay(gx2Stream(), &cpu->m_memory);
        EXPECT_EQ((std::array{replay.framebuffer()[0], replay.framebuffer()[1], replay.framebuffer()[2], replay.framebuffer()[3]}),
                  (std::array<std::uint8_t, 4>{0, 255, 0, 255}));
    }
}
