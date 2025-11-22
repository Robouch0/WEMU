/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#pragma once
#include <cstdint>

namespace Core
{

    union FixedPointExceptionRegister
    {
        std::uint32_t raw = 0;

        struct
        {
            std::uint32_t so : 1;
            std::uint32_t ov : 1;
            std::uint32_t ca : 1;
            std::uint32_t : 22;
            std::uint32_t nbrBytes: 7;
        };
    };

    union ConditionRegister
    {
        std::uint32_t raw = 0;

        struct
        {
            std::uint32_t cr0 : 4;
            std::uint32_t cr1 : 4;
            std::uint32_t cr2 : 4;
            std::uint32_t cr3 : 4;
            std::uint32_t cr4 : 4;
            std::uint32_t cr5 : 4;
            std::uint32_t cr6 : 4;
            std::uint32_t cr7 : 4;
        };
    };

    union FloatingPointStatusAndControlRegister
    {
        std::uint32_t raw = 0;

        struct
        {
            std::uint32_t fx : 1;
            std::uint32_t fex : 1;
            std::uint32_t vx : 1;
            std::uint32_t ox : 1;
            std::uint32_t ux : 1;
            std::uint32_t zx : 1;
            std::uint32_t xx : 1;
            std::uint32_t vxsnan : 1;
            std::uint32_t vxisi : 1;
            std::uint32_t vxidi : 1;
            std::uint32_t vxzdz : 1;
            std::uint32_t vximz : 1;
            std::uint32_t vxvc : 1;
            std::uint32_t fr : 1;
            std::uint32_t fi : 1;
            std::uint32_t : 6;
            std::uint32_t vxsoft : 1;
            std::uint32_t vxsqrt : 1;
            std::uint32_t vxcvi : 1;
            std::uint32_t ve : 1;
            std::uint32_t oe : 1;
            std::uint32_t ue : 1;
            std::uint32_t ze : 1;
            std::uint32_t xe : 1;
            std::uint32_t ni : 1;
            std::uint32_t rn : 2;
        };

        struct { std::uint32_t : 14; std::uint32_t fprf : 5; std::uint32_t : 13; };
        struct { std::uint32_t : 14; std::uint32_t c : 1; std::uint32_t fpcc : 4; std::uint32_t : 13; };
    };
}
