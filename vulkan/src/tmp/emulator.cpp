#include "emulator.hpp"

namespace WEMU {
    void Emulator::run() {
        while (!lveWindow.shouldClose()) {
            glfwPollEvents();
        }
    }
}