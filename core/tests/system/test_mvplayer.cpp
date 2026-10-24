#include <cmath>
#include <filesystem>
#include <numeric>

#include "TestFixture.hpp"
#include "audio/Output.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "gfx/Gx2CommandStream.hpp"
#include "hle/Ax.hpp"
#include "hle/CoreinitExtra.hpp"
#include "hle/Fs.hpp"
#include "hle/MvPlayer.hpp"
#include "video/MovieDecoder.hpp"

// Generated with ffmpeg testsrc2=size=64x48:rate=10:duration=2,
// libx264, yuv420p, keyframe interval 5, two B frames, MP4 container.
namespace {
    const auto movieData = std::filesystem::path(__FILE__).parent_path() / "data";
}
TEST(MovieDecoderTest, DemuxesReorderedFramesSeeksAndDrainsWithoutRepeatingDecode)
{
    Core::Video::MovieDecoder decoder((movieData / "menu-video.bin").string());
    EXPECT_EQ(decoder.width(), 64u);
    EXPECT_EQ(decoder.height(), 48u);
    EXPECT_NEAR(decoder.duration(), 2, .001);
    const auto first = decoder.frameAt(0);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->rgba.size(), 64u * 48 * 4);
    EXPECT_DOUBLE_EQ(first->timestamp, 0);
    EXPECT_EQ(decoder.frameAt(.01), first);
    const auto middle = decoder.frameAt(.7);
    ASSERT_TRUE(middle);
    EXPECT_NEAR(middle->timestamp, .7, .001);
    EXPECT_NE(middle->rgba, first->rgba);
    decoder.seek(.2);
    const auto earlier = decoder.frameAt(.2);
    ASSERT_TRUE(earlier);
    EXPECT_NEAR(earlier->timestamp, .2, .001);
    EXPECT_GT(earlier->serial, middle->serial);
    const auto last = decoder.frameAt(2);
    ASSERT_TRUE(last);
    EXPECT_NEAR(last->timestamp, 1.9, .001);
    EXPECT_EQ(decoder.frameAt(3), last);
    EXPECT_EQ(decoder.frameAt(0)->rgba, first->rgba);
    EXPECT_THROW(decoder.seek(-1), std::runtime_error);
    EXPECT_THROW(decoder.seek(1.e300), std::runtime_error);
    EXPECT_THROW(decoder.frameAt(NAN), std::runtime_error);
    EXPECT_THROW(Core::Video::MovieDecoder((movieData / "missing.mp4").string()), std::runtime_error);
}

class MvPlayerTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterCoreinitExtraFunctions();
            InstallMVPlayerHooks(*cpu);
            SetFsContentRoot(movieData.string());
            Core::Gfx::gx2Stream().clear();
        }
        static void call(const char *name) { Core::syscallHandler.get(name)(*cpu); }
        static unsigned create()
        {
            call("Create__8MVPlayerSFv");
            return cpu->m_gpr[3];
        }
        static void method(unsigned object, unsigned slot)
        {
            cpu->m_gpr[3] = object;
            cpu->m_hooks.at(0x0FFD0000 + slot * 4)(*cpu);
        }
};
TEST_F(MvPlayerTest, IndependentPlayersUseGuestClockSeekAndPreserveQueuedPixelsAfterDestroy)
{
    const unsigned object = create(), second = create();
    EXPECT_NE(object, second);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(object + 0xD0 + 0xA4), object);
    const char uri[] = "file:///vol/content/menu-video.bin";
    constexpr unsigned path = 0x28000000;
    for (unsigned i = 0; i < sizeof(uri); ++i)
        cpu->m_memory.write<std::uint8_t>(path + i, uri[i]);
    cpu->m_gpr[4] = path;
    method(object, 3);
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    method(object, 5);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(object + 0x1C), 2u);
    method(object, 7);
    cpu->m_scheduler.advanceFrame(6215625);
    method(object, 21);
    EXPECT_NEAR(cpu->m_fpr[1], .1, 1.e-6);
    method(second, 21);
    EXPECT_DOUBLE_EQ(cpu->m_fpr[1], 0);
    method(object, 13);
    cpu->m_scheduler.advanceFrame(6215625);
    method(object, 21);
    EXPECT_NEAR(cpu->m_fpr[1], .1, 1.e-6);
    cpu->m_fpr[1] = .7;
    method(object, 17);
    method(object, 21);
    EXPECT_DOUBLE_EQ(cpu->m_fpr[1], .7);
    cpu->m_gpr[3] = object + 0xD0;
    call("Draw__6MVViewFv");
    const auto &commands = Core::Gfx::gx2Stream().commands();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].type, Core::Gfx::Gx2Cmd::DrawImage);
    EXPECT_EQ(commands[0].gpr[0], 64u);
    EXPECT_EQ(commands[0].gpr[1], 48u);
    EXPECT_EQ(commands[0].payload.size(), 32u + 64 * 48);
    const auto copied = commands[0].payload;
    cpu->m_gpr[3] = object;
    call("Destroy__8MVPlayerSFP8MVPlayer");
    EXPECT_EQ(commands[0].payload, copied);
    cpu->m_gpr[3] = object + 0xD0;
    call("Draw__6MVViewFv");
    EXPECT_EQ(commands.size(), 1u);
    EXPECT_TRUE(ResolveFsContentPath("/vol/content/../../save/no").empty());
}

