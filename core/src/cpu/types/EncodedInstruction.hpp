/*
** EPITECH PROJECT, 2025
** core
** File description:
** EncodedInstruction
*/

#pragma once

#include <cstdint>

union EncodedInstruction {
    explicit EncodedInstruction(const std::uint32_t &raw) : opcd(0) { this->raw = raw; }
    std::uint32_t raw;

    struct {
        std::uint32_t : 26;
        std::uint32_t opcd: 6;
    };

    struct {
        std::uint32_t : 1;
        std::uint32_t xo10: 10;
        std::uint32_t : 21;
    };

    struct {
        std::uint32_t : 1;
        std::uint32_t xo9: 9;
        std::uint32_t : 20;
    };

    struct {
        std::uint32_t : 21;
        std::uint32_t rt: 5;
        std::uint32_t : 6;
    };

    struct {
        std::uint32_t : 16;
        std::uint32_t ra: 5;
        std::uint32_t : 11;
    };

    struct {
        std::uint32_t : 11;
        std::uint32_t rb: 5;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t si: 16;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t rc: 1;
        std::uint32_t : 31;
    };

    struct {
        std::uint32_t : 10;
        std::uint32_t oe: 1;
        std::uint32_t : 21;
    };
};
