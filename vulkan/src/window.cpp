//
// Created by nicolas on 2/18/26.
//

#include "headers/wemuEngineVulkan.hpp"

void WemuEngineVulkan::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // don't create an useless OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); // for now can't resize the window

    m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}