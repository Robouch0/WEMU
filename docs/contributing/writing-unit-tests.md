# Writing Unit Tests

This guide covers how to write, register, and run unit tests for the WEMU core emulator.

---

## Setup overview

Tests live in `core/tests/`. The test executable is built alongside the emulator when using the `core/` standalone build.

GoogleTest is fetched automatically via CMake `FetchContent` on the first build — no manual installation needed.

```
core/
├── tests/
│   ├── CMakeLists.txt   — auto-discovers all *.cpp files in this directory
│   └── test_add.cpp     — existing instruction tests
```

The `CMakeLists.txt` uses `file(GLOB TEST_SOURCES "*.cpp")`, which means **any `.cpp` file you drop in `core/tests/` is automatically compiled and linked into the test binary**. No CMake edits needed to add a new test file.

---

## Building and running tests

```bash
cd core && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Run all tests
ctest

# Show test output even on success
ctest --verbose

# Show output only on failure (useful in CI)
ctest --output-on-failure

# Run the test binary directly for more control
./tests/wemu_tests

# Filter by test suite
./tests/wemu_tests --gtest_filter=InstructionTest.*

# Filter by individual test
./tests/wemu_tests --gtest_filter=InstructionTest.ADD_NoOE_NoRc

# List all registered tests without running them
./tests/wemu_tests --gtest_list_tests
```

---

## The `#define private public` pattern

`Interpreter`'s CPU state (`m_gpr`, `m_cr`, `m_xer`, `m_fpr`, etc.) is declared `private`. Tests need to read and write these fields directly to set up CPU state before calling an instruction and verify register values after.

The convention used in this project is to redefine `private` as `public` for the single translation unit of the test file:

```cpp
#define private public
#include "cpu/interpreter/Interpreter.hpp"
#undef private
```

**This only affects the current `.cpp` file.** The `#undef` restores normal visibility immediately after. The `Interpreter` class itself is not modified.

> This is an intentional testing convention. Do not use it in production code.

---

## Creating a minimal CPU for tests

Most instruction tests do not need a real binary file. Construct a dummy `Interpreter` with an empty `Binary`:

```cpp
static Core::Interpreter makeCPU()
{
    const Core::Binary dummyBinary = {};
    return Core::Interpreter(dummyBinary);
}
```

This gives you a fully initialized CPU with all registers zeroed and the instruction dispatch map built. Calling `cpu.run()` on it would fail (no `.text` section), but calling instruction functions directly works fine.

---

## Constructing an `EncodedInstruction` manually

Instruction functions take a `const EncodedInstruction &` argument. In real execution, this comes from decoding a 32-bit word read from the binary. In tests, construct it by hand:

```cpp
EncodedInstruction inst(0);   // start from all-zeros

inst.rt = 5;    // destination register
inst.ra = 3;    // source register A
inst.rb = 4;    // source register B
inst.oe = 0;    // disable overflow recording
inst.rc = 0;    // disable CR0 update
```

`EncodedInstruction(0)` zeroes the raw 32-bit word. Assigning to a named field writes the correct bits in place via the union's bit-field layout. You do not need to set `opcd` or `xo9` for direct calls — those are only used by the dispatcher, not by the instruction functions themselves.

---

## Test structure and naming conventions

```cpp
TEST(SuiteName, TestName)
{
    // 1. Arrange — build CPU state
    auto cpu = makeCPU();
    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 32;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    // 2. Act — call the instruction directly
    Core::Instruction::ADD(cpu, inst);

    // 3. Assert — check register state
    EXPECT_EQ(cpu.m_gpr[5], 42u);
    EXPECT_EQ(cpu.m_xer.ov, 0);
}
```

**Naming convention:** `SuiteName` groups related tests; `TestName` describes the exact scenario.

Existing pattern from `test_add.cpp`:

| Test name | What it covers |
|---|---|
| `ADD_NoOE_NoRc` | Basic result, no flag updates |
| `ADD_WithRc_UpdateCR` | `Rc=1` triggers CR0 update |
| `ADDO_WithOverflow_SetsOVandSO` | `OE=1`, signed overflow sets XER.OV and XER.SO |
| `ADDO_WithOE_And_Rc` | Both flags active simultaneously |
| `ADDE_NoOE_NoRc_NoCarry` | Carry-in = 0 |
| `ADDE_NoOE_NoRc_WithCarry` | Carry-in = 1 |

---

## What to test for each instruction

For any instruction that supports `OE` and `Rc`, write at least these four cases:

| Case | `oe` | `rc` | What to verify |
|---|---|---|---|
| Basic | 0 | 0 | Result in `m_gpr[rt]`, flags unchanged |
| Record | 0 | 1 | CR0 fields (`lt`, `gt`, `eq`) set correctly |
| Overflow | 1 | 0 | `XER.OV` and `XER.SO` set on signed overflow |
| Both | 1 | 1 | Both CR0 and XER updated |

For instructions with a carry input (`ADDE`, `SUBFZE`, …), add variants with `XER.CA = 0` and `XER.CA = 1`.

For immediate-form instructions (`ADDI`, `ADDIS`, …), test:
- Positive immediate
- Negative immediate (sign extension)
- `RA = 0` special case (if the ISA specifies one)

---

## Useful assertions

```cpp
// Exact equality (use 'u' suffix for unsigned comparisons to avoid sign mismatch warnings)
EXPECT_EQ(cpu.m_gpr[5], 42u);
EXPECT_EQ(cpu.m_gprSigned[5], -10);

// Condition register fields
EXPECT_EQ(cpu.m_cr.cr0.lt, 1);   // result < 0
EXPECT_EQ(cpu.m_cr.cr0.gt, 0);   // result > 0
EXPECT_EQ(cpu.m_cr.cr0.eq, 0);   // result == 0
EXPECT_EQ(cpu.m_cr.cr0.so, 0);   // summary overflow

// Fixed-point exception register
EXPECT_EQ(cpu.m_xer.ov, 1);      // overflow occurred
EXPECT_EQ(cpu.m_xer.so, 1);      // sticky overflow (set once, stays set)
EXPECT_EQ(cpu.m_xer.ca, 1);      // carry out

// For floating-point instructions
EXPECT_FLOAT_EQ(cpu.m_fpr[5], 1.5f);
```

---

## Adding a test file

1. Create `core/tests/test_myinstruction.cpp`
2. No CMake changes needed — `file(GLOB TEST_SOURCES "*.cpp")` picks it up automatically
3. Rebuild and run:

```bash
# From core/build/
make -j$(nproc)
./tests/wemu_tests --gtest_filter=MyInstructionTest.*
```

Minimal file template:

```cpp
#include <gtest/gtest.h>

#define private public
#include "cpu/interpreter/Interpreter.hpp"
#undef private

#include "cpu/types/EncodedInstruction.hpp"

static Core::Interpreter makeCPU()
{
    return Core::Interpreter(Core::Binary{});
}

TEST(MyInstructionTest, BasicCase)
{
    auto cpu = makeCPU();

    // set up registers ...

    EncodedInstruction inst(0);
    // set fields ...

    Core::Instruction::MYINSTR(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[inst.rt], expected_value);
}
```
