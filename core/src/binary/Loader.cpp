/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#include "Loader.hpp"

#include <bitset>
#include <cassert>
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

void Core::Loader::loadSectionsName()
{
    const Section &shStr = m_bin.sections[m_bin.header.e_shstrndx];
    char const *sectionNames = shStr.raw.data.data();

    for (std::size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        section.name = sectionNames + section.raw.header.sh_name;
    }
}

void Core::Loader::updateAddressRangeProgram(const Section &section, const std::size_t start, const unsigned long end)
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

void Core::Loader::updatAddressRangeImports(Core::Section &section, const unsigned long end)
{
    if (section.raw.header.sh_flags & SHF_EXECINSTR) {
        // section.address = (codeAddressRange.second + section.raw.header.sh_addralign) & ~section.raw.header.sh_addralign;
        codeAddressRange.second = end;
    } else {
        // section.address = (dataAddressRange.second + section.raw.header.sh_addralign) & ~section.raw.header.sh_addralign;
        dataAddressRange.second = end;
    }
}

void Core::Loader::updateSectionsAddress()
{
    for (std::size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        section.meta.address = section.raw.header.sh_addr;
        section.meta.size = section.raw.data.size();
        const auto start = section.meta.address;
        const auto end = section.meta.address + section.meta.size;

        if (section.raw.header.sh_type == SHT_NOBITS)
            section.meta.size = section.raw.header.sh_size;
        if (!(section.raw.header.sh_type != SHT_PROGBITS && section.raw.header.sh_type != SHT_NOBITS))
            updateAddressRangeProgram(section, start, end);
        if (section.raw.header.sh_type == SHT_RPL_IMPORTS)
            updatAddressRangeImports(section, end);
    }
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

void Core::Loader::loadSections()
{
    loadSectionsRaw();
    loadSectionsName();
    updateSectionsAddress();
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

void Core::Loader::writeFunctionThunk(Core::Symbol &symbol, Core::Section &section)
{
    EncodedInstruction syscall(0);
    syscall.opcd = INSTRUCTIONARRAY[InstructionID::E_SC].OPCODE;
    syscall.bd = symbol.meta.index;

    const std::uint32_t offset = symbol.meta.address - section.meta.address;
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
        const std::size_t offset = symbol.raw.header.st_value - section.raw.header.sh_addr;
        symbol.meta.address = section.meta.address + offset;
        if (!symbol.meta.address)
            continue;
        const auto type = ELF32_ST_TYPE(symbol.raw.header.st_info);
        if (type == STT_FUNC)
            writeFunctionThunk(symbol, section);
    }
}


void Core::Loader::loadSymbols()
{
    loadSymbolsRaw();
    loadSymbolsName();
    resolveSymbols();
}

void Core::Loader::loadSymbolsRaw()
{
    const auto &symSection = m_bin.findSection(".symtab");
    Utils::BeDecoder symDecoder(symSection.raw.data);
    const std::size_t symAmount = symSection.raw.header.sh_size / symSection.raw.header.sh_entsize;

    m_bin.symbols.resize(symAmount);
    for (std::uint32_t i = 0; i < symAmount; i++) {
        auto &symbol = m_bin.symbols[i];
        loadSymbolHeader(symDecoder, symbol);
        symbol.meta.index = i;
    }
}
