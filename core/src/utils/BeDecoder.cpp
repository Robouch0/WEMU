/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** BeDecoder
*/

#include "BeDecoder.hpp"

Utils::BeDecoder::BeDecoder(std::vector<char> buffer) : m_Buffer(std::move(buffer)),
    m_view(m_Buffer.data(), m_Buffer.size()), m_offset(0)
{
}

Utils::BeDecoder::BeDecoder(const std::string &filepath)
{
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw BeDecoderException("Cannot open the binary file.");
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
        throw BeDecoderException("Cannot read the binary file.");
    file.close();
    m_Buffer = std::move(buffer);
    m_view = std::string_view(m_Buffer.data(), m_Buffer.size());
    m_offset = 0;
}

void Utils::BeDecoder::seek(std::size_t pos)
{
    m_offset = pos;
}
