#include "firstapp.hpp"

namespace WEMU {
    void FirstApp::run() {
        while (!lveWindow.shouldClose()) {
            glfwPollEvents();
        }
    }
}