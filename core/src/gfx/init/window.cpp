//
// Created by nicolas on 2/18/26.
//

#include "../Renderer.hpp"

// void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
//     if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
//         m_open = false;
//     }
// }

void Renderer::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // don't create an useless OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); // for now can't resize the window

    m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(m_window, this);
    // glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    // glfwSetKeyCallback(m_window, key_callback);
}