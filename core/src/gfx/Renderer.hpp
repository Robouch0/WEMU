#pragma once
/*
** SDL2 framebuffer renderer for the WEMU core.
** Opened from main(), referenced by the HLE callbacks (OSScreenFlipBuffersEx).
** The TV screen is 1280×720, DRC is 854×480.
*/
#include <SDL2/SDL.h>
#include <cstdint>

class Renderer {
    public:
        static constexpr int TV_W = 1280, TV_H = 720;
        static constexpr int DRC_W = 854, DRC_H = 480;

        // Opens a TV-sized window and initialises the SDL2 texture pipeline.
        Renderer();
        ~Renderer();

        // Blit 1280×720 RGBX (big-endian, 4 bytes/pixel) to the window.
        // Called from OSScreenFlipBuffersEx.
        void flip_tv(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h);

        // Poll SDL events; returns false if the user closed the window.
        bool poll_events();

        // Keyboard state → VPAD button bitmask.
        std::uint32_t get_buttons();

        bool is_open() const { return m_open; }

    private:
        SDL_Window *m_window = nullptr;
        SDL_Renderer *m_sdl_rend = nullptr;
        SDL_Texture *m_texture = nullptr;
        bool m_open = true;
};
