/*
** Renderer.cpp — SDL2 + SDL_Texture framebuffer display.
**
** PPC OSScreen pixels are stored RGBX (R=byte0, G=byte1, B=byte2, X=byte3).
** SDL_PIXELFORMAT_ABGR8888 on little-endian reads bytes from memory as
** [R, G, B, A] — identical layout. No bswap needed; copy pixels directly.
**
**   clear (0x00000000) → [0,0,0,0]     → black background   ✓
**   draw_rect black    → [0,0,0,FF]    → black (center line) ✓
**   draw_rect red      → [FF,0,0,FF]   → red (ball)          ✓
**   draw_rect green    → [0,FF,0,FF]   → green (paddles)     ✓
**   blit_char white    → [FF,FF,FF,FF] → white (text)        ✓
*/

#include "Renderer.hpp"

#include <cstring>
#include <stdexcept>

// VPAD button bitmasks (same as wut header vpad/input.h)
static constexpr std::uint32_t BTN_UP = 0x0200;
static constexpr std::uint32_t BTN_DOWN = 0x0100;
static constexpr std::uint32_t BTN_LEFT = 0x0800;
static constexpr std::uint32_t BTN_RIGHT = 0x0400;
static constexpr std::uint32_t BTN_A = 0x8000;
static constexpr std::uint32_t BTN_B = 0x4000;
static constexpr std::uint32_t BTN_PLUS = 0x0008;
static constexpr std::uint32_t BTN_MINUS = 0x0004;

Renderer::Renderer()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(SDL_GetError());

    m_window = SDL_CreateWindow("WEMU — pong", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, TV_W, TV_H, SDL_WINDOW_HIDDEN);
    if (!m_window)
        throw std::runtime_error(SDL_GetError());

    m_sdl_rend = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_sdl_rend)
        throw std::runtime_error(SDL_GetError());

    // ABGR8888: on little-endian, memory order is [R, G, B, A] — matches PPC RGBX directly
    m_texture = SDL_CreateTexture(m_sdl_rend, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, TV_W, TV_H);
    if (!m_texture)
        throw std::runtime_error(SDL_GetError());
}

Renderer::~Renderer()
{
    if (m_texture)
        SDL_DestroyTexture(m_texture);
    if (m_sdl_rend)
        SDL_DestroyRenderer(m_sdl_rend);
    if (m_window)
        SDL_DestroyWindow(m_window);
}

// Copy RGBX framebuffer to ABGR8888 texture (same memory layout) and present.
void Renderer::flip_tv(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h)
{
    void *pixels;
    int pitch;
    if (SDL_LockTexture(m_texture, nullptr, &pixels, &pitch) != 0)
        return;

    const std::uint32_t cols = (w < (std::uint32_t) TV_W) ? w : TV_W;
    const std::uint32_t rows = (h < (std::uint32_t) TV_H) ? h : TV_H;

    auto *dst32 = static_cast<std::uint32_t *>(pixels);
    const auto *src32 = reinterpret_cast<const std::uint32_t *>(rgbx);
    const std::uint32_t stride32 = pitch / 4;

    for (std::uint32_t y = 0; y < rows; ++y) {
        const std::uint32_t *srow = src32 + static_cast<std::size_t>(y) * w;
        std::uint32_t *drow = dst32 + static_cast<std::size_t>(y) * stride32;
        for (std::uint32_t x = 0; x < cols; ++x)
            drow[x] = srow[x];
    }

    SDL_UnlockTexture(m_texture);
    SDL_RenderClear(m_sdl_rend);
    SDL_RenderCopy(m_sdl_rend, m_texture, nullptr, nullptr);

    // Cap to 60fps (real WiiU framerate), regardless of monitor refresh rate
    static std::uint64_t s_last_flip = 0;
    std::uint64_t now = SDL_GetTicks64();
    if (s_last_flip && now - s_last_flip < 16)
        SDL_Delay(16 - (now - s_last_flip));
    s_last_flip = SDL_GetTicks64();

    SDL_RenderPresent(m_sdl_rend);
}

bool Renderer::poll_events()
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            m_open = false;
            return false;
        }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
            m_open = false;
            return false;
        }
    }
    return m_open;
}

std::uint32_t Renderer::get_buttons()
{
    const std::uint8_t *keys = SDL_GetKeyboardState(nullptr);
    std::uint32_t btns = 0;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])
        btns |= BTN_UP;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])
        btns |= BTN_DOWN;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])
        btns |= BTN_LEFT;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])
        btns |= BTN_RIGHT;
    if (keys[SDL_SCANCODE_RETURN])
        btns |= BTN_A;
    if (keys[SDL_SCANCODE_BACKSPACE])
        btns |= BTN_B;
    if (keys[SDL_SCANCODE_P])
        btns |= BTN_PLUS;
    if (keys[SDL_SCANCODE_M])
        btns |= BTN_MINUS;
    return btns;
}
