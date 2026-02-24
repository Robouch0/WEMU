/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#pragma once
#include <bitset>
#include <cstdint>
#include <iostream>

namespace Core {
    union FixedPointExceptionRegister {
        std::uint32_t raw = 0;

        struct {
            std::uint32_t so: 1;
            std::uint32_t ov: 1;
            std::uint32_t ca: 1;
            std::uint32_t : 22;
            std::uint32_t nbrBytes: 7;
        };
    };

    union ConditionRegister {
        std::uint32_t raw = 0;

        union Register {
            std::uint32_t raw: 4;

            struct {
                std::uint32_t lt: 1; // Negative
                std::uint32_t gt: 1; // Positive
                std::uint32_t eq: 1; // Zero
                std::uint32_t so: 1; // Summary Overflow
            };
        };

        struct {
            ConditionRegister::Register cr0;
            ConditionRegister::Register cr1;
            ConditionRegister::Register cr2;
            ConditionRegister::Register cr3;
            ConditionRegister::Register cr4;
            ConditionRegister::Register cr5;
            ConditionRegister::Register cr6;
            ConditionRegister::Register cr7;
        };
    };

    union FloatingPointStatusAndControlRegister {
        std::uint32_t raw = 0;

        struct {
            std::uint32_t fx: 1;
            std::uint32_t fex: 1;
            std::uint32_t vx: 1;
            std::uint32_t ox: 1;
            std::uint32_t ux: 1;
            std::uint32_t zx: 1;
            std::uint32_t xx: 1;
            std::uint32_t vxsnan: 1;
            std::uint32_t vxisi: 1;
            std::uint32_t vxidi: 1;
            std::uint32_t vxzdz: 1;
            std::uint32_t vximz: 1;
            std::uint32_t vxvc: 1;
            std::uint32_t fr: 1;
            std::uint32_t fi: 1;
            std::uint32_t : 6;
            std::uint32_t vxsoft: 1;
            std::uint32_t vxsqrt: 1;
            std::uint32_t vxcvi: 1;
            std::uint32_t ve: 1;
            std::uint32_t oe: 1;
            std::uint32_t ue: 1;
            std::uint32_t ze: 1;
            std::uint32_t xe: 1;
            std::uint32_t ni: 1;
            std::uint32_t rn: 2;
        };

        struct {
            std::uint32_t : 14;
            std::uint32_t fprf: 5;
            std::uint32_t : 13;
        };

        struct {
            std::uint32_t : 14;
            std::uint32_t c: 1;
            std::uint32_t fpcc: 4;
            std::uint32_t : 13;
        };
    };
} // namespace Core
