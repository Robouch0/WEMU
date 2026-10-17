// //

#pragma once

#include <array>
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
            static constexpr std::uint32_t LOCKED_CACHE_SIZE = 0x4000;
            static constexpr std::array<std::uint32_t, 3> LOCKED_CACHE_BASES{0xFFC00000, 0xFFC40000, 0xFFC80000};

            explicit Memory(const std::size_t &size = ApplicationMemoryEnd - ApplicationCode) :
                m_memory(), m_virtAddress(ApplicationCode), m_memSize(size)
            {
                m_memory.resize(size);
                m_stack.resize(STACK_SIZE);
                m_dimport.resize(DIMPORT_SIZE);
                m_lockedCache.resize(LOCKED_CACHE_SIZE * LOCKED_CACHE_BASES.size());
                m_codePages.resize((size >> kCodePageShift) / 64 + 1, 0);
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
                for (std::size_t core = 0; core < LOCKED_CACHE_BASES.size(); ++core) {
                    const auto cacheOffset = static_cast<std::size_t>(address) - LOCKED_CACHE_BASES[core];
                    if (address >= LOCKED_CACHE_BASES[core] && cacheOffset < LOCKED_CACHE_SIZE && bytes <= LOCKED_CACHE_SIZE - cacheOffset)
                        return reinterpret_cast<const std::uint8_t *>(m_lockedCache.data() + core * LOCKED_CACHE_SIZE + cacheOffset);
                }
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
                const auto swapped = std::byteswap(value);
                // Main RAM fast path, with a page check so the interpreter can leave a decoded block
                // as soon as a store touches a page holding decoded code (see markCodePage).
                const std::size_t offset = static_cast<std::size_t>(address) - m_virtAddress;
                if (address >= m_virtAddress && offset < m_memory.size() && sizeof(T) <= m_memory.size() - offset) {
                    if (isCodePage(offset) || isCodePage(offset + sizeof(T) - 1)) [[unlikely]]
                        m_codeWritten = true;
                    std::memcpy(m_memory.data() + offset, &swapped, sizeof(T));
                    return;
                }
                auto *ptr = hostPtr(address, sizeof(T));
                if (ptr == nullptr)
                    throw MemoryException(std::format("Unmapped write<{}> @ 0x{:08X}", sizeof(T), address));
                std::memcpy(ptr, &swapped, sizeof(T));
            }

            // Pages of main RAM that hold decoded (cached) guest code, by memory-relative offset.
            // Memory::write raises codeWritten() when it stores into one of them; other write paths
            // (HLE memcpy, loaders) are caught by the interpreter's block revalidation instead.
            static constexpr unsigned kCodePageShift = 12;
            void markCodePage(const std::size_t offset)
            {
                const std::size_t page = offset >> kCodePageShift;
                if (page / 64 < m_codePages.size())
                    m_codePages[page / 64] |= std::uint64_t{1} << (page % 64);
            }
            [[nodiscard]] bool isCodePage(const std::size_t offset) const noexcept
            {
                // Callers pass offsets inside main RAM, which the bitmap covers completely.
                const std::size_t page = offset >> kCodePageShift;
                return m_codePages[page / 64] >> (page % 64) & 1;
            }
            void clearCodePages() noexcept { std::fill(m_codePages.begin(), m_codePages.end(), 0); }
            [[nodiscard]] bool codeWritten() const noexcept { return m_codeWritten; }
            void clearCodeWritten() noexcept { m_codeWritten = false; }

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
            std::vector<char> m_lockedCache;
            std::vector<std::uint64_t> m_codePages;
            bool m_codeWritten{false};
            std::size_t m_heapPtr = HEAP_BASE;
            std::size_t m_virtAddress;
            std::size_t m_memSize;
    };
} // namespace Core
