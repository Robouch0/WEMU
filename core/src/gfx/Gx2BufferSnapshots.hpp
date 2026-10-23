#pragma once

#include <algorithm>
#include <cstring>
#include <optional>
#include <unordered_map>

#include "cpu/memory/Memory.hpp"
#include "gfx/Gx2CommandStream.hpp"

namespace Core::Gfx {
    // Host-owned versions let replay read a submitted frame while the CPU fills
    // the following frame. Tracked, unchanged static buffers share their bytes.
    class Gx2BufferSnapshots {
        public:
            void capture(Gx2CommandStream &stream, const Memory &memory)
            {
                using Range = std::pair<std::uint64_t, std::uint64_t>;
                std::vector<Range> requested, produced;
                const auto rememberBindings = [&] {
                    for (const auto &range: m_state.attributes)
                        if (range.second > range.first)
                            requested.push_back(range);
                    for (const auto &range: m_state.outputs)
                        if (range.second > range.first)
                            produced.push_back(range);
                };
                rememberBindings();
                for (const auto &command: stream.commands()) {
                    if (command.type == Gx2Cmd::SetContextState) {
                        if (m_context)
                            m_contexts[m_context] = m_state;
                        m_context = command.gpr[0];
                        if (m_context)
                            if (const auto found = m_contexts.find(m_context); found != m_contexts.end())
                                m_state = found->second;
                        rememberBindings();
                    }
                    if (command.type == Gx2Cmd::SetStreamOutBuffer && command.payload.size() == 8) {
                        const auto &p = command.payload;
                        const auto address = p[1] ? p[1] : p[6];
                        const auto bytes = p[1] ? std::uint64_t(p[0]) : std::uint64_t(p[4]) * p[5];
                        if (command.gpr[0] < m_state.outputs.size())
                            m_state.outputs[command.gpr[0]] = {address, std::uint64_t(address) + bytes};
                        if (address && bytes)
                            produced.emplace_back(address, std::uint64_t(address) + bytes);
                    }
                    if (command.type == Gx2Cmd::SetAttribBuffer && command.gpr[0] < m_state.attributes.size()) {
                        // Fetches can read up to a vector past the declared stride.
                        const auto address = command.gpr[3];
                        const auto bytes = std::size_t(command.gpr[1]);
                        auto &range = m_state.attributes[command.gpr[0]];
                        range = {address, std::uint64_t(address) + bytes + (bytes && memory.hostPtr(address, bytes + 16) ? 16 : 0)};
                        if (address && bytes)
                            requested.push_back(range);
                    }
                    if (command.type == Gx2Cmd::DrawIndexedEx && command.gpr[3]) {
                        const auto type = command.gpr[2] & 15;
                        if (type == 0 || type == 1 || type == 4 || type == 9) {
                            const auto count = std::min(command.gpr[1], 0x10000u);
                            requested.emplace_back(command.gpr[3],
                                                   std::uint64_t(command.gpr[3]) + std::uint64_t(count) * (type == 1 || type == 9 ? 4 : 2));
                        }
                    }
                }
                std::sort(requested.begin(), requested.end());
                std::vector<Range> ranges;
                for (const auto &[begin, end]: requested) {
                    if (end <= begin || end > 0x100000000ull ||
                        std::ranges::any_of(produced, [&](const Range &output) { return begin < output.second && output.first < end; }))
                        continue; // stream output is produced during this replay, not by the CPU
                    if (!ranges.empty() && begin <= ranges.back().second)
                        ranges.back().second = std::max(ranges.back().second, end);
                    else
                        ranges.emplace_back(begin, end);
                }
                auto &snapshots = stream.bufferSnapshots();
                snapshots.clear();
                for (const auto &[begin, end]: ranges) {
                    const auto address = static_cast<std::uint32_t>(begin);
                    const auto bytes = static_cast<std::size_t>(end - begin);
                    const auto *source = memory.hostPtr(address, bytes);
                    if (!source)
                        continue;
                    const auto key = (begin << 32) | bytes;
                    memory.watchWrites(address, bytes);
                    const auto generation = memory.writeGeneration(address, bytes);
                    auto &entry = m_entries[key];
                    if (entry.data.empty() || (!generation || generation != entry.generation)) {
                        if (entry.data.empty() || std::memcmp(source, std::as_const(entry.data).data(), bytes)) {
                            if (m_bytes + bytes > 128ull * 1024 * 1024) {
                                // Keep in-flight snapshots alive through their shared owners.
                                m_entries.clear();
                                m_bytes = 0;
                                Gx2Payload data;
                                std::memcpy(data.overwrite((bytes + 3) / 4).data(), source, bytes);
                                const auto hash = hashData(data);
                                snapshots.push_back({address, bytes, std::move(data), hash});
                                continue;
                            }
                            m_bytes += entry.data.empty() ? bytes : 0;
                            std::memcpy(entry.data.overwrite((bytes + 3) / 4).data(), source, bytes);
                            entry.hash = hashData(entry.data);
                        }
                        entry.generation = generation;
                    }
                    snapshots.push_back({address, bytes, entry.data, entry.hash});
                }
            }

        private:
            struct State {
                    std::array<std::pair<std::uint64_t, std::uint64_t>, 8> attributes{};
                    std::array<std::pair<std::uint64_t, std::uint64_t>, 4> outputs{};
            };
            struct Entry {
                    Gx2Payload data;
                    std::optional<std::uint64_t> generation;
                    std::uint64_t hash{};
            };
            static std::uint64_t hashData(const Gx2Payload &data)
            {
                std::uint64_t hash = 1469598103934665603ull;
                for (const auto word: data)
                    hash = (hash ^ word) * 1099511628211ull;
                return hash;
            }
            std::unordered_map<std::uint64_t, Entry> m_entries;
            std::uint64_t m_bytes{};
            State m_state;
            std::uint32_t m_context{};
            std::unordered_map<std::uint32_t, State> m_contexts;
    };
} // namespace Core::Gfx
