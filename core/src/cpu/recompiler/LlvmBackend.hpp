#pragma once

#include <memory>
#include <string>

#include "Block.hpp"

namespace Core::Ppc {
    struct MemoryRegion {
            std::uint8_t *data{};
            std::uint64_t size{};
            std::uint32_t base{};
    };
    // Plain host ABI shared by runtime JIT and cached/AOT objects. Unmapped
    // memory exits before the instruction, leaving precise interpreter fallback.
    struct ExecutionState {
            std::uint32_t *registers{};
            MemoryRegion regions[3]{};
            // LR uses the interpreter's code-buffer offset; nextPc is a guest address.
            std::uint32_t condition{}, summaryOverflow{}, link{}, counter{}, nextPc{};
            std::uint32_t instructionBudget{}; // Zero executes one block; runtime loops retire at most 64 instructions.
    };

    class NativeBlock;
    // Non-owning, valid only through its single-threaded dispatcher generation.
    struct NativeBinding {
            NativeBlock *code{};
            std::uint64_t epoch{};
    };

    class NativeCompiler {
        public:
            NativeCompiler();
            ~NativeCompiler();
            NativeCompiler(const NativeCompiler &) = delete;
            NativeCompiler &operator=(const NativeCompiler &) = delete;

        private:
            friend class NativeBlock;
            struct Impl;
            std::shared_ptr<Impl> m_impl;
    };

    class NativeBlock {
        public:
            NativeBlock(NativeCompiler &compiler, const Block &block);
            // Only compiler-produced, trusted host objects; not guest binary input.
            NativeBlock(NativeCompiler &compiler, std::span<const std::uint8_t> object);
            ~NativeBlock();
            NativeBlock(const NativeBlock &) = delete;
            NativeBlock &operator=(const NativeBlock &) = delete;
            // Convenience for register-only clients; full guest execution requires state.
            void execute(std::span<std::uint32_t, 32> registers) const;
            unsigned execute(ExecutionState &state) const;

        private:
            struct Impl;
            std::unique_ptr<Impl> m_impl;
    };

    // Host-specific relocatable object, not a standalone game or a validated cache.
    std::vector<std::uint8_t> emitObject(const Block &block);
    std::string nativeObjectIdentity();
} // namespace Core::Ppc
