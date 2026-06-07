/*
** EPITECH PROJECT, 2025
** core
** File description:
** InstructionInfo
*/

#pragma once


#include <cstdint>
#include <functional>
#include <vector>

#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Field.hpp"
#include "cpu/types/InstructionID.hpp"

namespace Core {
    class Interpreter;
}

struct InstructionInfo {
        InstructionID id;
        std::vector<std::pair<Field, uint32_t>> matchFields;
        std::function<void(Core::Interpreter &, const EncodedInstruction &)> function;
};
