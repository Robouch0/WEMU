//
// Created by nicolas on 2/18/26.
//

#include "headers/wemuEngineVulkan.hpp"
#include <stdexcept>

void WemuEngineVulkan::createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
}