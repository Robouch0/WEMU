#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numeric>

#include "TestFixture.hpp"
#include "audio/MovieAudio.hpp"
#include "audio/Output.hpp"
#include "audio/Resampler.hpp"
#include "audio/TimeStretcher.hpp"
#include "audio/Voice.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Ax.hpp"

TEST(AudioOutputTest, PreservesStereoScalesAtReadAndZeroFillsUnderruns)
{
    Core::Audio::Output output(0);
    const std::array<std::int16_t, 4> source{1000, -1000, 32767, -32768};
    output.submit(source);
    output.setVolume(50);
    std::array<std::int16_t, 6> received{};
    output.read(received);
    EXPECT_EQ(received, (std::array<std::int16_t, 6>{500, -500, 16383, -16384, 0, 0}));
    EXPECT_EQ(output.queuedFrames(), 0u);
    output.submit(source);
    output.setVolume(0);
    output.read(received);
    EXPECT_EQ(received, (std::array<std::int16_t, 6>{}));
    output.pause(true);
    output.submit(source);
    EXPECT_EQ(output.queuedFrames(), 0u);
    output.pause(false);
    output.submit(source);
    output.pause(true);
    EXPECT_EQ(output.queuedFrames(), 0u);
}

TEST(AudioOutputTest, OverflowDropsOldestWholeFramesAndWrapsWithoutUnboundedLatency)
{
    Core::Audio::Output output(0);
    std::vector<std::int16_t> source(Core::Audio::Output::capacityFrames * 2 + 4);
    std::iota(source.begin(), source.end(), 0);
    output.submit(source);
    EXPECT_EQ(output.queuedFrames(), Core::Audio::Output::capacityFrames);
    std::array<std::int16_t, 2> received{};
    output.read(received);
    EXPECT_EQ(received, (std::array<std::int16_t, 2>{4, 5}));
    output.submit(std::array<std::int16_t, 4>{-1, -2, -3, -4});
    output.read(received);
    EXPECT_EQ(received, (std::array<std::int16_t, 2>{8, 9}));
    std::vector<std::int16_t> remainder((Core::Audio::Output::capacityFrames - 1) * 2);
    output.read(remainder);
    EXPECT_EQ(remainder[remainder.size() - 4], -1);
    EXPECT_EQ(remainder.back(), -4);
}

TEST(AudioOutputTest, OpensPausesClosesAndReopensDummyDevice)
{
    // Only this explicit integration test opens a device; no speakers needed.
    ASSERT_EQ(setenv("SDL_AUDIODRIVER", "dummy", 1), 0);
    Core::Audio::Output output(0);
    ASSERT_TRUE(output.start());
    output.pause(true);
    EXPECT_EQ(output.queuedFrames(), 0u);
    output.stop();
    ASSERT_TRUE(output.start());
    output.pause(false);
    output.stop();
    unsetenv("SDL_AUDIODRIVER");
}

TEST(AudioOutputTest, StartupAndRecoveryWaitForHeadroomAndFadeAtGaps)
{
    Core::Audio::Output output(960, false);
    std::vector<float> small(128 * 2, 1000), full(1920 * 2, 1000);
    std::array<std::int16_t, 1024> block{};
    output.submit(small);
    output.read(block);
    EXPECT_TRUE(output.statistics().buffering);
    EXPECT_EQ(output.queuedFrames(), 128u);
    EXPECT_TRUE(std::all_of(block.begin(), block.end(), [](auto value) { return value == 0; }));
    output.submit(full);
    output.read(block);
    EXPECT_FALSE(output.statistics().buffering);
    EXPECT_GT(block.front(), 0);
    EXPECT_LT(block.front(), 10);
    EXPECT_EQ(block.back(), 1000);
    std::vector<std::int16_t> exhausted(2048 * 2);
    output.read(exhausted);
    EXPECT_EQ(output.statistics().underruns, 1u);
    EXPECT_EQ(output.statistics().missingFrames, 512u);
    EXPECT_TRUE(output.statistics().buffering);
    EXPECT_EQ(exhausted[1536 * 2 - 1], 0);
    output.submit(small);
    output.read(block);
    EXPECT_EQ(output.queuedFrames(), 128u); // Keep short arrivals until recovery is ready.
    EXPECT_TRUE(std::all_of(block.begin(), block.end(), [](auto value) { return value == 0; }));
    output.submit(full);
    output.read(block);
    EXPECT_LT(block.front(), 10);
    EXPECT_EQ(block.back(), 1000);
    output.pause(true);
    output.read(block);
    EXPECT_EQ(output.queuedFrames(), 0u);
    EXPECT_TRUE(std::all_of(block.begin(), block.end(), [](auto value) { return value == 0; }));
}

TEST(AudioOutputTest, ExactCallbackSizedArrivalsNeverTriggerFalseUnderrunsOrRefills)
{
    Core::Audio::Output output(512, false);
    std::array<float, 1024> source{};
    source.fill(10000);
    std::array<std::int16_t, 1024> received{};
    for (unsigned block = 0; block < 100; ++block) {
        output.submit(source);
        output.read(received);
        EXPECT_EQ(output.queuedFrames(), 0u);
        EXPECT_FALSE(output.statistics().buffering);
        EXPECT_EQ(output.statistics().underruns, 0u);
        EXPECT_EQ(received.back(), 10000);
        if (block)
            EXPECT_TRUE(std::all_of(received.begin(), received.end(), [](auto value) { return value == 10000; }));
    }
}

