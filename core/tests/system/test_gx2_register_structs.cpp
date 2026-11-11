// GX2 "Reg" structs: a title can initialise a register struct once (GX2Init*Reg) and
// apply it every frame (GX2Set*Reg). Both routes must record exactly the command the
// direct GX2Set* call would.

#include <array>
#include <bit>
#include <cstdint>
#include <vector>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "gfx/Gx2CommandStream.hpp"
#include "hle/Gx2.hpp"

class Gx2RegisterStructTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterGx2Functions();
            Core::Gfx::gx2Stream().clear();
        }

        static const Core::Gfx::Gx2Command &call(const char *name)
        {
            Core::syscallHandler.get(name)(*cpu);
            return Core::Gfx::gx2Stream().commands().back();
        }
};

TEST_F(Gx2RegisterStructTest, StencilMaskStructRecordsTheSameCommandAsTheDirectCall)
{
    // Front: prewrite mask 0x12, write mask 0x34, reference 0x56; back: 0x78, 0x9A, 0xBC.
    constexpr std::array<std::uint32_t, 6> masks{0x112, 0x134, 0x156, 0x178, 0x19A, 0x1BC}; // high bits must be dropped
    for (unsigned i = 0; i < 6; ++i)
        cpu->m_gpr[3 + i] = masks[i];
    const auto direct = call("GX2SetStencilMask");
    ASSERT_EQ(direct.payload.size(), 2u);
    EXPECT_EQ(direct.payload[0], 0x00341256u) << "reference | prewrite << 8 | write << 16";
    EXPECT_EQ(direct.payload[1], 0x009A78BCu);

    constexpr std::uint32_t Reg = 0x28600000;
    cpu->m_gpr[3] = Reg;
    for (unsigned i = 0; i < 6; ++i)
        cpu->m_gpr[4 + i] = masks[i];
    Core::syscallHandler.get("GX2InitStencilMaskReg")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    cpu->m_gpr[3] = Reg;
    const auto viaStruct = call("GX2SetStencilMaskReg");
    EXPECT_EQ(viaStruct.type, direct.type);
    EXPECT_EQ(std::vector<std::uint32_t>(viaStruct.payload.begin(), viaStruct.payload.end()),
              std::vector<std::uint32_t>(direct.payload.begin(), direct.payload.end()));
}

TEST_F(Gx2RegisterStructTest, BlendConstantColorStructRecordsTheSameColourAsTheDirectCall)
{
    constexpr std::array<float, 4> colour{0.25f, 0.5f, 0.75f, 1.0f};
    for (unsigned i = 0; i < 4; ++i)
        cpu->m_fpr[i + 1] = colour[i];
    const auto direct = call("GX2SetBlendConstantColor");

    constexpr std::uint32_t Reg = 0x28610000;
    cpu->m_gpr[3] = Reg;
    Core::syscallHandler.get("GX2InitBlendConstantColorReg")(*cpu);
    for (unsigned i = 0; i < 4; ++i) {
        cpu->m_fpr[i + 1] = -9.0f; // the struct, not the registers, must carry the colour
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Reg + i * 4), std::bit_cast<std::uint32_t>(colour[i]));
    }
    cpu->m_gpr[3] = Reg;
    const auto viaStruct = call("GX2SetBlendConstantColorReg");
    EXPECT_EQ(viaStruct.type, direct.type);
    for (unsigned i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(static_cast<float>(viaStruct.fpr[i]), colour[i]) << i;
        EXPECT_FLOAT_EQ(static_cast<float>(direct.fpr[i]), colour[i]) << i;
    }
}
