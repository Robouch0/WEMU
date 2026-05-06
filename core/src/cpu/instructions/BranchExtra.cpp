/*
** Conditional branches and indirect branches (BLR / BCTR).
**
** m_pc  = byte offset from ApplicationCode (= PPC addr - 0x02000000).
** m_lr  = return-address also stored as a buffer offset.
** m_ctr = absolute PPC value (used by BCTR).
**
** BO/BI encoding (PPC ISA):
**   BO[0] = decrement-and-test CTR?  (bit 25 in raw, i.e. rt>>4 & 1)
**   BO[1] = branch if CTR != 0 / == 0 after decrement
**   BO[2] = ignore CR?               (rt>>2 & 1)
**   BO[3] = branch if CR[BI] == 1
**   BO[4] = branch-prediction hint (ignored here)
**
**   Standard aliases:
**     BLR  → BCLR with BO=20 (0b10100) = always: ignore CTR, ignore CR
**     BLRL → same + LK=1
**     BCTR → BCTR with BO=20
**     BNE  → BC with BO=4 (0b00100), BI=2 (CR0[EQ])
**     BEQ  → BC with BO=12 (0b01100), BI=2
**     BLT  → BC with BO=12, BI=0
**     BGT  → BC with BO=12, BI=1
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // Evaluate the BO condition.
    // Returns true if the branch should be taken.
    static bool eval_bo(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t bo = instr.rt; // bits [25:21]
        const std::uint32_t bi = instr.ra; // bits [20:16] — which CR bit to test

        // PPC BO field (5-bit integer, bit 0 = LSB):
        //   bit 2 (0x04): 1 = don't decrement or test CTR
        //   bit 1 (0x02): when bit2=0: 1 = branch if CTR==0, 0 = branch if CTR!=0
        //   bit 4 (0x10): 1 = don't test CR (always satisfy CR condition)
        //   bit 3 (0x08): when bit4=0: expected CR[BI] value to branch (1=branch if CR=1)
        //   bit 0 (0x01): branch prediction hint, ignored
        //
        // Verified against working PPC ISA: BLR/BCTR use BO=20=0b10100 (bit2=1,bit4=1 → always)
        // BEQ uses BO=12=0b01100 (bit2=1 → no CTR, bit4=0 → test CR, bit3=1 → CR must be 1)
        // BNE uses BO=4=0b00100  (bit2=1 → no CTR, bit4=0 → test CR, bit3=0 → CR must be 0)
        const bool no_ctr = (bo >> 2) & 1; // bit 2: 1 = ignore CTR
        const bool ctr_zero = (bo >> 1) & 1; // bit 1: 1 = branch if CTR=0
        const bool no_cr = (bo >> 4) & 1; // bit 4: 1 = ignore CR
        const bool cr_val = (bo >> 3) & 1; // bit 3: expected CR[BI] value

        // CTR condition
        bool ctr_cond = true;
        if (!no_ctr) {
            cpu.m_ctr--;
            ctr_cond = ctr_zero ? (cpu.m_ctr == 0) : (cpu.m_ctr != 0);
        }

        bool cr_cond = true;
        if (!no_cr) {
            const bool bit = (cpu.m_cr.raw >> (31 - bi)) & 1;
            cr_cond = (bit == cr_val);
        }
        return ctr_cond && cr_cond;
    }

    // ── BC — Branch Conditional ──────────────────────────────────────────────────
    void BC(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        if (!eval_bo(cpu, instr))
            return;

        // BD field: bits [15:2], sign-extended, represents byte offset / 4
        const std::int32_t bd = static_cast<std::int32_t>(static_cast<std::int16_t>(instr.raw & 0xFFFC)); // sign-extend 16-bit

        if (instr.lk)
            cpu.m_lr = cpu.m_pc + 4;

        if (instr.aa)
            // Absolute: address is the immediate (minus ApplicationCode for buffer offset)
            cpu.m_nextPc = static_cast<std::uint32_t>(bd) - Core::Memory::MemoryMap::ApplicationCode;
        else
            cpu.m_nextPc = cpu.m_pc + static_cast<std::uint32_t>(bd);
    }

    // ── BCLR — Branch Conditional to LR ─────────────────────────────────────────
    void BCLR(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ret = cpu.m_lr;

        if (instr.lk)
            cpu.m_lr = cpu.m_pc + 4;

        if (!eval_bo(cpu, instr)) {
            if (instr.lk)
                cpu.m_lr = ret; // restore if not taken
            return;
        }

        cpu.m_nextPc = ret;
    }

    // ── BCTR — Branch Conditional to CTR ─────────────────────────────────────────
    void BCTR(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        Utils::Log::debug("BCTR called with m_ctr -> {}", cpu.m_ctr);
        if (instr.lk)
            cpu.m_lr = cpu.m_pc + 4;

        const std::uint32_t bo = instr.rt;
        const bool no_ctr = (bo >> 2) & 1; // bit 2: 1 = ignore CTR
        const bool no_cr = (bo >> 4) & 1; // bit 4: 1 = ignore CR

        bool taken = true;
        if (!no_ctr) {
            cpu.m_ctr--;
            const bool ctr_zero = (bo >> 1) & 1; // bit 1: 1 = branch if CTR==0
            taken = taken && (ctr_zero ? (cpu.m_ctr == 0) : (cpu.m_ctr != 0));
        }
        if (!no_cr && taken) {
            const std::uint32_t bi = instr.ra;
            const bool cr_val = (bo >> 3) & 1;
            taken = ((cpu.m_cr.raw >> (31 - bi)) & 1) == static_cast<uint32_t>(cr_val);
        }

        if (!taken)
            return;

        // Import interception: RPL import stubs live at 0xC0000xxx (outside the flat buffer).
        // Dispatch to the HLE handler and return to LR, just like SC does.
        if (cpu.m_ctr >= 0xC0000000U) {
            const std::string *sym_name = nullptr;
            for (const auto &sym: cpu.m_binary.symbols) {
                if (sym.raw.header.st_value == cpu.m_ctr) {
                    sym_name = &sym.name;
                    break;
                }
            }
            cpu.m_hle_redirected = false;
            if (sym_name && Core::syscallHandler.syscallTable.contains(*sym_name)) {
                Utils::Log::debug("[BCTR] symbol name found {}", *sym_name);
                Utils::Log::debug("[HLE] [BCTR] SyscallTable has found symbol {}", *sym_name);
                Core::syscallHandler.get (*sym_name)(cpu);
            } else {
                Utils::Log::debug("[BCTR] We put the value 0 inside gpr 0 bc why not (salut matthieu)");
                cpu.m_gpr[3] = 0;
            }
            if (!cpu.m_hle_redirected)
                cpu.m_nextPc = cpu.m_lr;
            return;
        }

        cpu.m_nextPc = cpu.m_ctr - Core::Memory::MemoryMap::ApplicationCode;
    }

    // Fix BA: absolute branches must subtract ApplicationCode to get buffer offset
    // (The existing Branch.cpp BA is wrong — it sets m_nextPc to the raw PPC address)

} // namespace Core::Instruction
