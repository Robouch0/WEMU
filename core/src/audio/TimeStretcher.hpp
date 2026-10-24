#pragma once

#include <memory>
#include <span>

namespace Core::Audio {
    // Pitch-preserving tempo conversion of stereo samples in PCM16 amplitude
    // units. Processing belongs to the producer, never the SDL callback.
    class TimeStretcher {
        public:
            TimeStretcher();
            ~TimeStretcher();
            void reset();
            void setTempo(double tempo);
            void submit(std::span<const float> stereo);
            unsigned receive(std::span<float> stereo);

        private:
            struct Impl;
            std::unique_ptr<Impl> impl;
    };
} // namespace Core::Audio