TEST(AudioOutputTest, BufferedPlaybackSurvivesBurstyFrameDeliveryWithoutChangingRate)
{
    const auto run = [](unsigned latency) {
        Core::Audio::Output output(latency, false);
        std::vector<float> packet(800 * 2, 12000);
        std::array<std::int16_t, 1024> block{};
        unsigned produced = 0;
        // 60 Hz delivery with up to 14 ms jitter; the host consumes 512 frames
        // each callback. Both clocks average exactly 48 kHz.
        for (unsigned now = 0; now < 48000 * 2; now += 512) {
            while (produced * 800 + (produced % 4) * 224 <= now) {
                output.submit(packet);
                ++produced;
            }
            output.read(block);
        }
        return output.statistics();
    };
    EXPECT_GT(run(0).underruns, 0u);
    const auto buffered = run(Core::Audio::Output::defaultLatencyFrames);
    EXPECT_EQ(buffered.underruns, 0u);
    EXPECT_EQ(buffered.droppedFrames, 0u);
    EXPECT_EQ(buffered.clippedSamples, 0u);
}

TEST(AudioOutputTest, VolumeIsAppliedBeforeClippingAndInvalidSamplesCannotReachDevice)
{
    Core::Audio::Output output(0);
    output.setVolume(25);
    output.submit(std::array<float, 6>{60000, -60000, NAN, INFINITY, 1.e20f, -1.e20f});
    std::array<std::int16_t, 6> received{};
    output.read(received);
    EXPECT_EQ(received, (std::array<std::int16_t, 6>{15000, -15000, 0, 0, 32767, -32768}));
    EXPECT_EQ(output.statistics().clippedSamples, 2u);
    EXPECT_EQ(output.statistics().invalidSamples, 2u);
}

TEST(AudioOutputTest, MutingWhileBufferingCannotPlayAnAudibleRecoveryRamp)
{
    Core::Audio::Output output(960, false);
    output.setVolume(0);
    std::vector<float> packet(1920 * 2, 30000);
    output.submit(packet);
    std::array<std::int16_t, 1024> block{};
    output.read(block);
    EXPECT_TRUE(std::all_of(block.begin(), block.end(), [](auto value) { return value == 0; }));
    EXPECT_FALSE(output.statistics().buffering);
}

TEST(AudioOutputTest, LiveVolumeChangesAndOverflowSplicesDoNotJumpBetweenSamples)
{
    Core::Audio::Output output(960, false);
    std::vector<float> constant(2400 * 2, 10000);
    output.submit(constant);
    std::array<std::int16_t, 1024> block{};
    output.read(block);
    EXPECT_EQ(block.back(), 10000);
    output.setVolume(0);
    output.read(block);
    EXPECT_LT(std::abs(block.front() - 10000), 30);
    EXPECT_EQ(block.back(), 0);
    for (unsigned i = 2; i < block.size(); i += 2)
        EXPECT_LE(std::abs(block[i] - block[i - 2]), 22);

    output.setVolume(100);
    output.read(block);
    EXPECT_EQ(block.back(), 10000);
    std::vector<float> replacement(Core::Audio::Output::capacityFrames * 2, -10000);
    output.submit(replacement);
    output.read(block);
    EXPECT_GT(output.statistics().droppedFrames, 0u);
    EXPECT_LE(std::abs(block.front() - 10000), 85);
    EXPECT_EQ(block.back(), -10000);
    for (unsigned i = 2; i < block.size(); i += 2)
        EXPECT_LE(std::abs(block[i] - block[i - 2]), 85);
}

TEST(AudioResamplerTest, ChunkedConversionMatchesOneContinuousStream)
{
    Core::Audio::LinearUpsampler complete, chunked;
    std::vector<float> input(96 * 20), reference(144 * 20), actual(reference.size());
    for (unsigned i = 0; i < input.size(); ++i)
        input[i] = std::sin(i * .731) * 10000;
    complete.convert(input, reference);
    for (unsigned block = 0; block < 20; ++block)
        chunked.convert(std::span<const float>(input).subspan(block * 96, 96), std::span<float>(actual).subspan(block * 144, 144));
    for (unsigned i = 0; i < reference.size(); ++i)
        EXPECT_FLOAT_EQ(actual[i], reference[i]);
    // For a ramp, every sample after startup advances exactly 2/3 source units,
    // including the transition between consecutive 3 ms AX frames.
    std::iota(input.begin(), input.end(), 0.f);
    chunked.reset();
    for (unsigned block = 0; block < 20; ++block)
        chunked.convert(std::span<const float>(input).subspan(block * 96, 96), std::span<float>(actual).subspan(block * 144, 144));
    for (unsigned i = 2; i < actual.size(); ++i)
        EXPECT_NEAR(actual[i] - actual[i - 1], 2.f / 3, .0001);
    chunked.reset();
    chunked.convert(std::span<const float>(input).first(96), std::span<float>(actual).first(144));
    EXPECT_EQ(actual[0], input[0]);
}

