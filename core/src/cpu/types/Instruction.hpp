/*
** EPITECH PROJECT, 2025
** core
** File description:
** Instruction
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Field.hpp"
#include "cpu/types/InstructionID.hpp"
#include "cpu/types/InstructionInfo.hpp"

// Define every instructions prototypes
namespace Core::Instruction {
    #define INSTR(name, ...) void name(Core::Interpreter &, const EncodedInstruction &);
    #include "cpu/tables/cpu_instructions.anh"
    #undef INSTR
} // namespace Core::Instruction

#define INSTR(name, ...) {E_##name, {__VA_ARGS__}, Core::Instruction::name},
static const InstructionInfo INSTRUCTIONARRAY[] = {
    #include "cpu/tables/cpu_instructions.anh"
};
#undef INSTR

#define OPCODE matchFields[0].second

inline std::string fieldToString(const Field f)
{
    switch (f) {
            #define FIELD(name)                                                                                                    \
    case Field::F_##name:                                                                                              \
        return #name;
            #include "cpu/tables/cpu_fields.anh"
            #undef FIELD
    }
    return "?";
}

inline std::string instructionIDToString(const InstructionID id)
{
    switch (id) {
            #define INSTR(name, ...)                                                                                               \
    case InstructionID::E_##name:                                                                                      \
        return #name;
            #include "cpu/tables/cpu_instructions.anh"
            #undef INSTR
    }
    return "?";
}
