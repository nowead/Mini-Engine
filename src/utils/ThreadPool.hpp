#pragma once

// ============================================================================
// Roadmap D2 -- worker thread pool (C++20 std::jthread) + task submit / wait.
//
// Foundation for parallel command recording (D3): independent RenderGraph
// passes at the same dependency level are dispatched to workers, each records
// into its own primary command buffer, and the main thread waits on the level
// (waitForAll) before submitting in order.
//
// As of D2 this pool is standalone infrastructure -- nothing in the render loop
// calls it yet, so the single-threaded path is unchanged. Vulkan-only / native
// target (WebGPU has no multithreaded command recording).
//
// Concurrency model:
//   * std::jthread workers -- their destructors join automatically.
//   * Mutex-guarded task queue + plain std::condition_variable with an explicit
//     m_stopping flag. (We deliberately avoid condition_variable_any +
//     stop_token waits: that combination has deadlocked on shutdown under
//     MSVC's STL. jthread is still used purely for its auto-join semantics.)
//   * Shutdown DRAINS the queue: already-queued tasks run to completion before
//     workers exit. Safe for frame-scoped dispatch.
//   * submit() wraps the callable in a packaged_task, so exceptions propagate
//     to the caller through the returned std::future.
// ============================================================================

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace utils {

class ThreadPool {
public:
    // Default leaves one core for the main (submitting) thread.
    static std::size_t defaultWorkerCount() {
        const unsigned hw = std::thread::hardware_concurrency();
        return hw > 1 ? static_cast<std::size_t>(hw - 1) : std::size_t{1};
    }

    explicit ThreadPool(std::size_t workerCount = defaultWorkerCount());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Enqueue a callable; result (or thrown exception) is delivered via future.
    template <class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // Block until every submitted task (queued + running) has completed.
    // Used by D3 as a per-dependency-level barrier.
    void waitForAll();

    std::size_t workerCount() const { return m_workers.size(); }

private:
    void workerLoop();

    std::vector<std::jthread> m_workers;
    std::queue<std::function<void()>> m_tasks;

    std::mutex m_mutex;
    std::condition_variable m_taskAvailable;  // wakes idle workers
    std::condition_variable m_allDone;         // wakes waitForAll
    std::size_t m_outstanding = 0;             // queued + running, under m_mutex
    bool m_stopping = false;                   // set in dtor, under m_mutex
};

template <class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using Result = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<Result()>>(
        [func = std::forward<F>(f),
         ... capturedArgs = std::forward<Args>(args)]() mutable -> Result {
            return std::invoke(std::move(func), std::move(capturedArgs)...);
        });

    std::future<Result> future = task->get_future();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_outstanding;
        m_tasks.emplace([task]() { (*task)(); });
    }
    m_taskAvailable.notify_one();
    return future;
}

}  // namespace utils
