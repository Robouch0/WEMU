/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#include <algorithm>
#include <utility>
#include <bitset>

#include "Interpreter.hpp"
#include "utils/BeDecoder.hpp"

Core::Interpreter::Interpreter(Core::Binary binary) : m_binary(std::move(binary)), m_gpRegisters{{}}, m_fpRegisters{{}}
{
    initInstructionMap();
}

void Core::Interpreter::run()
{
    const Core::Section &textSection = m_binary.findSection(".text");
    auto instructionDecoder = Utils::BeDecoder(textSection.data);

    std::cout << "Content of section " << textSection.name << std::endl;
    for (Elf32_Off offset = 0; offset < textSection.header.sh_size; offset += 4) {
        const EncodedInstruction encodedInstruction(instructionDecoder.extractSwap<uint32_t>());
        std::cout << " "
            << std::hex << (offset + textSection.header.sh_addr) << std::dec << "\t"
            << std::bitset<sizeof(uint32_t) * 8>(encodedInstruction.m_raw) << "\t";
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

    for (const auto &[id, fields, _] : candidates) {
        const bool found = std::ranges::all_of(fields.begin(), fields.end(), [&](const auto &p) {
            const auto &[field, value] = p;
            switch (field) {
                case Field::F_XO10: return instr.xo10 == value;
                case Field::F_XO9:  return instr.xo9  == value;
                case Field::F_OPCD: return true;
                default:
                    throw Core::InterpreterException("Unknown field.");
                    return false;
            }
        });
        if (found)
            return id;
    }
    throw Core::InterpreterException("No instruction found with this fields. (opcode == " + std::to_string(instr.opcd) + ")");
}

void Core::Interpreter::executeInstruction(const EncodedInstruction &instr)
{
    INSTRUCTIONARRAY[findInstructionID(instr)].function(*this, instr);
}

void Core::Interpreter::initInstructionMap()
{
    for (auto &instrInfo : INSTRUCTIONARRAY) {
        const auto opcode = instrInfo.matchFields[0].second;
        if (m_instructionMap.contains(opcode)) {
            m_instructionMap[opcode].push_back(instrInfo);
        } else {
            m_instructionMap.emplace(opcode, std::vector {instrInfo});
        }
    }
}
