/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Binary
*/

#pragma once

#include <string>
#include <vector>
#include <cinttypes>

#include "elf.h"
#include "exception/Exception.hpp"

namespace Core {
    class BinaryException final : public Core::Exception {
        public:
            explicit BinaryException(const std::string &errorMessage) : Core::Exception(
                "BinaryException", errorMessage) {}

            ~BinaryException() override = default;
    };

    struct Section {
        std::string name;
        Elf32_Shdr header;
        std::vector<char> data;
        std::size_t size;
        std::size_t address;
    };

    struct Symbol {
        std::string name;
        Elf32_Sym header;
        std::size_t index;
        std::size_t address;
    };

    struct Binary {
        Section &findSection(const std::string &name)
        {
            for (auto &section: sections) {
                if (section.name == name) {
                    return section;
                }
            }
            throw Core::BinaryException("Could not find section with specified name.");
        }

        Section &findSection(const std::uint32_t symbolAddress)
        {
            for (auto &section: sections) {
                if (symbolAddress >= section.address && symbolAddress < section.address + section.size) {
                    return section;
                }
            }
            throw Core::BinaryException(std::to_string(symbolAddress));
        }

        Elf32_Ehdr header;
        std::vector<Section> sections;
        std::vector<Symbol> symbols;
    };
} // namespace Core
