#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace Core::Gfx {
    // Presentation/control transport, independent of the guest GPU and toolkit.
    class HostPresenter {
        public:
            enum class Action : std::uint32_t { Pause = 1, Resume, Stop, Buttons };
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
            virtual void paused(bool value, const Statistics &statistics) = 0;
    };
} // namespace Core::Gfx
