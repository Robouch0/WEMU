// The x64 JIT calls interpreter handlers through a table of callouts generated from
// cpu_instructions.anh. Every instruction in that table must (1) be reachable by the
// decoder and (2) behave identically whether it runs through its callout or through the
// interpreter: same registers, flags, next PC and memory, or the same fault. A callout
// table that drifted out of order would make compiled code run the wrong instruction.

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "TestFixture.hpp"
#include "cpu/jit/Callouts.hpp"
#include "cpu/jit/X64Jit.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/Instruction.hpp"

namespace {
    constexpr std::uint32_t App = Core::Memory::MemoryMap::ApplicationCode;
    constexpr std::uint32_t Pc = 0x1000; // guest PC relative to the code base
    // Registers hold Base + 4*n, so rA + d lands around Base and rA + rB around 2*Base:
    // both windows are mapped and compared after every instruction.
    constexpr std::uint32_t Base = 0x20000000; // data range: stores here are not code writes
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 2> Windows{{{Base - 0x10000, 0x20000}, {2 * Base - 0x1000, 0x2000}}};

    struct State {
            std::array<std::uint32_t, 32> gpr{};
            std::array<std::uint64_t, 32> fpr{}, ps1{};
            std::array<std::uint32_t, 8> gqr{};
            std::uint32_t cr{}, xer{}, lr{}, ctr{}, fpscr{}, nextPc{};
            std::vector<std::uint8_t> memory;

            bool operator==(const State &) const = default;
    };

    State capture(const Core::Interpreter &cpu)
    {
        State s;
        for (unsigned i = 0; i < 32; ++i) {
            s.gpr[i] = cpu.m_gpr[i];
            s.fpr[i] = std::bit_cast<std::uint64_t>(cpu.m_fpr[i]);
            s.ps1[i] = std::bit_cast<std::uint64_t>(cpu.m_ps1[i]);
        }
        for (unsigned i = 0; i < 8; ++i)
            s.gqr[i] = cpu.m_gqr[i];
        s.cr = cpu.m_cr.raw;
        s.xer = cpu.m_xer.raw;
        s.lr = cpu.m_lr;
        s.ctr = cpu.m_ctr;
        s.fpscr = cpu.m_fpscr.raw;
        s.nextPc = cpu.m_nextPc;
        for (const auto &[start, size]: Windows) {
            const auto *bytes = cpu.m_memory.hostPtr(start, size);
            s.memory.insert(s.memory.end(), bytes, bytes + size);
        }
        return s;
    }

    void restore(Core::Interpreter &cpu, const State &s)
    {
        for (unsigned i = 0; i < 32; ++i) {
            cpu.m_gpr[i] = s.gpr[i];
            cpu.m_fpr[i] = std::bit_cast<double>(s.fpr[i]);
            cpu.m_ps1[i] = std::bit_cast<double>(s.ps1[i]);
        }
        for (unsigned i = 0; i < 8; ++i)
            cpu.m_gqr[i] = s.gqr[i];
        cpu.m_cr.raw = s.cr;
        cpu.m_xer.raw = s.xer;
        cpu.m_lr = s.lr;
        cpu.m_ctr = s.ctr;
        cpu.m_fpscr.raw = s.fpscr;
        std::size_t offset = 0;
        for (const auto &[start, size]: Windows) {
            std::memcpy(cpu.m_memory.hostPtr(start, size), s.memory.data() + offset, size);
            offset += size;
        }
    }

    State randomState(std::mt19937 &rng)
    {
        State s;
        for (unsigned i = 0; i < 32; ++i) {
            s.gpr[i] = Base + i * 4;
            s.fpr[i] = std::bit_cast<std::uint64_t>(std::uniform_real_distribution<double>(-1000, 1000)(rng));
            s.ps1[i] = std::bit_cast<std::uint64_t>(std::uniform_real_distribution<double>(-1000, 1000)(rng));
        }
        s.cr = rng();
        s.xer = rng() & 0xE000007F; // SO, OV, CA and the string byte count
        s.lr = App + 0x2000;
        s.ctr = App + 0x3000 + (rng() & 0xFC); // a code address, never an import sentinel
        s.memory.resize(Windows[0].second + Windows[1].second);
        for (auto &byte: s.memory)
            byte = static_cast<std::uint8_t>(rng());
        return s;
    }

