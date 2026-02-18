//
// Created by nicolas on 2/18/26.
//

#include "wemuEngineVulkan.hpp"

void WemuEngineVulkan::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // don't create an useless OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); // for now can't resize the window

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}