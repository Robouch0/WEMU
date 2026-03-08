/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#include <algorithm>
#include <bitset>
#include <utility>

#include "Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "utils/BeDecoder.hpp"

Core::Interpreter::Interpreter(Core::Binary binary)
    : m_binary(std::move(binary))
{
    initInstructionMap();

    // Build OS address → handler map from the binary's symbol table.
    // For Wii U import symbols, st_value holds the OS virtual address
    // (0xC0000000 range) that the game may call directly via addis/addi/bctr.
    for (const auto &sym : m_binary.symbols) {
        const auto osAddr = static_cast<std::uint32_t>(sym.raw.header.st_value);
        if (osAddr >= 0xC0000000u && Core::syscallHandler.syscallTable.contains(sym.name))
            Core::syscallHandler.registerAddress(osAddr, Core::syscallHandler.get(sym.name));
    }
}

void Core::Interpreter::run()
{
    auto instructionDecoder = Utils::BeDecoder(m_binary.m_memory.getMemory());

    // m_pc stores the full virtual address (same convention as GPRs, LR, CTR).
    // The instruction decoder offset = m_pc - ApplicationCode.
    m_gpr[1] = Core::Memory::MemoryMap::ApplicationData + 0x1000000 - 0x10;
    m_lr = 0; // sentinel: BLR from main() returns here → exit loop (0 is never a valid vaddr)

    m_pc = m_binary.header.e_entry;
    std::cout << "Starting at entrypoint 0x" << std::hex << m_pc << std::dec << std::endl;

    while (true) {
        // Sentinel: LR was initialised to 0; when main() returns via BLR, m_pc becomes 0.
        if (m_pc == 0)
            break;

        // Intercept direct calls to Wii U OS addresses (0xC0000000+ range).
        // These come from addis/addi/bctr sequences that bypass import stubs.
        if (m_pc >= 0xC0000000u) {
            if (Core::syscallHandler.addressTable.contains(m_pc))
                Core::syscallHandler.addressTable[m_pc](*this);
            else
                std::cout << "[HLE] unhandled OS call at 0x" << std::hex << m_pc << std::dec << std::endl;
            m_pc = m_lr & ~3u; // return to caller
            continue;
        }

        instructionDecoder.seek(m_pc - Core::Memory::MemoryMap::ApplicationCode);
        const EncodedInstruction encodedInstruction(instructionDecoder.extractSwap<uint32_t>());
        m_nextPc = m_pc + 4;

        std::cout << " 0x" << std::hex << m_pc
                  << std::dec << "\t"
                  << std::bitset<sizeof(uint32_t) * 8>(encodedInstruction.raw) << "\t";

        try {
            executeInstruction(encodedInstruction);
        } catch (Core::InterpreterException &e) {
            std::cout << e.what() << std::endl;
        }

        m_pc = m_nextPc;
    }

    std::cout << "Execution finished." << std::endl;
    debugDumpGPR();
}

InstructionID Core::Interpreter::findInstructionID(const EncodedInstruction &instr)
{
    const auto &candidates = m_instructionMap[instr.opcd];

    for (const auto &[id, fields, _]: candidates) {
        const bool found = std::ranges::all_of(fields.begin(), fields.end(), [&](const auto &p) {
            const auto &[field, value] = p;
            switch (field) {
                case Field::F_AA:
                    return instr.aa == value;
                case Field::F_LK:
                    return instr.lk == value;
                case Field::F_XO10:
                    return instr.xo10 == value;
                case Field::F_XO9:
                    return instr.xo9 == value;
                case Field::F_OE:
                    return instr.oe == value;
                case Field::F_BO:
                    return instr.bo == value;
                case Field::F_BI:
                    return instr.bi == value;
                case Field::F_OPCD:
                    return true;
                default:
                    throw Core::InterpreterException("Unknown field.");
                    return false;
            }
        });
        if (found)
            return id;
    }
    std::cout << std::endl;
    throw Core::InterpreterException("No instruction found with this fields. (opcode == " +
                                     std::to_string(instr.opcd) + ")");
}

void Core::Interpreter::executeInstruction(const EncodedInstruction &instr)
{
    InstructionID id = findInstructionID(instr);

    std::cout << instructionIDToString(id) << std::endl;
    INSTRUCTIONARRAY[id].function(*this, instr);
}

void Core::Interpreter::initInstructionMap()
{
    for (auto &instrInfo: INSTRUCTIONARRAY) {
        const auto opcode = instrInfo.matchFields[0].second;
        if (m_instructionMap.contains(opcode)) {
            m_instructionMap[opcode].push_back(instrInfo);
        } else {
            m_instructionMap.emplace(opcode, std::vector{instrInfo});
        }
    }
}

void Core::Interpreter::updateCR(Core::ConditionRegister::Register &cr, const std::int32_t &result,
                                 const EncodedInstruction &instr) const
{
    if (instr.rc) {
        cr.lt = result < 0;
        cr.gt = result > 0;
        cr.eq = result == 0;
        cr.so = m_xer.so;
    }
}

void Core::Interpreter::updateOverflow(const std::int32_t &a, const std::int32_t &b, const std::int32_t &result,
                                       const EncodedInstruction &instr, const std::uint32_t &carry)
{
    if (!instr.oe)
        return;

    const bool aSign = a < 0;
    const bool bSign = b < 0;
    const bool resultSign = result < 0;

    m_xer.ov = (aSign == bSign) && (aSign != resultSign) ||
               ((carry == 1) && (aSign == resultSign) && (bSign == resultSign));

    if (m_xer.ov)
        m_xer.so = true;
}
