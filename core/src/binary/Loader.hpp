/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#pragma once

#include <cstdint>
#include <map>
#include <utility>

#include "Binary.hpp"
#include "Loader.hpp"
#include "utils/BeDecoder.hpp"

#define SHF_DEFLATED 0x08000000

namespace Core {
    class LoaderException final : public Core::Exception {
        public:
            explicit LoaderException(const std::string &errorMessage) : Core::Exception("LoaderException", errorMessage) {}

            ~LoaderException() override = default;
    };

    class Loader {
        public:
            explicit Loader(const std::string &filepath);

            // Inspect the loaded image without copying guest RAM. The view belongs to this loader.
            [[nodiscard]] const Binary &getBinary() const & noexcept { return m_bin; }
            const Binary &getBinary() const && = delete;
            // Consume the loaded image; its guest pointers remain stable across the transfer.
            [[nodiscard]] Binary takeBinary() && noexcept { return std::move(m_bin); }

        private:
            void loadHeader();

            void loadSections();
            void loadSectionsRaw();
            void loadSectionsName();
            void loadSectionsMeta();
            void loadSectionsInMemory() const;

            void loadSectionHeader(Section &section);
            void loadSectionData(Section &section);
            void loadAndDecompressSectionData(Section &section);

            void loadAddressRangeProgram(const Section &section, std::size_t start, unsigned long end);
            void loadAddressRangeImports(std::vector<Core::Section>::value_type &section, unsigned long end);

            void loadSymbols();
            void loadSymbolsRaw();
            void loadSymbolsName();
            void loadSymbolsMeta();
            void resolveSymbols();
            void resolveDataImports();

            // Parse the RPL FILEINFO section for the SDA base pointers (r13/r2).
            void loadFileInfo();

            // Relocations (SHT_RELA): patch loaded code/data with resolved symbol
            // addresses. Mandatory for real RPX files -- homebrew often links without
            // needing them, but Cafe-SDK titles like MK8 rely entirely on them.
            void loadRelocations();
            void applyRelaSection(const Section &relaSection);
            void applyRelocation(const Elf32_Rela &rela);

            static void loadSymbolHeader(Utils::BeDecoder &symDecoder, Core::Symbol &symbol);

            static void writeFunctionThunk(Core::Symbol &symbol, Core::Section &section);

            Binary m_bin;
            Utils::BeDecoder m_beDecoder;

            // Relocation statistics, filled by loadRelocations() and printed as a
            // summary so we can see at a glance how much of a real title we cover.
            std::size_t m_relocApplied = 0;
            std::map<std::uint32_t, std::size_t> m_relocUnhandled;

        public:
            std::pair<std::uint32_t, std::uint32_t> codeAddressRange;
            std::pair<std::uint32_t, std::uint32_t> dataAddressRange;
    };
} // namespace Core
