#pragma once
#include <iostream>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"

inline std::uint32_t OSScreenInit(const std::uint32_t retValue, const std::uint32_t b)
{
    std::cout << __FUNCTION__ << " called ! " << retValue + b <<  std::endl;
    return retValue;
}

inline void RegisterCoreInitFunctions()
{
    REGISTER_SYSCALL(OSScreenInit);
}
