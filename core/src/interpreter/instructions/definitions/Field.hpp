/*
** EPITECH PROJECT, 2025
** core
** File description:
** Field
*/

#pragma once

enum Field
{
#define FIELD(name) F_##name,
    #include "interpreter/instructions/config/cpu_fields.anh"
#undef FIELD
};

#define FIELD(name) \
        inline std::pair<Field,uint32_t> name(uint32_t v) { \
            return std::make_pair(Field::F_##name, v); \
        }
    #include "interpreter/instructions/config/cpu_fields.anh"
#undef FIELD
