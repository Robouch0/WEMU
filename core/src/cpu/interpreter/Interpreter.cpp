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
#include "utils/Logger.hpp"

Core::Interpreter::Interpreter(Core::Binary binary) : m_binary(std::move(binary)) { initInstructionMap(); }

[[nodiscard]] bool Core::Interpreter::step(Utils::BeDecoder &decoder, const std::uint32_t ppc_pc)
{
    decoder.seek(m_pc);
    const EncodedInstruction encodedInstruction(decoder.extractSwap<std::uint32_t>());
    Utils::Log::trace("[PC=0x{:08X}]\t{}", ppc_pc, std::bitset<32>(encodedInstruction.raw).to_string());
    m_nextPc = m_pc + Core::INSTR_SIZE;
    try {
        executeInstruction(encodedInstruction);
        debugDumpGPR();
    } catch (Core::Exception &e) {
        Utils::Log::error("[PC=0x{:08X}] {}", ppc_pc, e.what());
        if (e.isFatal())
            return false;
    }
    return true;
}

void Core::Interpreter::run()
{
    auto instructionDecoder = Utils::BeDecoder(m_binary.m_memory.getMemory());
    std::uint32_t ppc_pc = m_binary.header.e_entry;

    m_pc = ppc_pc - Core::Memory::MemoryMap::ApplicationCode;
    Utils::Log::info("[EMU] Starting at entrypoint 0x{:08X}", ppc_pc);
    while (true) {
        ppc_pc = m_pc + Core::Memory::MemoryMap::ApplicationCode;
        if (!step(instructionDecoder, ppc_pc))
            break;
        m_pc = m_nextPc;
    }
    Utils::Log::info("[EMU] Exited. PC=0x{:08X}", ppc_pc);
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
    throw Core::InterpreterException("No instruction found with this fields. (opcode == " + std::to_string(instr.opcd) +
                                     ")");
}

void Core::Interpreter::executeInstruction(const EncodedInstruction &instr)
{
    InstructionID id = findInstructionID(instr);

    Utils::Log::trace("{}", instructionIDToString(id));
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
                                 const EncodedInstruction &instr, const bool forceUpdate) const
{
    if (instr.rc || forceUpdate) {
        cr.lt = result < 0;
        cr.gt = result > 0;
        cr.eq = result == 0;
        cr.so = m_xer.so;
    }
}

void Core::Interpreter::updateOverflow(const bool overflow, const EncodedInstruction &instr)
{
    if (!instr.oe)
        return;

    m_xer.ov = overflow;
    if (m_xer.ov)
        m_xer.so = true;
}

void Core::Interpreter::updateOverflow(const std::int32_t &a, const std::int32_t &b, const std::int32_t &result,
                                       const EncodedInstruction &instr)
{
    const bool aSign = a < 0;
    const bool bSign = b < 0;
    const bool resultSign = result < 0;
    const bool overflow = (aSign == bSign) && (aSign != resultSign);

    this->updateOverflow(overflow, instr);
}
