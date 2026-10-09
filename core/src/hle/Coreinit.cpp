/*
** EPITECH PROJECT, 2025
** core
** File description:
** HLE coreinit stubs (OSScreen, OS*, MEM, DC, TLS, VPAD, ProcUI, FSA, WUT runtime)
*/

#include "Coreinit.hpp"

#include <SDL2/SDL.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "gfx/Renderer.hpp"
#include "hle/AsyncCallbacks.hpp"
#include "hle/Ax.hpp"
#include "hle/CoreinitExtra.hpp"
#include "hle/MemHeap.hpp"
#include "hle/NnOnline.hpp"
#include "utils/Diagnostics.hpp"

// ── Framebuffer addresses (TV=0, DRC=1) ──────────────────────────────────────

static std::uint32_t s_fb_addr[2] = {0, 0};

// ── 8x16 bitmap font (public domain, IBM PC style) ────────────────────────────

// clang-format off
static const std::uint8_t FONT8x16[128][16] = {
#define BLK {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        BLK,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // space
        {0, 0, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0, 0x18, 0x18, 0, 0, 0, 0, 0}, // !
        {0, 0x6C, 0x6C, 0x6C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // "
        {0, 0, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0, 0, 0, 0, 0, 0}, // #
        {0, 0x18, 0x7E, 0xDB, 0xD8, 0x7C, 0x1E, 0xDB, 0x7E, 0x18, 0, 0, 0, 0, 0, 0}, // $
        {0, 0, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0, 0, 0, 0, 0, 0, 0, 0}, // %
        {0, 0, 0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0, 0, 0, 0, 0, 0, 0}, // &
        {0, 0x18, 0x18, 0x18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // '
        {0, 0, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0, 0, 0, 0, 0, 0, 0}, // (
        {0, 0, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0, 0, 0, 0, 0, 0, 0}, // )
        {0, 0, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // *
        {0, 0, 0, 0x18, 0x18, 0x7E, 0x18, 0x18, 0, 0, 0, 0, 0, 0, 0, 0}, // +
        {0, 0, 0, 0, 0, 0, 0, 0x18, 0x18, 0x08, 0x30, 0, 0, 0, 0, 0}, // ,
        {0, 0, 0, 0, 0, 0x7E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // -
        {0, 0, 0, 0, 0, 0, 0, 0, 0x18, 0x18, 0, 0, 0, 0, 0, 0}, // .
        {0, 0, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0}, // /
        {0, 0, 0x7C, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // 0
        {0, 0, 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0, 0, 0, 0, 0, 0, 0}, // 1
        {0, 0, 0x7C, 0xC6, 0x06, 0x3C, 0x60, 0xC6, 0xFE, 0, 0, 0, 0, 0, 0, 0}, // 2
        {0, 0, 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // 3
        {0, 0, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0, 0, 0, 0, 0, 0, 0}, // 4
        {0, 0, 0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // 5
        {0, 0, 0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // 6
        {0, 0, 0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0, 0, 0, 0, 0, 0, 0}, // 7
        {0, 0, 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // 8
        {0, 0, 0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0, 0, 0, 0, 0, 0, 0}, // 9
        {0, 0, 0, 0, 0x18, 0, 0, 0, 0x18, 0, 0, 0, 0, 0, 0, 0}, // :
        {0, 0, 0, 0, 0x18, 0, 0, 0, 0x18, 0x18, 0x08, 0x30, 0, 0, 0, 0}, // ;
        {0, 0, 0, 0x06, 0x1C, 0x70, 0x1C, 0x06, 0, 0, 0, 0, 0, 0, 0, 0}, // <
        {0, 0, 0, 0, 0x7E, 0, 0x7E, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // =
        {0, 0, 0, 0xC0, 0x70, 0x1C, 0x70, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0}, // >
        {0, 0, 0x7C, 0xC6, 0x0C, 0x18, 0x18, 0, 0x18, 0, 0, 0, 0, 0, 0, 0}, // ?
        {0, 0, 0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // @
        {0, 0, 0x10, 0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // A
        {0, 0, 0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0, 0, 0, 0, 0, 0, 0}, // B
        {0, 0, 0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // C
        {0, 0, 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0, 0, 0, 0, 0, 0, 0}, // D
        {0, 0, 0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0, 0, 0, 0, 0, 0, 0}, // E
        {0, 0, 0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0, 0, 0, 0, 0, 0, 0}, // F
        {0, 0, 0x3C, 0x66, 0xC0, 0xCE, 0xC6, 0x66, 0x3A, 0, 0, 0, 0, 0, 0, 0}, // G
        {0, 0, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // H
        {0, 0, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // I
        {0, 0, 0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0, 0, 0, 0, 0, 0, 0}, // J
        {0, 0, 0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0, 0, 0, 0, 0, 0, 0}, // K
        {0, 0, 0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0, 0, 0, 0, 0, 0, 0}, // L
        {0, 0, 0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // M
        {0, 0, 0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // N
        {0, 0, 0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0, 0, 0, 0, 0, 0, 0}, // O
        {0, 0, 0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0, 0, 0, 0, 0, 0, 0}, // P
        {0, 0, 0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x0C, 0, 0, 0, 0, 0, 0}, // Q
        {0, 0, 0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0, 0, 0, 0, 0, 0, 0}, // R
        {0, 0, 0x7C, 0xC6, 0x60, 0x38, 0x0C, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // S
        {0, 0, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // T
        {0, 0, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // U
        {0, 0, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0, 0, 0, 0, 0, 0, 0}, // V
        {0, 0, 0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // W
        {0, 0, 0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // X
        {0, 0, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // Y
        {0, 0, 0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0, 0, 0, 0, 0, 0, 0}, // Z
        {0, 0, 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // [
        {0, 0, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0, 0, 0, 0, 0, 0, 0, 0}, // backslash
        {0, 0, 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // ]
        {0, 0x10, 0x38, 0x6C, 0xC6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // ^
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0}, // _
        {0, 0x30, 0x18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // `
        {0, 0, 0, 0, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0, 0, 0, 0, 0, 0, 0}, // a
        {0, 0, 0xE0, 0x60, 0x60, 0x7C, 0x66, 0x66, 0xDC, 0, 0, 0, 0, 0, 0, 0}, // b
        {0, 0, 0, 0, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // c
        {0, 0, 0x1C, 0x0C, 0x0C, 0x7C, 0xCC, 0xCC, 0x76, 0, 0, 0, 0, 0, 0, 0}, // d
        {0, 0, 0, 0, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // e
        {0, 0, 0x1C, 0x36, 0x30, 0x78, 0x30, 0x30, 0x78, 0, 0, 0, 0, 0, 0, 0}, // f
        {0, 0, 0, 0, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0xCC, 0x78, 0, 0, 0, 0, 0}, // g
        {0, 0, 0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0, 0, 0, 0, 0, 0, 0}, // h
        {0, 0, 0x18, 0, 0x38, 0x18, 0x18, 0x18, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // i
        {0, 0, 0x06, 0, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C, 0, 0, 0, 0, 0, 0}, // j
        {0, 0, 0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0, 0, 0, 0, 0, 0, 0}, // k
        {0, 0, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0, 0, 0, 0, 0, 0, 0}, // l
        {0, 0, 0, 0, 0xEC, 0xFE, 0xD6, 0xD6, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // m
        {0, 0, 0, 0, 0xDC, 0x66, 0x66, 0x66, 0x66, 0, 0, 0, 0, 0, 0, 0}, // n
        {0, 0, 0, 0, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0, 0, 0, 0, 0, 0, 0}, // o
        {0, 0, 0, 0, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0, 0, 0, 0, 0}, // p
        {0, 0, 0, 0, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x0C, 0x1E, 0, 0, 0, 0, 0}, // q
        {0, 0, 0, 0, 0xDC, 0x76, 0x66, 0x60, 0xF0, 0, 0, 0, 0, 0, 0, 0}, // r
        {0, 0, 0, 0, 0x7C, 0xC6, 0x70, 0x1C, 0xFC, 0, 0, 0, 0, 0, 0, 0}, // s
        {0, 0, 0x10, 0x30, 0x7C, 0x30, 0x30, 0x36, 0x1C, 0, 0, 0, 0, 0, 0, 0}, // t
        {0, 0, 0, 0, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0, 0, 0, 0, 0, 0, 0}, // u
        {0, 0, 0, 0, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0, 0, 0, 0, 0, 0, 0}, // v
        {0, 0, 0, 0, 0xC6, 0xD6, 0xFE, 0xFE, 0x6C, 0, 0, 0, 0, 0, 0, 0}, // w
        {0, 0, 0, 0, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0, 0, 0, 0, 0, 0, 0}, // x
        {0, 0, 0, 0, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0xF8, 0, 0, 0, 0, 0}, // y
        {0, 0, 0, 0, 0xFE, 0xCC, 0x18, 0x36, 0xFE, 0, 0, 0, 0, 0, 0, 0}, // z
        {0, 0, 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0, 0, 0, 0, 0, 0, 0}, // {
        {0, 0, 0x18, 0x18, 0x18, 0, 0x18, 0x18, 0x18, 0, 0, 0, 0, 0, 0, 0}, // |
        {0, 0, 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0, 0, 0, 0, 0, 0, 0}, // }
        {0, 0x76, 0xDC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // ~
};
#undef BLK
// clang-format on

// ── Font blit helper ──────────────────────────────────────────────────────────

static void blit_char(std::uint8_t *fb, std::uint32_t stride_px, int col_px, int row_px, char ch, std::uint32_t fg_color)
{
    if ((unsigned char) ch >= 128)
        ch = '?';
    const std::uint8_t *glyph = FONT8x16[(std::uint8_t) ch];
    std::uint8_t r = (fg_color >> 24) & 0xFF;
    std::uint8_t g = (fg_color >> 16) & 0xFF;
    std::uint8_t b = (fg_color >> 8) & 0xFF;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 8; x++) {
            if (glyph[y] & (0x80 >> x)) {
                int px = col_px + x;
                int py = row_px + y;
                if (px < (int) stride_px) {
                    std::uint8_t *dst = fb + static_cast<std::ptrdiff_t>(py * (int) stride_px + px) * 4;
                    dst[0] = r;
                    dst[1] = g;
                    dst[2] = b;
                    dst[3] = 0xFF;
                }
            }
        }
    }
}

// ── Thread-local storage (per guest thread, 64 keys max) ─────────────────────

static std::unordered_map<std::uint32_t, std::array<std::uint32_t, 64>> s_tls; // thread handle -> slots

// ── HLE implementations ───────────────────────────────────────────────────────

// ---- OSScreen ----

static void hle_OSScreenInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSScreenShutdown(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

static void hle_OSScreenGetBufferSizeEx(Core::Interpreter &cpu)
{
    std::uint32_t id = cpu.m_gpr[3];

    cpu.m_gpr[3] = (id == 0) ? (1280 * 720 * 4) : (854 * 480 * 4);
}

static void hle_OSScreenSetBufferEx(Core::Interpreter &cpu)
{
    std::uint32_t id = cpu.m_gpr[3];
    std::uint32_t ppc_addr = cpu.m_gpr[4];

    if (id < 2)
        s_fb_addr[id] = ppc_addr;
    cpu.m_gpr[3] = 0;
}

static void hle_OSScreenEnableEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

static void hle_OSScreenClearBufferEx(Core::Interpreter &cpu)
{
    std::uint32_t id = cpu.m_gpr[3];
    std::uint32_t color = cpu.m_gpr[4];
    if (id >= 2)
        return;

    std::uint32_t fb_addr = s_fb_addr[id];
    if (!fb_addr)
        return;

    std::uint32_t width = (id == 0) ? 1280u : 854u;
    std::uint32_t height = (id == 0) ? 720u : 480u;
    std::uint8_t *fb = cpu.m_memory.hostPtr(fb_addr);
    if (!fb)
        return;

    std::uint8_t rv = (color >> 24) & 0xFF;
    std::uint8_t gv = (color >> 16) & 0xFF;
    std::uint8_t bv = (color >> 8) & 0xFF;
    std::uint8_t xv = color & 0xFF; // low byte (X / padding, not alpha)

    for (std::uint32_t i = 0; i < width * height; i++) {
        fb[i * 4 + 0] = rv;
        fb[i * 4 + 1] = gv;
        fb[i * 4 + 2] = bv;
        fb[i * 4 + 3] = xv;
    }
    cpu.m_gpr[3] = 0;
}

static void hle_OSScreenPutFontEx(Core::Interpreter &cpu)
{
    std::uint32_t id = cpu.m_gpr[3];
    std::uint32_t col = cpu.m_gpr[4];
    std::uint32_t row = cpu.m_gpr[5];
    std::uint32_t str_addr = cpu.m_gpr[6];
    if (id >= 2)
        return;

    std::uint32_t fb_addr = s_fb_addr[id];
    if (!fb_addr)
        return;

    std::uint8_t *fb = cpu.m_memory.hostPtr(fb_addr);
    if (!fb)
        return;

    std::uint32_t width = (id == 0) ? 1280u : 854u;
    int x_px = (int) (col * 8);
    int y_px = (int) (row * 16);

    while (true) {
        std::uint8_t ch = cpu.m_memory.read<std::uint8_t>(str_addr++);
        if (!ch)
            break;
        blit_char(fb, width, x_px, y_px, (char) ch, 0xFFFFFF00u);
        x_px += 8;
    }
    cpu.m_gpr[3] = 0;
}

static void hle_OSScreenFlipBuffersEx(Core::Interpreter &cpu)
{
    std::uint32_t id = cpu.m_gpr[3];
    if (id != 0 || !cpu.m_renderer)
        return;

    std::uint32_t fb_addr = s_fb_addr[0];
    if (!fb_addr)
        return;

    std::uint8_t *fb = cpu.m_memory.hostPtr(fb_addr);
    if (!fb)
        return;

    cpu.m_renderer->flip_tv(fb, 1280, 720);

    // FPS counter
    static std::uint32_t frames = 0;

    // multiplied by 1000 to get in milliseconds
    static std::uint64_t last_ms = SDL_GetPerformanceCounter() * 1000 / SDL_GetPerformanceFrequency();
    ++frames;
    std::uint64_t now = SDL_GetPerformanceCounter() * 1000 / SDL_GetPerformanceFrequency();
    if (now - last_ms >= 1000) {
        fprintf(stderr, "[FPS] %u\n", frames);
        frames = 0;
        last_ms = now;
    }

    cpu.m_gpr[3] = 0;
}

// ---- OS misc ----

// Espresso/Latte clock rates (Hz). The Cafe runtime derives its timer frequency from busSpeed,
// so these must be non-zero or the title's OSTicksToSeconds math divides by zero.
static constexpr std::uint32_t kBusClockSpeed = 248625000u;
static constexpr std::uint32_t kCoreClockSpeed = 1243125000u; // busSpeed * 5

// OS queries and scheduler deadlines share one per-interpreter time base.
// Reading the clock must not itself advance time; interpreter service ticks do.
namespace Core {
    void advanceGuestFrameClock(Interpreter &cpu)
    {
        static const bool on = []() {
            const char *e = std::getenv("WEMU_FRAME_CLOCK");
            return e && e[0] == '1';
        }();
        if (on)
            cpu.m_scheduler.advanceFrame(1035937ull); // busSpeed / 4 / 60
    }
} // namespace Core

static void hle_OSGetTick(Core::Interpreter &cpu) { cpu.m_gpr[3] = static_cast<std::uint32_t>(cpu.m_scheduler.now()); }
static void hle_OSGetTime(Core::Interpreter &cpu)
{
    const std::uint64_t t = cpu.m_scheduler.now();
    cpu.m_gpr[3] = static_cast<std::uint32_t>(t >> 32);
    cpu.m_gpr[4] = static_cast<std::uint32_t>(t & 0xFFFFFFFFu);
}
static void hle_OSGetSystemTime(Core::Interpreter &cpu)
{
    const std::uint64_t t = cpu.m_scheduler.now();
    cpu.m_gpr[3] = static_cast<std::uint32_t>(t >> 32);
    cpu.m_gpr[4] = static_cast<std::uint32_t>(t & 0xFFFFFFFFu);
}
static void hle_OSGetTitleID(Core::Interpreter &cpu)
{
    cpu.m_gpr[3] = 0;
    cpu.m_gpr[4] = 0;
}

// OSGetSystemInfo returns a pointer to a static OSSystemInfo struct. Layout (decaf/Cafe SDK):
// 0x00 busSpeed, 0x04 coreSpeed, 0x08 baseTime(u64), 0x10..0x18 per-core L2 size, 0x1C cpuRatio.
static void hle_OSGetSystemInfo(Core::Interpreter &cpu)
{
    static std::uint32_t infoAddr = 0;
    if (!infoAddr) {
        infoAddr = cpu.m_memory.heapAllocate(0x20, 8);
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x00, kBusClockSpeed);
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x04, kCoreClockSpeed);
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x08, 0u); // baseTime hi
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x0C, 0u); // baseTime lo
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x10, 0x80000u); // L2 core0
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x14, 0x200000u); // L2 core1
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x18, 0x80000u); // L2 core2
        cpu.m_memory.write<std::uint32_t>(infoAddr + 0x1C, 5u); // core/bus ratio
    }
    cpu.m_gpr[3] = infoAddr;
}
static void hle_OSGetCoreId(Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_scheduler.currentCoreId(); }
static void hle_OSGetMainCoreId(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
static void hle_OSGetCoreCount(Core::Interpreter &cpu) { cpu.m_gpr[3] = 3; }
static void hle_OSIsMainCore(Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_scheduler.currentCoreId() == 1; }
static void hle_OSIsDebuggerPresent(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSIsDebuggerInitialized(Core::Interpreter &cpu)
{
    Utils::Log::error("hle_OSIsDebuggerInitialized");
    cpu.m_gpr[3] = 0;
}
// Returns the PREVIOUS enabled state; MK8 loops "disable + pump ProcUI" until it sees 1.
static void hle_OSEnableHomeButtonMenu(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }

static void hle_OSCompareAndSwapAtomic(Core::Interpreter &cpu)
{
    const std::uint32_t addr = cpu.m_gpr[3];
    const std::uint32_t expected = cpu.m_gpr[4];
    const std::uint32_t newval = cpu.m_gpr[5];
    const std::uint32_t cur = cpu.m_memory.read<std::uint32_t>(addr);

    if (cur == expected) {
        cpu.m_memory.write<std::uint32_t>(addr, newval);
        cpu.m_gpr[3] = 1;
    } else {
        cpu.m_gpr[3] = 0;
    }
}

static void hle_OSCompareAndSwapAtomicEx(Core::Interpreter &cpu)
{
    const std::uint32_t addr = cpu.m_gpr[3];
    const std::uint32_t expected = cpu.m_gpr[4];
    const std::uint32_t newval = cpu.m_gpr[5];
    const std::uint32_t cur = cpu.m_memory.read<std::uint32_t>(addr);

    if (cpu.m_gpr[6])
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], cur);
    if (cur == expected) {
        cpu.m_memory.write<std::uint32_t>(addr, newval);
        cpu.m_gpr[3] = 1;
    } else {
        cpu.m_gpr[3] = 0;
    }
}

static void hle_OSReport(Core::Interpreter &cpu)
{
    const std::uint8_t *p = cpu.m_memory.hostPtr(cpu.m_gpr[3]);

    if (p)
        fprintf(stderr, "[OSReport] %s\n", (const char *) p);
    cpu.m_gpr[3] = 0;
}

static void hle_OSFatal(Core::Interpreter &cpu)
{
    const std::uint8_t *p = cpu.m_memory.hostPtr(cpu.m_gpr[3]);

    if (p)
        fprintf(stderr, "[OSFatal] %s\n", (const char *) p);
    else
        fprintf(stderr, "[OSFatal] <unmapped>\n");
    cpu.m_gpr[3] = 0;
}

static void hle_os_snprintf(Core::Interpreter &cpu)
{
    uint32_t buf = cpu.m_gpr[3];
    uint32_t size = cpu.m_gpr[4];

    if (buf && size) {
        std::uint8_t *p = cpu.m_memory.hostPtr(buf);
        if (p)
            p[0] = 0;
    }
    cpu.m_gpr[3] = 0;
}

// ---- OS Mutex ----
// Real exclusion matters: with timeslice preemption, guest critical sections (nw::snd's voice
// pool, sead heaps) rely on these actually blocking, exactly as on multi-core hardware.

static void hle_OSInitMutex(Core::Interpreter &cpu)
{
    cpu.m_scheduler.mutexReset(cpu.m_gpr[3]);
    cpu.m_gpr[3] = 0;
}
static void hle_OSInitMutexEx(Core::Interpreter &cpu)
{
    cpu.m_scheduler.mutexReset(cpu.m_gpr[3]);
    cpu.m_gpr[3] = 0;
}
static void hle_OSLockMutex(Core::Interpreter &cpu)
{
    if (cpu.m_scheduler.mutexLock(cpu, cpu.m_gpr[3]))
        cpu.m_gpr[3] = 0;
    // else: parked on the mutex; the SC re-executes on wake and retries the acquire
}
static void hle_OSUnlockMutex(Core::Interpreter &cpu)
{
    cpu.m_scheduler.mutexUnlock(cpu.m_gpr[3]);
    cpu.m_gpr[3] = 0;
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}
static void hle_OSTryLockMutex(Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_scheduler.mutexTryLock(cpu.m_gpr[3]) ? 1 : 0; }

// ---- OS Thread (backed by the cooperative Scheduler) ----

// BOOL OSCreateThread(OSThread *thread, entry, int argc, void *argv, void *stackTop, u32 stackSize,
//                     int priority, u32 attr). stackTop is the high end; the stack grows down.
static void hle_OSCreateThread(Core::Interpreter &cpu)
{
    Core::ThreadContext *t = cpu.m_scheduler.create(cpu, cpu.m_gpr[3], cpu.m_gpr[4], cpu.m_gpr[5], cpu.m_gpr[6], cpu.m_gpr[7],
                                                    static_cast<std::int32_t>(cpu.m_gpr[9])); // r9 = Cafe priority (0 highest)
    if (const std::uint32_t affinity = cpu.m_gpr[10] & 7; affinity && t) // r10 = attr, bits 0-2 = core mask
        t->affinity = affinity;
    cpu.m_gpr[3] = t ? 1 : 0;
}

static void hle_OSResumeThread(Core::Interpreter &cpu)
{
    cpu.m_scheduler.resume(cpu.m_gpr[3]);
    cpu.m_gpr[3] = 1; // previous suspend count (good enough)
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}

// void OSSleepTicks(OSTime ticks) — 64-bit OSTime passed as r3:r4 (high:low).
static void hle_OSSleepTicks(Core::Interpreter &cpu)
{
    const std::uint64_t ticks = (static_cast<std::uint64_t>(cpu.m_gpr[3]) << 32) | cpu.m_gpr[4];
    cpu.m_scheduler.sleep(cpu, ticks);
}

static void hle_OSYieldThread(Core::Interpreter &cpu) { cpu.m_scheduler.yield(cpu); }

static void hle_OSGetCurrentThread(Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_scheduler.currentHandle(); }

// BOOL OSJoinThread(OSThread *thread, int *result)
static void hle_OSJoinThread(Core::Interpreter &cpu)
{
    cpu.m_scheduler.join(cpu, cpu.m_gpr[3]);
    if (!cpu.m_hle_redirected)
        cpu.m_gpr[3] = 1;
}

// void OSExitThread(int code) — terminates the calling thread.
static void hle_OSExitThread(Core::Interpreter &cpu)
{
    if (cpu.m_scheduler.exitCurrent(cpu))
        cpu.m_hle_redirected = true; // resumed another thread; don't return to LR
    else
        cpu.m_running = false; // last thread finished
}

static void hle_OSSetThreadName(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSSetThreadPriority(Core::Interpreter &cpu)
{
    cpu.m_scheduler.setPriority(cpu.m_gpr[3], static_cast<std::int32_t>(cpu.m_gpr[4]));
    cpu.m_gpr[3] = 1;
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}
static void hle_OSSetThreadAffinity(Core::Interpreter &cpu)
{
    cpu.m_scheduler.setAffinity(cpu.m_gpr[3], cpu.m_gpr[4]);
    cpu.m_gpr[3] = 1;
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}

// ---- OS TLS ----

static void hle_wut_get_thread_specific(Core::Interpreter &cpu)
{
    std::uint32_t key = cpu.m_gpr[3];

    cpu.m_gpr[3] = (key < 64) ? s_tls[cpu.m_scheduler.currentHandle()][key] : 0;
}

static void hle_wut_set_thread_specific(Core::Interpreter &cpu)
{
    std::uint32_t key = cpu.m_gpr[3], val = cpu.m_gpr[4];

    if (key < 64)
        s_tls[cpu.m_scheduler.currentHandle()][key] = val;
    cpu.m_gpr[3] = 0;
}

// ---- MEM ----
// Real guest-visible heaps (base heaps + ExpHeap/FrmHeap with free) live in hle/MemHeap.cpp.

// ---- OS block mem ops ----

// OSBlockMove(dst, src, size, flush) — memmove between guest buffers (overlap-safe).
static void hle_OSBlockMove(Core::Interpreter &cpu)
{
    std::uint8_t *dst = cpu.m_memory.hostPtr(cpu.m_gpr[3]);
    const std::uint8_t *src = cpu.m_memory.hostPtr(cpu.m_gpr[4]);
    const std::uint32_t size = cpu.m_gpr[5];
    if (dst && src && size)
        std::memmove(dst, src, size);
    cpu.m_gpr[3] = 0;
}

// OSBlockSet(dst, value, size) — memset on a guest buffer.
static void hle_OSBlockSet(Core::Interpreter &cpu)
{
    std::uint8_t *dst = cpu.m_memory.hostPtr(cpu.m_gpr[3]);
    const std::uint32_t size = cpu.m_gpr[5];
    if (dst && size)
        std::memset(dst, static_cast<int>(cpu.m_gpr[4] & 0xFF), size);
    cpu.m_gpr[3] = 0;
}

// ---- DC (cache) ----

static void hle_DCFlushRange(Core::Interpreter &cpu) { (void) cpu; }
static void hle_DCInvalidateRange(Core::Interpreter &cpu) { (void) cpu; }

// ---- OS sync ----
// Spinlocks and fast mutexes share the scheduler's mutex table (distinct guest addresses).
// Cafe's spinlock Acquire also disables interrupts for the critical section, saving the prior
// state in the lock; we mirror that so preemption stays off until the matching Release.

static std::unordered_map<std::uint32_t, bool> g_spinSavedIntr;

static void hle_OSUninterruptibleSpinLock_Acquire(Core::Interpreter &cpu)
{
    const std::uint32_t lock = cpu.m_gpr[3];
    if (!cpu.m_scheduler.mutexLock(cpu, lock))
        return; // parked; the SC re-executes on wake
    g_spinSavedIntr[lock] = cpu.m_interruptsDisabled;
    cpu.m_interruptsDisabled = true;
    cpu.m_gpr[3] = 1; // BOOL acquired
}
static void hle_OSUninterruptibleSpinLock_Release(Core::Interpreter &cpu)
{
    const std::uint32_t lock = cpu.m_gpr[3];
    cpu.m_interruptsDisabled = g_spinSavedIntr[lock];
    cpu.m_scheduler.mutexUnlock(lock);
    cpu.m_gpr[3] = 1;
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}
static void hle_OSUninterruptibleSpinLock_TryAcquire(Core::Interpreter &cpu)
{
    const std::uint32_t lock = cpu.m_gpr[3];
    if (!cpu.m_scheduler.mutexTryLock(lock)) {
        cpu.m_gpr[3] = 0;
        return;
    }
    g_spinSavedIntr[lock] = cpu.m_interruptsDisabled;
    cpu.m_interruptsDisabled = true;
    cpu.m_gpr[3] = 1;
}

static void hle_OSFastMutex_Init(Core::Interpreter &cpu) { cpu.m_scheduler.mutexReset(cpu.m_gpr[3]); }
static void hle_OSFastMutex_Lock(Core::Interpreter &cpu) { cpu.m_scheduler.mutexLock(cpu, cpu.m_gpr[3]); }
static void hle_OSFastMutex_Unlock(Core::Interpreter &cpu)
{
    cpu.m_scheduler.mutexUnlock(cpu.m_gpr[3]);
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}
static void hle_OSFastMutex_TryLock(Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_scheduler.mutexTryLock(cpu.m_gpr[3]) ? 1 : 0; }

static void hle_OSGetThreadPriority(Core::Interpreter &cpu) { cpu.m_gpr[3] = 16; } // default priority
// ---- OSMessageQueue ----
// A faithful ring buffer. The guest owns the OSMessage[] storage (16 bytes/entry: message + args[3]);
// we track head/count host-side keyed by the guest queue pointer and copy whole 16-byte entries in
// guest-endian (raw memcpy, no field interpretation needed). Cooperative scheduling means callers
// already yield/sleep between polls, so non-blocking ring semantics are enough to let a producer
// thread fill a queue a consumer thread is draining.
namespace {
    struct MsgQueueState {
            std::uint32_t buffer{0};
            std::uint32_t capacity{0};
            std::uint32_t head{0};
            std::uint32_t count{0};
    };
    std::unordered_map<std::uint32_t, MsgQueueState> g_msgQueues;
    constexpr std::uint32_t kOSMessageSize = 16;

    void copyMessage(Core::Interpreter &cpu, std::uint32_t dst, std::uint32_t src)
    {
        std::uint8_t *d = cpu.m_memory.hostPtr(dst);
        const std::uint8_t *s = cpu.m_memory.hostPtr(src);
        if (d && s)
            std::memcpy(d, s, kOSMessageSize);
    }
} // namespace

static void hle_OSInitMessageQueue(Core::Interpreter &cpu)
{
    g_msgQueues[cpu.m_gpr[3]] = MsgQueueState{cpu.m_gpr[4], cpu.m_gpr[5], 0, 0};
    cpu.m_gpr[3] = 0;
}

// Senders park on queueAddr^1, receivers on queueAddr, so a send doesn't wake other senders.
static void hle_OSSendMessage(Core::Interpreter &cpu)
{
    const std::uint32_t queue = cpu.m_gpr[3];
    auto &q = g_msgQueues[queue];
    if (!q.capacity || q.count == q.capacity) { // queue full
        const bool blocking = cpu.m_gpr[5] & 1; // OS_MESSAGE_FLAGS_BLOCKING
        if (q.capacity && blocking && cpu.m_scheduler.blockOn(cpu, queue ^ 1, /*retryInstruction=*/true))
            return; // resume at the send SC and retry once a receiver frees a slot
        cpu.m_gpr[3] = 0;
        return;
    }
    const std::uint32_t tail = (q.head + q.count) % q.capacity;
    copyMessage(cpu, q.buffer + tail * kOSMessageSize, cpu.m_gpr[4]);
    q.count++;
    cpu.m_gpr[3] = 1;
    cpu.m_scheduler.wakeAll(queue);
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}

static void hle_OSJamMessage(Core::Interpreter &cpu) // insert at the front
{
    const std::uint32_t queue = cpu.m_gpr[3];
    auto &q = g_msgQueues[queue];
    if (!q.capacity || q.count == q.capacity) {
        const bool blocking = cpu.m_gpr[5] & 1;
        if (q.capacity && blocking && cpu.m_scheduler.blockOn(cpu, queue ^ 1, /*retryInstruction=*/true))
            return; // resume at the jam SC and retry once a receiver frees a slot
        cpu.m_gpr[3] = 0;
        return;
    }
    q.head = (q.head + q.capacity - 1) % q.capacity;
    copyMessage(cpu, q.buffer + q.head * kOSMessageSize, cpu.m_gpr[4]);
    q.count++;
    cpu.m_gpr[3] = 1;
    cpu.m_scheduler.wakeAll(queue);
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}

static void hle_OSReceiveMessage(Core::Interpreter &cpu)
{
    const std::uint32_t queue = cpu.m_gpr[3];
    auto &q = g_msgQueues[queue];
    // WEMU_TRACE_MAINQ=1: when the main thread polls an EMPTY queue, log the queue address and the
    // guest back-chain (once per unique call-site). Surfaces which producer-less queue main is
    // busy-waiting on when the title won't advance.
    static const bool traceMainQ = []() {
        const char *e = std::getenv("WEMU_TRACE_MAINQ");
        return e && e[0] == '1';
    }();
    if (traceMainQ && q.count == 0) {
        const auto *cur = cpu.m_scheduler.current();
        if (cur && cur->name == "main") {
            static std::unordered_map<std::uint32_t, int> seen;
            const std::uint32_t lr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
            if (seen[lr]++ < 2) {
                Utils::Log::error("[MAINQ] main recv empty queue=0x{:08X} flags=0x{:X} lr={} -- back-chain:", queue, cpu.m_gpr[5],
                                  Core::Diag::symbolize(cpu, lr));
                std::uint32_t sp = cpu.m_gpr[1];
                for (int f = 0; f < 12 && sp; f++) {
                    std::uint32_t nextSp = 0, savedLr = 0;
                    try {
                        nextSp = cpu.m_memory.read<std::uint32_t>(sp);
                        savedLr = cpu.m_memory.read<std::uint32_t>(sp + 4);
                    } catch (...) {
                        break;
                    }
                    if (savedLr)
                        Utils::Log::error("           #{} ret={}", f, Core::Diag::symbolize(cpu, savedLr));
                    if (nextSp <= sp)
                        break;
                    sp = nextSp;
                }
            }
        }
    }
    if (q.count == 0) { // empty
        // With OS_MESSAGE_FLAGS_BLOCKING, park until a sender posts. A blocking receive must not
        // return FALSE after wake; resume at the SC so the receive is re-executed and copies the
        // newly available message. Some Cafe wrappers use the returned payload immediately and do
        // not retry a FALSE receive themselves.
        const bool blocking = cpu.m_gpr[5] & 1;
        if (blocking && cpu.m_scheduler.blockOn(cpu, queue, /*retryInstruction=*/true))
            return;
        if (blocking) {
            cpu.m_gpr[3] = 0;
            return;
        }
        // A non-blocking receive observes this instant only. Retrying after a forced
        // context switch can prevent producers from building a queue ahead of consumers.
        cpu.m_gpr[3] = 0;
        return;
    }
    copyMessage(cpu, cpu.m_gpr[4], q.buffer + q.head * kOSMessageSize);
    q.head = (q.head + 1) % q.capacity;
    q.count--;
    cpu.m_gpr[3] = 1;
    cpu.m_scheduler.wakeAll(queue ^ 1); // a slot freed up: wake blocked senders
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}

static void hle_OSPeekMessage(Core::Interpreter &cpu)
{
    auto &q = g_msgQueues[cpu.m_gpr[3]];
    if (q.count == 0) {
        cpu.m_gpr[3] = 0;
        return;
    }
    copyMessage(cpu, cpu.m_gpr[4], q.buffer + q.head * kOSMessageSize);
    cpu.m_gpr[3] = 1;
}

// ---- hardware register / interrupt / dynamic-load primitives ----
// We have no GPU/HW register file; reads return 0 and writes are dropped. Under cooperative
// scheduling interrupts are meaningless, so disable returns "previously enabled" and restore is a
// no-op. OSDynLoad_Acquire must write a non-zero module handle to *outHandle (r4) or callers
// null-deref the returned handle.
static void hle_OSReadRegister32Ex(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSWriteRegister32Ex(Core::Interpreter &cpu) { (void) cpu; }
static void hle_OSReadRegister16(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSWriteRegister16(Core::Interpreter &cpu) { (void) cpu; }
// Track the disable window so the timeslice preemption doesn't fire inside guest critical
// sections. Returns the previous "enabled" state, per the Cafe API.
static void hle_OSDisableInterrupts(Core::Interpreter &cpu)
{
    cpu.m_gpr[3] = cpu.m_interruptsDisabled ? 0 : 1;
    cpu.m_interruptsDisabled = true;
}

static void hle_OSRestoreInterrupts(Core::Interpreter &cpu)
{
    const bool wasDisabled = cpu.m_interruptsDisabled;
    cpu.m_interruptsDisabled = cpu.m_gpr[3] == 0; // r3 = state to restore (1 = enabled)
    cpu.m_gpr[3] = wasDisabled ? 0 : 1;
    cpu.m_scheduler.rescheduleAfterHle(cpu);
}
static void hle_OSEnforceInorderIO(Core::Interpreter &cpu) { (void) cpu; }
static void hle_OSDriver_Register(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

static void hle_OSDynLoad_Acquire(Core::Interpreter &cpu)
{
    if (cpu.m_gpr[4]) // *outHandle = fake non-zero module handle
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], 0xD11B0001u);
    cpu.m_gpr[3] = 0; // OS_DYNLOAD_OK
}

static void hle_FSSetStateChangeNotification(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

// ---- WUT runtime ----

static void hle___init_wut(Core::Interpreter &cpu) // NOLINT(bugprone-reserved-identifier)
{
    for (const auto &sym: cpu.m_binary.symbols) {
        if (sym.name == "main" && sym.raw.header.st_value >= 0x02000000u && sym.raw.header.st_value < 0x10000000u) {
            fprintf(stderr, "[HLE] __init_wut → main() @ 0x%08X\n", sym.raw.header.st_value);
            cpu.m_nextPc = sym.raw.header.st_value - Core::Memory::MemoryMap::ApplicationCode;
            cpu.m_hle_redirected = true;
            return;
        }
    }
    fprintf(stderr, "[HLE] __init_wut: main() not found\n");
    cpu.m_gpr[3] = 0;
}

static void hle___rplwrap_exit(Core::Interpreter &cpu) // NOLINT(bugprone-reserved-identifier)
{
    fprintf(stderr, "[HLE] __rplwrap_exit — halting\n");
    cpu.m_running = false;
}

static void hle_main_import(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

// ---- misc library stubs (nn::act, SAVE, padscore/KPAD, WPAD, bsp) ----
// Stubbed so init paths that query them complete. Returning success/non-zero where a caller reads
// the result back; real behaviour is deferred.
static void hle_nn_act_Initialize(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; } // nn::Result OK

// Fake offline account in slot 1 so the boot-time account queries succeed without a network.
static void hle_nn_act_GetSlotNo(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
static void hle_nn_act_GetNumOfAccounts(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
static void hle_nn_act_IsSlotOccupied(Core::Interpreter &cpu) { cpu.m_gpr[3] = (cpu.m_gpr[3] & 0xFF) == 1; }
static void hle_nn_act_GetPersistentId(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x80000001u; }
static void hle_nn_act_GetPersistentIdEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = (cpu.m_gpr[3] & 0xFF) == 1 ? 0x80000001u : 0; }
static void hle_nn_act_IsNetworkAccount(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

// Out-parameter getters for the fake account: stable made-up ids, nn::Result OK.
static void hle_nn_act_GetUuidEx(Core::Interpreter &cpu) // (ACTUuid* out, u8 slot)
{
    for (std::uint32_t i = 0; i < 16 && cpu.m_gpr[3]; i++)
        cpu.m_memory.write<std::uint8_t>(cpu.m_gpr[3] + i, static_cast<std::uint8_t>(0xA0 + i));
    cpu.m_gpr[3] = 0;
}

static void hle_nn_act_GetPrincipalIdEx(Core::Interpreter &cpu) // (u32* out, u8 slot)
{
    if (cpu.m_gpr[3])
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3], 0x12345678u);
    cpu.m_gpr[3] = 0;
}

static void hle_nn_act_GetSimpleAddressIdEx(Core::Interpreter &cpu) // (u32* out, u8 slot)
{
    if (cpu.m_gpr[3])
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3], 0x11223344u);
    cpu.m_gpr[3] = 0;
}

static void hle_nn_act_GetTransferableIdEx(Core::Interpreter &cpu) // (u64* out, u32 unk, u8 slot)
{
    if (cpu.m_gpr[3])
        cpu.m_memory.write<std::uint64_t>(cpu.m_gpr[3], 0x1122334455667788ull);
    cpu.m_gpr[3] = 0;
}

static void hle_OSIsHomeButtonMenuEnabled(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }

// _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID id) -> u64 title id in r3:r4.
static void hle_SYSGetSystemApplicationTitleId(Core::Interpreter &cpu)
{
    cpu.m_gpr[4] = 0x10040200u | (cpu.m_gpr[3] << 8);
    cpu.m_gpr[3] = 0x00050010u;
}

// GetMii(FFLStoreData* out): zeroed Mii + OK result; real Mii data can come with FFL support.
static void hle_nn_act_GetMii(Core::Interpreter &cpu)
{
    for (std::uint32_t off = 0; off < 0x60 && cpu.m_gpr[3]; off += 4)
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3] + off, 0);
    cpu.m_gpr[3] = 0;
}

// GetMiiName(char16* out): "WEMU" in UTF-16BE + terminator, OK result.
static void hle_nn_act_GetMiiName(Core::Interpreter &cpu)
{
    if (cpu.m_gpr[3]) {
        const char16_t name[] = u"WEMU";
        for (std::uint32_t i = 0; i < 5; ++i)
            cpu.m_memory.write<std::uint16_t>(cpu.m_gpr[3] + i * 2, static_cast<std::uint16_t>(name[i]));
    }
    cpu.m_gpr[3] = 0;
}
static void hle_SAVEInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_KPADInitEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_KPADGetMplsWorkSize(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x1000; }
static void hle_KPADSetMplsWorkarea(Core::Interpreter &cpu) { (void) cpu; }
static void hle_WPADEnableURCC(Core::Interpreter &cpu) { (void) cpu; }

// bspGetHardwareVersion(u32* outVersion) -> writes a plausible Latte hardware version, returns 0.
static void hle_bspGetHardwareVersion(Core::Interpreter &cpu)
{
    if (cpu.m_gpr[3])
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3], 0x21100010u);
    cpu.m_gpr[3] = 0;
}

// ---- ProcUI ----

static void hle_ProcUIInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIInitEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIShutdown(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIIsRunning(Core::Interpreter &cpu)
{
    // BOOL ProcUIIsRunning(void).  Returning false here tells the title that the process is in
    // shutdown and lets otherwise-healthy loops fall through to libc exit(0).  Keep the app
    // running until the host window is closed.
    cpu.m_gpr[3] = (!cpu.m_renderer || cpu.m_renderer->is_open()) ? 1u : 0u;
}
static void hle_ProcUISubProcessMessages(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIRegisterCallback(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

static void hle_ProcUIProcessMessages(Core::Interpreter &cpu)
{
    // ProcUIStatus: 0=IN_FOREGROUND, 1=IN_BACKGROUND, 2=RELEASE_FOREGROUND, 3=EXITING.
    //
    // A host SDL quit/escape is not a Cafe ProcUI shutdown request from the emulated OS.  Reporting
    // EXITING here makes MK8 cleanly fall through to _Exit(0), hiding the real next boot gate.
    // Keep the title in foreground; if the host asks to close, stop the interpreter directly.
    if (cpu.m_renderer) {
        if (!cpu.m_renderer->poll_events())
            cpu.m_running = false;
    }
    cpu.m_gpr[3] = 0; // PROCUI_STATUS_IN_FOREGROUND
    cpu.m_scheduler.yield(cpu);
}

// ---- VPAD ----

static void hle_VPADInit(Core::Interpreter &cpu)
{
    cpu.m_vpadPreviousHold = 0;
    cpu.m_vpadReadCount = 0;
    cpu.m_gpr[3] = 0;
}
static void hle_VPADSetAccParam(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

static void hle_VPADRead(Core::Interpreter &cpu)
{
    std::uint32_t buf_addr = cpu.m_gpr[4];
    std::uint32_t err_addr = cpu.m_gpr[6];

    const auto channel = cpu.m_gpr[3];
    const auto count = static_cast<std::int32_t>(cpu.m_gpr[5]);
    if (channel || count <= 0 || !buf_addr) {
        if (err_addr)
            cpu.m_memory.write<std::uint32_t>(err_addr, channel ? -2u : -1u);
        cpu.m_gpr[3] = 0;
        return;
    }

    if (cpu.m_renderer)
        cpu.m_renderer->poll_events();
    std::uint32_t hold = (cpu.m_renderer ? cpu.m_renderer->get_buttons() : 0u) | cpu.m_controllerMask;

    // WEMU_AUTO_A=1: pulse the confirm buttons so unattended boots pass "press +/A" gates (MK8's
    // title is "Press +"). Pulsing is driven by the VPADRead CALL COUNT, not wall time, so the
    // rising edge (a read with the button up followed by a read with it down) is always clean even
    // when the guest polls sparsely at a low frame rate — MK8's title reads the `trigger` (edge)
    // field, so a missed edge means no advance. We press A (0x8000) + PLUS/Start (0x0008) together.
    static const bool autoA = []() {
        const char *e = std::getenv("WEMU_AUTO_A");
        return e && e[0] == '1';
    }();
    ++cpu.m_vpadReadCount;
    if (autoA) {
        // ~20-read press, ~100-read gap: guarantees an up->down->up cycle the edge detector sees.
        if (cpu.m_vpadReadCount % 120 < 20)
            hold |= 0x8000u | 0x0008u;
    }

    std::uint32_t trigger = hold & ~cpu.m_vpadPreviousHold;
    std::uint32_t released = cpu.m_vpadPreviousHold & ~hold;

    // One complete sample: centered sticks, no touch/motion, and an identity orientation.
    for (std::uint32_t offset = 0; offset < 0xAC; offset += 4)
        cpu.m_memory.write<std::uint32_t>(buf_addr + offset, 0);
    for (const auto offset: {0x6Cu, 0x7Cu, 0x8Cu})
        cpu.m_memory.write<std::uint32_t>(buf_addr + offset, 0x3F800000);

    cpu.m_memory.write<std::uint32_t>(buf_addr + 0, hold);
    cpu.m_memory.write<std::uint32_t>(buf_addr + 4, trigger);
    cpu.m_memory.write<std::uint32_t>(buf_addr + 8, released);
    cpu.m_vpadPreviousHold = hold;

    if (err_addr)
        cpu.m_memory.write<std::uint32_t>(err_addr, 0);

    cpu.m_gpr[3] = 1;
}

// ---- FS / FSA stubs ----

static void hle_FSInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSShutdown(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSInitCmdBlock(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAShutdown(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAAddClient(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSADelClient(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSACloseFile(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAMount(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAUnmount(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAChangeDir(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAChangeMode(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSARemove(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSARename(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

// ---- Sysapp ----

static void hle_SYSRelaunchTitle(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_SYSLaunchMenu(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

// ── Registration ──────────────────────────────────────────────────────────────

void RegisterCoreinitFunctions()
{
    // OSScreen
    Core::syscallHandler.registerSyscall("OSScreenInit", hle_OSScreenInit);
    Core::syscallHandler.registerSyscall("OSScreenShutdown", hle_OSScreenShutdown);
    Core::syscallHandler.registerSyscall("OSScreenGetBufferSizeEx", hle_OSScreenGetBufferSizeEx);
    Core::syscallHandler.registerSyscall("OSScreenSetBufferEx", hle_OSScreenSetBufferEx);
    Core::syscallHandler.registerSyscall("OSScreenEnableEx", hle_OSScreenEnableEx);
    Core::syscallHandler.registerSyscall("OSScreenClearBufferEx", hle_OSScreenClearBufferEx);
    Core::syscallHandler.registerSyscall("OSScreenPutFontEx", hle_OSScreenPutFontEx);
    Core::syscallHandler.registerSyscall("OSScreenFlipBuffersEx", hle_OSScreenFlipBuffersEx);

    // OS misc
    Core::syscallHandler.registerSyscall("OSGetTick", hle_OSGetTick);
    Core::syscallHandler.registerSyscall("OSGetTime", hle_OSGetTime);
    Core::syscallHandler.registerSyscall("OSGetTitleID", hle_OSGetTitleID);
    Core::syscallHandler.registerSyscall("OSGetSystemInfo", hle_OSGetSystemInfo);
    Core::syscallHandler.registerSyscall("OSGetSystemTime", hle_OSGetSystemTime);
    Core::syscallHandler.registerSyscall("OSFastMutex_Init", hle_OSFastMutex_Init);
    Core::syscallHandler.registerSyscall("OSFastMutex_Lock", hle_OSFastMutex_Lock);
    Core::syscallHandler.registerSyscall("OSFastMutex_Unlock", hle_OSFastMutex_Unlock);
    Core::syscallHandler.registerSyscall("OSFastMutex_TryLock", hle_OSFastMutex_TryLock);
    Core::syscallHandler.registerSyscall("OSBlockMove", hle_OSBlockMove);
    Core::syscallHandler.registerSyscall("OSBlockSet", hle_OSBlockSet);
    Core::syscallHandler.registerSyscall("OSGetThreadPriority", hle_OSGetThreadPriority);
    Core::syscallHandler.registerSyscall("OSInitMessageQueue", hle_OSInitMessageQueue);
    Core::syscallHandler.registerSyscall("OSSendMessage", hle_OSSendMessage);
    Core::syscallHandler.registerSyscall("OSReceiveMessage", hle_OSReceiveMessage);
    Core::syscallHandler.registerSyscall("OSPeekMessage", hle_OSPeekMessage);
    Core::syscallHandler.registerSyscall("OSJamMessage", hle_OSJamMessage);
    Core::syscallHandler.registerSyscall("__OSReadRegister32Ex", hle_OSReadRegister32Ex);
    Core::syscallHandler.registerSyscall("__OSWriteRegister32Ex", hle_OSWriteRegister32Ex);
    Core::syscallHandler.registerSyscall("OSReadRegister16", hle_OSReadRegister16);
    Core::syscallHandler.registerSyscall("OSWriteRegister16", hle_OSWriteRegister16);
    Core::syscallHandler.registerSyscall("OSDisableInterrupts", hle_OSDisableInterrupts);
    Core::syscallHandler.registerSyscall("OSRestoreInterrupts", hle_OSRestoreInterrupts);
    Core::syscallHandler.registerSyscall("OSEnforceInorderIO", hle_OSEnforceInorderIO);
    Core::syscallHandler.registerSyscall("OSDriver_Register", hle_OSDriver_Register);
    Core::syscallHandler.registerSyscall("OSDynLoad_Acquire", hle_OSDynLoad_Acquire);
    Core::syscallHandler.registerSyscall("Initialize__Q2_2nn3actFv", hle_nn_act_Initialize);
    Core::syscallHandler.registerSyscall("Finalize__Q2_2nn3actFv", hle_nn_act_Initialize);
    Core::syscallHandler.registerSyscall("GetSlotNo__Q2_2nn3actFv", hle_nn_act_GetSlotNo);
    Core::syscallHandler.registerSyscall("GetDefaultAccount__Q2_2nn3actFv", hle_nn_act_GetSlotNo);
    Core::syscallHandler.registerSyscall("GetParentalControlSlotNo__Q2_2nn3actFv", hle_nn_act_GetSlotNo);
    Core::syscallHandler.registerSyscall("GetNumOfAccounts__Q2_2nn3actFv", hle_nn_act_GetNumOfAccounts);
    Core::syscallHandler.registerSyscall("IsSlotOccupied__Q2_2nn3actFUc", hle_nn_act_IsSlotOccupied);
    Core::syscallHandler.registerSyscall("GetPersistentId__Q2_2nn3actFv", hle_nn_act_GetPersistentId);
    Core::syscallHandler.registerSyscall("GetPersistentIdEx__Q2_2nn3actFUc", hle_nn_act_GetPersistentIdEx);
    Core::syscallHandler.registerSyscall("IsNetworkAccount__Q2_2nn3actFv", hle_nn_act_IsNetworkAccount);
    Core::syscallHandler.registerSyscall("IsNetworkAccountEx__Q2_2nn3actFUc", hle_nn_act_IsNetworkAccount);
    Core::syscallHandler.registerSyscall("GetMii__Q2_2nn3actFP12FFLStoreData", hle_nn_act_GetMii);
    Core::syscallHandler.registerSyscall("GetMiiEx__Q2_2nn3actFP12FFLStoreDataUc", hle_nn_act_GetMii);
    Core::syscallHandler.registerSyscall("GetMiiName__Q2_2nn3actFPw", hle_nn_act_GetMiiName);
    Core::syscallHandler.registerSyscall("GetMiiNameEx__Q2_2nn3actFPwUc", hle_nn_act_GetMiiName);
    Core::syscallHandler.registerSyscall("GetUuidEx__Q2_2nn3actFP7ACTUuidUc", hle_nn_act_GetUuidEx);
    Core::syscallHandler.registerSyscall("GetPrincipalIdEx__Q2_2nn3actFPUiUc", hle_nn_act_GetPrincipalIdEx);
    Core::syscallHandler.registerSyscall("GetSimpleAddressIdEx__Q2_2nn3actFPUiUc", hle_nn_act_GetSimpleAddressIdEx);
    Core::syscallHandler.registerSyscall("GetTransferableIdEx__Q2_2nn3actFPULUiUc", hle_nn_act_GetTransferableIdEx);
    Core::syscallHandler.registerSyscall("OSIsHomeButtonMenuEnabled", hle_OSIsHomeButtonMenuEnabled);
    Core::syscallHandler.registerSyscall("OSEnableHomeButtonMenu", hle_OSEnableHomeButtonMenu);
    Core::syscallHandler.registerSyscall("_SYSGetSystemApplicationTitleId", hle_SYSGetSystemApplicationTitleId);
    Core::syscallHandler.registerSyscall("SAVEInit", hle_SAVEInit);
    Core::syscallHandler.registerSyscall("KPADInitEx", hle_KPADInitEx);
    Core::syscallHandler.registerSyscall("KPADGetMplsWorkSize", hle_KPADGetMplsWorkSize);
    Core::syscallHandler.registerSyscall("KPADSetMplsWorkarea", hle_KPADSetMplsWorkarea);
    Core::syscallHandler.registerSyscall("WPADEnableURCC", hle_WPADEnableURCC);
    Core::syscallHandler.registerSyscall("bspGetHardwareVersion", hle_bspGetHardwareVersion);
    Core::syscallHandler.registerSyscall("FSSetStateChangeNotification", hle_FSSetStateChangeNotification);
    Core::syscallHandler.registerSyscall("OSGetCoreId", hle_OSGetCoreId);
    Core::syscallHandler.registerSyscall("OSGetMainCoreId", hle_OSGetMainCoreId);
    Core::syscallHandler.registerSyscall("OSGetCoreCount", hle_OSGetCoreCount);
    Core::syscallHandler.registerSyscall("OSIsMainCore", hle_OSIsMainCore);
    Core::syscallHandler.registerSyscall("OSIsDebuggerPresent", hle_OSIsDebuggerPresent);
    Core::syscallHandler.registerSyscall("OSIsDebuggerInitialized", hle_OSIsDebuggerInitialized);
    Core::syscallHandler.registerSyscall("OSEnableHomeButtonMenu", hle_OSEnableHomeButtonMenu);
    Core::syscallHandler.registerSyscall("OSCompareAndSwapAtomic", hle_OSCompareAndSwapAtomic);
    Core::syscallHandler.registerSyscall("OSCompareAndSwapAtomicEx", hle_OSCompareAndSwapAtomicEx);
    Core::syscallHandler.registerSyscall("OSReport", hle_OSReport);
    Core::syscallHandler.registerSyscall("OSFatal", hle_OSFatal);
    Core::syscallHandler.registerSyscall("__os_snprintf", hle_os_snprintf);

    // OS Mutex
    Core::syscallHandler.registerSyscall("OSInitMutex", hle_OSInitMutex);
    Core::syscallHandler.registerSyscall("OSInitMutexEx", hle_OSInitMutexEx);
    Core::syscallHandler.registerSyscall("OSLockMutex", hle_OSLockMutex);
    Core::syscallHandler.registerSyscall("OSUnlockMutex", hle_OSUnlockMutex);
    Core::syscallHandler.registerSyscall("OSTryLockMutex", hle_OSTryLockMutex);

    // OS Thread
    Core::syscallHandler.registerSyscall("OSCreateThread", hle_OSCreateThread);
    Core::syscallHandler.registerSyscall("OSResumeThread", hle_OSResumeThread);
    Core::syscallHandler.registerSyscall("OSSleepTicks", hle_OSSleepTicks);
    Core::syscallHandler.registerSyscall("OSYieldThread", hle_OSYieldThread);
    Core::syscallHandler.registerSyscall("OSGetCurrentThread", hle_OSGetCurrentThread);
    Core::syscallHandler.registerSyscall("OSJoinThread", hle_OSJoinThread);
    Core::syscallHandler.registerSyscall("OSExitThread", hle_OSExitThread);
    Core::syscallHandler.registerSyscall("OSSetThreadName", hle_OSSetThreadName);
    Core::syscallHandler.registerSyscall("OSSetThreadPriority", hle_OSSetThreadPriority);
    Core::syscallHandler.registerSyscall("OSSetThreadAffinity", hle_OSSetThreadAffinity);

    // OS TLS
    Core::syscallHandler.registerSyscall("wut_get_thread_specific", hle_wut_get_thread_specific);
    Core::syscallHandler.registerSyscall("wut_set_thread_specific", hle_wut_set_thread_specific);
    Core::syscallHandler.registerSyscall("OSGetThreadSpecific", hle_wut_get_thread_specific);
    Core::syscallHandler.registerSyscall("OSSetThreadSpecific", hle_wut_set_thread_specific);

    // MEM (real guest-visible heaps)
    RegisterMemHeapFunctions();

    // Cache
    Core::syscallHandler.registerSyscall("DCFlushRange", hle_DCFlushRange);
    Core::syscallHandler.registerSyscall("DCInvalidateRange", hle_DCInvalidateRange);

    // OS sync
    Core::syscallHandler.registerSyscall("OSUninterruptibleSpinLock_Acquire", hle_OSUninterruptibleSpinLock_Acquire);
    Core::syscallHandler.registerSyscall("OSUninterruptibleSpinLock_Release", hle_OSUninterruptibleSpinLock_Release);
    Core::syscallHandler.registerSyscall("OSUninterruptibleSpinLock_TryAcquire", hle_OSUninterruptibleSpinLock_TryAcquire);

    // WUT runtime
    Core::syscallHandler.registerSyscall("__init_wut", hle___init_wut);
    Core::syscallHandler.registerSyscall("__rplwrap_exit", hle___rplwrap_exit);
    Core::syscallHandler.registerSyscall("wut_main_exit", hle___rplwrap_exit);
    Core::syscallHandler.registerSyscall("main", hle_main_import);

    // ProcUI
    Core::syscallHandler.registerSyscall("ProcUIInit", hle_ProcUIInit);
    Core::syscallHandler.registerSyscall("ProcUIInitEx", hle_ProcUIInitEx);
    Core::syscallHandler.registerSyscall("ProcUIShutdown", hle_ProcUIShutdown);
    Core::syscallHandler.registerSyscall("ProcUIIsRunning", hle_ProcUIIsRunning);
    Core::syscallHandler.registerSyscall("ProcUIProcessMessages", hle_ProcUIProcessMessages);
    Core::syscallHandler.registerSyscall("ProcUISubProcessMessages", hle_ProcUISubProcessMessages);
    Core::syscallHandler.registerSyscall("ProcUIRegisterCallback", hle_ProcUIRegisterCallback);

    // VPAD
    Core::syscallHandler.registerSyscall("VPADInit", hle_VPADInit);
    Core::syscallHandler.registerSyscall("VPADSetAccParam", hle_VPADSetAccParam);
    Core::syscallHandler.registerSyscall("VPADRead", hle_VPADRead);

    // FS / FSA
    Core::syscallHandler.registerSyscall("FSInit", hle_FSInit);
    Core::syscallHandler.registerSyscall("FSShutdown", hle_FSShutdown);
    Core::syscallHandler.registerSyscall("FSInitCmdBlock", hle_FSInitCmdBlock);
    Core::syscallHandler.registerSyscall("FSAInit", hle_FSAInit);
    Core::syscallHandler.registerSyscall("FSAShutdown", hle_FSAShutdown);
    Core::syscallHandler.registerSyscall("FSAAddClient", hle_FSAAddClient);
    Core::syscallHandler.registerSyscall("FSADelClient", hle_FSADelClient);
    Core::syscallHandler.registerSyscall("FSACloseFile", hle_FSACloseFile);
    Core::syscallHandler.registerSyscall("FSAMount", hle_FSAMount);
    Core::syscallHandler.registerSyscall("FSAUnmount", hle_FSAUnmount);
    Core::syscallHandler.registerSyscall("FSAChangeDir", hle_FSAChangeDir);
    Core::syscallHandler.registerSyscall("FSAChangeMode", hle_FSAChangeMode);
    Core::syscallHandler.registerSyscall("FSARemove", hle_FSARemove);
    Core::syscallHandler.registerSyscall("FSARename", hle_FSARename);

    // Sysapp
    Core::syscallHandler.registerSyscall("SYSRelaunchTitle", hle_SYSRelaunchTitle);
    Core::syscallHandler.registerSyscall("SYSLaunchMenu", hle_SYSLaunchMenu);

    // Events, conds, alarms, GHS runtime, cache ops, panic/report, UC/MCP, OSDynLoad, zlib
    RegisterCoreinitExtraFunctions();

    // sndcore2 (AX) voices + frame-callback registry
    RegisterAxFunctions();

    // Online/account service libraries (nn::ac/fp/olv/boss/ec, AOC) + async login callbacks
    RegisterNnOnlineFunctions();
}