namespace {
    double toneFrequency(std::span<const std::int16_t> stereo, unsigned channel)
    {
        unsigned crossings = 0;
        for (std::size_t i = 2 + channel; i < stereo.size(); i += 2)
            crossings += stereo[i - 2] <= 0 && stereo[i] > 0;
        return crossings * 48000.0 / (stereo.size() / 2);
    }
} // namespace

TEST(AudioTimeStretchTest, SlowerTempoPreservesBothStereoPitchesAndIncreasesDuration)
{
    Core::Audio::TimeStretcher stretch;
    stretch.setTempo(.6);
    std::array<float, 288> input{}, received{};
    std::vector<std::int16_t> result;
    for (unsigned offset = 0; offset < 48000 * 3; offset += 144) {
        for (unsigned i = 0; i < 144; ++i) {
            input[i * 2] = std::sin((offset + i) * 440.0 * 2 * M_PI / 48000) * 10000;
            input[i * 2 + 1] = std::sin((offset + i) * 660.0 * 2 * M_PI / 48000) * 7000;
        }
        stretch.submit(input);
        while (const auto frames = stretch.receive(received))
            for (unsigned i = 0; i < frames * 2; ++i) {
                ASSERT_TRUE(std::isfinite(received[i]));
                result.push_back(std::clamp(received[i], -32768.f, 32767.f));
            }
    }
    EXPECT_GT(result.size() / 2, 48000u * 4.8);
    EXPECT_LT(result.size() / 2, 48000u * 5.1);
    EXPECT_NEAR(toneFrequency(result, 0), 440, 2);
    EXPECT_NEAR(toneFrequency(result, 1), 660, 2);
    stretch.reset();
    EXPECT_EQ(stretch.receive(received), 0u);
    EXPECT_THROW(stretch.setTempo(NAN), std::invalid_argument);
    EXPECT_THROW(stretch.setTempo(.1), std::invalid_argument);
}

TEST(AudioTimeStretchTest, PauseClearsProcessingHistoryWithoutLosingLearnedTempo)
{
    Core::Audio::Output output;
    std::array<float, 1600> source{};
    std::array<std::int16_t, 1024> received{};
    source.fill(10000);
    unsigned producer = 0;
    for (unsigned now = 0; now < 48000; now += 512) {
        while (std::uint64_t(producer) * 48000 <= std::uint64_t(now) * 40) {
            output.submit(source);
            ++producer;
        }
        output.read(received);
    }
    const auto tempo = output.statistics().tempo;
    EXPECT_NEAR(tempo, 2.0 / 3, .06);
    output.pause(true);
    output.pause(false);
    EXPECT_DOUBLE_EQ(output.statistics().tempo, tempo);
    EXPECT_EQ(output.queuedFrames(), 0u);
    source.fill(0);
    for (unsigned i = 0; i < 20; ++i)
        output.submit(source);
    output.read(received);
    EXPECT_TRUE(std::all_of(received.begin(), received.end(), [](auto value) { return value == 0; }));
    output.clear();
    EXPECT_DOUBLE_EQ(output.statistics().tempo, 1);
}

TEST(AudioTimeStretchTest, MissingInputEventuallyProducesSilenceInsteadOfRepeatingOldSound)
{
    Core::Audio::Output output;
    std::array<float, 1600> source{};
    source.fill(10000);
    for (unsigned i = 0; i < 8; ++i)
        output.submit(source);
    std::array<std::int16_t, 1024> received{};
    for (unsigned i = 0; i < 30; ++i)
        output.read(received);
    EXPECT_TRUE(output.statistics().buffering);
    EXPECT_TRUE(std::all_of(received.begin(), received.end(), [](auto value) { return value == 0; }));
}

TEST(AudioTimeStretchTest, Sustained35And40FpsDeliveryDoesNotCycleThroughSilence)
{
    const auto run = [](unsigned fps, bool enabled) {
        Core::Audio::Output output(Core::Audio::Output::defaultLatencyFrames, enabled);
        std::array<float, 1600> source{}; // 800 samples for each nominal 60 Hz frame
        std::array<std::int16_t, 1024> block{};
        std::vector<std::int16_t> steady;
        unsigned producer = 0;
        std::uint64_t warmUnderruns = 0;
        for (unsigned now = 0; now < 48000 * 6; now += 512) {
            // Up to 20 ms delivery jitter is independent of the sustained FPS.
            while (std::uint64_t(producer) * 48000 / fps + (producer % 4) * 320 <= now) {
                for (unsigned i = 0; i < 800; ++i) {
                    source[i * 2] = std::sin((producer * 800 + i) * 440.0 * 2 * M_PI / 48000) * 10000;
                    source[i * 2 + 1] = std::sin((producer * 800 + i) * 660.0 * 2 * M_PI / 48000) * 7000;
                }
                output.submit(source);
                ++producer;
            }
            output.read(block);
            if (now < 48000 * 2)
                warmUnderruns = output.statistics().underruns;
            else
                steady.insert(steady.end(), block.begin(), block.end());
        }
        const auto stats = output.statistics();
        if (enabled) {
            EXPECT_EQ(stats.underruns - warmUnderruns, 0u) << "fps=" << fps << " tempo=" << stats.tempo;
            EXPECT_EQ(stats.droppedFrames, 0u);
            EXPECT_NEAR(stats.tempo, fps / 60.0, .06);
            EXPECT_NEAR(toneFrequency(steady, 0), 440, 3);
            EXPECT_NEAR(toneFrequency(steady, 1), 660, 3);
            // No whole silent callback after settling, which the old recovery
            // loop produced repeatedly even with a smooth-looking picture.
            for (unsigned offset = 0; offset < steady.size(); offset += block.size()) {
                const auto end = std::min<std::size_t>(offset + block.size(), steady.size());
                double energy = 0;
                for (auto i = offset; i < end; ++i)
                    energy += double(steady[i]) * steady[i];
                EXPECT_GT(energy, 1.e8) << "silent callback at fps=" << fps;
            }
        }
        return stats.underruns - warmUnderruns;
    };
    EXPECT_GT(run(35, false), 5u);
    EXPECT_GT(run(40, false), 5u);
    run(35, true);
    run(40, true);
    run(60, true);
}

