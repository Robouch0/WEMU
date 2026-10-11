#pragma once

#include <deque>
#include <unordered_map>

#include "LlvmBackend.hpp"
#include "ObjectCache.hpp"

namespace Core::Ppc {
    // Single emulation thread. Caller validates live words and hook/control-flow boundaries.
    // Zero means no execution: use the interpreter without advancing guest state.
    class Dispatcher {
        public:
            struct Stats {
                    std::uint64_t compiled{}, calls{}, instructions{}, invalidations{}, evictions{};
                    std::uint64_t cacheHits{}, cacheMisses{}, cacheWrites{}, cacheLinkFailures{};
            };
            explicit Dispatcher(std::size_t capacity = 16384, unsigned threshold = 256, const std::string &cacheDirectory = {},
                                unsigned minimumPrefix = 2);
            unsigned execute(std::uint32_t pc, std::span<const std::uint32_t> words, std::span<std::uint32_t, 32> registers, unsigned budget);
            unsigned execute(std::uint32_t pc, std::span<const std::uint32_t> words, ExecutionState &state, unsigned budget,
                             NativeBinding *binding = nullptr);
            [[nodiscard]] NativeBlock *bound(const NativeBinding &binding) const noexcept
            {
                return binding.epoch == m_epoch ? binding.code : nullptr;
            }
            void recordExecution(unsigned count)
            {
                ++m_stats.calls;
                m_stats.instructions += count;
            }
            [[nodiscard]] const Stats &stats() const { return m_stats; }
            [[nodiscard]] std::size_t size() const { return m_entries.size(); }
            [[nodiscard]] bool cacheEnabled() const { return m_cache && m_cache->available(); }

        private:
            struct Entry {
                    std::vector<std::uint32_t> words;
                    std::optional<Block> block;
                    std::unique_ptr<NativeBlock> native;
                    unsigned visits{};
            };
            NativeCompiler m_compiler;
            std::unique_ptr<ObjectCache> m_cache;
            std::size_t m_capacity;
            unsigned m_threshold, m_minimumPrefix;
            std::unordered_map<std::uint32_t, Entry> m_entries;
            std::deque<std::uint32_t> m_order;
            Stats m_stats;
            std::uint64_t m_epoch;
    };
} // namespace Core::Ppc
