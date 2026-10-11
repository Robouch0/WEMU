#include "Dispatcher.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>

#include "utils/FrameProfiler.hpp"

namespace Core::Ppc {
    namespace {
        std::uint64_t nextEpoch()
        {
            static std::atomic<std::uint64_t> value{1};
            return value.fetch_add(1, std::memory_order_relaxed);
        }
    } // namespace

    Dispatcher::Dispatcher(std::size_t capacity, unsigned threshold, const std::string &cacheDirectory, unsigned minimumPrefix) :
        m_capacity(capacity), m_threshold(threshold), m_minimumPrefix(minimumPrefix), m_epoch(nextEpoch())
    {
        if (!capacity || !threshold || !minimumPrefix || minimumPrefix > 64)
            throw std::invalid_argument("Invalid native dispatch limits");
        if (!cacheDirectory.empty())
            m_cache = std::make_unique<ObjectCache>(cacheDirectory, nativeObjectIdentity());
    }

    unsigned Dispatcher::execute(std::uint32_t pc, std::span<const std::uint32_t> words, std::span<std::uint32_t, 32> registers, unsigned budget)
    {
        ExecutionState state{registers.data(), {}};
        return execute(pc, words, state, budget);
    }

    unsigned Dispatcher::execute(std::uint32_t pc, std::span<const std::uint32_t> words, ExecutionState &state, unsigned budget,
                                 NativeBinding *binding)
    {
        if (!budget || words.empty() || words.size() > 64 || (pc & 3) || std::uint64_t(pc) + words.size() * 4 > UINT32_MAX)
            return 0;
        if (words.size() < m_minimumPrefix && !Block::loopsToStart(pc, words))
            return 0;
        auto it = m_entries.find(pc);
        const bool changed = it == m_entries.end() || !std::equal(words.begin(), words.end(), it->second.words.begin(), it->second.words.end());
        // Decode only a new live code version. The caller has already validated
        // the decoded block, and repeated frontend decoding is expensive for
        // small hot prefixes. Unsupported starts must not displace useful code.
        if (changed && (words.size() < 2 || !Block::supports(words[0]) || !Block::supports(words[1])))
            return 0;
        if (it == m_entries.end()) {
            if (m_entries.size() == m_capacity) {
                if (m_entries.at(m_order.front()).native)
                    m_epoch = nextEpoch();
                m_entries.erase(m_order.front());
                m_order.pop_front();
                ++m_stats.evictions;
            }
            it = m_entries.try_emplace(pc).first;
            m_order.push_back(pc);
        }
        auto &entry = it->second;
        if (changed) {
            if (!entry.words.empty())
                ++m_stats.invalidations;
            if (entry.native)
                m_epoch = nextEpoch();
            entry = Entry{};
            entry.words.assign(words.begin(), words.end());
            std::size_t count = 0;
            while (count < words.size() && Block::supports(words[count]))
                ++count;
            if (count)
                entry.block = Block::decode(pc, words.first(count));
        }
        if (!entry.block || entry.block->words().size() > budget ||
            (entry.block->words().size() < m_minimumPrefix && !Block::loopsToStart(pc, entry.block->words())))
            return 0;
        if (!entry.native) {
            if (++entry.visits < m_threshold)
                return 0;
            Profile::Scope compiling(Profile::Stage::Compile);
            if (cacheEnabled()) {
                if (const auto object = m_cache->load(*entry.block)) {
                    try {
                        entry.native = std::make_unique<NativeBlock>(m_compiler, *object);
                        ++m_stats.cacheHits;
                    } catch (const std::exception &) {
                        ++m_stats.cacheLinkFailures;
                    }
                }
                if (!entry.native) {
                    ++m_stats.cacheMisses;
                    const auto object = emitObject(*entry.block);
                    entry.native = std::make_unique<NativeBlock>(m_compiler, object);
                    ++m_stats.compiled;
                    if (m_cache->store(*entry.block, object))
                        ++m_stats.cacheWrites;
                }
            } else {
                entry.native = std::make_unique<NativeBlock>(m_compiler, *entry.block);
                ++m_stats.compiled;
            }
        }
        if (binding)
            *binding = {entry.native.get(), m_epoch};
        state.instructionBudget = budget;
        const auto count = entry.native->execute(state);
        recordExecution(count);
        return count;
    }
} // namespace Core::Ppc
