/*
** EPITECH PROJECT, 2025
** core
** File description:
** InstructionID
*/

#pragma once

enum InstructionID
{
#define INSTR(name, ...) E_##name,
    #include "cpu/tables/cpu_instructions.anh"
#undef INSTR
};
