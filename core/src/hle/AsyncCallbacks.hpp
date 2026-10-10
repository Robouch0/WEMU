/*
** EPITECH PROJECT, 2026
** core
** File description:
** AsyncCallbacks -- deferred guest-callback delivery. Several coreinit/nn "async" APIs take a
** completion callback that hardware fires later on a system thread (e.g. nn::fp::LoginAsync). We
** queue those calls and run them on one synthetic system thread, generalising the AX frame-callback
** pump. The API returns success synchronously; the callback then fires a moment later.
*/

#pragma once

#include <cstdint>

namespace Core {
    class Interpreter;
} // namespace Core

namespace Core::Async {

    // Queue a guest function to be called soon with up to four word-sized arguments (r3..r6).
    // Safe to call from any HLE handler; the call runs later on the async pump thread.
    void enqueue(std::uint32_t func, std::uint32_t a0 = 0, std::uint32_t a1 = 0, std::uint32_t a2 = 0, std::uint32_t a3 = 0);

    // Called periodically from the interpreter run loop: if calls are queued and the pump thread is
    // idle, arm it to run the next one. No-op when the queue is empty or the pump is still busy.
    void OnTick(Interpreter &cpu);

    // The pump thread returned to ASYNC_CB_SENTINEL: run the next queued call or park the thread.
    void OnSentinelReturn(Interpreter &cpu);

} // namespace Core::Async
