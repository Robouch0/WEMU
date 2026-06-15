//
// Created by nicolas on 2/18/26.
//

#include <stdexcept>

#include "../Renderer.hpp"

void Renderer::createSurface()
{
    if (SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface) == SDL_FALSE)
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
}
