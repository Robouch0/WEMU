#pragma once
#include <cstdint>

namespace cpu_frontend {

    struct IRInstruction {
        uint32_t opcode;   // IR opcode ID
        uint64_t operand;  // Simplified operand
    };

    class CpuTranslator {
        public:
            // Takes raw instruction (4 bytes PowerPC) and decodes into IR
            static IRInstruction decodeInstruction(uint32_t rawInstruction);
    };

}
