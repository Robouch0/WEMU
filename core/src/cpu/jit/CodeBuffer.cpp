/*
** EPITECH PROJECT, 2026
** core
** File description:
** CodeBuffer -- executable memory for the x86-64 recompiler
*/

#include "CodeBuffer.hpp"

#include <initializer_list>
#include <sys/mman.h>
#include <unistd.h>

namespace Core::Jit {
    namespace {
        std::size_t pageSize() noexcept
        {
            static const auto size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
            return size;
        }

        // Tries reservations below and above `near`, within the +-2 GiB a rel32 call reaches, before
        // accepting any address.
        void *reserve(const std::size_t bytes, const void *near) noexcept
        {
            constexpr std::uintptr_t kStep = std::uintptr_t{1} << 28;
            const auto anchor = reinterpret_cast<std::uintptr_t>(near) & ~(kStep - 1);
            for (std::uintptr_t distance = kStep; near && distance + bytes + kStep < (std::uintptr_t{1} << 31); distance += kStep) {
                for (const auto hint: {anchor - distance - bytes, anchor + distance}) {
                    if (hint > anchor + distance || hint < kStep) // wrapped below zero
                        continue;
                    void *const address = reinterpret_cast<void *>(hint); // NOLINT(performance-no-int-to-ptr)
                    void *at = mmap(address, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
                    if (at != MAP_FAILED)
                        return at;
                }
            }
            void *at = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            return at == MAP_FAILED ? nullptr : at;
        }
    } // namespace

    CodeBuffer::CodeBuffer(const std::size_t codeBytes, const std::size_t dataBytes, const void *near) noexcept
    {
        const std::size_t page = pageSize();
        const std::size_t code = (codeBytes + page - 1) & ~(page - 1);
        const std::size_t data = (dataBytes + page - 1) & ~(page - 1);
        auto *base = static_cast<std::uint8_t *>(reserve(code + data, near));
        if (!base)
            return;
        if (mprotect(base, code, PROT_READ | PROT_EXEC) != 0) {
            munmap(base, code + data);
            return;
        }
        // The lookup table is probed on every register branch: huge pages keep it TLB-resident.
        madvise(base + code, data, MADV_HUGEPAGE);
        m_base = base;
        m_codeBytes = code;
        m_dataBytes = data;
    }

    CodeBuffer::~CodeBuffer()
    {
        if (m_base)
            munmap(m_base, m_codeBytes + m_dataBytes);
    }

    bool CodeBuffer::writable(const std::uint8_t *begin, const std::size_t bytes) noexcept
    {
        const std::size_t page = pageSize();
        const auto offset = reinterpret_cast<std::uintptr_t>(begin) & (page - 1);
        const std::size_t length = (offset + bytes + page - 1) & ~(page - 1);
        return mprotect(const_cast<std::uint8_t *>(begin) - offset, length, PROT_READ | PROT_WRITE) == 0;
    }

    bool CodeBuffer::executable(const std::uint8_t *begin, const std::size_t bytes) noexcept
    {
        const std::size_t page = pageSize();
        const auto offset = reinterpret_cast<std::uintptr_t>(begin) & (page - 1);
        const std::size_t length = (offset + bytes + page - 1) & ~(page - 1);
        return mprotect(const_cast<std::uint8_t *>(begin) - offset, length, PROT_READ | PROT_EXEC) == 0;
    }
} // namespace Core::Jit
