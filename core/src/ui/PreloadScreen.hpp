#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Core::UI {
    constexpr unsigned preloadScreenWidth = 1280, preloadScreenHeight = 720;
    // Black RGBX frame shown while recorded pipelines are created before a title starts:
    // "Preparing shaders", the done/total count and a progress bar.
    std::vector<std::uint8_t> preloadScreen(std::size_t done, std::size_t total);
} // namespace Core::UI
