#include "ui/PauseMenu.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

#include "gfx/BitmapFont.hpp"

namespace Core::UI {
    namespace {
        constexpr int panelWidth = 560, rowHeight = 64, firstRow = 294;
        using Color = std::array<std::uint8_t, 3>;
        void rectangle(std::span<std::uint8_t> pixels, unsigned width, unsigned height, int x, int y, int w, int h, Color color,
                       unsigned opacity = 255)
        {
            for (int yy = std::max(y, 0); yy < std::min(y + h, int(height)); ++yy)
                for (int xx = std::max(x, 0); xx < std::min(x + w, int(width)); ++xx)
                    for (unsigned c = 0; c < 3; ++c) {
                        auto &v = pixels[(std::size_t(yy) * width + unsigned(xx)) * 4 + c];
                        v = std::uint8_t((unsigned(v) * (255 - opacity) + unsigned(color[c]) * opacity + 127) / 255);
                    }
        }
        void text(std::span<std::uint8_t> pixels, unsigned width, unsigned height, int x, int y, std::string_view label, Color color, int scale = 2)
        {
            for (const unsigned char ch: label) {
                const auto &glyph = Gfx::kFont8x16[ch < 128 ? ch : '?'];
                for (int yy = 0; yy < 16; ++yy)
                    for (int xx = 0; xx < 8; ++xx)
                        if (glyph[yy] & (0x80 >> xx))
                            rectangle(pixels, width, height, x + xx * scale, y + yy * scale, scale, scale, color);
                x += 8 * scale;
            }
        }
        bool valid(std::span<std::uint8_t> pixels, unsigned width, unsigned height)
        {
            return width && height && std::uint64_t(width) * height * 4 == pixels.size();
        }
    } // namespace

    bool PauseMenu::suppress(SDL_Scancode key) const
    {
        return m_state != State::Running || (key >= 0 && key < SDL_NUM_SCANCODES && m_suppressed.test(unsigned(key)));
    }

    void PauseMenu::acknowledgePause(std::span<const std::uint8_t> heldKeys)
    {
        if (m_state != State::PauseRequested)
            return;
        for (unsigned i = 0; i < std::min(heldKeys.size(), std::size_t(SDL_NUM_SCANCODES)); ++i)
            if (heldKeys[i])
                m_suppressed.set(i);
        m_state = State::Paused;
        m_confirm = false;
        m_selection = 0;
    }

    void PauseMenu::activate()
    {
        if (m_confirm) {
            if (m_selection == 1)
                m_state = State::Stopping;
            else {
                m_confirm = false;
                m_selection = 2;
            }
        } else if (m_selection == 0)
            m_state = State::Running;
        else if (m_selection == 1)
            m_showFps = !m_showFps;
        else {
            m_confirm = true;
            m_selection = 0; // Cancel is the default.
        }
    }

