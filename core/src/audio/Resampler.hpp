#pragma once

#include <span>
#include <stdexcept>

namespace Core::Audio {
    // Causal 32 -> 48 kHz linear interpolation. One history sample bridges
    // successive AX frames instead of holding the last sample every 3 ms.
    class LinearUpsampler {
        public:
            void reset() { primed = false; }
            void convert(std::span<const float> input, std::span<float> output)
            {
                if (input.empty() || input.size() % 2 || output.size() != input.size() * 3 / 2)
                    throw std::invalid_argument("Invalid AX upsampling frame size");
                if (!primed)
                    previous = input.front();
                for (std::size_t i = 0; i < output.size(); ++i) {
                    const int phase = int(i) * 2 - 2;
                    const int index = phase < 0 ? -1 : phase / 3;
                    const float fraction = float(phase - index * 3) / 3;
                    const float first = index < 0 ? previous : input[index];
                    output[i] = first + (input[index + 1] - first) * fraction;
                }
                previous = input.back();
                primed = true;
            }

        private:
            float previous{};
            bool primed{};
    };
} // namespace Core::Audio
