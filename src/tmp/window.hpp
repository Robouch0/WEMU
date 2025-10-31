#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

namespace WEMU {

    class Window {
        public:
            Window(int newWidth, int newHeight, std::string newName);
            ~Window();

            bool shouldClose() { return glfwWindowShouldClose(window);}

        private:

            void initWindow();

            const int width;
            const int height;
            std::string windowName;

            GLFWwindow *window;
    };
}