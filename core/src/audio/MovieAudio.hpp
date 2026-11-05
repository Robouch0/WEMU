#pragma once

#include <memory>
#include <span>
#include <string>

namespace Core::Audio {
    // Independent demux cursor: video lookahead must not consume audio packets.
    // Decodes one bounded block at a time and adds 48 kHz stereo to the AX mix.
    class MovieAudio {
        public:
            explicit MovieAudio(const std::string &path);
            ~MovieAudio();
            void mix(double seconds, std::span<float> stereo);
            [[nodiscard]] bool available() const;

        private:
            struct Impl;
            std::unique_ptr<Impl> impl;
    };
} // namespace Core::Audio
