#pragma once

#include <cstdint>

namespace rhi {

/**
 * @brief Fence for CPU-GPU synchronization
 *
 * Fences are used to synchronize the CPU with GPU work. They can be waited on
 * by the CPU to ensure GPU work has completed.
 */
class RHIFence {
public:
    virtual ~RHIFence() = default;

    /**
     * @brief Wait for the fence to be signaled
     * @param timeout Timeout in nanoseconds (UINT64_MAX for infinite wait)
     * @return true if fence was signaled, false if timeout occurred
     */
    virtual bool wait(uint64_t timeout = UINT64_MAX) = 0;

    /**
     * @brief Check if the fence is currently signaled
     * @return true if signaled, false otherwise
     */
    virtual bool isSignaled() const = 0;

    /**
     * @brief Reset the fence to unsignaled state
     */
    virtual void reset() = 0;
};

/**
 * @brief Semaphore for GPU-GPU synchronization
 *
 * Semaphores are used to synchronize GPU operations across different queues.
 * Unlike fences, they cannot be directly queried or manipulated by the CPU.
 * They are used in queue submit operations to signal and wait.
 */
class RHISemaphore {
public:
    virtual ~RHISemaphore() = default;

    // Semaphores are opaque objects used only in queue submissions
    // No CPU-side operations are exposed
};

/**
 * @brief Timeline semaphore for fine-grained GPU-GPU and CPU-GPU synchronization
 *
 * Timeline semaphores maintain a monotonically increasing 64-bit counter.
 * They can be signaled and waited on from both CPU and GPU, enabling
 * precise synchronization between async compute and graphics queues.
 */
class RHITimelineSemaphore {
public:
    virtual ~RHITimelineSemaphore() = default;

    virtual uint64_t getCompletedValue() const = 0;
    virtual void wait(uint64_t value, uint64_t timeout = UINT64_MAX) = 0;
    virtual void signal(uint64_t value) = 0;
};

} // namespace rhi
