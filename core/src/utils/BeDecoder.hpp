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
#include <fstream>
#include <iostream>
#include <cstring>

#include "exception/Exception.hpp"

namespace Utils {

    class BeDecoderException final : public Core::Exception {
        public:
        explicit BeDecoderException(const std::string &errorMessage) : Core::Exception("BeDecoderException", errorMessage) {}
        ~BeDecoderException() override = default;
    };

    class BeDecoder
    {
        public:
            BeDecoder() : m_offset(0) {}
            explicit BeDecoder(std::vector<char> buffer);
            explicit BeDecoder(const std::string &filepath);

            template<typename T>
            T extract()
            {
                T const &value = *reinterpret_cast<T const *>(m_view.data() + m_offset);
                m_offset += sizeof(T);
                return value;
            }

            template<typename T>
            T extractSwap()
            {
                T const &value = *reinterpret_cast<T const *>(m_view.data() + m_offset);
                m_offset += sizeof(T);
                return std::byteswap(value);
            }

            template<typename T>
            void extractSwap(T *buffer, std::size_t count)
            {
                std::memcpy(buffer, m_Buffer.data() + m_offset, count);
                m_offset += count;
            }

            void seek(std::size_t pos);

        private:
            std::vector<char> m_Buffer;
            std::string_view m_view;
            std::size_t m_offset;
    };
}
