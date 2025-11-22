/*
** EPITECH PROJECT, 2025
** core
** File description:
** Instruction
*/

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Field.hpp"
#include "EncodedInstruction.hpp"

namespace Core {
    class Interpreter;
}

enum InstructionID
{
#define INSTR(name, ...) E_##name,
    #include "interpreter/instructions/config/cpu_instructions.anh"
#undef INSTR
};

struct InstructionInfo
{
    InstructionID id;
    std::vector<std::pair<Field, uint32_t>> matchFields;
    std::function<void (Core::Interpreter &, const EncodedInstruction &)> function;
};

namespace Core::Instruction
{
#define INSTR(name, ...) void name(Core::Interpreter &, const EncodedInstruction &);
    #include "interpreter/instructions/config/cpu_instructions.anh"
#undef INSTR
}

// Define every instructions prototypes
#define INSTR(name, ...) \
    { E_##name, { __VA_ARGS__ }, Core::Instruction::name },
static const InstructionInfo INSTRUCTIONARRAY[] = {
    #include "interpreter/instructions/config/cpu_instructions.anh"
};
#undef INSTR

inline std::string fieldToString(const Field f)
{
    switch (f) {
#define FIELD(name) case Field::F_##name: return #name;
    #include "interpreter/instructions/config/cpu_fields.anh"
#undef FIELD
    }
    return "?";
}

inline std::string instructionIDToString(const InstructionID id)
{
    switch (id) {
#define INSTR(name, ...) case InstructionID::E_##name: return #name;
    #include "interpreter/instructions/config/cpu_instructions.anh"
#undef INSTR
    }
    return "?";
}
