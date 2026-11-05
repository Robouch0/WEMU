#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <vector>

namespace Core::Gfx {
    // Guest ranges a title invalidated for the GPU shader cache (GX2Invalidate with the shader bit).
    // Titles load programs before their first draw; the gap between that invalidation and the first
    // bind is the time available to prepare a shader ahead of use.
    class ShaderInvalidations {
        public:
            using Clock = std::chrono::steady_clock;
            struct Hint {
                    std::uint32_t address{}, size{}, swap{};
                    Clock::time_point time{};
            };
            static constexpr std::size_t maxRanges = 4096;

            // Whole-address-space invalidations (MK8 issues several per frame) locate nothing and are
            // not recorded. Returns whether the range was kept as a hint.
            bool record(std::uint32_t address, std::uint32_t size, std::uint32_t swap, Clock::time_point time = Clock::now())
            {
                if (!size || (address == 0 && size == 0xFFFFFFFFu))
                    return false;
                std::scoped_lock lock(m_mutex);
                for (auto &hint: m_hints)
                    if (hint.address == address && hint.size == size) {
                        hint.swap = swap;
                        hint.time = time;
                        return true;
                    }
                if (m_hints.size() >= maxRanges)
                    m_hints.erase(m_hints.begin());
                m_hints.push_back({address, size, swap, time});
                return true;
            }
            // Latest invalidation whose range contains [address, address + size).
            std::optional<Hint> covering(std::uint32_t address, std::uint32_t size) const
            {
                const auto end = std::uint64_t(address) + size;
                std::scoped_lock lock(m_mutex);
                std::optional<Hint> latest;
                for (const auto &hint: m_hints)
                    if (hint.address <= address && std::uint64_t(hint.address) + hint.size >= end && (!latest || hint.time >= latest->time))
                        latest = hint;
                return latest;
            }
            std::size_t size() const
            {
                std::scoped_lock lock(m_mutex);
                return m_hints.size();
            }

        private:
            mutable std::mutex m_mutex;
            std::vector<Hint> m_hints;
    };

    inline ShaderInvalidations &shaderInvalidations()
    {
        static ShaderInvalidations value;
        return value;
    }

    // WEMU_GX2_INVALIDATE_TRACE=1 records shader invalidations and reports, at each program's
    // first bind, whether and how long before it was announced.
    inline bool shaderInvalidationTrace()
    {
        static const bool value = [] {
            const auto *setting = std::getenv("WEMU_GX2_INVALIDATE_TRACE");
            return setting && setting[0] == '1';
        }();
        return value;
    }
} // namespace Core::Gfx
