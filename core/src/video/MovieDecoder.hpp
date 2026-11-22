#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Core::Video {
    struct MovieFrame {
            unsigned width{}, height{};
            double timestamp{};
            std::uint64_t serial{};
            std::vector<std::uint8_t> rgba;
    };

    // Demuxes local movie files. Playback policy and the guest clock belong to
    // the caller; seeking flushes codec reorder state before decoding again.
    class MovieDecoder {
        public:
            explicit MovieDecoder(const std::string &path);
            ~MovieDecoder();
            MovieDecoder(const MovieDecoder &) = delete;
            MovieDecoder &operator=(const MovieDecoder &) = delete;
            void seek(double seconds);
            std::shared_ptr<const MovieFrame> frameAt(double seconds);
            [[nodiscard]] double duration() const;
            [[nodiscard]] unsigned width() const;
            [[nodiscard]] unsigned height() const;

        private:
            struct Impl;
            std::unique_ptr<Impl> impl;
    };
} // namespace Core::Video
