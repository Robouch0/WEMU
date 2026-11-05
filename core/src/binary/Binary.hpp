/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Binary
*/

#pragma once

#include <cinttypes>
#include <cstring>
#include <string>
#include <vector>

#include "cpu/memory/Memory.hpp"
#include "elf.h"
#include "exception/Exception.hpp"

namespace Core {
    class BinaryException final : public Core::Exception {
        public:
            explicit BinaryException(const std::string &errorMessage) : Core::Exception("BinaryException", errorMessage) {}

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
                    std::size_t virtAddress;
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
                    std::size_t virtAddress;
                    std::uint32_t type;
            } meta;
    };

    struct Binary {
            // Export names are offsets into the export section; function addresses may have
            // been relocated in guest memory. Do not depend on optional debug symbols.
            [[nodiscard]] std::uint32_t functionExport(const std::string &name) const
            {
                for (const auto &section: sections) {
                    if (section.meta.type != SHT_RPL_EXPORTS || !(section.raw.header.sh_flags & SHF_EXECINSTR))
                        continue;
                    const auto &data = section.raw.data;
                    const auto word = [&](std::size_t offset) {
                        std::uint32_t value;
                        std::memcpy(&value, data.data() + offset, sizeof(value));
                        return std::byteswap(value);
                    };
                    if (data.size() < 8)
                        continue;
                    const auto count = word(0);
                    if (count > (data.size() - 8) / 8)
                        continue;
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto offset = 8 + i * 8;
                        const auto nameOffset = word(offset + 4) & 0x7FFFFFFFu;
                        if (nameOffset >= data.size() || name.size() >= data.size() - nameOffset ||
                            std::memcmp(data.data() + nameOffset, name.c_str(), name.size() + 1) != 0)
                            continue;
                        const auto slot = static_cast<std::uint32_t>(section.meta.virtAddress + offset);
                        const auto address = m_memory.hostPtr(slot, 4) ? m_memory.read<std::uint32_t>(slot) : word(offset);
                        for (const auto &code: sections)
                            if ((code.raw.header.sh_flags & SHF_EXECINSTR) && code.meta.type == SHT_PROGBITS && !(address & 3) &&
                                address >= code.meta.virtAddress && address - code.meta.virtAddress < code.meta.size)
                                return address;
                    }
                }
                return 0;
            }

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
                    if (symbolAddress >= section.meta.virtAddress && symbolAddress < section.meta.virtAddress + section.meta.size) {
                        return section;
                    }
                }
                throw Core::BinaryException(std::to_string(symbolAddress));
            }

            Elf32_Ehdr header;
            std::vector<Section> sections;
            std::vector<Symbol> symbols;
            // Owns guest RAM; the loaded image is moved into the interpreter rather than copied.
            Core::Memory m_memory{};

            // Small-data-area base pointers from the RPL FILEINFO section. The Wii U loader places
            // these in r13 (sdaBase) and r2 (sda2Base) before entering the module; code compiled
            // with -msdata accesses globals relative to them, so they must be set or the entry's
            // control flow reads garbage.
            std::uint32_t sdaBase{0}; // -> r13
            std::uint32_t sda2Base{0}; // -> r2
    };
} // namespace Core
