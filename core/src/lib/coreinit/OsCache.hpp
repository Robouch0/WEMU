/*
** EPITECH PROJECT, 2025
** core
** File description:
** coreinit — cache management HLE (no-ops)
*/

#pragma once

#include <cstdint>

#include "cpu/interpreter/Interpreter.hpp"

// ─── Cache management ─────────────────────────────────────────────────────────
// In emulation, host memory is always coherent between "CPU" and "GPU".
// All Wii U cache operations are therefore no-ops.

inline void DCFlushRange(Core::Interpreter & /*cpu*/,
                          std::uint32_t /*addr*/,
                          std::uint32_t /*size*/) {}

inline void DCInvalidateRange(Core::Interpreter & /*cpu*/,
                               std::uint32_t /*addr*/,
                               std::uint32_t /*size*/) {}

inline void DCStoreRange(Core::Interpreter & /*cpu*/,
                          std::uint32_t /*addr*/,
                          std::uint32_t /*size*/) {}

inline void DCFlushRangeNoSync(Core::Interpreter & /*cpu*/,
                                std::uint32_t /*addr*/,
                                std::uint32_t /*size*/) {}

inline void ICInvalidateRange(Core::Interpreter & /*cpu*/,
                               std::uint32_t /*addr*/,
                               std::uint32_t /*size*/) {}