class AudioVoiceTest : public InstructionTest {
    protected:
        static constexpr unsigned data = 0x28000000;
        static Core::Audio::Voice pcm(unsigned count)
        {
            Core::Audio::Voice voice;
            voice.format = 10;
            voice.base = data;
            voice.current = data / 2;
            voice.loop = voice.current;
            voice.end = voice.current + count - 1;
            voice.state = 1;
            voice.envelope = 0x8000;
            return voice;
        }
};

TEST_F(AudioVoiceTest, Pcm16UsesBigEndianSignedSamplesAndStopsAfterInclusiveEnd)
{
    cpu->m_memory.write<std::int16_t>(data, -32768);
    cpu->m_memory.write<std::int16_t>(data + 2, 32767);
    auto voice = pcm(2);
    EXPECT_EQ(voice.render(cpu->m_memory), -32768);
    EXPECT_EQ(voice.render(cpu->m_memory), 32767);
    EXPECT_EQ(voice.state, 0u);
    EXPECT_EQ(voice.current, voice.end);
    EXPECT_EQ(voice.render(cpu->m_memory), 0);
}

TEST_F(AudioVoiceTest, Pcm8LoopsAndReportsLoopCountAcrossMultipleWraps)
{
    cpu->m_memory.write<std::int8_t>(data, -128);
    cpu->m_memory.write<std::int8_t>(data + 1, 127);
    auto voice = pcm(2);
    voice.format = 25;
    voice.current = voice.loop = data;
    voice.end = data + 1;
    voice.loopFlag = 1;
    for (unsigned i = 0; i < 6; ++i)
        EXPECT_EQ(voice.render(cpu->m_memory), i % 2 ? 32512 : -32768);
    EXPECT_EQ(voice.loopCount, 3u);
    EXPECT_EQ(voice.current, data);
    EXPECT_EQ(voice.state, 1u);
}

TEST_F(AudioVoiceTest, StreamingLoopCanJumpBeyondTheOldEndUntilTheCallbackMovesIt)
{
    for (unsigned i = 0; i < 8; ++i)
        cpu->m_memory.write<std::int16_t>(data + i * 2, 1000 + i * 100);
    auto voice = pcm(2);
    voice.loopFlag = 1;
    voice.loop = data / 2 + 4;
    EXPECT_EQ(voice.render(cpu->m_memory), 1000);
    EXPECT_EQ(voice.render(cpu->m_memory), 1100);
    EXPECT_EQ(voice.current, data / 2 + 4);
    EXPECT_EQ(voice.state, 1u);
    EXPECT_EQ(voice.render(cpu->m_memory), 1400);
    voice.end = data / 2 + 7;
    voice.loop = data / 2;
    EXPECT_EQ(voice.render(cpu->m_memory), 1500);
    EXPECT_EQ(voice.render(cpu->m_memory), 1600);
    EXPECT_EQ(voice.render(cpu->m_memory), 1700);
    EXPECT_EQ(voice.current, data / 2);
    EXPECT_EQ(voice.loopCount, 2u);
}

TEST_F(AudioVoiceTest, SrcInterpolatesAndPreservesFractionAndEnvelopeBetweenFrames)
{
    for (unsigned i = 0; i < 8; ++i)
        cpu->m_memory.write<std::int16_t>(data + i * 2, i * 1000);
    auto voice = pcm(8);
    voice.ratio = .5;
    for (unsigned i = 0; i < 8; ++i)
        EXPECT_EQ(voice.render(cpu->m_memory), i * 500);
    EXPECT_EQ(voice.current, data / 2 + 4);
    EXPECT_DOUBLE_EQ(voice.fraction, 0);
    voice.envelope = 0x4000;
    voice.envelopeDelta = -0x4000;
    EXPECT_EQ(voice.render(cpu->m_memory), 2000);
    EXPECT_EQ(voice.render(cpu->m_memory), 0);
    EXPECT_EQ(voice.envelope, 0);
}

