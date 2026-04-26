/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#include "Loader.hpp"

#include <bitset>
#include <cassert>
#include <iostream>
#include <cpu/types/InstructionID.hpp>

#include "zlib.h"
#include "elf.h"
#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"

namespace Core {
    class InterpreterException;
}

Core::Loader::Loader(const std::string &filepath) : m_bin({}), m_beDecoder(filepath)
{
    loadHeader();
    loadSections();
    loadSymbols();
    loadSectionsInMemory();
    std::cout << "FINAL 268437924 MEM -> " << std::hex << this->m_bin.m_memory.read<uint32_t>(268437924) << std::dec << std::endl;
}

void Core::Loader::loadHeader()
{
    for (unsigned char &i: m_bin.header.e_ident)
        i = m_beDecoder.extract<unsigned char>();
    m_bin.header.e_type = m_beDecoder.extractSwap<Elf32_Half>();
    m_bin.header.e_machine = m_beDecoder.extractSwap<Elf32_Half>();
    m_bin.header.e_version = m_beDecoder.extractSwap<Elf32_Word>();
    m_bin.header.e_entry = m_beDecoder.extractSwap<Elf32_Addr>();
    m_bin.header.e_phoff = m_beDecoder.extractSwap<Elf32_Off>();
    m_bin.header.e_shoff = m_beDecoder.extractSwap<Elf32_Off>();
    m_bin.header.e_flags = m_beDecoder.extractSwap<Elf32_Word>();
    m_bin.header.e_ehsize = m_beDecoder.extractSwap<Elf32_Half>();
    m_bin.header.e_phentsize = m_beDecoder.extractSwap<Elf32_Half>();
    m_bin.header.e_phnum = m_beDecoder.extractSwap<Elf32_Half>();
    m_bin.header.e_shentsize = m_beDecoder.extractSwap<Elf32_Half>();
    m_bin.header.e_shnum = m_beDecoder.extractSwap<Elf32_Half>();
    m_bin.header.e_shstrndx = m_beDecoder.extractSwap<Elf32_Half>();
}

void Core::Loader::loadSectionsRaw()
{
    m_bin.sections.resize(m_bin.header.e_shnum);
    for (size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        m_beDecoder.seek(m_bin.header.e_shoff + i * m_bin.header.e_shentsize);
        loadSectionHeader(section);
        m_beDecoder.seek(section.raw.header.sh_offset);
        if (section.raw.header.sh_flags & SHF_DEFLATED) {
            loadAndDecompressSectionData(section);
        } else {
            loadSectionData(section);
        }
    }
}

void Core::Loader::loadSectionsName()
{
    const Section &shStr = m_bin.sections[m_bin.header.e_shstrndx];
    char const *sectionNames = shStr.raw.data.data();

    for (std::size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        section.name = sectionNames + section.raw.header.sh_name;
    }
}

void Core::Loader::loadSectionHeader(Section &section)
{
    section.raw.header.sh_name = m_beDecoder.extractSwap<Elf32_Word>();
    section.raw.header.sh_type = m_beDecoder.extractSwap<Elf32_Word>();
    section.raw.header.sh_flags = m_beDecoder.extractSwap<Elf32_Word>();
    section.raw.header.sh_addr = m_beDecoder.extractSwap<Elf32_Addr>();
    section.raw.header.sh_offset = m_beDecoder.extractSwap<Elf32_Off>();
    section.raw.header.sh_size = m_beDecoder.extractSwap<Elf32_Word>();
    section.raw.header.sh_link = m_beDecoder.extractSwap<Elf32_Word>();
    section.raw.header.sh_info = m_beDecoder.extractSwap<Elf32_Word>();
    section.raw.header.sh_addralign = m_beDecoder.extractSwap<Elf32_Word>();
    section.raw.header.sh_entsize = m_beDecoder.extractSwap<Elf32_Word>();
}

