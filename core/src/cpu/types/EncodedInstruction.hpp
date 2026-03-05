/*
** EPITECH PROJECT, 2025
** core
** File description:
** EncodedInstruction
*/

#pragma once

#include <bit>
#include <cstdint>

union EncodedInstruction {
    explicit EncodedInstruction(const std::uint32_t &raw) : opcd(0) { this->raw = raw; }
    std::uint32_t raw;

    [[nodiscard]] EncodedInstruction endianSwap() const { return EncodedInstruction(std::byteswap(raw)); }

    // Opcode field (bits 26-31, MSB)
    struct {
        std::uint32_t : 26;
        std::uint32_t opcd: 6;
    };

    // LK bit (bit 0, LSB) — link flag for branch instructions
    struct {
        std::uint32_t lk : 1;
        std::uint32_t : 31;
    };

    // AA bit (bit 1) — absolute address flag
    struct {
        std::uint32_t : 1;
        std::uint32_t aa : 1;
        std::uint32_t : 30;
    };

    // LI field (bits 2-25, 24 bits) — branch offset for B-form
    struct {
        std::uint32_t : 2;
        std::uint32_t li : 24;
        std::uint32_t : 6;
    };

    // BD field (bits 2-15, 14 bits) — branch displacement for BC-form / syscall index for SC
    struct {
        std::uint32_t : 2;
        std::uint32_t bd : 14;
        std::uint32_t : 16;
    };

    // XO10 extended opcode (bits 1-10)
    struct {
        std::uint32_t : 1;
        std::uint32_t xo10: 10;
        std::uint32_t : 21;
    };

    // XO9 extended opcode (bits 1-9)
    struct {
        std::uint32_t : 1;
        std::uint32_t xo9: 9;
        std::uint32_t : 20;
    };

    // RT / RS / RD destination register (bits 21-25)
    struct {
        std::uint32_t : 21;
        std::uint32_t rt: 5;
        std::uint32_t : 6;
    };

    // RA register (bits 16-20)
    struct {
        std::uint32_t : 16;
        std::uint32_t ra: 5;
        std::uint32_t : 11;
    };

    // RB register (bits 11-15)
    struct {
        std::uint32_t : 11;
        std::uint32_t rb: 5;
        std::uint32_t : 16;
    };

    // SI immediate (bits 0-15, signed 16-bit)
    struct {
        std::uint32_t si: 16;
        std::uint32_t : 16;
    };

    // RC bit (bit 0) — record condition
    struct {
        std::uint32_t rc: 1;
        std::uint32_t : 31;
    };

    // OE bit (bit 10) — overflow enable
    struct {
        std::uint32_t : 10;
        std::uint32_t oe: 1;
        std::uint32_t : 21;
    };

    // BO field (bits 21-25) — branch options for BC/BCLR/BCCTR
    struct {
        std::uint32_t : 21;
        std::uint32_t bo: 5;
        std::uint32_t : 6;
    };

    // BI field (bits 16-20) — CR bit index for conditional branches
    struct {
        std::uint32_t : 16;
        std::uint32_t bi: 5;
        std::uint32_t : 11;
    };
};
