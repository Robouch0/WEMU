#include <iostream>
#include "../../libs/cpu_IR/include/cpuInterface.hpp"
#include "../../libs/gfx_IR/include/gpuInterface.hpp"

int main() {
    std::cout << "Wemu core skeleton\n";

    cpu_frontend::CpuTranslator cpuTrans;
    std::vector<uint8_t> sample(16, 0xAA);
    std::vector<cpu_frontend::IRInstruction> irOut;

    for (size_t i = 0; i + 4 <= sample.size(); i += 4) {
        irOut.push_back(cpu_frontend::CpuTranslator::decodeInstruction(
            (uint32_t(sample[i]) << 24) |
            (uint32_t(sample[i+1]) << 16) |
            (uint32_t(sample[i+2]) << 8) |
            uint32_t(sample[i+3])
        ));
    }

    std::cout << "Parsed " << irOut.size() << " dummy instructions\n";

    graphics_frontend::GfxTranslator gfxTrans;
    std::vector<graphics_frontend::GraphicsIR> gfxOut;
    gfxOut.push_back(graphics_frontend::GfxTranslator::translateCommand(0xDEADBEEF));

    std::cout << "Parsed " << gfxOut.size() << " dummy graphics calls\n";

    return 0;
}
