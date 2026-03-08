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

        template<auto func, std::size_t... I>
        void performFunctionCall(Core::Interpreter& cpu, std::index_sequence<I...>)
        {
            using Traits = FunctionTraits<decltype(func)>;
            using ReturnType = Traits::ReturnType;

            if constexpr (std::is_void_v<ReturnType>) {
                func(cpu.readArgs<typename Traits::template Arg<I>>(I)...);
            } else {
                ReturnType ret = func(cpu.readArgs<typename Traits::template Arg<I>>(I)...);
                cpu.m_gpr[3] = ret;
            }
        }

        template<auto func>
        void invoke(Core::Interpreter &cpu)
        {
            using Traits = FunctionTraits<decltype(func)>;

            performFunctionCall<func>(cpu, std::make_index_sequence<Traits::arity>{});
        }

    typedef void(*Handler)(Core::Interpreter &cpu);

#define REGISTER_SYSCALL(func) Core::syscallHandler.registerSyscall(#func, &Core::invoke<func>)

    struct SyscallHandler {
        Handler get(const std::string &symbolName) { return syscallTable[symbolName]; }

        void registerSyscall(const std::string &symbolName, const Handler handler) { syscallTable[symbolName] = handler; }

        std::map<std::string, Handler> syscallTable;
    };

    inline SyscallHandler syscallHandler;
}
