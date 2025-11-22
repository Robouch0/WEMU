/*
** EPITECH PROJECT, 2025
** core
** File description:
** EncodedInstruction
*/

#pragma once

#include <cstdint>

union EncodedInstruction
{
    explicit EncodedInstruction(const uint32_t &raw) : opcd(0) { m_raw = raw; }
    uint32_t m_raw;

    struct { uint32_t : 26; uint32_t opcd : 6; };
    struct { uint32_t : 1; uint32_t xo10 : 10; uint32_t : 21; };
    struct { uint32_t : 1; uint32_t xo9 : 9; uint32_t : 20; };
    struct { uint32_t : 21; uint32_t rt : 5; uint32_t : 6; };
    struct { uint32_t : 16; uint32_t ra : 5; uint32_t : 11; };
    struct { uint32_t : 11; uint32_t rb : 5; uint32_t : 16; };
    struct { uint32_t si : 16; uint32_t : 16; };
};
