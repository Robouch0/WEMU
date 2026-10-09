#include "RasterWorkers.hpp"

#include <algorithm>
#include <cstdint>

namespace Core::Gfx {

RasterWorkers::RasterWorkers(unsigned count) : m_count(std::clamp(count, 1u, 16u))
{
    try {
        for (unsigned i = 1; i < m_count; ++i)
            m_threads.emplace_back([this, i] { worker(i); });
    } catch (...) {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_ready.notify_all();
        for (auto &thread : m_threads) thread.join();
        throw;
    }
}

RasterWorkers::~RasterWorkers()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_ready.notify_all();
    for (auto &thread : m_threads) thread.join();
}

void RasterWorkers::execute(unsigned index) noexcept
{
    try {
        const auto first = unsigned(std::uint64_t(m_rows) * index / m_count);
        const auto end = unsigned(std::uint64_t(m_rows) * (index + 1) / m_count);
        if (first != end) m_work(first, end);
    } catch (...) {
        std::lock_guard lock(m_mutex);
        if (!m_error) m_error = std::current_exception();
    }
}

void RasterWorkers::worker(unsigned index)
{
    unsigned long long generation = 0;
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_ready.wait(lock, [&] { return m_stop || generation != m_generation; });
        if (m_stop) return;
        generation = m_generation;
        lock.unlock();
        execute(index);
        lock.lock();
        if (--m_pending == 0) m_done.notify_one();
    }
}

void RasterWorkers::run(unsigned rows, const std::function<void(unsigned, unsigned)> &work)
{
    if (!rows) return;
    if (m_count == 1) { work(0, rows); return; }
    {
        std::lock_guard lock(m_mutex);
        m_work = work;
        m_rows = rows;
        m_error = nullptr;
        m_pending = m_count - 1;
        ++m_generation;
    }
    m_ready.notify_all();
    execute(0);
    std::unique_lock lock(m_mutex);
    m_done.wait(lock, [&] { return m_pending == 0; });
    m_work = {};
    if (m_error) std::rethrow_exception(m_error);
}

} // namespace Core::Gfx
