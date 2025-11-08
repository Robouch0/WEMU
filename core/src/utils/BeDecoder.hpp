/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** BEDecoder
*/

#pragma once

#include <string_view>
#include <vector>
#include <bit>

namespace Utils {

    class BeDecoder
    {
        public:
            explicit BeDecoder(const std::string_view &buffer) : m_Buffer(buffer), m_offset(0) {}
            ~BeDecoder() = default;

            template<typename T>
            T extract()
            {
                T const &value = *reinterpret_cast<T const *>(m_Buffer.data() + m_offset);
                m_offset += sizeof(T);
                return value;
            }

            template<typename T>
            T extractSwap()
            {
                T const &value = *reinterpret_cast<T const *>(m_Buffer.data() + m_offset);
                m_offset += sizeof(T);
                return std::byteswap(value);
            }

        private:
            const std::string_view &m_Buffer;
            std::size_t m_offset;
    };
}
