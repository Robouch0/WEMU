/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#pragma once

#include "Binary.hpp"
#include "utils/BeDecoder.hpp"

#define SHF_DEFLATED 0x08000000
#define SHT_RPL_EXPORTS  0x80000001
#define SHT_RPL_IMPORTS  0x80000002
#define SHT_RPL_CRCS     0x80000003
#define SHT_RPL_FILEINFO 0x80000004

namespace Core {
    class LoaderException final : public Core::Exception {
        public:
            explicit LoaderException(const std::string &errorMessage) : Core::Exception(
                "LoaderException", errorMessage) {}

            ~LoaderException() override = default;
    };

    class Loader {
        public:
            explicit Loader(const std::string &filepath);

            [[nodiscard]] Binary getBinary() const noexcept { return m_bin; }

        public:
            std::pair<std::uint32_t, std::uint32_t> codeAddressRange;
            std::pair<std::uint32_t, std::uint32_t> dataAddressRange;

        private:
            void loadHeader();

            void loadSections();
            void loadSectionsRaw();
            void loadSectionsName();
            void loadSectionsMeta();
            void loadSectionsInMemory();

            void loadSectionHeader(Section &section);
            void loadSectionData(Section &section);
            void loadAndDecompressSectionData(Section &section);

            void loadAddressRangeProgram(const Section &section, std::size_t start, unsigned long end);
            void loadAddressRangeImports(Section &section, unsigned long end);

            void loadSymbols();
            void loadSymbolsRaw();
            void loadSymbolsName();
            void loadSymbolsMeta();
            void resolveSymbols();

            static void loadSymbolHeader(Utils::BeDecoder &symDecoder, Core::Symbol &symbol);
            static void writeFunctionThunk(Core::Symbol &symbol, Core::Section &section);

            Binary m_bin;
            Utils::BeDecoder m_beDecoder;
    };
} // namespace Core
