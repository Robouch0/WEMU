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
#include "utils/BeDecoder.hpp"

Core::Interpreter::Interpreter(Core::Binary binary) : m_binary(std::move(binary)) { initInstructionMap(); }

void Core::Interpreter::run()
{
    const Core::Section &textSection = m_binary.findSection(".text");
    auto instructionDecoder = Utils::BeDecoder(textSection.raw.data);

    std::cout << "Content of section " << textSection.name << std::endl;
    for (Elf32_Off offset = 0; offset < textSection.raw.header.sh_size; offset += 4) {
        const EncodedInstruction encodedInstruction(instructionDecoder.extractSwap<uint32_t>());
        std::cout << " " << std::hex << (offset + textSection.raw.header.sh_addr) << std::dec << "\t"
                << std::bitset<sizeof(uint32_t) * 8>(encodedInstruction.raw) << "\t";
        try {
            executeInstruction(encodedInstruction);
        } catch (Core::InterpreterException &e) {
            std::cout << e.what() << std::endl;
        }
    }
}

InstructionID Core::Interpreter::findInstructionID(const EncodedInstruction &instr)
{
    const auto &candidates = m_instructionMap[instr.opcd];

    for (const auto &[id, fields, _]: candidates) {
        const bool found = std::ranges::all_of(fields.begin(), fields.end(), [&](const auto &p) {
            const auto &[field, value] = p;
            switch (field) {
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
               ((carry == 1) && (aSign == resultSign) && (bSign == resultSign));;

    if (m_xer.ov)
        m_xer.so = true;
}
