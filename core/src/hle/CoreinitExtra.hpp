/*
** EPITECH PROJECT, 2026
** core
** File description:
** CoreinitExtra -- second wave of coreinit HLE: events/conds/alarms, GHS runtime, cache ops,
** report/panic visibility, UC/MCP system config, OSDynLoad exports, zlib125 bridge
*/

#pragma once

#include <cstdint>
#include <string>

namespace Core {
    class Interpreter;
    namespace Hle {
        // Callable through the same indirect-import path as OSDynLoad_FindExport.
        std::uint32_t exportFunction(Interpreter &cpu, const std::string &name);
    } // namespace Hle
} // namespace Core

void RegisterCoreinitExtraFunctions();
void ProcessCoreinitAlarms(Core::Interpreter &cpu);

// Installs per-method intercepts for the MVPlayer (menu movie) vtable. Must run before
// Interpreter::run() so the hook PC range is known. See MvPlayer.cpp for the library implementation.
void InstallMVPlayerHooks(Core::Interpreter &interp);
