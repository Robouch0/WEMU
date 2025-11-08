/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** CustomException
*/

#pragma once

#include <exception>
#include <string>
#include <utility>

#define ERROR_VALUE 84
#define SUCCESS_VALUE 0

namespace Core {

    class Exception : public std::exception
    {
        public:
            Exception(std::string errorType, std::string errorMessage) : m_errorType(std::move(errorType)), m_errorMessage(std::move(errorMessage)) {}
            ~Exception() override = 0;

        protected:
            std::string m_errorType;
            std::string m_errorMessage;
            mutable std::string m_formatedMessage;
    };
}
