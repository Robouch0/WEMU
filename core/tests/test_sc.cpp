/*
** EPITECH PROJECT, 2025
** core
** File description:
** Unit tests for SC — system call / HLE dispatch
*/

#include <gtest/gtest.h>

#define private public
#include "cpu/interpreter/Interpreter.hpp"
#undef private

#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/types/EncodedInstruction.hpp"

static Core::Interpreter makeCPU()
{
    const Core::Binary dummyBinary = {};
    return Core::Interpreter(dummyBinary);
}

//
// ───────────────────────────────────────────────
//  SC — out-of-bounds index: no crash
// ───────────────────────────────────────────────
//

TEST(ScTest, SC_InvalidIndexNoCrash)
{
    auto cpu = makeCPU();
    // symbols is empty, so bd=0 is out of bounds → should print to cerr and return

    EncodedInstruction inst(0);
    inst.bd = 0;

    EXPECT_NO_FATAL_FAILURE(Core::Instruction::SC(cpu, inst));
}

//
// ───────────────────────────────────────────────
//  SC — valid symbol with no handler: no crash
// ───────────────────────────────────────────────
//

TEST(ScTest, SC_NoHandlerNoCrash)
{
    Core::Binary binary = {};
    Core::Symbol sym = {};
    sym.name = "UnknownFuncForTest_NeverRegistered";
    binary.symbols.push_back(sym);

    Core::Interpreter cpu(binary);

    EncodedInstruction inst(0);
    inst.bd = 0; // index 0 → "UnknownFuncForTest_NeverRegistered"

    EXPECT_NO_FATAL_FAILURE(Core::Instruction::SC(cpu, inst));
}

//
// ───────────────────────────────────────────────
//  SC — registered handler is called
// ───────────────────────────────────────────────
//

static bool g_scTestHandlerCalled = false;

TEST(ScTest, SC_CallsRegisteredHandler)
{
    g_scTestHandlerCalled = false;

    Core::Binary binary = {};
    Core::Symbol sym = {};
    sym.name = "TestFunc_SC_Unit";
    binary.symbols.push_back(sym);

    Core::syscallHandler.registerSyscall("TestFunc_SC_Unit",
        [](Core::Interpreter &) { g_scTestHandlerCalled = true; });

    Core::Interpreter cpu(binary);

    EncodedInstruction inst(0);
    inst.bd = 0; // index 0 → "TestFunc_SC_Unit"

    Core::Instruction::SC(cpu, inst);

    EXPECT_TRUE(g_scTestHandlerCalled);
}