TEST_F(AudioVoiceTest, AdpcmUsesPredictorHistoryFrameHeadersAndLoopContext)
{
    // Predictor 0 = previous sample, signed residuals 1, -2, 3, ...
    cpu->m_memory.write<std::uint8_t>(data, 0);
    cpu->m_memory.write<std::uint8_t>(data + 1, 0x1E);
    for (unsigned i = 2; i < 8; ++i)
        cpu->m_memory.write<std::uint8_t>(data + i, 0x33);
    cpu->m_memory.write<std::uint8_t>(data + 8, 1); // next frame scales residual by 2
    cpu->m_memory.write<std::uint8_t>(data + 9, 0x11);
    auto voice = pcm(1);
    voice.format = 0;
    voice.current = voice.loop = std::uint64_t(data) * 2 + 2;
    voice.end = std::uint64_t(data) * 2 + 18;
    voice.coefficients[0] = 2048;
    voice.history1 = 100;
    EXPECT_EQ(voice.render(cpu->m_memory), 101);
    EXPECT_EQ(voice.render(cpu->m_memory), 99);
    for (unsigned i = 0; i < 12; ++i)
        voice.render(cpu->m_memory);
    EXPECT_EQ(voice.current, std::uint64_t(data) * 2 + 18);
    EXPECT_EQ(voice.predScale, 1);
    EXPECT_EQ(voice.render(cpu->m_memory), 137);
    EXPECT_EQ(voice.state, 0u);

    voice.state = 1;
    voice.loopFlag = 1;
    voice.loopPredScale = 2;
    voice.loopHistory1 = 200;
    voice.render(cpu->m_memory);
    EXPECT_EQ(voice.current, voice.loop);
    EXPECT_EQ(voice.render(cpu->m_memory), 204);
    EXPECT_EQ(voice.loopCount, 1u);
}

TEST_F(AudioVoiceTest, AdpcmUsesBothOldHistorySamplesBeforeShiftingThem)
{
    cpu->m_memory.write<std::uint8_t>(data + 1, 0);
    auto voice = pcm(1);
    voice.format = 0;
    voice.current = voice.end = std::uint64_t(data) * 2 + 2;
    voice.coefficients[0] = 1024;
    voice.coefficients[1] = 512;
    voice.history1 = 100;
    voice.history2 = 200;
    EXPECT_EQ(voice.render(cpu->m_memory), 100);
    EXPECT_EQ(voice.history1, 100);
    EXPECT_EQ(voice.history2, 100);
}

TEST_F(AudioVoiceTest, AdpcmInterpolationDoesNotMutateHistoryAndInvalidPointersStaySilent)
{
    cpu->m_memory.write<std::uint8_t>(data + 1, 0x11);
    auto voice = pcm(1);
    voice.format = 0;
    voice.current = std::uint64_t(data) * 2 + 2;
    voice.end = voice.current + 1;
    voice.coefficients[0] = 2048;
    voice.history1 = 100;
    voice.ratio = .5;
    EXPECT_EQ(voice.render(cpu->m_memory), 101);
    EXPECT_EQ(voice.history1, 100);
    EXPECT_EQ(voice.render(cpu->m_memory), 101.5f);
    EXPECT_EQ(voice.history1, 101);
    voice.current = UINT64_MAX;
    voice.end = UINT64_MAX;
    EXPECT_EQ(voice.sample(cpu->m_memory), 0);
}

class AudioHleTest : public InstructionTest {
    protected:
        static constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterAxFunctions();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_scheduler.bootstrap(*cpu);
            Core::Audio::output().setLatencyFrames(0);
            Core::Audio::output().pause(false);
            Core::Audio::output().setVolume(100);
        }
        void TearDown() override
        {
            call("AXQuit");
            RegisterAxFunctions();
            cpu->m_scheduler = Core::Scheduler{};
        }
        static unsigned call(const char *name, std::initializer_list<unsigned> args = {})
        {
            unsigned reg = 3;
            for (auto value: args)
                cpu->m_gpr[reg++] = value;
            auto handler = Core::syscallHandler.get(name);
            EXPECT_NE(handler, nullptr) << name;
            if (handler)
                handler(*cpu);
            return cpu->m_gpr[3];
        }
        static unsigned allocate(unsigned bytes) { return cpu->m_memory.heapAllocate(bytes, 32); }
        static unsigned voice(unsigned envelope = 0x8000, unsigned bus = 0)
        {
            const auto v = call("AXAcquireVoice", {17, app + 0x80, 0x1234});
            const auto data = allocate(1024), offsets = allocate(20), mix = allocate(96), ve = allocate(4);
            for (unsigned i = 0; i < 512; ++i)
                cpu->m_memory.write<std::int16_t>(data + i * 2, 1000);
            cpu->m_memory.write<std::uint16_t>(offsets, 10);
            cpu->m_memory.write<std::uint16_t>(offsets + 2, 1);
            cpu->m_memory.write<unsigned>(offsets + 4, 0);
            cpu->m_memory.write<unsigned>(offsets + 8, 511);
            cpu->m_memory.write<unsigned>(offsets + 12, 0);
            cpu->m_memory.write<unsigned>(offsets + 16, data);
            call("AXSetVoiceOffsets", {v, offsets});
            cpu->m_memory.write<std::uint16_t>(ve, envelope);
            cpu->m_memory.write<std::uint16_t>(ve + 2, 0);
            call("AXSetVoiceVe", {v, ve});
            for (unsigned i = 0; i < 96; i += 4)
                cpu->m_memory.write<unsigned>(mix + i, 0);
            cpu->m_memory.write<std::uint16_t>(mix + bus * 4, 0x8000);
            cpu->m_memory.write<std::uint16_t>(mix + 16 + bus * 4, 0x4000);
            call("AXSetVoiceDeviceMix", {v, 0, 0, mix});
            return v;
        }
        static void tick()
        {
            cpu->m_scheduler.advanceTicks(186469);
            Core::Ax::OnFrameTick(*cpu);
            ASSERT_TRUE(cpu->m_scheduler.preempt(*cpu));
        }
        static unsigned plane(unsigned parameter, unsigned channel)
        {
            return cpu->m_memory.read<unsigned>(cpu->m_memory.read<unsigned>(parameter) + channel * 4);
        }
};

