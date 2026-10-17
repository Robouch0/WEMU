#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace Core::Gfx {
    // Analog half of the Wii U GamePad (buttons travel through Action::Buttons so tap retention
    // keeps working). Produced by the host (e.g. a phone acting as the GamePad), read by VPADRead.
    struct GamepadState {
            std::array<float, 2> leftStick{}, rightStick{}; // -1..1, +y is up
            bool touched{};
            std::uint16_t touchX{}, touchY{}; // DRC screen pixels, 0..853 x 0..479
            std::array<float, 3> accel{0.0f, -1.0f, 0.0f}; // g, GamePad axes (at rest, face up: y = -1)
            std::array<float, 3> gyro{}; // revolutions per second around the GamePad axes
    };

    // Presentation/control transport, independent of the guest GPU and toolkit.
    class HostPresenter {
        public:
            enum class Action : std::uint32_t { Pause = 1, Resume, Stop, Buttons, DrcStream };
            struct Command {
                    Action action{};
                    std::uint32_t value{};
            };
            struct Statistics {
                    double presentFps{-1}, movieFps{-1};
                    std::uint64_t presents{}, movies{};
            };
            virtual ~HostPresenter() = default;
            virtual bool connected() const = 0;
            virtual std::vector<Command> poll(std::chrono::milliseconds wait = {}) = 0;
            virtual bool present(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, const Statistics &statistics) = 0;
            // The GamePad (DRC) screen. Only sent while the host asked for it (Action::DrcStream).
            virtual bool presentDrc(std::span<const std::uint8_t> /*rgba*/, std::uint32_t /*width*/, std::uint32_t /*height*/) { return true; }
            virtual void paused(bool value, const Statistics &statistics) = 0;
            // Latest analog GamePad state received from the host.
            [[nodiscard]] virtual GamepadState gamepad() const { return {}; }
    };
} // namespace Core::Gfx
