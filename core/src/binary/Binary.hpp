/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Binary
*/

#pragma once

#include <string>
#include <vector>

#include "BinaryException.hpp"
#include "elf.h"

namespace Core {

    struct Section
    {
        std::string name;
        Elf32_Shdr header;
        std::vector<char> data;
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
    };
}
