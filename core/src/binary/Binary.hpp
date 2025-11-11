/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Binary
*/

#pragma once

#include <string>
#include <vector>

#include "elf.h"
#include "exception/Exception.hpp"

namespace Core {

    class BinaryException final : public Core::Exception {
        public:
        explicit BinaryException(const std::string &errorMessage) : Core::Exception("BinaryException", errorMessage) {}
        ~BinaryException() override = default;
    };

    struct Section
    {
        std::string name;
        Elf32_Shdr header;
        std::vector<char> data;
    };

    struct Symbol
    {
        std::string name;
        Elf32_Sym header;
    };

    struct Binary
    {
        Section &findSection(const std::string &name)
        {
            for (auto &section : sections) {
                if (section.name == name) {
                    return section;
                }
            }
            throw Core::BinaryException("Could not find section with specified name.");
        }

        Elf32_Ehdr header;
        std::vector<Section> sections;
        std::vector<Symbol> symbols;
    };
}