TEST_F(AudioHleTest, ApplicationUpdatesPrecedeMixingAndFinalCallbackEditsReachHost)
{
    call("AXInit");
    const auto v = voice(0x4000);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(v + 0x1C), 17u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(v + 0x20), app + 0x80);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(v + 0x24), 0x1234u);
    call("AXSetDeviceVolume", {0, 0, 0x4000});
    call("AXRegisterAppFrameCallback", {app + 0x100});
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x200});
    tick();
    EXPECT_EQ(cpu->m_pc, 0x100u);
    call("AXSetVoiceState", {v, 1}); // Emulate the app frame callback starting its voice.
    Core::Ax::OnSentinelReturn(*cpu);
    EXPECT_EQ(cpu->m_pc, 0x200u);
    const auto left = plane(cpu->m_gpr[3], 0), right = plane(cpu->m_gpr[3], 1);
    EXPECT_EQ(cpu->m_memory.read<std::int32_t>(left), 250);
    EXPECT_EQ(cpu->m_memory.read<std::int32_t>(right), 125);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(v + 0x40), 96u);
    cpu->m_memory.write<std::int32_t>(left, 1234);
    cpu->m_memory.write<std::int32_t>(right, -222);
    Core::Ax::OnSentinelReturn(*cpu);
    std::array<std::int16_t, 2> output{};
    Core::Audio::output().read(output);
    EXPECT_EQ(output, (std::array<std::int16_t, 2>{1234, -222}));
}

TEST_F(AudioHleTest, AuxReceivesPlanarBusAndUserDataBeforeReturningIntoFinalMix)
{
    call("AXInit");
    const auto v = voice(0x8000, 1);
    call("AXSetVoiceState", {v, 1});
    call("AXRegisterAuxCallback", {0, 0, 0, app + 0x300, 0x12345678});
    call("AXSetAuxReturnVolume", {0, 0, 0, 0x4000});
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x400});
    tick();
    EXPECT_EQ(cpu->m_pc, 0x300u);
    EXPECT_EQ(cpu->m_gpr[4], 0x12345678u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(cpu->m_gpr[5]), 6u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(cpu->m_gpr[5] + 4), 96u);
    const auto data = cpu->m_memory.read<unsigned>(cpu->m_gpr[3]);
    EXPECT_EQ(cpu->m_memory.read<std::int32_t>(data), 1000);
    cpu->m_memory.write<std::int32_t>(data, 2000);
    Core::Ax::OnSentinelReturn(*cpu);
    EXPECT_EQ(cpu->m_pc, 0x400u);
    EXPECT_EQ(cpu->m_memory.read<std::int32_t>(plane(cpu->m_gpr[3], 0)), 1000);
    Core::Ax::OnSentinelReturn(*cpu);
}

TEST_F(AudioHleTest, VoicesPlayWithoutAnyGuestCallbackAnd48KhzConsumes144Samples)
{
    const auto params = allocate(12);
    cpu->m_memory.write<unsigned>(params, 1);
    call("AXInitWithParams", {params});
    EXPECT_EQ(call("AXGetInputSamplesPerFrame"), 144u);
    EXPECT_EQ(call("AXGetInputSamplesPerSec"), 48000u);
    const auto v = voice();
    call("AXSetVoiceState", {v, 1});
    tick();
    EXPECT_EQ(Core::Audio::output().queuedFrames(), 144u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(v + 0x40), 144u);
    std::array<std::int16_t, 2> output{};
    Core::Audio::output().read(output);
    EXPECT_EQ(output, (std::array<std::int16_t, 2>{1000, 500}));
    Core::Ax::OnSentinelReturn(*cpu);
    EXPECT_EQ(cpu->m_scheduler.threads().back()->wakeTick, 372938u);
    cpu->m_fpr[1] = NAN;
    EXPECT_EQ(call("AXSetVoiceSrcRatio", {v}), unsigned(-2));
    cpu->m_fpr[1] = -1;
    EXPECT_EQ(call("AXSetVoiceSrcRatio", {v}), unsigned(-1));
    EXPECT_EQ(call("AXSetVoiceDeviceMix", {v, 8, 0, 0}), unsigned(-1));
}

