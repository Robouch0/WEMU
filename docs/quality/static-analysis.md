# Static Analysis

How WEMU's code quality is measured: which tools run, which rules they apply, the initial and final measurements, and what was fixed in between.

The raw reports (every finding, file and line) are in [`analysis/`](analysis/).

---

## 1. Tools and where they run

| Tool | Version | Scope | Where it runs | Failure policy |
|---|---|---|---|---|
| **clang-tidy** (linting + static analysis) | 19 | `core/src` (`.cpp` files and the `core/src` headers they include) | CI job `clang-tidy` on every push and pull request to `dev`/`main` | Any finding fails the job (`WarningsAsErrors: '*'`) |
| **clang-format** (formatting) | 19 | `core/src`, `core/tests` | CI job `clang-format` | Any difference fails the job (`--dry-run --Werror`) |
| **GCC 14** (type checking, compiler warnings) | 14 | Everything | CI jobs `Full Build (core + gui)` and `Unit Tests` | Errors fail the build; warnings are reported (no `-Werror`) |

C++ is statically typed, so type checking is the compiler itself: a type error stops the build, and the build is a required check on `dev` (see [testing-policy.md](testing-policy.md#branch-protection--review-policies)).

Configuration files: [`core/.clang-tidy`](../../core/.clang-tidy) and [`core/.clang-format`](../../core/.clang-format). The CI jobs are in [`.github/workflows/wemu-checks.yml`](../../.github/workflows/wemu-checks.yml).

---

## 2. Rules adopted

### clang-tidy

| Check group | Why we use it |
|---|---|
| `clang-diagnostic-*` | Compiler warnings, as clang sees them. |
| `clang-analyzer-*` | Path-sensitive analysis: null dereferences, use after free, leaks, uninitialised reads. An emulator works with raw guest memory, so these are the bugs that cost the most. |
| `bugprone-*` | Patterns that compile but are usually wrong: narrowing and widening conversions, a `memcmp` result used as a boolean, a switch with no `default`, swapped or misplaced arguments. Guest arithmetic is full of 32/64-bit mixing, which is where these checks catch real bugs. |
| `performance-*` | Needless copies, missed moves, and casts that defeat optimisation. The interpreter, JIT and GX2 replay run in hot loops. |

All four groups are errors in CI (`WarningsAsErrors: '*'`): a finding blocks the change until it is fixed or explicitly suppressed.

### clang-format

LLVM base style, adapted to the existing code: 4-space indentation, 150 columns, braces on their own line after functions only, namespaces indented, `T *p` pointer alignment, includes regrouped (system, then project). Formatting is never a matter of review: CI rejects any difference.

---

## 3. Rules deliberately disabled

| Rule | Why it is disabled |
|---|---|
| `bugprone-easily-swappable-parameters` | Emulated hardware APIs take runs of same-typed integers by design (`GX2SetViewport(x, y, w, h, near, far)`, `memcpy(dst, src, size)`). The check would flag most HLE functions, and their signatures are fixed by the Wii U API. |
| `bugprone-macro-parentheses` | The instruction tables use X-macros (`INSTR(NAME, OPCD(n), ...)` in `cpu/tables/*.anh`) whose arguments are tokens pasted into names, not expressions. Parenthesising them would break the expansion. |
| `performance-avoid-endl` | `std::endl` is only used in console diagnostics (loader and branch traces), where the flush is wanted: a crash must not lose the last line printed. |
| `performance-enum-size` (disabled 2026-11-15) | Most WEMU enums mirror a hardware or wire encoding: Latte opcodes (`CfOp`, `Op2`, `Op3`), GX2 command words (`Gx2Cmd`), the session protocol (`QtSessionProtocol::Type`, sent as `quint32`), JIT exit codes read from generated code. Their width is part of the format, and shrinking them to `uint8_t` would change struct layouts and serialisation for a saving of a few bytes. Of the eighteen findings (see §5), the others are small internal enums (`PauseMenu::State`, `ShaderStage`) where one byte saved per value buys nothing, so the rule costs more review time than it returns. |

### Inline suppressions

A finding may be suppressed on one line with `// NOLINT(check-name)`, never with a bare `NOLINT`, and the reason goes on the same line or the line above. There are 12 in `core/src`:

| Check | Count | Reason |
|---|---|---|
| `performance-no-int-to-ptr` | 5 | Host addresses computed as integers on purpose: section copies into guest memory (`Loader.cpp`), the JIT RAM bias and code buffer (`X64Jit.cpp`, `CodeBuffer.cpp`), a huge-page hint (`Memory.hpp`). |
| `bugprone-reserved-identifier` | 2 | HLE entry points must carry the guest symbol's name (`__init_wut`, `__rplwrap_exit`). |
| `bugprone-branch-clone` | 1 | `Loader.cpp`: the two branches have the same shape but update different ranges (code or data). The check compares the shape, so it is a false positive. |
| `bugprone-infinite-loop` | 1 | `Rotate.cpp`: the rotate-mask loop counts modulo 32 (`i = (i + 1) % 32`) until it reaches `me`, which the check cannot prove. |
| `performance-enum-size` | 1 | `InstructionID.hpp`: kept at 16 bits so the instruction table can grow past 255 entries. Redundant since the rule was disabled. |
| `clang-analyzer-optin.performance.Padding` | 1 | `Gx2Replayer` exists once per session; its members are grouped by feature, and reordering them for 56 bytes would scatter related state. |

---

## 4. Initial measurement (2026-05-03)

clang-tidy and clang-format entered CI in commit `c6f2fad` on 2026-05-03. The initial measurement applies those rules to the code just before the clean-up commit (`50025a4`, 2026-04-29), with the same tool versions as CI (clang-tidy 19.1.0, clang-format 19.1.7).

| Tool | Findings | Files | Report |
|---|---|---|---|
| clang-tidy | **7 errors**: 5 `performance-enum-size`, 2 `performance-no-int-to-ptr` | 6 | [clang-tidy-initial.txt](analysis/clang-tidy-initial.txt) |
| clang-format | **1,257 differences** | 77 of 131 files | (counted with `clang-format --dry-run`) |

Commit `188d955` ("apply clang-format and address clang-tidy diagnostics") brought both to zero: enum base types narrowed in `Registers.hpp`, `Logger.hpp`, `Field.hpp`, `InstructionID.hpp`, `Memory.hpp`; the two loader casts suppressed with a reason; every file reformatted.

---

## 5. What the pipeline missed, and the fix (2026-11-15)

Re-measuring for the final report showed that **CI's clang-tidy never analysed a header**. The configuration said:

```yaml
HeaderFilterRegex: '^(?!.*stb_image).*core/src/.*'
```

`(?!...)` is a lookahead, which LLVM's regular expressions do not support. The filter matched no file, so clang-tidy silently skipped every header, and the CI job stayed green. On 2026-05-03 this hid 5 of the 7 initial findings (CI saw only the 2 in `Loader.cpp`: [clang-tidy-initial-ci-filter.txt](analysis/clang-tidy-initial-ci-filter.txt)).

With headers included, the code at `19ac5f7` (2026-11-14) had **26 findings** that CI had never reported ([clang-tidy-before-header-fix.txt](analysis/clang-tidy-before-header-fix.txt)):

| Check | Count | Resolution |
|---|---|---|
| `performance-enum-size` | 18 | Rule disabled (§3): the enums mirror hardware and protocol encodings. |
| `bugprone-implicit-widening-of-multiplication-result` | 2 | Fixed: `audio/Output.hpp` and `gfx/LatteShaderLowering.hpp` now multiply in `std::size_t`. |
| `bugprone-narrowing-conversions` | 2 | Fixed: `gfx/TextureSampler.hpp` converts the integer texel coordinate to `double` explicitly. |
| `bugprone-suspicious-string-compare` | 1 | Fixed: `gfx/Gx2BufferSnapshots.hpp` compares the `memcmp` result with `!= 0`. |
| `bugprone-switch-missing-default-case` | 1 | Fixed: `gfx/PrimitiveAssembly.hpp` has a `default` for points and lines. |
| `performance-no-int-to-ptr` | 1 | Suppressed with its reason (`Memory.hpp`, huge-page hint on a host buffer). |
| `clang-analyzer-optin.performance.Padding` | 1 | Suppressed with its reason (`Gx2Replayer`). |

The configuration now uses two filters, which clang-tidy 19 supports:

```yaml
HeaderFilterRegex: '.*/core/src/.*'
ExcludeHeaderFilterRegex: '.*stb_image.*'
```

The same round fixed two files that clang-format 19 would have rejected (`gfx/VulkanRasterBackend.cpp`, 8 differences; `cpu/memory/Memory.hpp`, 1) and the 7 GCC warnings in the build: `core/tests/system/test_memory.cpp` discarded `[[nodiscard]]` reads inside `EXPECT_THROW`, which now cast them to `void`.

---

## 6. Final measurement

| Measurement | Initial (`50025a4`, 2026-04-29) | Before this round (`19ac5f7`, 2026-11-14) | Final (2026-11-15) |
|---|---|---|---|
| Source files checked (`core/src` + `core/tests`) | 131 | 326 | 326 |
| clang-tidy findings, as CI ran it | 2 | 0 | **0** |
| clang-tidy findings, headers included | 7 | 26 | **0** ([clang-tidy-final.txt](analysis/clang-tidy-final.txt)) |
| clang-format differences | 1,257 in 77 files | 9 in 2 files | **0** |
| GCC 14 warnings (default flags) | not measured | 7 | **0** |
| Inline suppressions (`NOLINT`) | 0 | 10 | 12 |

The code grew from 131 to 326 files over the term with the rules enforced on every change, and ends with no finding in any of the three tools.

### Not covered yet

- **`gui/` is outside the CI checks** ([tech-debt.md](tech-debt.md) item 11). Measured on 2026-11-15 with the core rules: 7 clang-tidy findings in 20 files (5 implicit widenings, 1 narrowing, 1 missed move), and 44 of 60 files do not follow the core format style.
- **The compiler runs with default warnings.** `-Wall -Wextra` is not enabled, so the GCC count above is a floor (tech-debt item 19).

---

## 7. Reproducing the measurements

```bash
# clang-tidy, as CI does it (needs a configured core build with compile_commands.json)
cmake -S core -B core/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build core/build -j3
find core/src -name '*.cpp' -print0 | xargs -0 -n 1 -P 4 clang-tidy-19 -p core/build

# clang-format
find core/src core/tests \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format-19 --dry-run --Werror
```

To measure an older commit, check it out in a separate worktree (`git worktree add /tmp/wemu-old <commit>`) with its own build directory, and copy the `.clang-tidy` from the commit whose rules you want to apply.
