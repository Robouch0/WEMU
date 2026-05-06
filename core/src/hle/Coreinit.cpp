/*
** EPITECH PROJECT, 2025
** core
** File description:
** HLE coreinit stubs (OSScreen, OS*, MEM, DC, TLS, VPAD, ProcUI, FSA, WUT runtime)
*/

#include "Coreinit.hpp"

#include <cstdio>
#include <cstring>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "gfx/Renderer.hpp"

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

// ── Thread-local storage (single-thread emulation, 64 keys max) ───────────────

static std::uint32_t s_tls[64] = {};

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
    static std::uint64_t last_ms = SDL_GetTicks64();
    ++frames;
    std::uint64_t now = SDL_GetTicks64();
    if (now - last_ms >= 1000) {
        fprintf(stderr, "[FPS] %u\n", frames);
        frames = 0;
        last_ms = now;
    }

    cpu.m_gpr[3] = 0;
}

// ---- OS misc ----

static void hle_OSGetTick(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSGetTime(Core::Interpreter &cpu)
{
    cpu.m_gpr[3] = 0;
    cpu.m_gpr[4] = 0;
}
static void hle_OSGetTitleID(Core::Interpreter &cpu)
{
    cpu.m_gpr[3] = 0;
    cpu.m_gpr[4] = 0;
}
static void hle_OSGetSystemInfo(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSGetCoreId(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
static void hle_OSGetMainCoreId(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
static void hle_OSIsDebuggerPresent(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSIsDebuggerInitialized(Core::Interpreter &cpu)
{
    Utils::Log::error("hle_OSIsDebuggerInitialized");
    cpu.m_gpr[3] = 0;
}
static void hle_OSEnableHomeButtonMenu(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

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

static void hle_OSInitMutex(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSInitMutexEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSLockMutex(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSUnlockMutex(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

// ---- OS Thread ----

static void hle_OSCreateThread(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSResumeThread(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSSleepTicks(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSYieldThread(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSGetCurrentThread(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
static void hle_OSSetThreadName(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_OSSetThreadPriority(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
static void hle_OSSetThreadAffinity(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }

// ---- OS TLS ----

static void hle_wut_get_thread_specific(Core::Interpreter &cpu)
{
    std::uint32_t key = cpu.m_gpr[3];

    cpu.m_gpr[3] = (key < 64) ? s_tls[key] : 0;
}

static void hle_wut_set_thread_specific(Core::Interpreter &cpu)
{
    std::uint32_t key = cpu.m_gpr[3], val = cpu.m_gpr[4];

    if (key < 64)
        s_tls[key] = val;
    cpu.m_gpr[3] = 0;
}

// ---- MEM ----

static void hle_MEMAllocFromDefaultHeap(Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[3], 8); }

static void hle_MEMAllocFromDefaultHeapEx(Core::Interpreter &cpu)
{
    std::uint32_t size = cpu.m_gpr[3];
    std::uint32_t align = cpu.m_gpr[4];

    cpu.m_gpr[3] = cpu.m_memory.heapAllocate(size, align ? align : 8);
}

static void hle_MEMFreeToDefaultHeap(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_MEMGetBaseHeapHandle(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0xDEAD0001u; }
static void hle_MEMCreateExpHeapEx(Core::Interpreter &cpu) { /* return base addr as handle */ }
static void hle_MEMFreeToExpHeap(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_MEMDestroyExpHeap(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_MEMGetAllocatableSizeForExpHeapEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x1000000; }

static void hle_MEMAllocFromExpHeapEx(Core::Interpreter &cpu)
{
    std::uint32_t size = cpu.m_gpr[4];
    std::uint32_t align = cpu.m_gpr[5];

    cpu.m_gpr[3] = cpu.m_memory.heapAllocate(size, align ? align : 8);
}

// ---- DC (cache) ----

static void hle_DCFlushRange(Core::Interpreter &cpu) { (void) cpu; }
static void hle_DCInvalidateRange(Core::Interpreter &cpu) { (void) cpu; }

// ---- OS sync ----

static void hle_OSUninterruptibleSpinLock_Acquire(Core::Interpreter &cpu) { (void) cpu; }
static void hle_OSUninterruptibleSpinLock_Release(Core::Interpreter &cpu) { (void) cpu; }

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

// ---- ProcUI ----

static void hle_ProcUIInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIInitEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIShutdown(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIIsRunning(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUISubProcessMessages(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_ProcUIRegisterCallback(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

static void hle_ProcUIProcessMessages(Core::Interpreter &cpu)
{
    if (cpu.m_renderer) {
        cpu.m_renderer->poll_events();
        cpu.m_gpr[3] = cpu.m_renderer->is_open() ? 0u : 3u;
    } else {
        cpu.m_gpr[3] = 0;
    }
}

// ---- VPAD ----

static void hle_VPADInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_VPADSetAccParam(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

static void hle_VPADRead(Core::Interpreter &cpu)
{
    std::uint32_t buf_addr = cpu.m_gpr[4];
    std::uint32_t err_addr = cpu.m_gpr[6];

    if (!cpu.m_renderer) {
        cpu.m_gpr[3] = 0;
        return;
    }

    cpu.m_renderer->poll_events();
    std::uint32_t hold = cpu.m_renderer->get_buttons();

    static std::uint32_t s_prev_hold = 0;
    std::uint32_t trigger = hold & ~s_prev_hold;
    std::uint32_t released = s_prev_hold & ~hold;
    s_prev_hold = hold;

    cpu.m_memory.write<std::uint32_t>(buf_addr + 0, hold);
    cpu.m_memory.write<std::uint32_t>(buf_addr + 4, trigger);
    cpu.m_memory.write<std::uint32_t>(buf_addr + 8, released);

    if (err_addr)
        cpu.m_memory.write<std::uint32_t>(err_addr, 0);

    cpu.m_gpr[3] = 1;
}

// ---- FS / FSA stubs ----

static void hle_FSInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSShutdown(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSAddClient(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
static void hle_FSDelClient(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
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
    Core::syscallHandler.registerSyscall("OSGetCoreId", hle_OSGetCoreId);
    Core::syscallHandler.registerSyscall("OSGetMainCoreId", hle_OSGetMainCoreId);
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

    // OS Thread
    Core::syscallHandler.registerSyscall("OSCreateThread", hle_OSCreateThread);
    Core::syscallHandler.registerSyscall("OSResumeThread", hle_OSResumeThread);
    Core::syscallHandler.registerSyscall("OSSleepTicks", hle_OSSleepTicks);
    Core::syscallHandler.registerSyscall("OSYieldThread", hle_OSYieldThread);
    Core::syscallHandler.registerSyscall("OSGetCurrentThread", hle_OSGetCurrentThread);
    Core::syscallHandler.registerSyscall("OSSetThreadName", hle_OSSetThreadName);
    Core::syscallHandler.registerSyscall("OSSetThreadPriority", hle_OSSetThreadPriority);
    Core::syscallHandler.registerSyscall("OSSetThreadAffinity", hle_OSSetThreadAffinity);

    // OS TLS
    Core::syscallHandler.registerSyscall("wut_get_thread_specific", hle_wut_get_thread_specific);
    Core::syscallHandler.registerSyscall("wut_set_thread_specific", hle_wut_set_thread_specific);
    Core::syscallHandler.registerSyscall("OSGetThreadSpecific", hle_wut_get_thread_specific);
    Core::syscallHandler.registerSyscall("OSSetThreadSpecific", hle_wut_set_thread_specific);

    // MEM
    Core::syscallHandler.registerSyscall("MEMAllocFromDefaultHeap", hle_MEMAllocFromDefaultHeap);
    Core::syscallHandler.registerSyscall("MEMAllocFromDefaultHeapEx", hle_MEMAllocFromDefaultHeapEx);
    Core::syscallHandler.registerSyscall("MEMFreeToDefaultHeap", hle_MEMFreeToDefaultHeap);
    Core::syscallHandler.registerSyscall("MEMGetBaseHeapHandle", hle_MEMGetBaseHeapHandle);
    Core::syscallHandler.registerSyscall("MEMCreateExpHeapEx", hle_MEMCreateExpHeapEx);
    Core::syscallHandler.registerSyscall("MEMAllocFromExpHeapEx", hle_MEMAllocFromExpHeapEx);
    Core::syscallHandler.registerSyscall("MEMFreeToExpHeap", hle_MEMFreeToExpHeap);
    Core::syscallHandler.registerSyscall("MEMDestroyExpHeap", hle_MEMDestroyExpHeap);
    Core::syscallHandler.registerSyscall("MEMGetAllocatableSizeForExpHeapEx", hle_MEMGetAllocatableSizeForExpHeapEx);

    // Cache
    Core::syscallHandler.registerSyscall("DCFlushRange", hle_DCFlushRange);
    Core::syscallHandler.registerSyscall("DCInvalidateRange", hle_DCInvalidateRange);

    // OS sync
    Core::syscallHandler.registerSyscall("OSUninterruptibleSpinLock_Acquire", hle_OSUninterruptibleSpinLock_Acquire);
    Core::syscallHandler.registerSyscall("OSUninterruptibleSpinLock_Release", hle_OSUninterruptibleSpinLock_Release);

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
    Core::syscallHandler.registerSyscall("FSAddClient", hle_FSAddClient);
    Core::syscallHandler.registerSyscall("FSDelClient", hle_FSDelClient);
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
}
