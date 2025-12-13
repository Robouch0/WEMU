#pragma once
#include <iostream>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"

inline int OSScreenInit()
{
    std::cout << __FUNCTION__ << "called ! " << std::endl;
    return 42;
}

inline void RegisterCoreInitFunctions()
{
    REGISTER_SYSCALL(OSScreenInit);
}
