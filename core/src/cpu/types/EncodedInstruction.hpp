/*
** EPITECH PROJECT, 2025
** core
** File description:
** EncodedInstruction
*/

#pragma once

#include <cstdint>

static constexpr std::uint32_t CARRY_OFFSET = 32;

union EncodedInstruction {
    explicit EncodedInstruction(const std::uint32_t &raw) : opcd(0) { this->raw = raw; }
    std::uint32_t raw;

    [[nodiscard]] EncodedInstruction endianSwap() const { return EncodedInstruction(std::byteswap(raw)); };

    struct {
        std::uint32_t : 26;
        std::uint32_t opcd : 6;
    };

    struct {
        std::uint32_t lk : 1;
        std::uint32_t : 31;
    };

    struct {
        std::uint32_t : 1;
        std::uint32_t aa : 1;
        std::uint32_t : 30;
    };

    struct {
        std::uint32_t : 2;
        std::uint32_t li : 24;
        std::uint32_t : 6;
    };

    struct {
        std::uint32_t : 2;
        std::uint32_t bd : 14;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t : 1;
        std::uint32_t xo10 : 10;
        std::uint32_t : 21;
    };

    struct {
        std::uint32_t : 1;
        std::uint32_t xo9 : 9;
        std::uint32_t : 20;
    };

    struct {
        std::uint32_t : 21;
        std::uint32_t rt : 5;
        std::uint32_t : 6;
    };

    struct {
        std::uint32_t : 16;
        std::uint32_t ra : 5;
        std::uint32_t : 11;
    };

    struct {
        std::uint32_t : 11;
        std::uint32_t rb : 5;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t si : 16;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t d : 16;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t rc : 1;
        std::uint32_t : 31;
    };

    struct {
        std::uint32_t : 21;
        std::uint32_t rs : 5;
        std::uint32_t : 6;
    };

    struct {
        std::uint32_t ui : 16;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t : 10;
        std::uint32_t oe : 1;
        std::uint32_t : 21;
    };

    struct {
        std::uint32_t : 11;
        std::uint32_t spr : 10;
        std::uint32_t : 11;
    };

    struct {
        std::uint32_t : 12;
        std::uint32_t fxm : 8;
        std::uint32_t : 12;
    };

    struct {
        std::uint32_t : 21;
        std::uint32_t frt : 5;
        std::uint32_t : 6;
    };

    struct {
        std::uint32_t : 11;
        std::uint32_t frb : 5;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t : 11;
        std::uint32_t sh : 5;
        std::uint32_t : 16;
    };

    struct {
        std::uint32_t : 6;
        std::uint32_t mb : 5;
        std::uint32_t : 21;
    };

    struct {
        std::uint32_t : 1;
        std::uint32_t me : 5;
        std::uint32_t : 26;
    };
};
