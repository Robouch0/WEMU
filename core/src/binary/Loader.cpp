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
    section.header.sh_name = m_beDecoder.extractSwap<Elf32_Word>();
    section.header.sh_type = m_beDecoder.extractSwap<Elf32_Word>();
    section.header.sh_flags = m_beDecoder.extractSwap<Elf32_Word>();
    section.header.sh_addr = m_beDecoder.extractSwap<Elf32_Addr>();
    section.header.sh_offset = m_beDecoder.extractSwap<Elf32_Off>();
    section.header.sh_size = m_beDecoder.extractSwap<Elf32_Word>();
    section.header.sh_link = m_beDecoder.extractSwap<Elf32_Word>();
    section.header.sh_info = m_beDecoder.extractSwap<Elf32_Word>();
    section.header.sh_addralign = m_beDecoder.extractSwap<Elf32_Word>();
    section.header.sh_entsize = m_beDecoder.extractSwap<Elf32_Word>();
}

void Core::Loader::loadSectionData(Section &section)
{
    section.data.resize(section.header.sh_size);
    m_beDecoder.extract<>(section.data.data(), section.header.sh_size);
}

void Core::Loader::loadAndDecompressSectionData(Section &section)
{
    auto stream = z_stream{};
    auto ret = Z_OK;
    section.data.resize(m_beDecoder.extractSwap<uint32_t>());
    std::memset(&stream, 0, sizeof(stream));
    stream.zalloc = nullptr;
    stream.zfree = nullptr;
    stream.opaque = nullptr;
    ret = inflateInit(&stream);
    if (ret != Z_OK) {
        section.data.clear();
    } else {
        stream.avail_in = section.header.sh_size;
        stream.next_in = const_cast<unsigned char *>(m_beDecoder.extract<Bytef>(section.header.sh_size));
        stream.avail_out = static_cast<uInt>(section.data.size());
        stream.next_out = reinterpret_cast<Bytef *>(section.data.data());
        ret = inflate(&stream, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            section.data.clear();
        }
        inflateEnd(&stream);
    }
}

void Core::Loader::loadSectionName()
{
    const Section &shStr = m_bin.sections[m_bin.header.e_shstrndx];
    char const *sectionNames = shStr.data.data();

    for (std::size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        section.name = sectionNames + section.header.sh_name;
    }
}

void Core::Loader::loadSectionInfos()
{
    // load classic program sections infos
    for (std::size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        if (section.header.sh_type != SHT_PROGBITS && section.header.sh_type != SHT_NOBITS) {
            continue;
        }
        section.address = section.header.sh_addr;
        section.size = section.data.size();
        if (section.header.sh_type == SHT_NOBITS) {
            section.size = section.header.sh_size;
        }
        const auto start = section.address;
        const auto end = section.address + section.size;
        if (section.header.sh_flags & SHF_EXECINSTR) {
            if (codeAddressRange.first == 0 || start < codeAddressRange.first) {
                codeAddressRange.first = start;
            }
            if (codeAddressRange.second == 0 || end > codeAddressRange.second) {
                codeAddressRange.second = end;
            }
        } else {
            if (dataAddressRange.first == 0 || start < dataAddressRange.first) {
                dataAddressRange.first = start;
            }
            if (dataAddressRange.second == 0 || end > dataAddressRange.second) {
                dataAddressRange.second = end;
            }
        }
    }

    // load rpl imports sections
    for (std::size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        if (section.header.sh_type != SHT_RPL_IMPORTS) {
            continue;
        }
        section.size = section.data.size();
        if (section.header.sh_type == SHT_NOBITS) {
            section.size = section.header.sh_size;
        }
        section.address = section.header.sh_addr;
        assert(section.header.sh_addr & 0xC0000000);
        if (section.header.sh_flags & SHF_EXECINSTR) {
            // section.address = (codeAddressRange.second + section.header.sh_addralign) & ~section.header.sh_addralign;
            codeAddressRange.second = section.address + section.size;
        } else {
            // section.address = (dataAddressRange.second + section.header.sh_addralign) & ~section.header.sh_addralign;
            dataAddressRange.second = section.address + section.size;
        }
    }
}

