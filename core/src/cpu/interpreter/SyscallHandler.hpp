/*
** EPITECH PROJECT, 2025
** core
** File description:
** SyscallHandler
*/

#pragma once

#include <functional>
#include <map>
#include <string>

#include "Interpreter.hpp"

namespace Core {
    class Interpreter;

    template<typename T>
    struct FunctionTraits;

    template<typename R, typename... Args>
    struct FunctionTraits<R(*)(Args...)> {
        using ReturnType = R;
        static constexpr std::size_t arity = sizeof...(Args);

        template<std::size_t I>
        using Arg = std::tuple_element_t<I, std::tuple<Args...>>;
    };

    // Detect whether the first parameter of a function pointer is Core::Interpreter&.
    template<typename Func>
    struct HasInterpreterArg : std::false_type {};

    template<typename R, typename... Args>
    struct HasInterpreterArg<R(*)(Core::Interpreter &, Args...)> : std::true_type {};

    // HLE function does NOT take Interpreter& — read all args from GPRs.
    template<auto func, std::size_t... I>
    void performFunctionCall(Core::Interpreter &cpu, std::index_sequence<I...>)
    {
        using Traits = FunctionTraits<decltype(func)>;
        using ReturnType = Traits::ReturnType;

        if constexpr (std::is_void_v<ReturnType>) {
            func(cpu.readArgs<typename Traits::template Arg<I>>(I)...);
        } else {
            ReturnType ret = func(cpu.readArgs<typename Traits::template Arg<I>>(I)...);
            cpu.m_gpr[3] = static_cast<std::uint32_t>(ret);
        }
    }

    // HLE function takes Interpreter& as first arg — inject cpu, read remaining from GPRs.
    template<auto func, std::size_t... I>
    void performFunctionCallWithCpu(Core::Interpreter &cpu, std::index_sequence<I...>)
    {
        using Traits = FunctionTraits<decltype(func)>;
        using ReturnType = Traits::ReturnType;

        if constexpr (std::is_void_v<ReturnType>) {
            func(cpu, cpu.readArgs<typename Traits::template Arg<I + 1>>(I)...);
        } else {
            ReturnType ret = func(cpu, cpu.readArgs<typename Traits::template Arg<I + 1>>(I)...);
            cpu.m_gpr[3] = static_cast<std::uint32_t>(ret);
        }
    }

    template<auto func>
    void invoke(Core::Interpreter &cpu)
    {
        using Traits = FunctionTraits<decltype(func)>;
        if constexpr (HasInterpreterArg<decltype(func)>::value) {
            performFunctionCallWithCpu<func>(cpu, std::make_index_sequence<Traits::arity - 1>{});
        } else {
            performFunctionCall<func>(cpu, std::make_index_sequence<Traits::arity>{});
        }
    }

    typedef void(*Handler)(Core::Interpreter &cpu);

    struct SyscallHandler {
        Handler get(const std::string &symbolName) { return syscallTable[symbolName]; }

        void registerSyscall(const std::string &symbolName, const Handler handler)
        {
            syscallTable[symbolName] = handler;
        }

        // Register a handler for a Wii U OS virtual address (0xC0000000 range).
        // Used to intercept direct OS calls (addis/addi/bctr pattern).
        void registerAddress(const std::uint32_t osAddr, const Handler handler)
        {
            addressTable[osAddr] = handler;
        }

        std::map<std::string, Handler> syscallTable;
        std::map<std::uint32_t, Handler> addressTable;
    };

    inline SyscallHandler syscallHandler;
}

#define REGISTER_SYSCALL(func) Core::syscallHandler.registerSyscall(#func, &Core::invoke<func>)
