/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** main
*/

#include <iostream>
#include <fstream>

#include "binary/Binary.hpp"
#include "utils/BeDecoder.hpp"

void print_elf32_ehdr(const Elf32_Ehdr &ehdr)
{
    dprintf(1, "ELF header:\n");
    dprintf(1, "e_ident\t\t%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
        ehdr.e_ident[0], ehdr.e_ident[1], ehdr.e_ident[2], ehdr.e_ident[3],
        ehdr.e_ident[4], ehdr.e_ident[5], ehdr.e_ident[6], ehdr.e_ident[7],
        ehdr.e_ident[8], ehdr.e_ident[9], ehdr.e_ident[10], ehdr.e_ident[11],
        ehdr.e_ident[12], ehdr.e_ident[13], ehdr.e_ident[14], ehdr.e_ident[15]);
    dprintf(1, "e_type\t\t0x%04x [%s]\n", ehdr.e_type, (ehdr.e_type == 0xFE01) ? "RPL" : "UNKNOWN");
    dprintf(1, "e_machine\t0x%04x [%s]\n", ehdr.e_machine, (ehdr.e_machine == 0x0014) ? "PowerPC" : "UNKNOWN");
    dprintf(1, "e_version\t0x%08x\n", ehdr.e_version);
    dprintf(1, "e_entry\t\t0x%08x\n", ehdr.e_entry);
    dprintf(1, "e_phoff\t\t0x%08x\n", ehdr.e_phoff);
    dprintf(1, "e_shoff\t\t0x%08x\n", ehdr.e_shoff);
    dprintf(1, "e_flags\t\t0x%08x\n", ehdr.e_flags);
    dprintf(1, "e_ehsize\t0x%04x\n", ehdr.e_ehsize);
    dprintf(1, "e_phentsize\t0x%04x\n", ehdr.e_phentsize);
    dprintf(1, "e_phnum\t\t0x%04x\n", ehdr.e_phnum);
    dprintf(1, "e_shentsize\t0x%04x\n", ehdr.e_shentsize);
    dprintf(1, "e_shnum\t\t0x%04x\n", ehdr.e_shnum);
    dprintf(1, "e_shstrndx\t0x%04x\n", ehdr.e_shstrndx);
    dprintf(1, "\n");
}

int main(int ac, char const* const *av)
{
    if (ac != 2) {
        std::cerr << "Invalid arguments." << std::endl;
        return ERROR_VALUE;
    }

    std::ifstream file(av[1], std::ios::binary | std::ios::ate);
    std::vector<char> buffer;

    if (!file.is_open()) {
        std::cerr << "File could not open." << std::endl;
        return ERROR_VALUE;
    }

    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    buffer.resize(size);

    if (file.read(buffer.data(), size)) {
        Core::Binary bin;
        Utils::BeDecoder be(buffer.data());

        for (unsigned char &i : bin.header.e_ident)
            i = be.extract<unsigned char>();
        bin.header.e_type = be.extractSwap<Elf32_Half>();
        bin.header.e_machine = be.extractSwap<Elf32_Half>();
        bin.header.e_version = be.extractSwap<Elf32_Word>();
        bin.header.e_entry = be.extractSwap<Elf32_Addr>();
        bin.header.e_phoff = be.extractSwap<Elf32_Off>();
        bin.header.e_shoff = be.extractSwap<Elf32_Off>();
        bin.header.e_flags = be.extractSwap<Elf32_Word>();
        bin.header.e_ehsize = be.extractSwap<Elf32_Half>();
        bin.header.e_phentsize = be.extractSwap<Elf32_Half>();
        bin.header.e_phnum = be.extractSwap<Elf32_Half>();
        bin.header.e_shentsize = be.extractSwap<Elf32_Half>();
        bin.header.e_shnum = be.extractSwap<Elf32_Half>();
        bin.header.e_shstrndx = be.extractSwap<Elf32_Half>();
        print_elf32_ehdr(bin.header);
    } else {
        std::cerr << "Could not read the file." << std::endl;
    }
    file.close();
    return SUCCESS_VALUE;
}
