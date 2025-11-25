/*
** EPITECH PROJECT, 2025
** core
** File description:
** InstructionInfo
*/

#pragma once

#include "EncodedInstruction.hpp"

namespace Core {
    class Interpreter;
}

struct InstructionInfo
{
    InstructionID id;
    std::vector<std::pair<Field, uint32_t>> matchFields;
    std::function<void (Core::Interpreter &, const EncodedInstruction &)> function;
};