    // An encoding of `info`: random operand bits with the table's match fields forced in.
    std::uint32_t encode(const InstructionInfo &info, std::mt19937 &rng)
    {
        EncodedInstruction instr(rng());
        for (const auto &[field, value]: info.matchFields) {
            switch (field) {
                case Field::F_OPCD:
                    instr.opcd = value;
                    break;
                case Field::F_XO10:
                    instr.xo10 = value;
                    break;
                case Field::F_XO9:
                    instr.xo9 = value;
                    break;
                case Field::F_XO5:
                    instr.xo5 = value;
                    break;
                case Field::F_AA:
                    instr.aa = value;
                    break;
                case Field::F_LK:
                    instr.lk = value;
                    break;
            }
        }
        return instr.raw;
    }
} // namespace

class JitCalloutTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_scheduler.bootstrap(*cpu);
            cpu->m_running = true;
        }
};

TEST_F(JitCalloutTest, EveryInstructionNameIsDistinctAndKnown)
{
    std::set<std::string> names;
    for (const auto &info: INSTRUCTIONARRAY) {
        const auto name = instructionIDToString(info.id);
        EXPECT_NE(name, "?") << int(info.id);
        EXPECT_TRUE(names.insert(name).second) << name << " appears twice";
    }
    EXPECT_EQ(names.size(), std::size(INSTRUCTIONARRAY));
}

TEST_F(JitCalloutTest, EveryInstructionDecodesToItselfAndRunsTheSameThroughItsCallout)
{
    std::mt19937 rng(0x57454D55);
    unsigned compared = 0, faulted = 0;
    for (const auto &info: INSTRUCTIONARRAY) {
        const auto name = instructionIDToString(info.id);
        if (info.id == InstructionID::E_SC)
            continue; // dispatches HLE by symbol index; covered by the RPX import tests
        // The decoder must reach this entry; random operand bits may hit a more specific one, so retry.
        std::uint32_t raw = 0;
        bool reachable = false;
        for (int attempt = 0; attempt < 64 && !reachable; ++attempt) {
            raw = encode(info, rng);
            reachable = cpu->findInstructionID(EncodedInstruction(raw)) == info.id;
        }
        ASSERT_TRUE(reachable) << name << " is never selected by the decoder";

        for (int round = 0; round < 3; ++round) {
            const auto initial = randomState(rng);
            restore(*cpu, initial);
            cpu->m_pc = Pc;
            cpu->m_nextPc = Pc + 4;
            bool interpreterFaulted = false;
            try {
                info.function(*cpu, EncodedInstruction(raw));
            } catch (const std::exception &) {
                interpreterFaulted = true;
            }
            const auto interpreted = capture(*cpu);

            restore(*cpu, initial);
            const auto exit = static_cast<Core::Jit::ExitReason>(Core::Jit::callout(info.id)(*cpu, raw, Pc));
            if (interpreterFaulted) {
                EXPECT_TRUE(exit == Core::Jit::ExitReason::Fatal || exit == Core::Jit::ExitReason::Rethrow)
                        << name << " faulted in the interpreter but not through its callout";
                (void) Core::Jit::takePendingException();
                SetUp(); // a fatal guest fault ends the current thread
                ++faulted;
                continue;
            }
            ASSERT_EQ(exit, Core::Jit::ExitReason::Continue) << name << " raw=0x" << std::hex << raw;
            const auto called = capture(*cpu);
            EXPECT_TRUE(called == interpreted) << name << " raw=0x" << std::hex << raw << " differs between callout and interpreter";
            ++compared;
        }
    }
    EXPECT_GT(compared, 400u) << "most instructions must run, not fault";
    RecordProperty("faulted", static_cast<int>(faulted));
}
