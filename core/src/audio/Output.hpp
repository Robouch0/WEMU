#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <span>

namespace Core::Audio {
    // Stereo signed PCM at 48 kHz. The host callback only consumes this bounded
    // queue; guest memory and guest callbacks stay on the emulation thread.
    class Output {
        public:
            static constexpr unsigned sampleRate = 48000, capacityFrames = 4800;
            ~Output();
            bool start();
            void stop();
            void pause(bool paused);
            void setVolume(unsigned percent);
            void submit(std::span<const std::int16_t> stereo);
            void read(std::span<std::int16_t> stereo);
            void clear();
            [[nodiscard]] unsigned queuedFrames() const;

        private:
            mutable std::mutex mutex;
            std::array<std::int16_t, capacityFrames * 2> samples{};
            unsigned begin{}, size{}, volume{100};
            std::uint32_t device{};
            bool ownsSubsystem{}, paused{};
    };
    Output &output();
} // namespace Core::Audio
