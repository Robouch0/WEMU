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
#include <format>
#include <iostream>

#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"
#include "elf.h"
#include "utils/Logger.hpp"
#include "zlib.h"

namespace Core {
    class InterpreterException;
}

Core::Loader::Loader(const std::string &filepath) : m_bin({}), m_beDecoder(filepath)
{
    loadHeader();
    loadSections();
    loadSymbols();
    loadSectionsInMemory();
    loadRelocations();
    resolveDataImports();
    loadFileInfo();
}

// Data imports (.dimport_* sections: SHT_RPL_IMPORTS, not executable) are pointer-sized slots that
// the OS RPL loader would fill with the address of the exported object in the target library. The
// Cafe SDK exports several functions *as data pointers* (e.g. MEMAllocFromDefaultHeap[Ex]); a title
// calls them by loading the slot into a register, then `mtctr`/`bctrl`. We populate each slot with
// the symbol's import sentinel (st_value, always >= 0xC0000000); BCTR recognises those sentinels and
// dispatches to the matching HLE handler. Without this the slot stays zero and the indirect call
// jumps to NULL. Must run after loadSectionsInMemory (which maps/zeroes the slot) so it isn't clobbered.
void Core::Loader::resolveDataImports()
{
    for (const auto &symbol: m_bin.symbols) {
        if (symbol.raw.header.st_shndx >= m_bin.sections.size())
            continue;
        const auto &section = m_bin.sections[symbol.raw.header.st_shndx];
        if (section.meta.type != SHT_RPL_IMPORTS)
            continue;
        if (section.raw.header.sh_flags & SHF_EXECINSTR)
            continue; // function imports (.fimport_*) use sc thunks instead
        if (symbol.meta.type != STT_OBJECT)
            continue;
        // Only fill real data slots that the title actually maps and reads (the application data
        // window). Some import-section symbols resolve into the unmapped sentinel range (0xC000xxxx)
        // -- those are not addressable slots, so skip them rather than fault on the write.
        const auto slot = static_cast<std::uint32_t>(symbol.meta.virtAddress);
        if (slot < Memory::MemoryMap::ApplicationData || slot >= Memory::MemoryMap::ApplicationMemoryEnd)
            continue;
        m_bin.m_memory.write<std::uint32_t>(slot, symbol.raw.header.st_value);
        Utils::Log::debug("[LOADER] data-import slot 0x{:08X} = 0x{:08X} ({})", slot, symbol.raw.header.st_value, symbol.name);
    }
}

