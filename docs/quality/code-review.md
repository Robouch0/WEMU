# Code Review: Cases Where Review Changed the Code

Every change reaches `dev` and `main` through a pull request. The rulesets require 1 approving review on `dev` and 2 on `main`; on `dev`, the `Full Build (core + gui)` check (build plus core and GUI tests) must also pass (see [testing-policy.md](testing-policy.md#5-team-conventions--quality-gates)).

**Activity (2026-11-12):** 119 pull requests; 111 merged, 107 of them reviewed; 204 reviews; 13 pull requests received a "changes requested" review.

The four cases below are the ones where review had the clearest effect on the code. Each links to the pull request, the review comments and the commits that answered them.

| PR | Kind of finding | Outcome |
|---|---|---|
| [#118 `ADDIC`](#1-pr-118--addic-a-sign-extension-bug-and-the-tests-that-were-missing) | Correctness bug + missing test cases | Bug fixed, negative-immediate tests added |
| [#109 `ADDME`](#2-pr-109--addme-a-style-comment-whose-tests-found-three-bugs) | Style comment that led to new tests | The new tests exposed three arithmetic bugs, all fixed |
| [#274 `fcmpu`](#3-pr-274--fcmpu-two-review-rounds-on-hardware-accuracy) | Hardware accuracy, two review rounds | Correct FPSCR exception bit, double-precision compare |
| [#192 Memory checks](#4-pr-192--memory-checks-a-review-comment-that-became-a-project-feature) | Code hygiene and a questioned bounds check | Review led to the project-wide logging system (PR #198) |

---

## 1. PR #118 — `ADDIC`: a sign-extension bug and the tests that were missing

- **Pull request:** [#118 feat(core): added instruction ADDIC](https://github.com/Robouch0/WEMU/pull/118), by Robouch0, reviewed by Igoatyouu. Opened 2026-03-19, merged 2026-03-22.
- **Purpose:** implement the PowerPC `addic` instruction (add immediate carrying): `rD = rA + SIMM`, with the carry written to `XER[CA]`.

### Review ([changes requested, 2026-03-21](https://github.com/Robouch0/WEMU/pull/118#pullrequestreview-3985573764))

On `core/src/cpu/instructions/Add.cpp`, [line 92](https://github.com/Robouch0/WEMU/pull/118#discussion_r2969101962):
> Since imm is a signed integer of 32bits and you cast it to an unsigned integer of 64bits, it'll sign extend it to 64bits. So if we have a negative immediate value, it'll not work as expected. You should cast it to an uint32_t before casting it to an uint64_t.

On `core/tests/test_add.cpp`, [line 1](https://github.com/Robouch0/WEMU/pull/118#discussion_r2969102203):
> Tests only cover positive immediates, missing cases with negative si values to validate sign extension and carry behavior.

### Why it mattered
The carry is bit 32 of a 64-bit sum of two 32-bit values. Widening a negative immediate straight to 64 bits fills the upper half with ones, so bit 32 no longer means "the 32-bit addition carried". Every negative immediate gave the wrong `XER[CA]`. Compilers use `addic` with negative immediates in loop counters and in 64-bit arithmetic built from 32-bit halves (`addic` / `addze`), so this would have corrupted real game logic. The tests passed only because none of them used a negative immediate.

### What changed
| Commit | Change |
|---|---|
| [`0662d4e`](https://github.com/Robouch0/WEMU/commit/0662d4e) | `static_cast<uint64_t>(imm)` → `static_cast<uint64_t>(static_cast<uint32_t>(imm))` |
| [`a432fbe`](https://github.com/Robouch0/WEMU/commit/a432fbe) | Tests `ADDIC_NegativeImmediate` (100 + −50 = 50, `CA` = 1) and `ADDIC_NegativeImmediate_NoCarry` (0 + −1 = `0xFFFFFFFF`, `CA` = 0) |
| [`5703975`](https://github.com/Robouch0/WEMU/commit/5703975) | Tests moved to their own file, `core/tests/arithmetic/test_addic.cpp` |

The reviewer approved on 2026-03-22 ("LGTM"). Both tests are still in `test_addic.cpp` and run on every pull request.

---

## 2. PR #109 — `ADDME`: a style comment whose tests found three bugs

- **Pull request:** [#109 ADDME / ADDME. / ADDMEO / ADDMEO.](https://github.com/Robouch0/WEMU/pull/109), by NicolasMelet, reviewed by Igoatyouu. Opened 2026-03-14, merged 2026-03-19.
- **Purpose:** implement `addme` (add to minus one extended: `rD = rA + XER[CA] − 1`) and its record (`.`) and overflow (`o`) forms.

### Review ([changes requested, 2026-03-14](https://github.com/Robouch0/WEMU/pull/109#pullrequestreview-3948258088))

On `core/src/cpu/instructions/Add.cpp`, [line 35](https://github.com/Robouch0/WEMU/pull/109#discussion_r2934805933) and [line 51](https://github.com/Robouch0/WEMU/pull/109#discussion_r2934806087):
> You should change the 32 magic value to something else like a variable or a macro

### What changed
Replacing the magic number meant naming what `32` meant: the position of the carry bit. While doing that, the author added tests for every form with the carry set and cleared, and those tests failed:

| Commit | Change |
|---|---|
| [`8e165ec`](https://github.com/Robouch0/WEMU/commit/8e165ec) | `CARRY_OFFSET` constant instead of `32` (now in `EncodedInstruction.hpp`, used by every carrying add and subtract) |
| [`57e9bef`](https://github.com/Robouch0/WEMU/commit/57e9bef) | About 100 lines of carry tests for `addme`, `addme.`, `addmeo`, `addmeo.`. Also fixes the result being stored through `int32_t` instead of `uint32_t` |
| [`8523930`](https://github.com/Robouch0/WEMU/commit/8523930) | **Carry bug:** `rA + CA − 1` computed in 64 bits never sets bit 32, so `CA` was always 0. PowerPC defines `addme` as `rA + CA + 0xFFFFFFFF`, which carries out whenever `rA + CA ≥ 1`. Fixed, and the expected values in the tests corrected |
| [`aa41149`](https://github.com/Robouch0/WEMU/commit/aa41149) | **Overflow bug in a shared helper:** `updateOverflow` had an extra condition that set `XER[OV]` on some carries without a signed overflow. It is used by every `o`-form arithmetic instruction, so the fix applies to all of them |

The author answered "[all fixed](https://github.com/Robouch0/WEMU/pull/109#discussion_r2957835534)", and the reviewer approved on 2026-03-19. A one-line style comment led to tests, and the tests found two real bugs, one of them in code shared by every overflow-checking instruction.

---

## 3. PR #274 — `fcmpu`: two review rounds on hardware accuracy

- **Pull request:** [#274 feat(float): fcmpu](https://github.com/Robouch0/WEMU/pull/274), by ManuelR-T, reviewed by NicolasMelet and Robouch0. Opened 2026-05-14, merged 2026-06-07.
- **Purpose:** the shared decode helpers for floating-point A-form instructions (`fra`, `frc`, `bf`, `XO5`), plus `fcmpu` (floating compare unordered), which sets a condition-register field and `FPSCR[FPCC]`.

### Round 1 ([changes requested, 2026-05-14](https://github.com/Robouch0/WEMU/pull/274#pullrequestreview-4288079439))

On `core/src/cpu/instructions/float/FloatCompare.cpp`, [line 61](https://github.com/Robouch0/WEMU/pull/274#discussion_r3239760729):
> in this situation the field VXSNAN should be set — VXSNAN can be found in `cpu.m_fpscr`

The author [answered](https://github.com/Robouch0/WEMU/pull/274#discussion_r3239994322) that the manual sets `VXSNAN` only for a *signaling* NaN, while the code only tested for any NaN, and proposed adding a signaling-NaN check. Commit [`f84c6d1`](https://github.com/Robouch0/WEMU/commit/f84c6d1) added it, with a test (`FCMPU_SignalingNaN_SetsVXSNAN`).

### Round 2 ([changes requested, 2026-05-21](https://github.com/Robouch0/WEMU/pull/274#pullrequestreview-4334942222))

On the same file, [line 82](https://github.com/Robouch0/WEMU/pull/274#discussion_r3279474963):
> it would seem all Power PC floating points register are 64 bits wide, in other words only the double values are pertinent and should be used here. We can remove the float static_cast and SignalingNaN check to decrease the amount of work the instruction is doing

### Why it mattered
The first version converted both operands to `float` before comparing. Two doubles that differ only beyond single precision would compare as equal, and a double NaN can lose its signaling bit in the conversion. Games compare doubles with `fcmpu` constantly (physics, timers, culling), so an imprecise compare changes branches.

### What changed
| Commit | Change |
|---|---|
| [`f84c6d1`](https://github.com/Robouch0/WEMU/commit/f84c6d1) | Signaling-NaN detection; `FPSCR[VXSNAN]` set when an operand is a signaling NaN |
| [`36b6d76`](https://github.com/Robouch0/WEMU/commit/36b6d76) | Compare the 64-bit register values directly; remove the `float` path and its single-precision NaN check; new test `FCMPU_UsesDoublePrecision` (1.0 vs. the next double after 1.0 must compare as greater) |

The PR was approved again on 2026-06-07. Both tests are in `core/tests/float/test_fcmpu.cpp`.

---

## 4. PR #192 — Memory checks: a review comment that became a project feature

- **Pull request:** [#192 fix(Memory): bounds check, alignment checks, exceptions & cleanup](https://github.com/Robouch0/WEMU/pull/192), by Igoatyouu, reviewed by Robouch0 and NicolasMelet. Opened 2026-04-08, merged 2026-04-09.
- **Purpose:** harden guest memory access (bounds and alignment checks, typed exceptions) and move the run loop to a `step()` function.

### Review
Robouch0 (2026-04-08) asked for `[[nodiscard]]` on [`step()`](https://github.com/Robouch0/WEMU/pull/192#discussion_r3051147866) and [`isFatal()`](https://github.com/Robouch0/WEMU/pull/192#discussion_r3051159980), [`std::format`](https://github.com/Robouch0/WEMU/pull/192#discussion_r3051166367) for exception messages instead of string concatenation, and [`std::uint32_t`](https://github.com/Robouch0/WEMU/pull/192#discussion_r3051188802) for consistency.

NicolasMelet ([changes requested, 2026-04-09](https://github.com/Robouch0/WEMU/pull/192#pullrequestreview-4079261997)) on `Interpreter.cpp` [line 20](https://github.com/Robouch0/WEMU/pull/192#discussion_r3055053476) and [line 39](https://github.com/Robouch0/WEMU/pull/192#discussion_r3055058443):
> is this debugging ?
> if this is some logging information, maybe we could add this code if a MEMORY_LOG macro is set at compilation don't you think ?

and on `Memory.hpp`, [line 61](https://github.com/Robouch0/WEMU/pull/192#discussion_r3055065733):
> curious as to why you removed address + offset in this condition

### The discussion
On the bounds check, the author [explained](https://github.com/Robouch0/WEMU/pull/192#discussion_r3055126353) that the removed condition compared a guest *address* with the *size* of the host buffer, which is always wrong. The reviewer [agreed](https://github.com/Robouch0/WEMU/pull/192#discussion_r3055130026) ("oh my you are right"). Here the review confirmed that the change was correct, instead of reverting it.

### What changed
| Commit | Change |
|---|---|
| [`083be77`](https://github.com/Robouch0/WEMU/commit/083be77), [`1d94b0f`](https://github.com/Robouch0/WEMU/commit/1d94b0f) | `[[nodiscard]]` on `step()` and `isFatal()`: ignoring whether an instruction faulted no longer compiles cleanly |
| [`42b48df`](https://github.com/Robouch0/WEMU/commit/42b48df) | Memory exception messages built with `std::format` |
| [`bf2bf46`](https://github.com/Robouch0/WEMU/commit/bf2bf46), [`65987b7`](https://github.com/Robouch0/WEMU/commit/65987b7) | `std::uint32_t` throughout; test comments in English |
| [`da7f32b`](https://github.com/Robouch0/WEMU/commit/da7f32b) via [PR #198](https://github.com/Robouch0/WEMU/pull/198) | **The logging comment became a feature.** Instead of a `MEMORY_LOG` macro for one file, the author opened issue #197 and PR #198, *"generic logging system that does not impact run-time performances"*: `Utils::Log` with compile-time levels (Trace to Error), chosen with `-DWEMU_LOG_LEVEL`. Disabled levels generate no code (`if constexpr` on a `constexpr` level). The PR #192 debug prints were moved onto it |

### Result
`Utils::Log` is now WEMU's logger: 193 call sites in 23 source files (2026-11-12), at zero cost when a level is compiled out. The `[ERROR]`, `[DEBUG]` and `[INFO]` prefixes in WEMU's logs come from it.

---

## What these cases show
- **Review finds bugs that tests miss** when the tests only cover the cases the author thought of (#118).
- **Asking for tests is as valuable as finding bugs:** a style comment led to tests that found two arithmetic bugs (#109).
- **Hardware accuracy needs the manual:** reviewers checked behaviour against the PowerPC specification, over several rounds when needed (#274).
- **Review also confirms correct changes and starts better designs:** a questioned bounds check turned out to be a fix, and a request for a logging macro became the shared logging system (#192).
