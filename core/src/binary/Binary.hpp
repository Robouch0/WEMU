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
        struct RawSection {
            Elf32_Shdr header;
            std::vector<char> data;
        } raw;
        struct SectionMeta {
            std::size_t size;
            std::size_t address;
            std::uint32_t type;
        } meta;
    };

    struct Symbol {
        std::string name;
        struct RawSymbol {
            Elf32_Sym header;
        } raw;
        struct SymbolMeta {
            std::size_t index;
            std::size_t address;
            std::uint32_t type;
        } meta;
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
                if (symbolAddress >= section.meta.address && symbolAddress < section.meta.address + section.meta.size) {
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
