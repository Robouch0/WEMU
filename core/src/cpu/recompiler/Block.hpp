#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Core::Ppc {
    enum class IntegerOp {
        Add,
        Subtract,
        Multiply,
        And,
        Or,
        Xor,
        RotateMask,
        RotateInsert,
        ShiftLeft,
        ShiftRight,
        Load,
        Store,
        CompareSigned,
        CompareUnsigned,
        Negate,
        SignExtend8,
        SignExtend16,
        LeadingZeros,
        ReadLink,
        WriteLink,
        ReadCounter,
        WriteCounter,
        Branch,
        BranchConditional,
        BranchLinkRegister
    };
    struct Operand {
            bool constant;
            std::uint32_t value;
    };
    struct RegisterOperation {
            IntegerOp op;
            unsigned destination;
            Operand lhs, rhs;
            std::uint32_t mask{0xFFFFFFFFu};
            unsigned memoryBytes{};
            bool signedLoad{};
            unsigned updateRegister{}; // Zero means no address update; update RA=0 is invalid.
            bool record{};
            unsigned conditionField{};
    };

    // Integer, condition/control-flow and checked-memory subset. Unsupported words
    // reject the whole block. Branches must be final; stores require code-write exits.
    class Block {
        public:
            static bool supports(std::uint32_t word);
            static bool stores(std::uint32_t word);
            static bool branches(std::uint32_t word);
            static bool loopsToStart(std::uint32_t pc, std::span<const std::uint32_t> words);
            static std::optional<Block> decode(std::uint32_t pc, std::span<const std::uint32_t> words);
            [[nodiscard]] std::span<const RegisterOperation> operations() const { return m_operations; }
            [[nodiscard]] std::span<const std::uint32_t> words() const { return m_words; }
            [[nodiscard]] std::uint32_t pc() const { return m_pc; }

        private:
            Block() = default;
            std::uint32_t m_pc{};
            std::vector<std::uint32_t> m_words;
            std::vector<RegisterOperation> m_operations;
    };
} // namespace Core::Ppc
