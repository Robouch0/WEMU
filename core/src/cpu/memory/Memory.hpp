// //

#pragma once

#include <vector>
#include <cinttypes>
#include <bit>
#include <iomanip>
#include <iostream>
#include <format>
#include <sstream>
#include <stdexcept>

#include "exception/Exception.hpp"

namespace Core {

    class MemoryException final : public Core::Exception {
        public:
            explicit MemoryException(const std::string &errorMessage) : Core::Exception(
                "MemoryException", errorMessage) {}

            ~MemoryException() override = default;

            [[nodiscard]] bool isFatal() const override { return true; }
    };

    class Memory {
        public:
            enum MemoryMap : size_t
            {
                ApplicationCode = 0x02000000,
                ApplicationData = 0x10000000,
                ApplicationMemoryEnd = 0x42000000,
                GraphicsResources = 0xF4000000,
                GraphicsResourcesEnd = 0xF6000000
            };

            static constexpr uint32_t STACK_BASE = 0xC0FF0000;
            static constexpr uint32_t STACK_SIZE = 0x100000;   // 1 MB
            static constexpr uint32_t HEAP_BASE = 0x28000000;
            static constexpr uint32_t MIN_HEAP_ALIGN = 4;
            static constexpr uint32_t DIMPORT_BASE = 0xC0000000; // dimport slots
            static constexpr uint32_t DIMPORT_SIZE = 0x4000;     // 16 KB

            explicit Memory(const std::size_t &size = 0x40000000) : m_memory(), m_virtAddress(ApplicationCode), m_memSize(size)
            {
                m_memory.resize(size);
                m_stack.resize(STACK_SIZE);
                m_dimport.resize(DIMPORT_SIZE);
            }

            [[nodiscard]] static constexpr std::uint32_t alignUp(std::uint32_t value, std::uint32_t align) noexcept
            {
                return (value + align - 1) & ~(align - 1);
            }

            [[nodiscard]] std::uint8_t *hostPtr(const std::uint32_t address) noexcept
            {
                if (address >= STACK_BASE && address < STACK_BASE + STACK_SIZE)
                    return reinterpret_cast<std::uint8_t*>(m_stack.data() + (address - STACK_BASE));
                if (address >= DIMPORT_BASE && address < DIMPORT_BASE + DIMPORT_SIZE)
                    return reinterpret_cast<uint8_t*>(m_dimport.data()) + (address - DIMPORT_BASE);
                if (address >= m_virtAddress) {
                    const std::size_t offset = address - m_virtAddress;
                    if (offset < m_memory.size())
                        return reinterpret_cast<std::uint8_t*>(m_memory.data() + offset);
                }
                return nullptr;
            }

            template<typename T>
            [[nodiscard]] T read(const std::uint32_t address)
            {
                // if (address & (alignof(T) - 1))
                //     throw MemoryException(std::format("Unaligned read<{}> @ 0x{:08X}", sizeof(T), address));

                T *ptr = reinterpret_cast<T*>(this->hostPtr(address));

                if (ptr == nullptr)
                    throw MemoryException(std::format("Unmapped read<{}> @ 0x{:08X}", sizeof(T), address));
                return std::byteswap(ptr[0]);
            }

            template<typename T>
            void write(const std::uint32_t address, const T value)
            {
                // if (address & (alignof(T) - 1))
                //     throw MemoryException(std::format("Unaligned write<{}> @ 0x{:08X}", sizeof(T), address));

                T *ptr = reinterpret_cast<T*>(this->hostPtr(address));

                if (ptr == nullptr)
                    throw MemoryException(std::format("Unmapped write<{}> @ 0x{:08X}", sizeof(T), address));
                ptr[0] = std::byteswap(value);
            }

            [[nodiscard]] std::uint32_t heapAllocate(const std::uint32_t size, std::uint32_t align)
            {
                align = std::max(align, MIN_HEAP_ALIGN);

                // if (align & (align - 1))
                //     throw MemoryException(std::format("[MEM] Heap alignment not a power of two: {}", align));

                m_heapPtr = alignUp(m_heapPtr, align);
                m_heapPtr += size;
                return m_heapPtr - size;
            }

            [[nodiscard]] std::vector<char> &getMemory() noexcept { return m_memory; }

            [[nodiscard]] std::size_t translate(const std::size_t &address) const
            {
                if (address < m_virtAddress || address > m_virtAddress + m_memory.size())
                    return 0;
                return reinterpret_cast<std::size_t>(m_memory.data() + (address - m_virtAddress));
            }

            [[nodiscard]] std::size_t allocate(const std::size_t &address, const std::size_t &size) const
            {
                return translate(address);
            }

        private:
            std::vector<char> m_memory;
            std::vector<char> m_stack;
            std::vector<char> m_dimport;
            std::size_t m_heapPtr = HEAP_BASE;
            std::size_t m_virtAddress;
            std::size_t m_memSize;
    };
}
