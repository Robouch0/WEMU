#include <cstring>

#include "TestFixture.hpp"
#include "hle/ApplicationBoot.hpp"
#include "hle/MemHeap.hpp"

class ApplicationBootTest : public InstructionTest {
    protected:
        static constexpr unsigned APP = Core::Memory::ApplicationCode;
        static constexpr unsigned EXPORTS = Core::Memory::DIMPORT_BASE;

        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_binary.sections.clear();
            cpu->m_binary.symbols.clear();
            cpu->m_running = true;
        }

        void addPreinit()
        {
            Core::Section code{};
            code.meta = {.size = 0x1000, .virtAddress = APP, .type = SHT_PROGBITS};
            code.raw.header.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            cpu->m_binary.sections.push_back(code);
            Core::Section exports{};
            exports.meta = {.size = 32, .virtAddress = EXPORTS, .type = SHT_RPL_EXPORTS};
            exports.raw.header.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            exports.raw.data.resize(32);
            const auto put = [&](unsigned offset, unsigned value) {
                cpu->m_memory.write<unsigned>(EXPORTS + offset, value);
                value = std::byteswap(value);
                std::memcpy(exports.raw.data.data() + offset, &value, 4);
            };
            put(0, 1);
            put(8, APP + 0x100);
            put(12, 16);
            std::memcpy(exports.raw.data.data() + 16, "__preinit_user", 14);
            cpu->m_binary.sections.push_back(std::move(exports));
        }
};

TEST_F(ApplicationBootTest, MissingExportLeavesEntryAndArgumentsAlone)
{
    cpu->m_pc = 0x100;
    cpu->m_gpr[3] = 123;
    Core::Hle::ApplicationBoot boot;
    boot.begin(*cpu);
    EXPECT_FALSE(boot.active);
    EXPECT_EQ(cpu->m_pc, 0x100u);
    EXPECT_EQ(cpu->m_gpr[3], 123u);
}

TEST_F(ApplicationBootTest, ExportUsesRelocatedAddressWithoutDebugSymbols)
{
    addPreinit();
    cpu->m_memory.write<unsigned>(EXPORTS + 8, APP + 0x200);
    EXPECT_EQ(cpu->m_binary.functionExport("__preinit_user"), APP + 0x200);
    cpu->m_memory.write<unsigned>(EXPORTS + 8, 0);
    EXPECT_EQ(cpu->m_binary.functionExport("__preinit_user"), 0u);
    cpu->m_binary.sections.back().raw.data[3] = static_cast<char>(0xFF);
    EXPECT_EQ(cpu->m_binary.functionExport("__preinit_user"), 0u);
}

TEST_F(ApplicationBootTest, PreinitRunsBeforeEntryAndPreservesEntryArguments)
{
    addPreinit();
    cpu->m_binary.header.e_entry = APP;
    cpu->m_gpr[1] = 0xC0FFFFF0;
    cpu->m_gpr[3] = 7;
    cpu->m_gpr[4] = 11;
    cpu->m_gpr[9] = 0x10001000;
    cpu->m_lr = Core::RETURN_SENTINEL - APP;
    // Preinit reads the MEM2 handle reference and stores it before entry runs.
    cpu->m_memory.write<unsigned>(APP + 0x100, 0x80C50000); // lwz r6,0(r5)
    cpu->m_memory.write<unsigned>(APP + 0x104, 0x90C90000); // stw r6,0(r9)
    cpu->m_memory.write<unsigned>(APP + 0x108, 0x38600000); // li r3,0
    cpu->m_memory.write<unsigned>(APP + 0x10C, 0x4E800020); // blr
    cpu->m_memory.write<unsigned>(APP, 0x90690004); // stw r3,4(r9)
    cpu->m_memory.write<unsigned>(APP + 4, 0x90890008); // stw r4,8(r9)
    cpu->m_memory.write<unsigned>(APP + 8, 0x4E800020); // blr
    cpu->run();
    EXPECT_FALSE(cpu->failed());
    EXPECT_EQ(cpu->m_memory.read<unsigned>(0x10001000), Core::Hle::defaultHeapHandle(*cpu));
    EXPECT_EQ(cpu->m_memory.read<unsigned>(0x10001004), 7u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(0x10001008), 11u);
    EXPECT_EQ(cpu->m_gpr[1], 0xC0FFFFF0u);
}
