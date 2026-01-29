/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#pragma once

#include "Binary.hpp"
#include "Loader.hpp"
#include "utils/BeDecoder.hpp"

#define SHF_DEFLATED 0x08000000

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

        private:
            void loadHeader();

            void loadSections();
            void loadSectionHeader(Section &section);
            void loadSectionData(Section &section);
            void loadAndDecompressSectionData(Section &section);
            void loadSectionName();

            void loadSymbols();
            void loadSymbolHeader(const Section &symSection, std::size_t symAmount);
            void loadSymbolName(const Section &symSection);

            Binary m_bin;
            Utils::BeDecoder m_beDecoder;
    };
} // namespace Core
