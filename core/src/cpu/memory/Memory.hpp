/*
** EPITECH PROJECT, 2025
** core
** File description:
** Memory
*/

#pragma once

#include <bit>
#include <cstring>
#include <stdexcept>
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
                GraphicsResourcesEnd = 0xF6000000,
                HeapStart = 0x14000000, // HLE heap: after stack (ApplicationData + 0x4000000)
            };

            explicit Memory(const std::size_t &size = 0) : m_memory(), m_virtAddress(ApplicationCode), m_memAddress(0), m_memSize(size), m_heapPtr(HeapStart)
            {
                if (size > 0) {
                    m_memory.resize(size);
                    m_memAddress = reinterpret_cast<std::size_t>(m_memory.data());
                }
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
                    return 0;
                return (address - m_virtAddress) + m_memAddress;
            }

            [[nodiscard]] std::size_t allocate(const std::size_t &address, const std::size_t &size) const
            {
                return translate(address);
            }

            // Bump allocator for HLE use. Returns a virtual address.
            std::uint32_t heapAlloc(std::uint32_t size, std::int32_t align = 4)
            {
                if (align <= 1) align = 4;
                const std::uint32_t mask = static_cast<std::uint32_t>(align) - 1;
                m_heapPtr = (m_heapPtr + mask) & ~mask;
                const std::uint32_t result = m_heapPtr;
                m_heapPtr += size;
                return result;
            }

            // Write a 32-bit value to a virtual address in big-endian order (matches LWZ/STW).
            void write32(std::uint32_t vaddr, std::uint32_t value)
            {
                const std::size_t host = translate(vaddr);
                if (host) {
                    const std::uint32_t be = std::byteswap(value);
                    std::memcpy(reinterpret_cast<void *>(host), &be, 4);
                }
            }

        private:
            std::vector<char> m_memory;
            std::size_t m_virtAddress;
            std::size_t m_memAddress;
            std::size_t m_memSize;
            std::uint32_t m_heapPtr;
    };
} // namespace Core
