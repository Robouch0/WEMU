// //

#pragma once

#include <bit>
#include <cinttypes>
#include <cstring>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "exception/Exception.hpp"

namespace Core {

    class MemoryException final : public Core::Exception {
        public:
            explicit MemoryException(const std::string &errorMessage) : Core::Exception("MemoryException", errorMessage) {}

            ~MemoryException() override = default;

            [[nodiscard]] bool isFatal() const override { return true; }
    };

    class Memory {
        public:
            enum MemoryMap : std::uint32_t {
                ApplicationCode = 0x02000000,
                ApplicationData = 0x10000000,
                ApplicationMemoryEnd = 0x6D000000,
                GraphicsResources = 0xF4000000,
                GraphicsResourcesEnd = 0xF6000000
            };

            static constexpr uint32_t STACK_BASE = 0xC0FF0000;
            static constexpr uint32_t STACK_SIZE = 0x100000; // 1 MB
            // Internal HLE pool (0x26000000..0x28000000). The guest-visible MEM2 arena that the
            // title allocates from starts at 0x28000000 (see hle/MemHeap.cpp) — keep out of it.
            static constexpr uint32_t HEAP_BASE = 0x26000000;
            static constexpr uint32_t MIN_HEAP_ALIGN = 4;
            static constexpr uint32_t DIMPORT_BASE = 0xC0000000; // dimport slots
            static constexpr uint32_t DIMPORT_SIZE = 0x4000; // 16 KB

            explicit Memory(const std::size_t &size = ApplicationMemoryEnd - ApplicationCode) :
                m_memory(), m_virtAddress(ApplicationCode), m_memSize(size)
            {
                m_memory.resize(size);
                m_stack.resize(STACK_SIZE);
                m_dimport.resize(DIMPORT_SIZE);
            }

            // Guest RAM has one owner. Transfer its buffers without copying or reallocating them.
            Memory(const Memory &) = delete;
            Memory &operator=(const Memory &) = delete;
            Memory(Memory &&) noexcept = default;
            Memory &operator=(Memory &&) noexcept = default;

            [[nodiscard]] static constexpr std::uint32_t alignUp(std::uint32_t value, std::uint32_t align) noexcept
            {
                return (value + align - 1) & ~(align - 1);
            }

            // Returns a contiguous view only when the entire non-empty range fits in one region.
            // Subtraction-based checks reject boundary crossings without wrapping guest addresses.
            [[nodiscard]] const std::uint8_t *hostPtr(const std::uint32_t address, const std::size_t bytes = 1) const noexcept
            {
                if (!bytes)
                    return nullptr;
                // Main memory (code/data/heap, [ApplicationCode, ApplicationMemoryEnd)) is by far the
                // common case, so test it first: one predictable branch on the hot path. Stack and
                // dimport live at 0xC0xxxxxx, well above ApplicationMemoryEnd, so their offset here
                // exceeds m_memory.size() and correctly falls through to the checks below.
                const std::size_t offset = static_cast<std::size_t>(address) - m_virtAddress;
                if (address >= m_virtAddress && offset < m_memory.size() && bytes <= m_memory.size() - offset)
                    return reinterpret_cast<const std::uint8_t *>(m_memory.data() + offset);
                const std::size_t stackOffset = static_cast<std::size_t>(address) - STACK_BASE;
                if (address >= STACK_BASE && stackOffset < m_stack.size() && bytes <= m_stack.size() - stackOffset)
                    return reinterpret_cast<const std::uint8_t *>(m_stack.data() + stackOffset);
                const std::size_t importOffset = static_cast<std::size_t>(address) - DIMPORT_BASE;
                if (address >= DIMPORT_BASE && importOffset < m_dimport.size() && bytes <= m_dimport.size() - importOffset)
                    return reinterpret_cast<const std::uint8_t *>(m_dimport.data() + importOffset);
                return nullptr;
            }

            [[nodiscard]] std::uint8_t *hostPtr(const std::uint32_t address, const std::size_t bytes = 1) noexcept
            {
                return const_cast<std::uint8_t *>(std::as_const(*this).hostPtr(address, bytes));
            }

            template<typename T>
            [[nodiscard]] T read(const std::uint32_t address) const
            {
                const auto *ptr = hostPtr(address, sizeof(T));
                if (ptr == nullptr)
                    throw MemoryException(std::format("Unmapped read<{}> @ 0x{:08X}", sizeof(T), address));
                // Preserve unaligned guest accesses without violating host alignment or aliasing.
                T value;
                std::memcpy(&value, ptr, sizeof(T));
                return std::byteswap(value);
            }

            template<typename T>
            void write(const std::uint32_t address, const T value)
            {
                auto *ptr = hostPtr(address, sizeof(T));
                if (ptr == nullptr)
                    throw MemoryException(std::format("Unmapped write<{}> @ 0x{:08X}", sizeof(T), address));
                const auto swapped = std::byteswap(value);
                std::memcpy(ptr, &swapped, sizeof(T));
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
            [[nodiscard]] const std::vector<char> &getMemory() const noexcept { return m_memory; }

            [[nodiscard]] std::size_t translate(const std::size_t &address) const { return allocate(address, 1); }

            // Loader sections must fit completely in main RAM before memcpy/memset uses this view.
            [[nodiscard]] std::size_t allocate(const std::size_t &address, const std::size_t &size) const
            {
                if (!size || address < m_virtAddress)
                    return 0;
                const auto offset = address - m_virtAddress;
                if (offset >= m_memory.size() || size > m_memory.size() - offset)
                    return 0;
                return reinterpret_cast<std::size_t>(m_memory.data() + offset);
            }

        private:
            std::vector<char> m_memory;
            std::vector<char> m_stack;
            std::vector<char> m_dimport;
            std::size_t m_heapPtr = HEAP_BASE;
            std::size_t m_virtAddress;
            std::size_t m_memSize;
    };
} // namespace Core
