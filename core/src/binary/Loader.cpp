/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#include "Loader.hpp"
#include "zlib.h"


Core::Loader::Loader(const std::string &filepath) : m_bin({}), m_beDecoder(filepath)
{
    loadHeader();
    loadSections();
}

void Core::Loader::loadHeader()
{
    for (unsigned char &i : m_bin.header.e_ident)
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

void Core::Loader::loadSectionName()
{
    const auto &shstr = m_bin.sections[m_bin.header.e_shstrndx];
    char const *sectionNames = shstr.data.data();

    for (size_t i = 0; i <  m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        section.name = sectionNames + section.header.sh_name;
        std::cout << "id : " << i << " valuename : " << section.name << std::endl;
    }
}

void Core::Loader::loadSections()
{
    m_bin.sections.resize(m_bin.header.e_shnum);
    for (size_t i = 0; i < m_bin.header.e_shnum; i++) {
        auto &section = m_bin.sections[i];
        m_beDecoder.seek(m_bin.header.e_shoff + i * m_bin.header.e_shentsize);
        loadSectionHeader(section);
        if (section.header.sh_flags & SHF_DEFLATED) {
            auto stream = z_stream {};
            auto ret = Z_OK;
            m_beDecoder.seek(section.header.sh_offset);
            section.data.resize(m_beDecoder.extractSwap<uint32_t>());

            std::memset(&stream, 0, sizeof(stream));
            stream.zalloc = Z_NULL;
            stream.zfree = Z_NULL;
            stream.opaque = Z_NULL;

            ret = inflateInit(&stream);

            if (ret != Z_OK) {
                section.data.clear();
            } else {
                stream.avail_in = section.header.sh_size;
                m_beDecoder.extract(stream.next_in, section.header.sh_size);
                stream.avail_out = static_cast<uInt>(section.data.size());
                stream.next_out = reinterpret_cast<Bytef *>(section.data.data());

                ret = inflate(&stream, Z_FINISH);
                inflateEnd(&stream);
            }
        }
        m_beDecoder.seek(section.header.sh_offset);
        loadSectionData(section);
    }
    loadSectionName();
}