void Core::Loader::loadFileInfo()
{
    for (const auto &section: m_bin.sections) {
        if (section.meta.type != SHT_RPL_FILEINFO)
            continue;
        // RPL FileInfo layout: sdaBase at byte 0x24, sda2Base at 0x28 (big-endian).
        Utils::BeDecoder decoder(section.raw.data);
        if (section.raw.data.size() < 0x2C)
            return;
        decoder.seek(0x24);
        m_bin.sdaBase = decoder.extractSwap<std::uint32_t>();
        m_bin.sda2Base = decoder.extractSwap<std::uint32_t>();
        Utils::Log::info("[LOADER] SDA bases: r13=0x{:08X} r2=0x{:08X}", m_bin.sdaBase, m_bin.sda2Base);
        return;
    }
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
    if (section.raw.header.sh_flags & SHF_EXECINSTR) { // NOLINT(bugprone-branch-clone)
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

// Import sections (.fimport_*/.dimport_*) carry sentinel sh_addr values in the 0xC000xxxx range, so
// we relocate them to sit right after the real code (.fimport, executable) or data (.dimport) so that
// REL24 call sites in .text can still reach the import thunks within branch range. The running range
// pointer must advance by the *placed* size, not the section's original sh_addr+size end -- otherwise
// it jumps into the sentinel range and every subsequent import section lands at a bogus address.
void Core::Loader::loadAddressRangeImports(Core::Section &section, const unsigned long /*end*/)
{
    const std::uint32_t align = section.raw.header.sh_addralign ? section.raw.header.sh_addralign : 4;
    std::pair<std::uint32_t, std::uint32_t> &range = (section.raw.header.sh_flags & SHF_EXECINSTR) ? codeAddressRange : dataAddressRange;
    const std::uint32_t placed = (range.second + (align - 1)) & ~(align - 1);
    section.meta.virtAddress = placed;
    range.second = placed + static_cast<std::uint32_t>(section.meta.size);
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

void Core::Loader::loadSectionsInMemory() const
{
    for (auto &section: m_bin.sections) {
        if (!(section.raw.header.sh_flags & SHF_ALLOC))
            continue;
        if (section.meta.type == SHT_NOBITS) {
            const std::size_t ptr = m_bin.m_memory.allocate(section.meta.virtAddress, section.meta.size);

            if (ptr) {
                memset(reinterpret_cast<void *>(ptr), 0, section.meta.size); // NOLINT(performance-no-int-to-ptr)
            }
        } else {
            const std::size_t ptr = m_bin.m_memory.allocate(section.meta.virtAddress, section.meta.size);

            if (ptr) {
                memcpy(reinterpret_cast<void *>(ptr), section.raw.data.data(), section.meta.size); // NOLINT(performance-no-int-to-ptr)
            }
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
    for (auto &symbol: m_bin.symbols) {
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
    for (auto &symbol: m_bin.symbols) {
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

    for (auto &symbol: m_bin.symbols) {
        if (symbol.raw.header.st_name > 0 && symbol.raw.header.st_name < symStr.raw.data.size()) {
            symbol.name = symbolNames + symbol.raw.header.st_name;
        }
    }
}

// ---------------------------------------------------------------------------
// Relocations
//
// Real Cafe-SDK titles (e.g. MK8) ship every cross-section reference -- branches
// to other functions, pointers in data, and crucially the call sites that target
// imported library functions -- as SHT_RELA entries that must be applied after
// the sections are mapped and the import thunks are written. Without this pass
// every absolute pointer and inter-module call lands on a placeholder value and
// the CPU diverges into garbage immediately. (Most one-RPX homebrew links such
// that it can run without this, which is why we got away without it so far.)
// ---------------------------------------------------------------------------

void Core::Loader::loadRelocations()
{
    for (const auto &section: m_bin.sections) {
        if (section.meta.type == SHT_RELA)
            applyRelaSection(section);
    }

    std::cout << "[LOADER] Applied " << m_relocApplied << " relocations." << std::endl;
    if (!m_relocUnhandled.empty()) {
        std::cout << "[LOADER] Unhandled relocation types (type=count):";
        for (const auto &[type, count]: m_relocUnhandled)
            std::cout << " " << type << "=" << count;
        std::cout << std::endl;
    }
}

void Core::Loader::applyRelaSection(const Section &relaSection)
{
    const std::size_t entSize = relaSection.raw.header.sh_entsize ? relaSection.raw.header.sh_entsize : sizeof(Elf32_Rela);
    if (entSize == 0)
        return;
    const std::size_t count = relaSection.raw.data.size() / entSize;
    Utils::BeDecoder relaDecoder(relaSection.raw.data);

    for (std::size_t i = 0; i < count; i++) {
        Elf32_Rela rela{};
        rela.r_offset = relaDecoder.extractSwap<Elf32_Addr>();
        rela.r_info = relaDecoder.extractSwap<Elf32_Word>();
        rela.r_addend = relaDecoder.extractSwap<Elf32_Sword>();
        applyRelocation(rela);
    }
}

void Core::Loader::applyRelocation(const Elf32_Rela &rela)
{
    const std::uint32_t type = ELF32_R_TYPE(rela.r_info);
    const std::uint32_t symIdx = ELF32_R_SYM(rela.r_info);

    if (type == R_PPC_NONE)
        return;
    if (symIdx >= m_bin.symbols.size()) {
        Utils::Log::error("[LOADER] Relocation references out-of-range symbol {} @ 0x{:08X}", symIdx, rela.r_offset);
        return;
    }

    const std::uint32_t S = static_cast<std::uint32_t>(m_bin.symbols[symIdx].meta.virtAddress);
    const std::int32_t A = rela.r_addend;
    const std::uint32_t P = rela.r_offset; // target virtual address (progbits keep sh_addr)
    const std::uint32_t value = S + static_cast<std::uint32_t>(A);

    auto &mem = m_bin.m_memory;

    switch (type) {
        case R_PPC_ADDR32:
        case R_PPC_UADDR32:
            mem.write<std::uint32_t>(P, value);
            break;
        case R_PPC_ADDR16:
        case R_PPC_UADDR16:
        case R_PPC_ADDR16_LO:
            mem.write<std::uint16_t>(P, static_cast<std::uint16_t>(value & 0xFFFF));
            break;
        case R_PPC_ADDR16_HI:
            mem.write<std::uint16_t>(P, static_cast<std::uint16_t>((value >> 16) & 0xFFFF));
            break;
        case R_PPC_ADDR16_HA:
            mem.write<std::uint16_t>(P, static_cast<std::uint16_t>(((value + 0x8000) >> 16) & 0xFFFF));
            break;
        case R_PPC_ADDR24: { // 26-bit absolute branch, low 2 bits ignored
            std::uint32_t insn = mem.read<std::uint32_t>(P);
            insn = (insn & ~0x03FFFFFCu) | (value & 0x03FFFFFCu);
            mem.write<std::uint32_t>(P, insn);
            break;
        }
        case R_PPC_ADDR14:
        case R_PPC_ADDR14_BRTAKEN:
        case R_PPC_ADDR14_BRNTAKEN: {
            std::uint32_t insn = mem.read<std::uint32_t>(P);
            insn = (insn & ~0x0000FFFCu) | (value & 0x0000FFFCu);
            mem.write<std::uint32_t>(P, insn);
            break;
        }
        case R_PPC_REL24: { // PC-relative branch (bl/b)
            const std::uint32_t rel = value - P;
            std::uint32_t insn = mem.read<std::uint32_t>(P);
            insn = (insn & ~0x03FFFFFCu) | (rel & 0x03FFFFFCu);
            mem.write<std::uint32_t>(P, insn);
            break;
        }
        case R_PPC_REL14:
        case R_PPC_REL14_BRTAKEN:
        case R_PPC_REL14_BRNTAKEN: {
            const std::uint32_t rel = value - P;
            std::uint32_t insn = mem.read<std::uint32_t>(P);
            insn = (insn & ~0x0000FFFCu) | (rel & 0x0000FFFCu);
            mem.write<std::uint32_t>(P, insn);
            break;
        }
        case R_PPC_REL32:
            mem.write<std::uint32_t>(P, value - P);
            break;
        case R_PPC_RELATIVE:
            mem.write<std::uint32_t>(P, value);
            break;
        default:
            // Notably still TODO: R_PPC_EMB_SDA21 (small-data-area, r2/r13-relative).
            // Handling it requires wiring _SDA_BASE_/_SDA2_BASE_ into r13/r2 at
            // startup; deferred until thread/CRT bring-up so we don't mis-patch.
            m_relocUnhandled[type]++;
            return;
    }
    m_relocApplied++;
}
