/*
** EPITECH PROJECT, 2025
** core
** File description:
** interpreter
*/

#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

enum Field {
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

struct Instruction {
    std::string name;
    std::vector<std::pair<Field, uint32_t>> matchFields;
};

#define INSTR(name, ...) \
    { #name, { __VA_ARGS__ } },
static const Instruction instructionTable[] = {
    #include "cpu_instructions.anh"
};
#undef INSTR

#undef INSTR

inline std::string fieldToString(const Field f) {
    switch (f) {
#define FIELD(name) case Field::F_##name: return #name;
#include "cpu_fields.anh"
#undef FIELD
    }
    return "?";
}
