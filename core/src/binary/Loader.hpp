/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#pragma once

#include "Binary.hpp"
#include "utils/BeDecoder.hpp"
#include "Loader.hpp"

namespace Core {

    class LoaderException final : public Core::Exception {
        public:
        explicit LoaderException(const std::string &errorMessage) : Core::Exception("LoaderException", errorMessage) {}
        ~LoaderException() override = default;
    };

    class Loader
    {
        public:
            explicit Loader(const std::string &filepath);

            [[nodiscard]] Binary getBinary() const noexcept { return m_bin; }

        private:
            void loadHeader();
            void loadSections();

            Binary m_bin;
            Utils::BeDecoder m_beDecoder;
    };
}
