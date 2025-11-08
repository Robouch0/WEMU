/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** BinaryException
*/

#pragma once
#include "exception/Exception.hpp"

namespace Core {

    class BinaryException final : public Core::Exception {
        public:
            explicit BinaryException(const std::string &errorMessage) : Core::Exception("BinaryException", errorMessage) {}
            ~BinaryException() override = default;

            [[nodiscard]] char const *what() const noexcept override
            {
                m_formatedMessage = std::string("[" + m_errorType + "]: " + m_errorMessage);
                return m_formatedMessage.c_str();
            }
    };
}
