# Adding a PowerPC Instruction

This guide walks you through the full process of adding a new PowerPC instruction to the WEMU interpreter — from reading the ISA specification to writing the unit test.

## Prerequisites

- Basic understanding of C++
- The [PowerPC Architecture Book](https://math-atlas.sourceforge.net/devel/assembly/ppc_isa.pdf) (or any PowerPC ISA reference) to look up instruction encodings

---

## How the dispatch system works

Before writing any code, it helps to understand how the interpreter finds and executes an instruction.

### The X-macro table

All instructions are declared in a single file:

```
core/src/cpu/tables/cpu_instructions.anh
```

Each line looks like this:

```c
INSTR(ADD, OPCD(31), XO9(266))
```

This file is not compiled directly. It is `#include`d multiple times across the codebase, each time with a different definition of the `INSTR(...)` macro, generating different things from the same source of truth:

| Where | What `INSTR` generates |
|---|---|
| `InstructionID.hpp` | An enum value `E_ADD` |
| `Instruction.hpp` | A function prototype `void ADD(Interpreter&, const EncodedInstruction&)` |
| `Instruction.hpp` | An entry in the global `INSTRUCTIONARRAY[]` dispatch table |

This means **adding one line to `cpu_instructions.anh` automatically registers the instruction everywhere** — no other file needs to be modified for the wiring.

### The dispatch map

At runtime, `Interpreter::initInstructionMap()` iterates over `INSTRUCTIONARRAY[]` and groups instructions by their primary opcode (`OPCD`). When the interpreter encounters an instruction, it:

1. Looks up the primary opcode in the map
2. Iterates over the candidates, checking their extended fields (`XO9`, `XO10`, `OE`, …) until one matches
3. Calls the associated function pointer

### `EncodedInstruction`: reading bit-fields

PowerPC instructions are 32-bit words. `EncodedInstruction` is a union that exposes named bit-fields over the raw `uint32_t`, letting you access fields by name instead of shifting manually:

```cpp
EncodedInstruction instr(raw_word);

instr.opcd  // primary opcode (bits 0–5 in PPC notation)
instr.xo9   // 9-bit extended opcode (bits 22–30)
instr.xo10  // 10-bit extended opcode (bits 21–30)
instr.oe    // overflow enable bit (bit 21)
instr.rc    // record bit — update CR0 after execution (bit 31)
instr.rt    // destination register index (bits 6–10)
instr.ra    // source register A index (bits 11–15)
instr.rb    // source register B index (bits 16–20)
instr.si    // signed immediate value (bits 16–31)
```

> **Note on bit numbering:** The PowerPC ISA numbers bits from 0 (MSB) to 31 (LSB). `EncodedInstruction` uses C++ bit-fields stored in little-endian order, so the bit positions above are in PowerPC convention — the union handles the mapping for you.

---

## Step-by-step walkthrough: implementing `SUBF`

We will implement `SUBF` (Subtract From) as a concrete example. It computes `rT = rB − rA`, supports the `OE` and `Rc` flags, and is not yet implemented in WEMU.

### 1. Look up the instruction in the ISA

Open a PowerPC ISA reference and find `SUBF`. The relevant information is:

| Field | Value |
|---|---|
| Primary opcode (`OPCD`) | 31 |
| Extended opcode (`XO`, 9-bit) | 40 |
| Format | XO-form |
| Operation | `rT ← ¬rA + rB + 1` |
| Flags | `OE` (overflow enable), `Rc` (condition register update) |

### 2. Register the instruction in the table

Open `core/src/cpu/tables/cpu_instructions.anh` and add:

```c
// Operations
INSTR(ADD,  OPCD(31), XO9(266))
INSTR(ADDE, OPCD(31), XO9(138))
INSTR(SUBF, OPCD(31), XO9(40))   // ← add this line
```

The fields passed to `INSTR` after the name are the **match criteria** the dispatcher will check. For `SUBF`, the interpreter will match any instruction where `opcd == 31` AND `xo9 == 40`.

> **Choosing between `XO9` and `XO10`:** Use `XO9` for XO-form instructions (they have an `OE` bit at position 21, leaving 9 bits for XO). Use `XO10` for X-form instructions (no `OE` bit, 10 bits for XO).

### 3. Implement the function

Create a new file `core/src/cpu/instructions/Subtract.cpp` (or add to an existing one if the instruction family is small):

```cpp
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief rT = ¬rA + rB + 1  (i.e. rT = rB - rA)
     * PowerPC Architecture Book, section 3.3.8
     */
    void SUBF(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.rt] = cpu.m_gpr[instr.rb] - cpu.m_gpr[instr.ra];

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
        cpu.updateOverflow(
            cpu.m_gprSigned[instr.ra],
            cpu.m_gprSigned[instr.rb],
            cpu.m_gprSigned[instr.rt],
            instr
        );
    }

} // namespace Core::Instruction
```

Key points:
- Access registers via `cpu.m_gpr[instr.rt]` (unsigned) or `cpu.m_gprSigned[instr.rt]` (signed — they share the same memory).
- Call `updateCR(...)` to conditionally update `CR0`. It only writes if `instr.rc == 1`.
- Call `updateOverflow(...)` to conditionally set `XER.OV` and `XER.SO`. It only writes if `instr.oe == 1`.
- Both helpers are already implemented on `Interpreter` — you do not need to rewrite that logic.

### 4. Register the source file in CMake

Open `core/CMakeLists.txt` and add your new file to the `core` library target:

```cmake
add_library(core STATIC
        ...
        src/cpu/instructions/Add.cpp
        src/cpu/instructions/Subtract.cpp   # ← add this
        ...
)
```

### 5. Write the unit test

Add a test file `core/tests/test_subtract.cpp`. Tests directly call the instruction function, bypassing the full fetch/decode loop:

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

// SUBF: rT = rB - rA, no flags
TEST(SubtractTest, SUBF_Basic)
{
    auto cpu = makeCPU();
    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 42;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;   // subtracted
    inst.rb = 4;   // base
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBF(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 32u);   // 42 - 10
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

// SUBF.: Rc=1 updates CR0
TEST(SubtractTest, SUBF_Rc_UpdatesCR)
{
    auto cpu = makeCPU();
    cpu.m_gpr[3] = 5;
    cpu.m_gpr[4] = 5;

    EncodedInstruction inst(0);
    inst.rt = 6;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBF(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[6], 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 1);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
}

// SUBFO: OE=1 triggers overflow detection
TEST(SubtractTest, SUBFO_Overflow)
{
    auto cpu = makeCPU();
    cpu.m_gpr[3] = 1;
    cpu.m_gpr[4] = 0x80000000u;   // INT32_MIN

    EncodedInstruction inst(0);
    inst.rt = 7;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBF(cpu, inst);

    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);
}
```

> **Why `#define private public`?** GoogleTest cannot access private members of `Interpreter` (`m_gpr`, `m_cr`, etc.). This define temporarily makes them public for the test translation unit only. It is a deliberate testing convention in this project.

Build and run:

```bash
cd core/build
make -j$(nproc)
./wemu_tests --gtest_filter=SubtractTest.*
```

---

## Adding a new field

If the instruction you are implementing uses a bit-field not yet defined in `EncodedInstruction` (e.g. a `BA`, `BB` field for condition logic instructions), you need to:

1. **Add the bit-field struct** to `core/src/cpu/types/EncodedInstruction.hpp`:

```cpp
struct {
    std::uint32_t : 16;
    std::uint32_t ba: 5;
    std::uint32_t : 11;
};
```

2. **Register the field** in `core/src/cpu/tables/cpu_fields.anh`:

```c
FIELD(OPCD)
FIELD(XO10)
FIELD(XO9)
FIELD(OE)
FIELD(BA)   // ← add this
```

This makes `BA` available as a match criterion in `cpu_instructions.anh` and generates a helper function `BA(uint32_t v)` used to build `InstructionInfo::matchFields` entries.

---

## Checklist

- [ ] Found the instruction's `OPCD` and extended opcode in the ISA spec
- [ ] Added `INSTR(...)` entry to `cpu_instructions.anh`
- [ ] Implemented the function in `core/src/cpu/instructions/`
- [ ] Added the `.cpp` file to `core/CMakeLists.txt`
- [ ] Wrote unit tests covering: basic result, `Rc=1` CR update, `OE=1` overflow (when applicable)
- [ ] All tests pass: `./wemu_tests --gtest_filter=YourInstructionTest.*`
