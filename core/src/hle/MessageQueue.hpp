#pragma once

#include <cstdint>

namespace Core {
    class Interpreter;

    // Post a complete OSMessage without switching the current guest thread.
    bool enqueueMessage(Interpreter &cpu, std::uint32_t queue, std::uint32_t message);
} // namespace Core
