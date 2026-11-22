#include <array>
#include <chrono>
#include <gtest/gtest.h>
#include <vector>

#include "ui/PauseMenu.hpp"

namespace {
    using Core::UI::FrameRate;
    using Core::UI::PauseMenu;
    SDL_Event key(SDL_Scancode scancode, bool down = true, bool repeat = false)
    {
        SDL_Event event{};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.scancode = scancode;
        event.key.repeat = repeat;
        return event;
    }
    void press(PauseMenu &menu, SDL_Scancode scancode)
    {
        menu.handleEvent(key(scancode), 1280, 720);
        menu.handleEvent(key(scancode, false), 1280, 720);
    }
    void pause(PauseMenu &menu)
    {
        press(menu, SDL_SCANCODE_O);
        menu.acknowledgePause({});
    }
} // namespace

TEST(PauseMenuTest, PauseWaitsForAcknowledgementAndIgnoresRepeat)
{
    PauseMenu menu;
    EXPECT_FALSE(menu.handleEvent(key(SDL_SCANCODE_O, true, true), 1280, 720));
    EXPECT_EQ(menu.state(), PauseMenu::State::Running);
    press(menu, SDL_SCANCODE_O);
    EXPECT_EQ(menu.state(), PauseMenu::State::PauseRequested);
    press(menu, SDL_SCANCODE_RETURN);
    EXPECT_EQ(menu.state(), PauseMenu::State::PauseRequested);
    menu.acknowledgePause({});
    EXPECT_EQ(menu.state(), PauseMenu::State::Paused);
    menu.handleEvent(key(SDL_SCANCODE_O, true, true), 1280, 720);
    EXPECT_EQ(menu.state(), PauseMenu::State::Paused);
    press(menu, SDL_SCANCODE_O);
    EXPECT_EQ(menu.state(), PauseMenu::State::Running);
}

TEST(PauseMenuTest, FpsTogglePersistsAcrossResumeAndRepeatedPauses)
{
    PauseMenu menu;
    pause(menu);
    press(menu, SDL_SCANCODE_UP);
    EXPECT_EQ(menu.selection(), 2);
    press(menu, SDL_SCANCODE_DOWN);
    EXPECT_EQ(menu.selection(), 0);
    press(menu, SDL_SCANCODE_DOWN);
    press(menu, SDL_SCANCODE_RETURN);
    EXPECT_TRUE(menu.showFps());
    EXPECT_EQ(menu.state(), PauseMenu::State::Paused);
    press(menu, SDL_SCANCODE_O);
    pause(menu);
    EXPECT_TRUE(menu.showFps());
    EXPECT_EQ(menu.selection(), 0);
    press(menu, SDL_SCANCODE_DOWN);
    press(menu, SDL_SCANCODE_RETURN);
    EXPECT_FALSE(menu.showFps());
}

TEST(PauseMenuTest, QuittingRequiresExplicitConfirmationAndCancelKeepsPause)
{
    PauseMenu menu;
    pause(menu);
    press(menu, SDL_SCANCODE_UP);
    press(menu, SDL_SCANCODE_RETURN);
    EXPECT_TRUE(menu.confirmingQuit());
    EXPECT_EQ(menu.selection(), 0);
    press(menu, SDL_SCANCODE_RETURN);
    EXPECT_FALSE(menu.confirmingQuit());
    EXPECT_EQ(menu.state(), PauseMenu::State::Paused);
    press(menu, SDL_SCANCODE_RETURN);
    press(menu, SDL_SCANCODE_DOWN);
    press(menu, SDL_SCANCODE_RETURN);
    EXPECT_EQ(menu.state(), PauseMenu::State::Stopping);
    press(menu, SDL_SCANCODE_O);
    EXPECT_EQ(menu.state(), PauseMenu::State::Stopping);
}

TEST(PauseMenuTest, MenuAndHeldKeysStaySuppressedUntilTheirRelease)
{
    PauseMenu menu;
    press(menu, SDL_SCANCODE_O);
    std::array<std::uint8_t, SDL_NUM_SCANCODES> held{};
    held[SDL_SCANCODE_W] = 1;
    menu.acknowledgePause(held);
    EXPECT_TRUE(menu.suppress(SDL_SCANCODE_RETURN));
    menu.handleEvent(key(SDL_SCANCODE_RETURN), 1280, 720);
    EXPECT_EQ(menu.state(), PauseMenu::State::Running);
    EXPECT_TRUE(menu.suppress(SDL_SCANCODE_RETURN));
    EXPECT_TRUE(menu.suppress(SDL_SCANCODE_W));
    EXPECT_FALSE(menu.suppress(SDL_SCANCODE_A));
    menu.handleEvent(key(SDL_SCANCODE_W, false), 1280, 720);
    menu.handleEvent(key(SDL_SCANCODE_RETURN, false), 1280, 720);
    EXPECT_FALSE(menu.suppress(SDL_SCANCODE_W));
    EXPECT_FALSE(menu.suppress(SDL_SCANCODE_RETURN));
}

