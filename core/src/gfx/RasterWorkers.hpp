#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Core::Gfx {

    // Synchronous row batches: the caller owns all draw state until run() returns.
    // Calls are serialized by the replayer; callbacks must only write their own rows.
    class RasterWorkers {
        public:
            explicit RasterWorkers(unsigned count);
            ~RasterWorkers();
            RasterWorkers(const RasterWorkers &) = delete;
            RasterWorkers &operator=(const RasterWorkers &) = delete;
            void run(unsigned rows, const std::function<void(unsigned, unsigned)> &work);

        private:
            void worker(unsigned index);
            void execute(unsigned index) noexcept;
            std::mutex m_mutex;
            std::condition_variable m_ready, m_done;
            std::vector<std::thread> m_threads;
            std::function<void(unsigned, unsigned)> m_work;
            std::exception_ptr m_error;
            unsigned m_count, m_rows{}, m_pending{};
            unsigned long long m_generation{};
            bool m_stop{};
    };

} // namespace Core::Gfx
