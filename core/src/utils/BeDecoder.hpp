/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** BEDecoder
*/

#pragma once

#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "exception/Exception.hpp"

namespace Utils {
    class BeDecoderException final : public Core::Exception {
        public:
            explicit BeDecoderException(const std::string &errorMessage) : Core::Exception("BeDecoderException", errorMessage) {}

            ~BeDecoderException() override = default;
    };

    class BeDecoder {
        public:
            BeDecoder() : m_offset(0) {}

            explicit BeDecoder(std::vector<char> buffer);
            // The caller keeps this storage alive and does not resize it.
            explicit BeDecoder(std::span<const char> buffer) : m_view(buffer.data(), buffer.size()), m_offset(0) {}

            explicit BeDecoder(const std::string &filepath);

            template<typename T>
            T extract()
            {
                require(sizeof(T));
                T value;
                std::memcpy(&value, m_view.data() + m_offset, sizeof(T));
                m_offset += sizeof(T);
                return value;
            }

            template<typename T>
            void extract(T *buffer, const std::size_t count)
            {
                require(count);
                std::memcpy(buffer, m_view.data() + m_offset, count);
                m_offset += count;
            }

            template<typename Type>
            const Type *extract(std::size_t count)
            {
                if (count > m_view.size() / sizeof(Type))
                    throw BeDecoderException("Read exceeds decoder buffer");
                require(sizeof(Type) * count);
                auto ptr = reinterpret_cast<const Type *>(m_view.data() + m_offset);
                m_offset += sizeof(Type) * count;
                return ptr;
            }

            template<typename T>
            T extractSwap()
            {
                require(sizeof(T));
                T value;
                std::memcpy(&value, m_view.data() + m_offset, sizeof(T));
                m_offset += sizeof(T);
                return std::byteswap(value);
            }


            void seek(std::size_t pos);

        private:
            void require(std::size_t bytes) const
            {
                if (m_offset > m_view.size() || bytes > m_view.size() - m_offset)
                    throw BeDecoderException("Read exceeds decoder buffer");
            }
            std::vector<char> m_buffer;
            std::string_view m_view;
            std::size_t m_offset;
    };
} // namespace Utils
