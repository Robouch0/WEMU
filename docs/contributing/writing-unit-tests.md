# Writing Unit Tests

This guide covers how to write, register, and run unit tests for the WEMU core emulator.

---

## Setup overview

Tests live in `core/tests/`. The test executable is built alongside the emulator when using the `core/` standalone build.

GoogleTest is fetched automatically via CMake `FetchContent` on the first build — no manual installation needed.

    core/
    ├── tests/
    │   ├── CMakeLists.txt      — auto-discovers all *.cpp files recursively
    │   ├── TestFixture.hpp     — shared test fixture (CPU singleton)
    │   ├── TestFixture.cpp     — fixture static member definition
    │   ├── arithmetic/
    │   │   ├── test_add.cpp    — ADD, ADDO, ADD.
    │   │   ├── test_addi.cpp   — ADDI, ADDIS
    │   │   ├── test_adde.cpp   — ADDE, ADDEO
    │   │   ├── test_addme.cpp  — ADDME, ADDMEO
    │   │   ├── test_addc.cpp   — ADDC, ADDCO
    │   │   └── test_addic.cpp  — ADDIC, ADDIC.
    │   ├── branch/
    │   │   └── test_branch.cpp
    │   └── system/
    │       └── test_sc.cpp

The `CMakeLists.txt` uses `file(GLOB_RECURSE TEST_SOURCES "*.cpp")`, which means **any `.cpp` file you drop in any subfolder of `core/tests/` is automatically compiled and linked into the test binary**. No CMake edits needed to add a new test file.

---

## Building and running tests

```bash
cd core && mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Run all tests
ctest

# Show output only on failure (useful in CI)
ctest --output-on-failure

# Run the test binary directly for detailed per-test output
./tests/wemu_tests

# Filter by instruction family
./tests/wemu_tests --gtest_filter=InstructionTest.ADD*

# Filter by individual test
./tests/wemu_tests --gtest_filter=InstructionTest.ADD_NoOE_NoRc

# List all registered tests without running them
./tests/wemu_tests --gtest_list_tests
```

### With coverage report

```bash
cmake .. -DENABLE_COVERAGE=ON
make -j$(nproc)
make coverage          # terminal summary
make coverage-html     # + HTML report at build/coverage_report/index.html
```

> Requires `lcov`: `sudo dnf install lcov` (Fedora) or `sudo apt install lcov` (Debian/Ubuntu)

---

## The test fixture

All tests share a single `InstructionTest` fixture defined in `TestFixture.hpp`. It:

- Creates the `Interpreter` **once** for the entire test suite via `SetUpTestSuite` — avoiding the expensive `initInstructionMap()` call on every test
- Resets all CPU registers to zero before each test via `SetUp` → `cpu->reset()`

Tests use `TEST_F` instead of `TEST`, and access the CPU via `cpu->` instead of a local variable.

```cpp
#include "TestFixture.hpp"

TEST_F(InstructionTest, ADD_NoOE_NoRc)
{
    cpu->m_gpr[3] = 10;
    cpu->m_gpr[4] = 32;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADD(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 42u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
}
```

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

**Naming convention:** `TestName` describes the exact scenario being tested.

Existing pattern:

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
EXPECT_EQ(cpu->m_gpr[5], 42u);
EXPECT_EQ(cpu->m_gprSigned[5], -10);

// Condition register fields
EXPECT_EQ(cpu->m_cr.cr0.lt, 1);   // result < 0
EXPECT_EQ(cpu->m_cr.cr0.gt, 0);   // result > 0
EXPECT_EQ(cpu->m_cr.cr0.eq, 0);   // result == 0
EXPECT_EQ(cpu->m_cr.cr0.so, 0);   // summary overflow

// Fixed-point exception register
EXPECT_EQ(cpu->m_xer.ov, 1);      // overflow occurred
EXPECT_EQ(cpu->m_xer.so, 1);      // sticky overflow (set once, stays set)
EXPECT_EQ(cpu->m_xer.ca, 1);      // carry out

// For floating-point instructions
EXPECT_FLOAT_EQ(cpu->m_fpr[5], 1.5f);
```

---

## Adding a test file

1. Identify the right subfolder under `core/tests/` (`arithmetic/`, `branch/`, `system/`, or create a new one for a new family)
2. Create your `.cpp` file there — `GLOB_RECURSE` picks it up automatically, no CMake changes needed
3. Rebuild and run:

```bash
# From core/build/
make -j$(nproc)
./tests/wemu_tests --gtest_filter=InstructionTest.MYINSTR*
```

Minimal file template:

```cpp
#include "TestFixture.hpp"

TEST_F(InstructionTest, MYINSTR_BasicCase)
{
    cpu->m_gpr[3] = 42;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;

    Core::Instruction::MYINSTR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], expected_value);
}
```