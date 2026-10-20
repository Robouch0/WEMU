/*
** EPITECH PROJECT, 2026
** core
** File description:
** CodeBuffer -- executable memory for the x86-64 recompiler
*/

#pragma once

#include <cstddef>
#include <cstdint>

namespace Core::Jit {
    // One reservation holding generated code followed by the data that code addresses RIP-relative
    // (lookup table, per-block slots). Code pages are writable only while a block is emitted and
    // executable otherwise (W^X); data pages stay read/write and are never executable. The
    // reservation is placed near `near` when possible so generated code reaches host helpers with
    // direct calls.
    class CodeBuffer {
        public:
            CodeBuffer(std::size_t codeBytes, std::size_t dataBytes, const void *near) noexcept;
            ~CodeBuffer();
            CodeBuffer(const CodeBuffer &) = delete;
            CodeBuffer &operator=(const CodeBuffer &) = delete;

            [[nodiscard]] bool valid() const noexcept { return m_base != nullptr; }
            [[nodiscard]] std::uint8_t *code() const noexcept { return m_base; }
            [[nodiscard]] std::size_t codeBytes() const noexcept { return m_codeBytes; }
            [[nodiscard]] std::uint8_t *data() const noexcept { return m_base + m_codeBytes; }
            [[nodiscard]] std::size_t dataBytes() const noexcept { return m_dataBytes; }

            // Switches the code pages covering [begin, begin + bytes) between writable and executable.
            bool writable(const std::uint8_t *begin, std::size_t bytes) noexcept;
            bool executable(const std::uint8_t *begin, std::size_t bytes) noexcept;

        private:
            std::uint8_t *m_base{};
            std::size_t m_codeBytes{}, m_dataBytes{};
    };
} // namespace Core::Jit
