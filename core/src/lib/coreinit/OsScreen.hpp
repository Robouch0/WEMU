/*
** EPITECH PROJECT, 2025
** core
** File description:
** coreinit — OSScreen HLE
*/

#pragma once

#include <bit>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "cpu/interpreter/Interpreter.hpp"

// ─── OSScreen ─────────────────────────────────────────────────────────────────
// Wii U framebuffer text/graphics output.
//   Screen 0 = SCREEN_TV  (1280×720,  RGBX8888)
//   Screen 1 = SCREEN_DRC (854×480,   RGBX8888)
//
// Framebuffer pixels are stored big-endian in guest memory (matches STW/LWZ).
// OSScreenPutFontEx writes to emulator stdout so text output is visible without
// a connected renderer.

struct OSScreenState {
    std::uint32_t buffer  = 0;     // virtual address of the guest framebuffer
    std::uint32_t size    = 0;     // framebuffer size in bytes
    bool          enabled = false;
};

inline OSScreenState g_osScreenState[2];

// Buffer sizes: width × height × 4 bytes/pixel, rounded to nearest 0x100.
static constexpr std::uint32_t kTvBufSize  = 1280u * 720u  * 4u; // 3,686,400 bytes
static constexpr std::uint32_t kDrcBufSize =  854u * 480u  * 4u; // 1,639,680 bytes

// void OSScreenInit(void)
inline void OSScreenInit(Core::Interpreter & /*cpu*/)
{
    g_osScreenState[0] = {};
    g_osScreenState[1] = {};
}

// uint32_t OSScreenGetBufferSizeEx(OSScreenID screen)
inline std::uint32_t OSScreenGetBufferSizeEx(Core::Interpreter & /*cpu*/,
                                              std::uint32_t screen)
{
    return (screen == 0) ? kTvBufSize : kDrcBufSize;
}

// void OSScreenSetBufferEx(OSScreenID screen, void *buffer)
// buffer is a virtual address returned by OSAllocFromSystem / MEMAllocFromDefaultHeapEx.
inline void OSScreenSetBufferEx(Core::Interpreter & /*cpu*/,
                                 std::uint32_t screen,
                                 std::uint32_t buffer)
{
    if (screen < 2) {
        g_osScreenState[screen].buffer = buffer;
        g_osScreenState[screen].size   = (screen == 0) ? kTvBufSize : kDrcBufSize;
    }
}

// void OSScreenEnableEx(OSScreenID screen, BOOL enable)
inline void OSScreenEnableEx(Core::Interpreter & /*cpu*/,
                               std::uint32_t screen,
                               std::uint32_t enable)
{
    if (screen < 2)
        g_osScreenState[screen].enabled = (enable != 0u);
}

// void OSScreenClearBuffersEx(OSScreenID screen, uint32_t colour)
// Fills the entire framebuffer with the given RGBX8888 colour.
inline void OSScreenClearBuffersEx(Core::Interpreter &cpu,
                                    std::uint32_t screen,
                                    std::uint32_t color)
{
    if (screen >= 2 || !g_osScreenState[screen].buffer) return;
    const auto       &st   = g_osScreenState[screen];
    const std::size_t host = cpu.m_binary.m_memory.translate(st.buffer);
    if (!host) return;

    // Pixels are big-endian in guest memory (same convention as STW).
    const std::uint32_t be = std::byteswap(color);
    auto *fb = reinterpret_cast<std::uint32_t *>(host);
    std::fill(fb, fb + st.size / 4u, be);
}

// void OSScreenPutFontEx(OSScreenID screen, uint32_t row, uint32_t col, const char *text)
// Prints the string to stdout so output is visible without a renderer.
inline void OSScreenPutFontEx(Core::Interpreter & /*cpu*/,
                               std::uint32_t screen,
                               std::uint32_t row,
                               std::uint32_t col,
                               const char   *text)
{
    std::cout << "[" << (screen == 0 ? "TV " : "DRC") << " "
              << row << "," << col << "] "
              << (text ? text : "(null)") << "\n";
}

// void OSScreenFlipBuffersEx(OSScreenID screen)
// TODO: hand the framebuffer to the Vulkan renderer once the two are connected.
inline void OSScreenFlipBuffersEx(Core::Interpreter & /*cpu*/, std::uint32_t /*screen*/) {}
