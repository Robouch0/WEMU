/*
** EPITECH PROJECT, 2025
** core
** File description:
** interpreter
*/

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

enum Field
{
#define FIELD(name) F_##name,
    #include "cpu_fields.anh"
#undef FIELD
};

#define FIELD(name) \
    inline std::pair<Field,uint32_t> name(uint32_t v) { \
        return std::make_pair(Field::F_##name, v); \
    }
#include "cpu_fields.anh"
#undef FIELD

enum InstructionID
{
#define INSTR(name, ...) name,
#include "cpu_instructions.anh"
#undef INSTR
};

struct InstructionInfo
{
    InstructionID id;
    std::vector<std::pair<Field, uint32_t>> matchFields;
};

#define INSTR(name, ...) \
    { name, { __VA_ARGS__ } },
static const InstructionInfo instructionArray[] = {
    #include "cpu_instructions.anh"
};
#undef INSTR

inline std::string fieldToString(const Field f) {
    switch (f) {
#define FIELD(name) case Field::F_##name: return #name;
#include "cpu_fields.anh"
#undef FIELD
    }
    return "?";
}

inline std::string instructionIDToString(const InstructionID id) {
    switch (id) {
#define INSTR(name, ...) case InstructionID::name: return #name;
#include "cpu_instructions.anh"
#undef INSTR
    }
    return "?";
}

union EncodedInstruction
{
    explicit EncodedInstruction(const uint32_t &raw) : opcd(0) { m_raw = raw; }
    uint32_t m_raw;

    struct { uint32_t : 26; uint32_t opcd : 6; };
    struct { uint32_t : 1; uint32_t xo10 : 10; uint32_t : 21; };
    struct { uint32_t : 1; uint32_t xo9 : 9; uint32_t : 20; };
};
