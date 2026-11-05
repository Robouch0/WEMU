#include "ui/PreloadScreen.hpp"

#include <algorithm>
#include <format>
#include <string_view>

#include "gfx/BitmapFont.hpp"

namespace Core::UI {
    std::vector<std::uint8_t> preloadScreen(std::size_t done, std::size_t total)
    {
        constexpr unsigned width = preloadScreenWidth, height = preloadScreenHeight, scale = 3;
        std::vector<std::uint8_t> frame(std::size_t(width) * height * 4);
        const auto fill = [&](unsigned x, unsigned y, unsigned w, unsigned h, std::uint8_t value) {
            for (unsigned yy = y; yy < std::min(y + h, height); ++yy)
                for (unsigned xx = x; xx < std::min(x + w, width); ++xx)
                    std::fill_n(frame.data() + (std::size_t(yy) * width + xx) * 4, 3, value);
        };
        const auto text = [&](std::string_view label, unsigned y) {
            auto x = (width - std::min(unsigned(label.size()) * 8 * scale, width)) / 2;
            for (const unsigned char c: label) {
                const auto &glyph = Gfx::kFont8x16[c < 128 ? c : '?'];
                for (unsigned row = 0; row < 16; ++row)
                    for (unsigned column = 0; column < 8; ++column)
                        if (glyph[row] & (0x80 >> column))
                            fill(x + column * scale, y + row * scale, scale, scale, 0xE0);
                x += 8 * scale;
            }
        };
        text("Preparing shaders", 280);
        text(std::format("{} / {}", std::min(done, total), total), 350);
        constexpr unsigned barWidth = 640, barX = (width - barWidth) / 2, barY = 430, barHeight = 12;
        fill(barX, barY, barWidth, barHeight, 0x40);
        fill(barX, barY, total ? unsigned(barWidth * std::min(done, total) / total) : barWidth, barHeight, 0xE0);
        return frame;
    }
} // namespace Core::UI
