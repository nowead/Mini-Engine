// ============================================================================
// Roadmap D2 -- standalone correctness test for utils::ThreadPool.
//
// Pure std-library test (no Vulkan / GLFW). Validates:
//   1. submit() returns results through std::future
//   2. tasks actually run concurrently across workers
//   3. exceptions propagate to the caller via future
//   4. waitForAll() acts as a barrier (all work observed complete)
//   5. clean shutdown drains queued work (no leaks, no hangs)
//
// Exit code 0 = all checks passed; non-zero = failure (printed to stderr).
// ============================================================================

#include "src/utils/ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::printf("  [ok] %s\n", what);
    } else {
        std::fprintf(stderr, "  [FAIL] %s\n", what);
        ++g_failures;
    }
}

// 1. submit returns results via future.
void testFutureResults() {
    std::printf("test: future results\n");
    utils::ThreadPool pool(4);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() { return i * i; }));
    }

    long long sum = 0;
    for (int i = 0; i < 100; ++i) {
        sum += futures[i].get();
    }
    long long expected = 0;
    for (int i = 0; i < 100; ++i) expected += static_cast<long long>(i) * i;
    check(sum == expected, "sum of squares matches");
}

// 2. tasks run concurrently -- N blocking tasks on N workers all reach a
//    barrier point before any is released.
void testConcurrency() {
    std::printf("test: concurrency\n");
    const std::size_t workers = 4;
    utils::ThreadPool pool(workers);

    std::atomic<int> arrived{0};
    std::atomic<bool> release{false};
    std::set<std::thread::id> seenThreads;
    std::mutex seenMutex;

    std::vector<std::future<void>> futures;
    for (std::size_t i = 0; i < workers; ++i) {
        futures.push_back(pool.submit([&]() {
            {
                std::lock_guard<std::mutex> lock(seenMutex);
                seenThreads.insert(std::this_thread::get_id());
            }
            arrived.fetch_add(1);
            while (!release.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // All workers must reach the barrier even though none has been released:
    // only possible if they run in parallel.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (arrived.load() < static_cast<int>(workers) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(arrived.load() == static_cast<int>(workers),
          "all workers ran concurrently (reached barrier)");

    release.store(true);
    for (auto& f : futures) f.get();
    check(seenThreads.size() == workers, "distinct worker threads used");
}

// 3. exception propagation.
void testExceptionPropagation() {
    std::printf("test: exception propagation\n");
    utils::ThreadPool pool(2);

    auto fut = pool.submit([]() -> int {
        throw std::runtime_error("boom");
    });

    bool caught = false;
    try {
        fut.get();
    } catch (const std::runtime_error& e) {
        caught = (std::string(e.what()) == "boom");
    }
    check(caught, "exception thrown in task surfaces via future.get()");
}

// 4. waitForAll is a barrier across all outstanding work.
void testWaitForAll() {
    std::printf("test: waitForAll\n");
    utils::ThreadPool pool;  // default worker count

    std::atomic<int> counter{0};
    const int n = 500;
    for (int i = 0; i < n; ++i) {
        pool.submit([&counter]() {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            counter.fetch_add(1);
        });
    }

    pool.waitForAll();
    check(counter.load() == n, "all tasks completed before waitForAll returned");

    // Pool must be reusable after a barrier.
    auto fut = pool.submit([]() { return 7; });
    check(fut.get() == 7, "pool reusable after waitForAll");
}

// 5. shutdown drains queued work even without an explicit waitForAll.
void testShutdownDrains() {
    std::printf("test: shutdown drains queue\n");
    std::atomic<int> counter{0};
    const int n = 200;
    {
        utils::ThreadPool pool(2);
        for (int i = 0; i < n; ++i) {
            pool.submit([&counter]() {
                std::this_thread::sleep_for(std::chrono::microseconds(20));
                counter.fetch_add(1);
            });
        }
        // No waitForAll -- destructor must drain the remaining queue.
    }
    check(counter.load() == n, "destructor drained all queued tasks");
}

}  // namespace

int main() {
    // Unbuffered so progress is visible even if a hang occurs (output may be
    // redirected to a file, where stdout would otherwise be fully buffered).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    std::printf("ThreadPool test (hardware_concurrency=%u, default workers=%zu)\n",
                std::thread::hardware_concurrency(),
                utils::ThreadPool::defaultWorkerCount());

    testFutureResults();
    testConcurrency();
    testExceptionPropagation();
    testWaitForAll();
    testShutdownDrains();

    if (g_failures == 0) {
        std::printf("\nALL THREADPOOL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d THREADPOOL TEST(S) FAILED\n", g_failures);
    return 1;
}
