/*
** EPITECH PROJECT, 2025
** core
** File description:
** HLE coreinit stubs
*/

#pragma once

#include <cstdint>

void RegisterCoreinitFunctions();

namespace Core {
    class Interpreter;
    // Optional frame pacing on the shared scheduler time base.
    void advanceGuestFrameClock(Interpreter &cpu);
} // namespace Core
