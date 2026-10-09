/*
** EPITECH PROJECT, 2026
** core
** File description:
** Ax -- minimal sndcore2 (AX) HLE: voice objects, frame-callback pump, query stubs
*/

#pragma once

namespace Core {
    class Interpreter;
} // namespace Core

void RegisterAxFunctions();

namespace Core::Ax {
    // Audio frame tick, called periodically from the interpreter run loop: arms the synthetic
    // AX thread to run the registered frame callbacks (no-op while it is still busy or nothing
    // is registered).
    void OnFrameTick(Interpreter &cpu);

    // The AX thread returned to AX_FRAME_SENTINEL: run the next queued callback or park until
    // the next frame tick. Always leaves the interpreter on a runnable thread.
    void OnSentinelReturn(Interpreter &cpu);
} // namespace Core::Ax
