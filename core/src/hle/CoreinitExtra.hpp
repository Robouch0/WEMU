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

// Installs per-method intercepts for the MVPlayer (menu movie) vtable. Must run before
// Interpreter::run() so the hook PC range is known. See CoreinitExtra.cpp for the state machine.
void InstallMVPlayerHooks(Core::Interpreter &interp);

// Seeds MK8's title-sequence frame pool so the title advances to the menu. The pool is normally
// fed by a message queue whose producer we do not emulate; without backing, the first allocation
// returns NULL and the worker faults, leaving `main` blocked forever. Self-guarding: installs only
// when the refill routine's signature is present, so non-MK8 titles are untouched. Must run before
// Interpreter::run(). See CoreinitExtra.cpp for the full analysis.
void InstallTitleSeqPoolFix(Core::Interpreter &interp);

// Works around the scene-init gate: a scene-config method iterates a FIXED 14 entries of a
// container whose backing is never allocated (its resource descriptor yields 0 entries in our
// offline state), faulting on the NULL backing. Skipping the iteration lets MK8 run its scene
// state machine (frames transition instead of freezing). Self-guarding by instruction signature;
// opt out with WEMU_NO_SCENECFG_SKIP=1. Must run before Interpreter::run().
void InstallSceneConfigWorkaround(Core::Interpreter &interp);

// EXPERIMENT (WEMU_FORCE_SCENE=1): force the scene-transition is-busy predicate to 0. See impl.
void InstallSceneUpdateForce(Core::Interpreter &interp);

// EXPERIMENT (WEMU_FORCE_SUBSYS=1): force the scene-object subsystem "ready" flag. See impl.
void InstallSubsysReadyForce(Core::Interpreter &interp);