void Core::Loader::loadSectionData(Section &section)
{
    section.raw.data.resize(section.raw.header.sh_size);
    m_beDecoder.extract<>(section.raw.data.data(), section.raw.header.sh_size);
}

void Core::Loader::loadAndDecompressSectionData(Section &section)
{
    auto stream = z_stream{};
    auto ret = Z_OK;
    section.raw.data.resize(m_beDecoder.extractSwap<uint32_t>());
    std::memset(&stream, 0, sizeof(stream));
    stream.zalloc = nullptr;
    stream.zfree = nullptr;
    stream.opaque = nullptr;
    ret = inflateInit(&stream);
    if (ret != Z_OK) {
        section.raw.data.clear();
    } else {
        stream.avail_in = section.raw.header.sh_size;
        stream.next_in = const_cast<unsigned char *>(m_beDecoder.extract<Bytef>(section.raw.header.sh_size));
        stream.avail_out = static_cast<uInt>(section.raw.data.size());
        stream.next_out = reinterpret_cast<Bytef *>(section.raw.data.data());
        ret = inflate(&stream, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            section.raw.data.clear();
        }
        inflateEnd(&stream);
    }
}

void Core::Loader::loadAddressRangeProgram(const Section &section, const std::size_t start, const unsigned long end)
{
    if (section.raw.header.sh_flags & SHF_EXECINSTR) {
        if (codeAddressRange.first == 0 || start < codeAddressRange.first)
            codeAddressRange.first = start;
        if (codeAddressRange.second == 0 || end > codeAddressRange.second)
            codeAddressRange.second = end;
    } else {
        if (dataAddressRange.first == 0 || start < dataAddressRange.first)
            dataAddressRange.first = start;
        if (dataAddressRange.second == 0 || end > dataAddressRange.second)
            dataAddressRange.second = end;
    }
}

void Core::Loader::loadAddressRangeImports(Core::Section &section, const unsigned long end)
{
    if (section.raw.header.sh_flags & SHF_EXECINSTR) {
        section.meta.virtAddress = (codeAddressRange.second + section.raw.header.sh_addralign) & ~section.raw.header.sh_addralign;
        codeAddressRange.second = end;
    } else {
        section.meta.virtAddress = (dataAddressRange.second + section.raw.header.sh_addralign) & ~section.raw.header.sh_addralign;
        dataAddressRange.second = end;
    }
}

void Core::Loader::loadSectionsMeta()
{
    for (std::size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        section.meta.virtAddress = section.raw.header.sh_addr;
        section.meta.size = section.raw.data.size();
        section.meta.type = section.raw.header.sh_type;
        const auto start = section.meta.virtAddress;
        const auto end = section.meta.virtAddress + section.meta.size;

        if (section.meta.type == SHT_NOBITS)
            section.meta.size = section.raw.header.sh_size;
        if (section.meta.type == SHT_PROGBITS || section.meta.type == SHT_NOBITS)
            loadAddressRangeProgram(section, start, end);
        if (section.meta.type == SHT_RPL_IMPORTS)
            loadAddressRangeImports(section, end);
    }
}

void Core::Loader::loadSectionsInMemory()
{
    int frerot = 0;

    for (auto &section : m_bin.sections) {
        if (!(section.raw.header.sh_flags & SHF_ALLOC))
            continue;
        if (section.meta.type == SHT_NOBITS) {
            const std::size_t ptr = m_bin.m_memory.allocate(section.meta.virtAddress, section.meta.size);

            if (ptr) {
                memset(reinterpret_cast<void *>(ptr), 0, section.meta.size);
            }
        } else {
            const std::size_t ptr = m_bin.m_memory.allocate(section.meta.virtAddress, section.meta.size);

            if (ptr) {
                char *veritableptr = section.raw.data.data();
                fprintf(stdout, "Writing %llu {", section.meta.virtAddress);
                fprintf(stdout, "} Writing\n");
                memcpy(reinterpret_cast<void *>(ptr), section.raw.data.data(), section.meta.size);
            } else {
                fprintf(stdout, "\n %d Euuuhh j'suis où frérot ?\n", frerot++);
            }
            std::cout << "268437924 MEM -> " << std::hex << this->m_bin.m_memory.read<uint32_t>(268437924) << std::dec << std::endl;
        }
    }
}

