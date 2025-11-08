/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** Loader
*/

#include "Loader.hpp"

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

void Core::Loader::loadSections()
{
}