void Core::Loader::loadSections()
{
    m_bin.sections.resize(m_bin.header.e_shnum);
    for (size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        m_beDecoder.seek(m_bin.header.e_shoff + i * m_bin.header.e_shentsize);
        loadSectionHeader(section);
        m_beDecoder.seek(section.header.sh_offset);
        if (section.header.sh_flags & SHF_DEFLATED) {
            loadAndDecompressSectionData(section);
        } else {
            loadSectionData(section);
        }
    }
    loadSectionName();
    loadSectionInfos();
}

void Core::Loader::loadSymbolHeader(const Section &symSection, const std::size_t symAmount)
{
    auto symDecoder = Utils::BeDecoder(symSection.data);

    for (std::size_t i = 0; i < symAmount; i++) {
        auto &symbol = m_bin.symbols[i];
        symbol.header.st_name = symDecoder.extractSwap<Elf32_Word>();
        symbol.header.st_value = symDecoder.extractSwap<Elf32_Addr>();
        symbol.header.st_size = symDecoder.extractSwap<Elf32_Word>();
        symbol.header.st_info = symDecoder.extractSwap<unsigned char>();
        symbol.header.st_other = symDecoder.extractSwap<unsigned char>();
        symbol.header.st_shndx = symDecoder.extractSwap<Elf32_Section>();
        symbol.index = i;
    }
}

void Core::Loader::loadSymbolName(const Section &symSection)
{
    const Section &symStr = m_bin.sections[symSection.header.sh_link];
    char const *symbolNames = symStr.data.data();
    const std::size_t symAmount = symSection.header.sh_size / symSection.header.sh_entsize;

    for (std::size_t i = 0; i < symAmount; i++) {
        auto &symbol = m_bin.symbols[i];
        if (symbol.header.st_name > 0 && symbol.header.st_name < symStr.data.size()) {
            symbol.name = symbolNames + symbol.header.st_name;
        }
    }
}

void Core::Loader::writeFunctionThunk(Core::Symbol &symbol, Core::Section &section)
{
    EncodedInstruction sc(0);
    sc.opcd = INSTRUCTIONARRAY[InstructionID::E_SC].matchFields[0].second;
    sc.bd = symbol.index;

    std::vector<char> scData;

    scData.resize(32);
    char *tmp = scData.data();
    std::memcpy(tmp, &sc.raw, sizeof(EncodedInstruction));

    Utils::BeDecoder scBE(scData);
    const std::uint32_t offset = symbol.address - section.address;
    char *sectionData = section.data.data();
    const auto scEndiannessSwapped = scBE.extractSwap<std::uint32_t>();
    std::memcpy(sectionData + offset, &scEndiannessSwapped, sizeof(EncodedInstruction));
}

void Core::Loader::loadSymbols()
{
    const auto &symSection = m_bin.findSection(".symtab");
    const std::size_t symAmount = symSection.header.sh_size / symSection.header.sh_entsize;

    m_bin.symbols.resize(symAmount);
    loadSymbolHeader(symSection, symAmount);
    loadSymbolName(symSection);

    for (auto &symbol : m_bin.symbols) {
        if (symbol.header.st_shndx >= m_bin.sections.size()) {
            continue;
        }
        auto &section = m_bin.sections[symbol.header.st_shndx];
        std::size_t offset = symbol.header.st_value - section.header.sh_addr;
        std::size_t virtAddress = section.address + offset;

        symbol.address = virtAddress;

        if (symbol.address) {
            auto type = symbol.header.st_info & 0xF;
            if (type == STT_FUNC)
                writeFunctionThunk(symbol, section);
        }
    }
}
