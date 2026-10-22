/*
** EPITECH PROJECT, 2026
** core
** File description:
** InstructionTraits
*/

#pragma once

#include "cpu/types/InstructionID.hpp"

// Instructions that may write guest memory. A store can replace decoded code, so decoded and
// compiled blocks leave right after one that touched a code page.
constexpr bool isStoreInstruction(const InstructionID id) noexcept
{
    switch (id) {
        case E_STHX:
        case E_STBX:
        case E_STB:
        case E_STH:
        case E_STMW:
        case E_STW:
        case E_STWU:
        case E_STWX:
        case E_STBU:
        case E_STHU:
        case E_STWUX:
        case E_STBUX:
        case E_STHUX:
        case E_STSWI:
        case E_STWCX_:
        case E_STWBRX:
        case E_DCBZ:
        case E_STFS:
        case E_STFSU:
        case E_STFD:
        case E_STFDU:
        case E_STFSX:
        case E_STFSUX:
        case E_STFDX:
        case E_STFIWX:
        case E_PSQ_ST:
        case E_PSQ_STU:
            return true;
        default:
            return false;
    }
}

// Primary opcodes that end a decoded block: bc, sc, b, the opcode-19 family and twi. The next PC
// after them is data-dependent.
constexpr bool endsBlock(const std::uint32_t word) noexcept
{
    const std::uint32_t op = word >> 26;
    return op == 16 || op == 17 || op == 18 || op == 19 || op == 3;
}
