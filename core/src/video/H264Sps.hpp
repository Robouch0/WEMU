#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Core::Video {
    // FFmpeg's public frame parser does not expose SPS dimensions until a slice arrives.
    // Cafe also queries SPS-only buffers, so read just the bounded geometry prefix here.
    class SpsBits {
        public:
            explicit SpsBits(std::span<const std::uint8_t> data) : data(data) {}
            std::uint32_t bits(unsigned count)
            {
                if (count > 32 || position + count > data.size() * 8)
                    throw std::runtime_error("truncated SPS");
                std::uint32_t result = 0;
                for (unsigned i = 0; i < count; ++i, ++position)
                    result = (result << 1) | ((data[position / 8] >> (7 - position % 8)) & 1);
                return result;
            }
            std::uint32_t ue(std::uint32_t maximum = 0x7FFFFFFE)
            {
                unsigned zeros = 0;
                while (!bits(1))
                    if (++zeros > 30)
                        throw std::runtime_error("SPS Exp-Golomb overflow");
                const auto value = ((1u << zeros) - 1) + bits(zeros);
                if (value > maximum)
                    throw std::runtime_error("SPS field out of range");
                return value;
            }
            std::int32_t se()
            {
                const auto value = ue();
                return value & 1 ? std::int32_t((value + 1) / 2) : -std::int32_t(value / 2);
            }

        private:
            std::span<const std::uint8_t> data;
            std::size_t position{};
    };

    inline bool spsImageSize(std::span<const std::uint8_t> bytes, std::uint32_t &width, std::uint32_t &height)
    {
        for (std::size_t i = 0; i + 3 < bytes.size(); ++i) {
            if (bytes[i] || bytes[i + 1] || bytes[i + 2] != 1 || (bytes[i + 3] & 0x9F) != 7)
                continue;
            auto end = i + 4;
            while (end + 2 < bytes.size() && (bytes[end] || bytes[end + 1] || bytes[end + 2] != 1))
                ++end;
            if (end + 2 >= bytes.size())
                end = bytes.size();
            std::vector<std::uint8_t> rbsp;
            unsigned zeros = 0;
            for (auto pos = i + 4; pos < end; ++pos) {
                const auto value = bytes[pos];
                if (zeros >= 2 && value == 3) {
                    if (pos + 1 >= end || bytes[pos + 1] > 3)
                        return false;
                    zeros = 0;
                    continue;
                }
                rbsp.push_back(value);
                zeros = value == 0 ? zeros + 1 : 0;
            }
            try {
                SpsBits reader(rbsp);
                const auto profile = reader.bits(8);
                if (profile != 66 && profile != 77 && profile != 100)
                    return false;
                if (reader.bits(8) & 3)
                    return false;
                reader.bits(8); // level_idc
                reader.ue(31); // seq_parameter_set_id
                if (profile == 100) {
                    const auto chroma = reader.ue(3);
                    if (chroma == 3)
                        reader.bits(1);
                    reader.ue(6); // bit_depth_luma_minus8
                    reader.ue(6); // bit_depth_chroma_minus8
                    reader.bits(1);
                    if (reader.bits(1)) {
                        for (unsigned list = 0; list < (chroma == 3 ? 12u : 8u); ++list) {
                            if (!reader.bits(1))
                                continue;
                            int last = 8, next = 8;
                            for (unsigned element = 0; element < (list < 6 ? 16u : 64u); ++element) {
                                if (next)
                                    next = (last + reader.se()) & 255;
                                if (next)
                                    last = next;
                            }
                        }
                    }
                }
                reader.ue(12); // log2_max_frame_num_minus4
                const auto poc = reader.ue(2);
                if (poc == 0)
                    reader.ue(12);
                else if (poc == 1) {
                    reader.bits(1);
                    reader.se();
                    reader.se();
                    const auto cycle = reader.ue(255);
                    for (unsigned j = 0; j < cycle; ++j)
                        reader.se();
                }
                reader.ue(16); // max_num_ref_frames
                reader.bits(1);
                const auto mbWidth = reader.ue(255) + 1;
                const auto mapHeight = reader.ue(255) + 1;
                const auto frameOnly = reader.bits(1);
                if (!frameOnly)
                    reader.bits(1);
                reader.bits(1); // direct_8x8_inference_flag
                if (reader.bits(1))
                    for (unsigned j = 0; j < 4; ++j)
                        reader.ue(4096);
                reader.bits(1); // vui_parameters_present_flag; not needed for coded geometry
                const auto codedHeight = mapHeight * 16 * (2 - frameOnly);
                if (codedHeight > 4096)
                    return false;
                width = mbWidth * 16;
                height = codedHeight;
                return true;
            } catch (const std::runtime_error &) {
                return false;
            }
        }
        return false;
    }
} // namespace Core::Video
