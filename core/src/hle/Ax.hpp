/*
** EPITECH PROJECT, 2026
** core
** File description:
** Ax -- sndcore2 voice mixing and guest audio frame scheduling
*/

#pragma once

namespace Core {
    class Interpreter;
} // namespace Core

void RegisterAxFunctions();

namespace Core::Ax {
    // Arm audio for HLE movies even if the guest has not initialized AX.
    void StartMovieAudio(Interpreter &cpu);
    // Audio frame tick: wakes the AX thread for callbacks and sample mixing.
    void OnFrameTick(Interpreter &cpu);

    // The AX thread returned to AX_FRAME_SENTINEL: run the next queued callback or park until
    // the next frame tick. Always leaves the interpreter on a runnable thread.
    void OnSentinelReturn(Interpreter &cpu);
} // namespace Core::Ax
