#pragma once

#include <array>
#include <cstdint>

namespace Core {
    // ACT returns a 96-byte CFL/FFL store record: packed fields and names are little
    // endian, while its CRC is big endian. Generate an offline avatar without system assets.
    constexpr std::array<std::uint8_t, 96> offlineMii()
    {
        std::array<std::uint8_t, 96> data{};
        const auto word = [&](unsigned offset, unsigned value) {
            data[offset] = value & 0xFF;
            data[offset + 1] = (value >> 8) & 0xFF;
        };
        // Version 3, copying allowed, Wii U origin.
        data[0] = 3;
        data[1] = 1;
        data[3] = 0x40;
        for (unsigned i = 0; i < 8; ++i)
            data[4 + i] = 0xA0 + i;
        data[12] = 0x80; // normal, persistent creator ID
        for (unsigned i = 0; i < 6; ++i)
            data[16 + i] = 0xA8 + i;
        word(0x18, (1 << 10) | (1 << 5) | (1 << 1)); // favorite color 1, January 1, male
        constexpr char16_t name[] = u"WEMU";
        for (unsigned i = 0; i < 4; ++i)
            word(0x1A + i * 2, name[i]);
        data[0x2E] = data[0x2F] = 64; // height and build
        word(0x32, 1); // hair
        word(0x34, (3 << 13) | (4 << 9) | 2); // eyes
        word(0x36, (12 << 9) | (2 << 5) | 4);
        word(0x38, (3 << 12) | (4 << 8)); // eyebrows
        word(0x3A, (7 << 9) | (2 << 5) | 6);
        word(0x3C, (9 << 9) | (4 << 5) | 1); // nose
        word(0x3E, (3 << 13) | (4 << 9)); // mouth
        word(0x40, 13);
        word(0x42, (10 << 10) | (4 << 6)); // facial hair (disabled)
        word(0x44, (10 << 11) | (4 << 7)); // glasses (disabled)
        word(0x46, (20 << 10) | (2 << 5) | (4 << 1)); // mole (disabled)

        std::uint16_t crc = 0;
        for (const auto byte: data) {
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = static_cast<std::uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
            crc ^= byte;
        }
        data[94] = crc >> 8;
        data[95] = crc & 0xFF;
        return data;
    }
} // namespace Core
