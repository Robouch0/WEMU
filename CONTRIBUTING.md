# Contributing to WEMU

Thank you for your interest in contributing. WEMU is a Wii U emulator written in C++23, targeting Linux. The project is in early development — PowerPC instruction coverage is the most impactful area to contribute to right now.

---

## Developer documentation

Full technical documentation is available in the [project wiki](https://github.com/Robouch0/WEMU/wiki):

| Guide | Description |
|---|---|
| [Dev Environment Setup](https://github.com/Robouch0/WEMU/wiki/Dev-Environment-Setup) | Dependencies, build instructions, CLion setup |
| [Architecture Overview](https://github.com/Robouch0/WEMU/wiki/Architecture-Overview) | How the sub-projects fit together and the data flow |
| [Adding a PowerPC Instruction](https://github.com/Robouch0/WEMU/wiki/Adding-a-PowerPC-Instruction) | Step-by-step walkthrough: ISA lookup → implementation → tests |
| [ELF/RPX Loader Internals](https://github.com/Robouch0/WEMU/wiki/ELF-RPX-Loader-Internals) | `BeDecoder`, `Binary` struct, compressed sections |
| [Writing Unit Tests](https://github.com/Robouch0/WEMU/wiki/Writing-Unit-Tests) | Test fixture, `EncodedInstruction`, naming conventions |

---

## Workflow

- All development branches off `dev`. **Never target `main` directly.**
- Fork the repository, create a branch from `dev`, and open a pull request back to `dev`.
- Keep pull requests focused — one feature or fix per PR.

---

## CI checks

Every pull request runs three automated checks. All three must pass before merge.

| Check | What it verifies |
|---|---|
| **clang-format** | Code is formatted according to `core/.clang-format` |
| **clang-tidy** | No static analysis warnings in `core/src/` |
| **Unit tests** | All GoogleTest tests pass |

Fix formatting locally before pushing:

```bash
find core/src core/tests \( -name '*.cpp' -o -name '*.hpp' \) | xargs clang-format -i
```

---

## Code style

- **Standard:** C++23 for `core/`, C++20 for `gui/`, C++17 for `vulkan/`
- **Indentation:** 4 spaces, 120-column limit (see `core/.clang-format`)
- **Comments:** only when the *why* is non-obvious — no descriptive or task-reference comments
- **Namespaces:** implementation code lives under `Core::Instruction`, `Core::`, `Utils::`, etc.

---

## Legal

By submitting a pull request you confirm that your contribution is your own work and that you agree to license it under the [MIT License](LICENSE).

WEMU does not include or distribute any Nintendo copyrighted material. Do not submit code that incorporates dumped firmware, ROMs, or proprietary headers.
