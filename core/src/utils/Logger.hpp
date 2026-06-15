/*
** EPITECH PROJECT, 2025
** core
** File description:
** Logger
*/

#pragma once

#include <format>
#include <iostream>
#include <string_view>

namespace Utils::Log {

    enum class Level : std::uint8_t {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        None = 5,
    };

#ifndef WEMU_LOG_LEVEL
    #define WEMU_LOG_LEVEL 4
#endif

    inline constexpr Level kLevel = static_cast<Level>(WEMU_LOG_LEVEL);

    namespace detail {
        consteval std::string_view prefix(Level l) noexcept
        {
            switch (l) {
                case Level::Trace:
                    return "[TRACE] ";
                case Level::Debug:
                    return "[DEBUG] ";
                case Level::Info:
                    return "[INFO]  ";
                case Level::Warning:
                    return "[WARN]  ";
                case Level::Error:
                    return "[ERROR] ";
                default:
                    return "[?????] ";
            }
        }
    } // namespace detail

    template<Level L, typename... Args>
    inline void log(std::format_string<Args...> fmt, Args &&...args)
    {
        if constexpr (L >= kLevel) {
            auto msg = std::format("{}{}", detail::prefix(L), std::format(fmt, std::forward<Args>(args)...));
            if constexpr (L >= Level::Warning)
                std::cerr << msg << std::endl;
            else
                std::cout << msg << std::endl;
        }
    }

    template<typename... Args>
    void trace(std::format_string<Args...> f, Args &&...a)
    {
        log<Level::Trace>(f, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void debug(std::format_string<Args...> f, Args &&...a)
    {
        log<Level::Debug>(f, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void info(std::format_string<Args...> f, Args &&...a)
    {
        log<Level::Info>(f, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void warn(std::format_string<Args...> f, Args &&...a)
    {
        log<Level::Warning>(f, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void error(std::format_string<Args...> f, Args &&...a)
    {
        log<Level::Error>(f, std::forward<Args>(a)...);
    }

} // namespace Utils::Log
