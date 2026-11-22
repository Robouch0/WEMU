#include <gtest/gtest.h>

#include "utils/FrameProfiler.hpp"

namespace {
    std::uint64_t fakeTime{}, reads{};
    std::uint64_t fakeClock()
    {
        ++reads;
        return fakeTime;
    }
} // namespace

TEST(FrameProfilerTest, NestedStagesAndFrameBoundariesAccountForEachNanosecondOnce)
{
    using namespace Core::Profile;
    fakeTime = 0;
    Timeline clock(true, fakeClock);
    fakeTime = 10;
    {
        Scope hle(clock, Stage::Hle);
        fakeTime = 20;
        {
            Scope replay(clock, Stage::Replay);
            fakeTime = 25;
            {
                Scope wait(clock, Stage::GpuWait);
                fakeTime = 40;
                const auto frame = clock.frame();
                EXPECT_EQ(frame.wall, 40u);
                EXPECT_EQ(frame.time[std::size_t(Stage::Guest)], 10u);
                EXPECT_EQ(frame.time[std::size_t(Stage::Hle)], 10u);
                EXPECT_EQ(frame.time[std::size_t(Stage::Replay)], 5u);
                EXPECT_EQ(frame.time[std::size_t(Stage::GpuWait)], 15u);
                EXPECT_EQ(std::accumulate(frame.time.begin(), frame.time.end(), std::uint64_t{}), frame.wall);
                fakeTime = 50;
            }
            fakeTime = 60;
        }
        fakeTime = 80;
    }
    fakeTime = 100;
    const auto frame = clock.frame();
    EXPECT_EQ(frame.wall, 60u);
    EXPECT_EQ(frame.time[std::size_t(Stage::GpuWait)], 10u);
    EXPECT_EQ(frame.time[std::size_t(Stage::Replay)], 10u);
    EXPECT_EQ(frame.time[std::size_t(Stage::Hle)], 20u);
    EXPECT_EQ(frame.time[std::size_t(Stage::Guest)], 20u);
    EXPECT_EQ(std::accumulate(frame.time.begin(), frame.time.end(), std::uint64_t{}), frame.wall);
}

TEST(FrameProfilerTest, DisabledProfilingNeverReadsClockAndPauseIsSeparateFromActiveWork)
{
    using namespace Core::Profile;
    reads = 0;
    Timeline disabled(false, fakeClock);
    {
        Scope hle(disabled, Stage::Hle);
        disabled.reset();
        EXPECT_EQ(disabled.frame().wall, 0u);
    }
    EXPECT_EQ(reads, 0u);
    fakeTime = 0;
    Timeline enabled(true, fakeClock);
    fakeTime = 2;
    {
        Scope pause(enabled, Stage::Pause);
        fakeTime = 1002;
    }
    fakeTime = 1005;
    const auto frame = enabled.frame();
    EXPECT_EQ(frame.wall, 1005u);
    EXPECT_EQ(frame.time[std::size_t(Stage::Pause)], 1000u);
    EXPECT_EQ(frame.time[std::size_t(Stage::Guest)], 5u);
}
