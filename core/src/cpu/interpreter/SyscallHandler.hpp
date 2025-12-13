#pragma once
#include <functional>
#include <map>
#include <string>

#include "Interpreter.hpp"

namespace Core {
    class Interpreter;

    template<auto func>
    void invoke(Core::Interpreter &cpu)
    {
        using ReturnType = decltype(std::function{func})::result_type;

        if constexpr (std::is_void_v<ReturnType>) {
            func();
        } else {
            ReturnType ret = func();
            std::cout << "Here ret " << ret << std::endl;
        }
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
