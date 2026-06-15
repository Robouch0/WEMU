/*
** EPITECH PROJECT, 2025
** core
** File description:
** InstructionID
*/

#pragma once

#include <cstdint>
enum InstructionID : std::uint16_t { // NOLINT(performance-enum-size)
#define INSTR(name, ...) E_##name,
#include "cpu/tables/cpu_instructions.anh"
#undef INSTR
};