TEST(PauseMenuTest, ResizedMouseCoordinatesMatchMenuRows)
{
    PauseMenu menu;
    pause(menu);
    SDL_Event event{};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 320;
    event.button.y = 190; // second row, at half-size
    EXPECT_TRUE(menu.handleEvent(event, 640, 360));
    EXPECT_TRUE(menu.showFps());
    event.button.y = 221;
    menu.handleEvent(event, 640, 360);
    EXPECT_TRUE(menu.confirmingQuit());
    event.button.y = 190;
    menu.handleEvent(event, 640, 360);
    EXPECT_EQ(menu.state(), PauseMenu::State::Stopping);
}

TEST(PauseMenuTest, WindowCloseStopsEvenWhileConfirmingQuit)
{
    PauseMenu menu;
    pause(menu);
    press(menu, SDL_SCANCODE_UP);
    press(menu, SDL_SCANCODE_RETURN);
    SDL_Event event{};
    event.type = SDL_QUIT;
    menu.handleEvent(event, 1280, 720);
    EXPECT_EQ(menu.state(), PauseMenu::State::Stopping);
}

TEST(PauseMenuTest, EscapeCancelsConfirmationThenResumes)
{
    PauseMenu menu;
    pause(menu);
    press(menu, SDL_SCANCODE_UP);
    press(menu, SDL_SCANCODE_RETURN);
    press(menu, SDL_SCANCODE_ESCAPE);
    EXPECT_FALSE(menu.confirmingQuit());
    EXPECT_EQ(menu.state(), PauseMenu::State::Paused);
    press(menu, SDL_SCANCODE_ESCAPE);
    EXPECT_EQ(menu.state(), PauseMenu::State::Running);
}

TEST(PauseMenuTest, OverlayDimsHostCopyAndPreservesAlphaAndSource)
{
    PauseMenu menu;
    pause(menu);
    const std::vector<std::uint8_t> original(1280 * 720 * 4, 200);
    auto pixels = original;
    menu.paint(pixels, 1280, 720);
    EXPECT_LT(pixels[0], original[0]);
    EXPECT_GT(pixels[0], 0);
    EXPECT_NE(pixels, original);
    for (std::size_t i = 3; i < pixels.size(); i += 4)
        ASSERT_EQ(pixels[i], original[i]);
    auto undersized = std::vector<std::uint8_t>(16, 29);
    menu.paint(undersized, 1280, 720);
    EXPECT_EQ(undersized, std::vector<std::uint8_t>(16, 29));
}

TEST(PauseMenuTest, FpsOverlayHasNoEffectOutsideItsPanel)
{
    std::vector<std::uint8_t> pixels(1280 * 720 * 4, 200);
    PauseMenu::paintFps(pixels, 1280, 720, 30, 7.25);
    EXPECT_EQ(pixels[0], 200);
    EXPECT_LT(pixels[(12 * 1280 + 12) * 4], 200);
    EXPECT_EQ(pixels[(100 * 1280 + 400) * 4], 200);
}

TEST(FrameRateTest, CountsFrameIntervalsAndExpiresStaleMovieValues)
{
    FrameRate rate;
    const auto start = FrameRate::Clock::time_point{};
    EXPECT_LT(rate.value(start), 0);
    for (unsigned i = 0; i <= 25; ++i)
        rate.record(start + std::chrono::milliseconds(40 * i));
    EXPECT_DOUBLE_EQ(rate.value(start + std::chrono::seconds(1)), 25);
    EXPECT_EQ(rate.total(), 26u);
    EXPECT_LT(rate.value(start + std::chrono::seconds(3)), 0);
}

TEST(FrameRateTest, ResumeResetExcludesPausedTimeAndRetainsEventTotals)
{
    FrameRate rate;
    const auto start = FrameRate::Clock::time_point{};
    for (unsigned i = 0; i <= 5; ++i)
        rate.record(start + std::chrono::milliseconds(200 * i));
    EXPECT_DOUBLE_EQ(rate.value(start + std::chrono::seconds(1)), 5);
    rate.reset();
    EXPECT_LT(rate.value(start + std::chrono::seconds(120)), 0);
    for (unsigned i = 0; i <= 25; ++i)
        rate.record(start + std::chrono::seconds(120) + std::chrono::milliseconds(40 * i));
    EXPECT_DOUBLE_EQ(rate.value(start + std::chrono::seconds(121)), 25);
    EXPECT_EQ(rate.total(), 32u);
}
