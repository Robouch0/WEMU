//
// Created by nicolas on 2/18/26.
//

#include <stdexcept>

#include "../Renderer.hpp"

void Renderer::createSurface()
{
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
}
