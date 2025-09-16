#include "cpuInterface.hpp"

namespace cpu_frontend {

IRInstruction CpuTranslator::decodeInstruction(uint32_t rawInstruction) {
    IRInstruction instruction = {};
    instruction.opcode = rawInstruction;
    instruction.operand = 0; // dummy
    return instruction;
}

}
