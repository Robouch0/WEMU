#pragma once

#include <cstdint>

namespace Core::Gfx {
    constexpr std::uint32_t triangleCount(std::uint32_t mode, std::uint32_t count)
    {
        switch (mode) {
            case 4:
                return count / 3;
            case 5:
            case 6:
                return count > 2 ? count - 2 : 0;
            case 0x13:
                return count / 4 * 2;
            case 0x14:
                return count > 3 ? (count / 2 - 1) * 2 : 0;
            default:
                return 0;
        }
    }

    // Emit triangle corners as offsets into the submitted index/vertex sequence.
    // Strip winding alternates so face culling agrees with triangle lists.
    template<class Emit>
    void assembleTriangles(std::uint32_t mode, std::uint32_t count, Emit emit)
    {
        switch (mode) {
            case 4: // TRIANGLES
                for (std::uint32_t i = 0; i + 2 < count; i += 3)
                    emit(i, i + 1, i + 2);
                break;
            case 5: // TRIANGLE_FAN
                for (std::uint32_t i = 1; i + 1 < count; ++i)
                    emit(0, i, i + 1);
                break;
            case 6: // TRIANGLE_STRIP
                for (std::uint32_t i = 0; i + 2 < count; ++i)
                    emit(i + (i & 1), i + ((i & 1) ^ 1), i + 2);
                break;
            case 0x13: // QUADS
                for (std::uint32_t i = 0; i + 3 < count; i += 4) {
                    emit(i, i + 1, i + 2);
                    emit(i, i + 2, i + 3);
                }
                break;
            case 0x14: // QUAD_STRIP
                for (std::uint32_t i = 0; i + 3 < count; i += 2) {
                    emit(i, i + 1, i + 3);
                    emit(i, i + 3, i + 2);
                }
                break;
        }
    }
} // namespace Core::Gfx