TEST_F(AudioHleTest, PreUpsamplingFinalMixUses96SamplesAndDrcNeverDoublesTvAudio)
{
    call("AXInit");
    call("AXSetDeviceUpsampleStage", {0, 1});
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x100});
    call("AXRegisterDeviceFinalMixCallback", {1, app + 0x200});
    tick();
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(cpu->m_gpr[3] + 6), 96);
    const auto tv = plane(cpu->m_gpr[3], 0);
    cpu->m_memory.write<std::int32_t>(tv, 1000);
    cpu->m_memory.write<std::int32_t>(tv + 4, 4000);
    Core::Ax::OnSentinelReturn(*cpu);
    const auto drc = plane(cpu->m_gpr[3], 0);
    cpu->m_memory.write<std::int32_t>(drc, 30000);
    Core::Ax::OnSentinelReturn(*cpu);
    std::array<std::int16_t, 4> output{};
    Core::Audio::output().read(output);
    EXPECT_EQ(output[0], 1000);
    EXPECT_EQ(output[1], 0);
    EXPECT_EQ(output[2], 1000);
    // The causal converter carries the previous input sample into each frame.
}

TEST_F(AudioHleTest, FinalMixClampsToPcm16AndQuitDiscardsQueuedSoundAndCallbacks)
{
    call("AXInit");
    const auto v = voice();
    call("AXSetVoiceState", {v, 1});
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x100});
    tick();
    cpu->m_memory.write<std::int32_t>(plane(cpu->m_gpr[3], 0), INT32_MAX);
    cpu->m_memory.write<std::int32_t>(plane(cpu->m_gpr[3], 1), INT32_MIN);
    Core::Ax::OnSentinelReturn(*cpu);
    std::array<std::int16_t, 2> output{};
    Core::Audio::output().read(output);
    EXPECT_EQ(output, (std::array<std::int16_t, 2>{32767, -32768}));
    call("AXQuit");
    EXPECT_EQ(Core::Audio::output().queuedFrames(), 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(v + 4), 0u);
    EXPECT_EQ(call("AXIsInit"), 0u);
    const auto callback = allocate(4);
    call("AXGetDeviceFinalMixCallback", {0, callback});
    EXPECT_EQ(cpu->m_memory.read<unsigned>(callback), 0u);
    call("AXInit");
    EXPECT_EQ(call("AXIsInit"), 1u);
    EXPECT_EQ(call("AXGetInputSamplesPerFrame"), 96u);
    EXPECT_NE(call("AXAcquireVoiceEx", {3, app + 0x300, 0x5678}), 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(cpu->m_gpr[3] + 0x20), 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(cpu->m_gpr[3] + 0x48), app + 0x300);
}

TEST_F(AudioHleTest, UnregisteredEffectBusesDoNotAddDuplicateDryAudio)
{
    call("AXInit");
    const auto dry = voice(), wet = voice(0x8000, 1);
    call("AXSetVoiceState", {dry, 1});
    call("AXSetVoiceState", {wet, 1});
    tick();
    std::array<std::int16_t, 2> result{};
    Core::Audio::output().read(result);
    EXPECT_EQ(result, (std::array<std::int16_t, 2>{1000, 500}));
    Core::Ax::OnSentinelReturn(*cpu);
}

TEST_F(AudioHleTest, StereoIgnoresSurroundAndLfePlanes)
{
    call("AXInit");
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x100});
    tick();
    const auto parameter = cpu->m_gpr[3];
    for (unsigned ch = 0; ch < 6; ++ch)
        cpu->m_memory.write<std::int32_t>(plane(parameter, ch), ch < 2 ? 1000 + ch * 1000 : 30000);
    Core::Ax::OnSentinelReturn(*cpu);
    std::array<std::int16_t, 2> result{};
    Core::Audio::output().read(result);
    EXPECT_EQ(result, (std::array<std::int16_t, 2>{1000, 2000}));
}

TEST_F(AudioHleTest, MonoOutputDuplicatesOnlyItsMainChannel)
{
    call("AXInit");
    call("AXSetDeviceMode", {0, 5});
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x100});
    tick();
    const auto parameter = cpu->m_gpr[3];
    cpu->m_memory.write<std::int32_t>(plane(parameter, 0), -1234);
    cpu->m_memory.write<std::int32_t>(plane(parameter, 1), 30000);
    Core::Ax::OnSentinelReturn(*cpu);
    std::array<std::int16_t, 2> result{};
    Core::Audio::output().read(result);
    EXPECT_EQ(result, (std::array<std::int16_t, 2>{-1234, -1234}));
}

TEST_F(AudioHleTest, SurroundDownmixHasHeadroomAndDoesNotBoostLfe)
{
    call("AXInit");
    call("AXSetDeviceMode", {0, 3});
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x100});
    tick();
    const auto parameter = cpu->m_gpr[3];
    for (unsigned ch = 0; ch < 6; ++ch)
        cpu->m_memory.write<std::int32_t>(plane(parameter, ch), ch == 5 ? INT32_MAX : 30000);
    Core::Ax::OnSentinelReturn(*cpu);
    std::array<std::int16_t, 2> result{};
    Core::Audio::output().read(result);
    EXPECT_NEAR(result[0], 30000, 1);
    EXPECT_NEAR(result[1], 30000, 1);
}

