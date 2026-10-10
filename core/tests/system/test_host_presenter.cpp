#include <gtest/gtest.h>

#include "gfx/Renderer.hpp"

namespace {
    class TestPresenter final : public Core::Gfx::HostPresenter {
        public:
            bool open{true};
            std::vector<Command> commands, afterPause;
            std::vector<std::uint8_t> lastFrame;
            std::vector<bool> acknowledgements;
            Statistics lastStatistics{};
            unsigned frames{}, timedPolls{};
            bool connected() const override { return open; }
            std::vector<Command> poll(std::chrono::milliseconds wait) override
            {
                if (wait.count()) {
                    ++timedPolls;
                    return std::exchange(afterPause, {});
                }
                return std::exchange(commands, {});
            }
            bool present(std::span<const std::uint8_t> bytes, std::uint32_t, std::uint32_t, const Statistics &statistics) override
            {
                ++frames;
                lastFrame.assign(bytes.begin(), bytes.end());
                lastStatistics = statistics;
                return open;
            }
            void paused(bool paused, const Statistics &statistics) override
            {
                acknowledgements.push_back(paused);
                lastStatistics = statistics;
            }
    };
    using Action = Core::Gfx::HostPresenter::Action;
} // namespace

TEST(HostPresenterTest, RemoteFramesPreserveGuestPixelsWithoutAnSdlWindow)
{
    auto presenter = std::make_unique<TestPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    const std::vector<std::uint8_t> frame(4 * 4 * 4, 37);
    renderer.flip_tv(frame.data(), 4, 4);
    EXPECT_EQ(transport->lastFrame, frame);
    EXPECT_EQ(transport->frames, 1u);
    EXPECT_EQ(renderer.guest_present_count(), 1u);
}

TEST(HostPresenterTest, SafePauseAcknowledgementPreservesCountersAndStopsInput)
{
    auto presenter = std::make_unique<TestPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    transport->commands = {{Action::Buttons, 0x8000}};
    ASSERT_TRUE(renderer.poll_events());
    EXPECT_EQ(renderer.get_buttons(), 0x8000u);
    renderer.record_movie_frame();
    transport->commands = {{Action::Pause, 0}};
    transport->afterPause = {{Action::Buttons, 0x0200}, {Action::Resume, 0}};
    ASSERT_TRUE(renderer.poll_events());
    ASSERT_TRUE(renderer.pause_requested());
    bool checkpoint{};
    renderer.service_pause([&] {
        checkpoint = true;
        EXPECT_EQ(renderer.get_buttons(), 0u);
        EXPECT_EQ(renderer.movie_frame_count(), 1u);
        EXPECT_EQ(transport->acknowledgements, std::vector<bool>{true});
    });
    EXPECT_TRUE(checkpoint);
    EXPECT_EQ(transport->acknowledgements, (std::vector<bool>{true, false}));
    EXPECT_EQ(transport->frames, 0u);
    EXPECT_EQ(transport->timedPolls, 1u);
    EXPECT_EQ(renderer.get_buttons(), 0u);
    EXPECT_EQ(renderer.movie_frame_count(), 1u);
}

TEST(HostPresenterTest, StopDuringPauseDoesNotResumeTheGuest)
{
    auto presenter = std::make_unique<TestPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    transport->commands = {{Action::Pause, 0}};
    transport->afterPause = {{Action::Stop, 0}};
    ASSERT_TRUE(renderer.poll_events());
    renderer.service_pause([] {});
    EXPECT_FALSE(renderer.is_open());
    EXPECT_EQ(transport->acknowledgements, std::vector<bool>{true});
}

TEST(HostPresenterTest, WindowDisconnectStopsTheIsolatedCore)
{
    auto presenter = std::make_unique<TestPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    transport->open = false;
    EXPECT_FALSE(renderer.poll_events());
    EXPECT_FALSE(renderer.is_open());
}

TEST(HostPresenterTest, DisconnectWhilePresentingDoesNotTouchVulkanWindowResources)
{
    auto presenter = std::make_unique<TestPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    transport->open = false;
    const std::array<std::uint8_t, 4> frame{};
    renderer.flip_tv(frame.data(), 1, 1);
    EXPECT_FALSE(renderer.is_open());
}