    bool PauseMenu::handleEvent(const SDL_Event &event, int windowWidth, int windowHeight)
    {
        if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            m_state = State::Stopping;
            return true;
        }
        if (event.type == SDL_KEYUP) {
            const auto key = event.key.keysym.scancode;
            if (key >= 0 && key < SDL_NUM_SCANCODES)
                m_suppressed.reset(unsigned(key));
            return false;
        }
        if (event.type == SDL_KEYDOWN) {
            const auto key = event.key.keysym.scancode;
            if (m_state != State::Running || key == SDL_SCANCODE_O)
                if (key >= 0 && key < SDL_NUM_SCANCODES)
                    m_suppressed.set(unsigned(key));
            if (event.key.repeat || m_state == State::Stopping)
                return false;
            if (key == SDL_SCANCODE_O) {
                if (m_state == State::Running)
                    m_state = State::PauseRequested;
                else if (m_state == State::Paused)
                    m_state = State::Running;
                return true;
            }
            if (key == SDL_SCANCODE_ESCAPE) {
                if (m_state == State::Paused && m_confirm) {
                    m_confirm = false;
                    m_selection = 2;
                } else
                    m_state = m_state == State::Paused ? State::Running : State::Stopping;
                return true;
            }
            if (m_state != State::Paused)
                return false;
            const int count = m_confirm ? 2 : 3;
            if (key == SDL_SCANCODE_UP)
                m_selection = (m_selection + count - 1) % count;
            else if (key == SDL_SCANCODE_DOWN)
                m_selection = (m_selection + 1) % count;
            else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
                activate();
            else
                return false;
            return true;
        }
        if ((event.type == SDL_MOUSEMOTION || (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)) &&
            m_state == State::Paused && windowWidth > 0 && windowHeight > 0) {
            const int mx = event.type == SDL_MOUSEMOTION ? event.motion.x : event.button.x;
            const int my = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
            const auto x = std::int64_t(mx) * 1280 / windowWidth;
            const auto y = std::int64_t(my) * 720 / windowHeight;
            const int count = m_confirm ? 2 : 3;
            if (x >= (1280 - panelWidth) / 2 + 24 && x < (1280 + panelWidth) / 2 - 24 && y >= firstRow && y < firstRow + count * rowHeight) {
                m_selection = int((y - firstRow) / rowHeight);
                if (event.type == SDL_MOUSEBUTTONDOWN)
                    activate();
                return true;
            }
        }
        return event.type == SDL_WINDOWEVENT;
    }

    void PauseMenu::paint(std::span<std::uint8_t> pixels, unsigned width, unsigned height) const
    {
        if (!valid(pixels, width, height) || m_state != State::Paused)
            return;
        rectangle(pixels, width, height, 0, 0, int(width), int(height), {0, 0, 0}, 130);
        const int x = (int(width) - panelWidth) / 2, dy = (int(height) - 720) / 2;
        rectangle(pixels, width, height, x, 185 + dy, panelWidth, 368, {20, 27, 39}, 230);
        rectangle(pixels, width, height, x, 185 + dy, panelWidth, 3, {100, 180, 255});
        text(pixels, width, height, x + 32, 213 + dy, "WEMU - PAUSED", {240, 245, 255});
        if (m_confirm) {
            text(pixels, width, height, x + 32, 255 + dy, "Stop game? Unsaved progress will be lost.", {200, 208, 220}, 1);
        } else
            text(pixels, width, height, x + 32, 255 + dy, "Emulation is frozen. O resumes.", {200, 208, 220}, 1);
        const std::array<std::string, 3> labels =
                m_confirm ? std::array<std::string, 3>{"Cancel", "Stop and return to library", ""}
                          : std::array<std::string, 3>{"Resume", m_showFps ? "Show FPS: ON" : "Show FPS: OFF", "Return to library"};
        for (int i = 0; i < (m_confirm ? 2 : 3); ++i) {
            const int y = firstRow + dy + i * rowHeight;
            if (i == m_selection)
                rectangle(pixels, width, height, x + 24, y, panelWidth - 48, rowHeight - 8, {42, 92, 139});
            text(pixels, width, height, x + 40, y + 9, labels[i], {240, 245, 255});
        }
        text(pixels, width, height, x + 32, 516 + dy, "UP/DOWN select   ENTER accept   ESC back", {175, 188, 207}, 1);
    }

    void PauseMenu::paintFps(std::span<std::uint8_t> pixels, unsigned width, unsigned height, double presentFps, double movieFps)
    {
        if (!valid(pixels, width, height))
            return;
        rectangle(pixels, width, height, 12, 12, 340, 84, {10, 15, 22}, 210);
        const auto line = [](std::string_view label, double value) {
            return value < 0 ? std::format("{} FPS: --", label) : std::format("{} FPS: {:.1f}", label, value);
        };
        text(pixels, width, height, 24, 19, line("PRESENT", presentFps), {130, 245, 170});
        text(pixels, width, height, 24, 55, line("MOVIE", movieFps), {175, 210, 255});
    }

    void FrameRate::reset()
    {
        m_started = m_measured = false;
        m_intervals = 0;
        m_rate = 0;
    }

    void FrameRate::record(Clock::time_point now)
    {
        ++m_total;
        if (!m_started) {
            m_start = m_last = now;
            m_started = true;
            return;
        }
        ++m_intervals;
        m_last = now;
        const double elapsed = std::chrono::duration<double>(now - m_start).count();
        if (elapsed >= 1.0) {
            m_rate = double(m_intervals) / elapsed;
            m_measured = true;
            m_intervals = 0;
            m_start = now;
        }
    }

    double FrameRate::value(Clock::time_point now) const { return !m_measured || now - m_last >= std::chrono::seconds(2) ? -1.0 : m_rate; }
} // namespace Core::UI
