#pragma once

#include <SDL2/SDL.h>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <span>

namespace Core::UI {
    // Host session state; none of these transitions mutate guest state.
    class PauseMenu {
        public:
            enum class State { Running, PauseRequested, Paused, Stopping };
            [[nodiscard]] State state() const { return m_state; }
            [[nodiscard]] bool confirmingQuit() const { return m_confirm; }
            [[nodiscard]] int selection() const { return m_selection; }
            [[nodiscard]] bool showFps() const { return m_showFps; }
            void requestPause()
            {
                if (m_state == State::Running)
                    m_state = State::PauseRequested;
            }
            void resume()
            {
                if (m_state == State::Paused)
                    m_state = State::Running;
            }
            void stop() { m_state = State::Stopping; }
            [[nodiscard]] bool suppress(SDL_Scancode key) const;
            bool handleEvent(const SDL_Event &event, int windowWidth, int windowHeight);
            void acknowledgePause(std::span<const std::uint8_t> heldKeys);
            void paint(std::span<std::uint8_t> rgba, unsigned width, unsigned height) const;
            static void paintFps(std::span<std::uint8_t> rgba, unsigned width, unsigned height, double presentFps, double movieFps);

        private:
            void activate();
            State m_state{State::Running};
            bool m_confirm{}, m_showFps{};
            int m_selection{};
            std::bitset<SDL_NUM_SCANCODES> m_suppressed;
    };

    // Measures only guest events. Host overlay redraws never increment this counter.
    class FrameRate {
        public:
            using Clock = std::chrono::steady_clock;
            void record(Clock::time_point now);
            void reset();
            [[nodiscard]] double value(Clock::time_point now) const;
            [[nodiscard]] std::uint64_t total() const { return m_total; }

        private:
            Clock::time_point m_start{}, m_last{};
            std::uint64_t m_total{}, m_intervals{};
            double m_rate{};
            bool m_started{}, m_measured{};
    };
} // namespace Core::UI
