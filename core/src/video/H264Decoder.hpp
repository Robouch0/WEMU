#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Core::Video {
    struct DecodedFrame {
        std::uint32_t width{}, height{}, stride{};
        std::uint32_t cropTop{}, cropBottom{}, cropLeft{}, cropRight{};
        std::uint32_t guestBuffer{};
        double timestamp{};
        std::vector<std::uint8_t> nv12;
    };

    class H264Decoder {
    public:
        H264Decoder();
        ~H264Decoder();
        H264Decoder(const H264Decoder &) = delete;
        H264Decoder &operator=(const H264Decoder &) = delete;
        void reset();
        std::vector<DecodedFrame> decode(std::span<const std::uint8_t> bytes,
                                         double timestamp, std::uint32_t guestBuffer);
        std::vector<DecodedFrame> drain();
        static bool imageSize(std::span<const std::uint8_t> bytes, std::uint32_t &width, std::uint32_t &height);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