TEST_F(MvPlayerTest, MovieAudioRunsWithoutAxInitRespectsDeviceMixAndStopsWithThePlayer)
{
    RegisterAxFunctions();
    cpu->m_scheduler = Core::Scheduler{};
    cpu->m_scheduler.bootstrap(*cpu);
    const auto object = create();
    const char uri[] = "file:///vol/content/menu-audio.bin";
    constexpr unsigned path = 0x28000000;
    for (unsigned i = 0; i < sizeof(uri); ++i)
        cpu->m_memory.write<std::uint8_t>(path + i, uri[i]);
    cpu->m_gpr[4] = path;
    method(object, 3);
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    cpu->m_fpr[1] = .2;
    method(object, 17);
    method(object, 7);
    EXPECT_TRUE(Core::MvPlayer::hasPlayingAudio(*cpu));
    cpu->m_scheduler.advanceTicks(186469);
    Core::Ax::OnFrameTick(*cpu);
    ASSERT_TRUE(cpu->m_scheduler.preempt(*cpu));
    std::array<std::int16_t, 288> output{};
    Core::Audio::output().read(output);
    EXPECT_GT(std::inner_product(output.begin(), output.end(), output.begin(), 0.0), 1.e7);
    Core::Ax::OnSentinelReturn(*cpu);
    cpu->m_gpr[3] = cpu->m_gpr[4] = cpu->m_gpr[5] = 0;
    call("AXSetDeviceVolume");
    cpu->m_scheduler.advanceTicks(186469);
    ASSERT_TRUE(cpu->m_scheduler.preempt(*cpu));
    Core::Ax::OnSentinelReturn(*cpu);
    Core::Audio::output().read(output);
    EXPECT_EQ(output, (std::array<std::int16_t, 288>{}));
    EXPECT_TRUE(Core::MvPlayer::hasPlayingAudio(*cpu));

    cpu->m_gpr[3] = cpu->m_gpr[4] = 0;
    cpu->m_gpr[5] = 0x8000;
    call("AXSetDeviceVolume");
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = Core::Memory::MemoryMap::ApplicationCode + 0x400;
    call("AXRegisterDeviceFinalMixCallback");
    cpu->m_scheduler.advanceTicks(186469);
    ASSERT_TRUE(cpu->m_scheduler.preempt(*cpu));
    Core::Ax::OnSentinelReturn(*cpu);
    ASSERT_EQ(cpu->m_pc, 0x400u);
    const auto pointers = cpu->m_memory.read<unsigned>(cpu->m_gpr[3]);
    double energy = 0;
    for (unsigned ch = 0; ch < 2; ++ch) {
        const auto plane = cpu->m_memory.read<unsigned>(pointers + ch * 4);
        for (unsigned i = 0; i < 144; ++i) {
            const double sample = cpu->m_memory.read<std::int32_t>(plane + i * 4);
            energy += sample * sample;
            cpu->m_memory.write<std::int32_t>(plane + i * 4, 0);
        }
    }
    EXPECT_GT(energy, 1.e7);
    Core::Ax::OnSentinelReturn(*cpu);
    Core::Audio::output().read(output);
    EXPECT_EQ(output, (std::array<std::int16_t, 288>{}));
    method(object, 13);
    EXPECT_FALSE(Core::MvPlayer::hasPlayingAudio(*cpu));
    std::array<float, 288> stopped{};
    Core::MvPlayer::mixAudio(*cpu, stopped, cpu->m_scheduler.now());
    EXPECT_EQ(stopped, (std::array<float, 288>{}));
    cpu->m_gpr[3] = object;
    call("Destroy__8MVPlayerSFP8MVPlayer");
    RegisterAxFunctions();
    cpu->m_scheduler = Core::Scheduler{};
}
