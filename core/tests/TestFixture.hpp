#pragma once

#include <gtest/gtest.h>

#define private public
#include "cpu/interpreter/Interpreter.hpp"
#undef private

#include "cpu/types/EncodedInstruction.hpp"

class InstructionTest : public ::testing::Test {
    protected:
        static Core::Interpreter *cpu;

        static void SetUpTestSuite() { cpu = new Core::Interpreter(Core::Binary{}); }

        static void TearDownTestSuite()
        {
            delete cpu;
            cpu = nullptr;
        }

        void SetUp() override { cpu->reset(); }
};