void Core::Loader::loadSections()
{
    loadSectionsRaw();
    loadSectionsName();
    loadSectionsMeta();
}

void Core::Loader::loadSymbolHeader(Utils::BeDecoder &symDecoder, Core::Symbol &symbol)
{
    symbol.raw.header.st_name = symDecoder.extractSwap<Elf32_Word>();
    symbol.raw.header.st_value = symDecoder.extractSwap<Elf32_Addr>();
    symbol.raw.header.st_size = symDecoder.extractSwap<Elf32_Word>();
    symbol.raw.header.st_info = symDecoder.extractSwap<unsigned char>();
    symbol.raw.header.st_other = symDecoder.extractSwap<unsigned char>();
    symbol.raw.header.st_shndx = symDecoder.extractSwap<Elf32_Section>();
}

void Core::Loader::loadSymbolsMeta()
{
    for (auto &symbol : m_bin.symbols) {
        symbol.meta.type = ELF32_ST_TYPE(symbol.raw.header.st_info);
        if (symbol.raw.header.st_shndx >= m_bin.sections.size())
            continue;
        const auto &section = m_bin.sections[symbol.raw.header.st_shndx];
        const std::size_t offset = symbol.raw.header.st_value - section.raw.header.sh_addr;
        symbol.meta.virtAddress = section.meta.virtAddress + offset;
    }
}

void Core::Loader::writeFunctionThunk(Core::Symbol &symbol, Core::Section &section)
{
    EncodedInstruction syscall(0);
    syscall.opcd = INSTRUCTIONARRAY[InstructionID::E_SC].OPCODE;
    syscall.bd = symbol.meta.index;

    const std::uint32_t offset = symbol.meta.virtAddress - section.meta.virtAddress;
    char *sectionData = section.raw.data.data();
    EncodedInstruction syscallEndianSwapped = syscall.endianSwap();
    std::memcpy(sectionData + offset, &syscallEndianSwapped, sizeof(EncodedInstruction));
}

void Core::Loader::resolveSymbols()
{
    for (auto &symbol : m_bin.symbols) {
        if (symbol.raw.header.st_shndx >= m_bin.sections.size())
            continue;
        auto &section = m_bin.sections[symbol.raw.header.st_shndx];
        if (section.meta.type != SHT_RPL_IMPORTS)
            continue;
        if (section.raw.header.sh_flags & SHF_EXECINSTR) {
            if (symbol.meta.type == STT_FUNC)
                writeFunctionThunk(symbol, section);
        }
    }
}

void Core::Loader::loadSymbols()
{
    loadSymbolsRaw();
    loadSymbolsName();
    loadSymbolsMeta();
    resolveSymbols();
}

void Core::Loader::loadSymbolsRaw()
{
    const auto &symSection = m_bin.findSection(".symtab");
    Utils::BeDecoder symDecoder(symSection.raw.data);
    const std::size_t symAmount = symSection.raw.data.size() / symSection.raw.header.sh_entsize;

    m_bin.symbols.resize(symAmount);
    for (std::uint32_t i = 0; i < symAmount; i++) {
        auto &symbol = m_bin.symbols[i];
        loadSymbolHeader(symDecoder, symbol);
        symbol.meta.index = i;
    }
}

void Core::Loader::loadSymbolsName()
{
    const Section &symStr = m_bin.findSection(".strtab");
    char const *symbolNames = symStr.raw.data.data();

    for (auto &symbol : m_bin.symbols) {
        if (symbol.raw.header.st_name > 0 && symbol.raw.header.st_name < symStr.raw.data.size()) {
            symbol.name = symbolNames + symbol.raw.header.st_name;
        }
    }
}
