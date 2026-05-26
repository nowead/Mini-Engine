#include "ThreadPool.hpp"

namespace utils {

ThreadPool::ThreadPool(std::size_t workerCount) {
    if (workerCount == 0) {
        workerCount = 1;
    }
    m_workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        // jthread is used only for its auto-join destructor; the worker takes no
        // stop_token (shutdown is driven by m_stopping + m_taskAvailable).
        m_workers.emplace_back([this]() { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_taskAvailable.notify_all();

    // Join workers HERE, inside the destructor body, while m_mutex / m_tasks /
    // the condition variables are still alive. We cannot rely on the implicit
    // jthread join from ~m_workers: m_workers is declared first, so it is
    // destroyed LAST -- the queue and mutex would be torn down out from under
    // still-running workers, which then observe a destroyed queue and abandon
    // the remaining tasks. Joining first keeps the drain well-defined.
    for (std::jthread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_taskAvailable.wait(lock, [this]() {
                return m_stopping || !m_tasks.empty();
            });

            if (m_tasks.empty()) {
                // Predicate woke us with an empty queue -> m_stopping is set and
                // all queued work is drained, so this worker is done.
                return;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        task();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (--m_outstanding == 0) {
                m_allDone.notify_all();
            }
        }
    }
}

void ThreadPool::waitForAll() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_allDone.wait(lock, [this]() { return m_outstanding == 0; });
}

}  // namespace utils
