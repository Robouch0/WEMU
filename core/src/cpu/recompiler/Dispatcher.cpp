#include "Dispatcher.hpp"
#include <algorithm>
#include <stdexcept>

namespace Core::Ppc {
    Dispatcher::Dispatcher(std::size_t capacity, unsigned threshold, const std::string &cacheDirectory)
        : m_capacity(capacity), m_threshold(threshold)
    {
        if (!capacity || !threshold) throw std::invalid_argument("Invalid native dispatch limits");
        if (!cacheDirectory.empty()) m_cache = std::make_unique<ObjectCache>(cacheDirectory, nativeObjectIdentity());
    }

    unsigned Dispatcher::execute(std::uint32_t pc, std::span<const std::uint32_t> words,
                                 std::span<std::uint32_t, 32> registers, unsigned budget)
    {
        if (!budget || words.empty() || words.size() > 64 || (pc & 3)
            || std::uint64_t(pc) + words.size() * 4 > UINT32_MAX) return 0;
        // A one-instruction host call rarely repays dispatch overhead. Reject
        // these and unsupported starts before they displace useful cache entries.
        if (words.size() < 2 || !Block::supports(words[0]) || !Block::supports(words[1])) return 0;
        auto it = m_entries.find(pc);
        if (it == m_entries.end()) {
            if (m_entries.size() == m_capacity) {
                m_entries.erase(m_order.front());
                m_order.pop_front();
                ++m_stats.evictions;
            }
            it = m_entries.try_emplace(pc).first;
            m_order.push_back(pc);
        }
        auto &entry = it->second;
        if (!std::equal(words.begin(), words.end(), entry.words.begin(), entry.words.end())) {
            if (!entry.words.empty()) ++m_stats.invalidations;
            entry = Entry{};
            entry.words.assign(words.begin(), words.end());
            std::size_t count = 0;
            while (count < words.size() && Block::supports(words[count]))
                ++count;
            if (count) entry.block = Block::decode(pc, words.first(count));
        }
        if (!entry.block || entry.block->words().size() > budget) return 0;
        if (!entry.native) {
            if (++entry.visits < m_threshold) return 0;
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
                    if (m_cache->store(*entry.block, object)) ++m_stats.cacheWrites;
                }
            } else {
                entry.native = std::make_unique<NativeBlock>(m_compiler, *entry.block);
                ++m_stats.compiled;
            }
        }
        entry.native->execute(registers);
        const auto count = static_cast<unsigned>(entry.block->words().size());
        ++m_stats.calls;
        m_stats.instructions += count;
        return count;
    }
}
