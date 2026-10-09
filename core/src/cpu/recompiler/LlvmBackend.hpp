#pragma once

#include "Block.hpp"
#include <memory>
#include <string>

namespace Core::Ppc {
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
            void execute(std::span<std::uint32_t, 32> registers) const;
        private:
            struct Impl;
            std::unique_ptr<Impl> m_impl;
    };

    // Host-specific relocatable object, not a standalone game or a validated cache.
    std::vector<std::uint8_t> emitObject(const Block &block);
    std::string nativeObjectIdentity();
}