TEST_F(AudioHleTest, AuxAndFinalBuffersRetainHeadroomBeforeMasterAndHostVolume)
{
    call("AXInit");
    for (unsigned i = 0; i < 2; ++i) {
        const auto v = voice(0x8000, 1);
        const auto source = cpu->m_memory.read<unsigned>(v + 0x44);
        for (unsigned sample = 0; sample < 512; ++sample)
            cpu->m_memory.write<std::int16_t>(source + sample * 2, 30000);
        call("AXSetVoiceState", {v, 1});
    }
    call("AXRegisterAuxCallback", {0, 0, 0, app + 0x100, 0});
    call("AXSetDeviceVolume", {0, 0, 0x4000});
    call("AXRegisterDeviceFinalMixCallback", {0, app + 0x200});
    tick();
    EXPECT_EQ(cpu->m_memory.read<std::int32_t>(cpu->m_memory.read<unsigned>(cpu->m_gpr[3])), 60000);
    Core::Ax::OnSentinelReturn(*cpu);
    EXPECT_EQ(cpu->m_memory.read<std::int32_t>(plane(cpu->m_gpr[3], 0)), 30000);
    // Final callback can supply values outside PCM16; host attenuation must
    // happen before saturation so turning down volume can preserve the signal.
    cpu->m_memory.write<std::int32_t>(plane(cpu->m_gpr[3], 0), 60000);
    Core::Audio::output().setVolume(25);
    Core::Ax::OnSentinelReturn(*cpu);
    std::array<std::int16_t, 2> result{};
    Core::Audio::output().read(result);
    EXPECT_EQ(result[0], 15000);
}

TEST(MovieAudioTest, DecodesResamplesSeeksAndDrainsAacInBoundedBlocks)
{
    const auto path = std::filesystem::path(__FILE__).parent_path() / "data/menu-audio.bin";
    Core::Audio::MovieAudio audio(path.string());
    ASSERT_TRUE(audio.available());
    std::vector<float> samples(4800 * 2);
    audio.mix(.2, samples);
    const double energy = std::inner_product(samples.begin(), samples.end(), samples.begin(), 0.0);
    EXPECT_GT(energy, 1.e8);
    for (unsigned i = 0; i < samples.size(); i += 2)
        EXPECT_NEAR(samples[i], samples[i + 1], 2);
    Core::Audio::MovieAudio reference(path.string());
    std::vector<float> first(288), resumed(288);
    reference.mix(0, first);
    audio.mix(0, resumed);
    for (unsigned i = 0; i < first.size(); ++i)
        EXPECT_NEAR(first[i], resumed[i], 1);
    std::fill(samples.begin(), samples.end(), 0);
    audio.mix(1.5, samples);
    EXPECT_TRUE(std::all_of(samples.begin(), samples.end(), [](float value) { return value == 0; }));
    EXPECT_THROW(audio.mix(NAN, samples), std::runtime_error);
    EXPECT_THROW(audio.mix(-1, samples), std::runtime_error);
}

TEST(MovieAudioTest, ConsecutiveAudioFramesMatchAContinuousDecodeAcrossPacketBoundaries)
{
    const auto path = std::filesystem::path(__FILE__).parent_path() / "data/menu-audio.bin";
    Core::Audio::MovieAudio contiguous(path.string()), segmented(path.string());
    std::vector<float> expected(144 * 2 * 40), actual(expected.size());
    contiguous.mix(.2, expected);
    for (unsigned frame = 0; frame < 40; ++frame)
        segmented.mix(.2 + frame * .003, std::span<float>(actual).subspan(frame * 288, 288));
    for (unsigned i = 0; i < expected.size(); ++i)
        EXPECT_FLOAT_EQ(actual[i], expected[i]);
}

TEST(MovieAudioTest, VideoOnlyFilesRemainSilent)
{
    const auto path = std::filesystem::path(__FILE__).parent_path() / "data/menu-video.bin";
    Core::Audio::MovieAudio audio(path.string());
    EXPECT_FALSE(audio.available());
    std::array<float, 288> samples{};
    audio.mix(0, samples);
    EXPECT_EQ(samples, (std::array<float, 288>{}));
}

TEST(MovieAudioTest, OptionalLocalMovieProducesFiniteNonSilentSound)
{
    const char *path = std::getenv("WEMU_TEST_MOVIE");
    if (!path)
        GTEST_SKIP() << "Set WEMU_TEST_MOVIE to validate a local cinematic soundtrack";
    Core::Audio::MovieAudio audio(path);
    ASSERT_TRUE(audio.available());
    std::vector<float> samples(48000 * 2);
    audio.mix(1, samples);
    EXPECT_TRUE(std::all_of(samples.begin(), samples.end(), [](float sample) { return std::isfinite(sample); }));
    EXPECT_GT(std::inner_product(samples.begin(), samples.end(), samples.begin(), 0.0), 1.e7);
}
