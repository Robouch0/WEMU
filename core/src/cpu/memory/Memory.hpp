// //

#pragma once

#include <vector>
#include <cinttypes>

namespace Core {
    class Memory {
        public:
            enum MemoryMap : size_t
            {
                ApplicationCode = 0x02000000,
                ApplicationData = 0x10000000,
                ApplicationMemoryEnd = 0x02000000 + 0x40000000, // 1GB
                GraphicsResources = 0xF4000000,
                GraphicsResourcesEnd = 0xF6000000
             };

            explicit Memory(const std::size_t &size = 0x40000000) : m_memory(),  m_virtAddress(0x2000000), m_memAddress(), m_memSize(size)
            {
                m_memory.resize(size);
                m_memAddress = reinterpret_cast<std::size_t>(m_memory.data());
            }

            std::vector<char> &getMemory() { return m_memory; }

            [[nodiscard]] std::size_t translate(const std::size_t &address) const
            {
                if (address >= ApplicationCode && address < ApplicationData) {
                } else if (address >= ApplicationData && address < ApplicationMemoryEnd) {
                } else if (address >= GraphicsResources && address <= GraphicsResourcesEnd) {
                } else {
                    return 0;
                }
                if (address < m_virtAddress)
                    throw std::exception();
                return (address - m_virtAddress) + m_memAddress;
            }

            [[nodiscard]] std::size_t allocate(const std::size_t &address, const std::size_t &size) const
            {
                return translate(address);
            }

        private:
            std::vector<char> m_memory;
            std::size_t m_virtAddress;
            std::size_t m_memAddress;
            std::size_t m_memSize;
    };
}
