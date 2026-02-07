#pragma once

#include "RHITypes.hpp"
#include <vector>

namespace rhi {

// Forward declarations
class RHICommandBuffer;
class RHIFence;
class RHISemaphore;
class RHITimelineSemaphore;
class RHISwapchain;

/**
 * @brief Submit info for queue submission
 */
struct TimelineWait {
    RHITimelineSemaphore* semaphore = nullptr;
    uint64_t value = 0;
};

struct TimelineSignal {
    RHITimelineSemaphore* semaphore = nullptr;
    uint64_t value = 0;
};

struct SubmitInfo {
    std::vector<RHICommandBuffer*> commandBuffers;

    // Binary semaphore synchronization (optional)
    std::vector<RHISemaphore*> waitSemaphores;      // Semaphores to wait on before execution
    std::vector<RHISemaphore*> signalSemaphores;    // Semaphores to signal after execution

    // Timeline semaphore synchronization (optional, Phase 3.2)
    std::vector<TimelineWait> timelineWaits;
    std::vector<TimelineSignal> timelineSignals;

    RHIFence* signalFence = nullptr;  // Fence to signal after execution (optional)

    SubmitInfo() = default;
};

/**
 * @brief Queue interface for command submission
 *
 * Queues are used to submit command buffers for execution on the GPU.
 * Different queue types support different operations (Graphics, Compute, Transfer).
 */
class RHIQueue {
public:
    virtual ~RHIQueue() = default;

    /**
     * @brief Submit command buffers to the queue
     * @param submitInfo Submit information including command buffers and synchronization
     *
     * The command buffers will be executed in order.
     */
    virtual void submit(const SubmitInfo& submitInfo) = 0;

    /**
     * @brief Submit a single command buffer with optional fence
     * @param commandBuffer Command buffer to submit
     * @param signalFence Fence to signal after execution (optional)
     *
     * Convenience method for simple submissions.
     */
    virtual void submit(RHICommandBuffer* commandBuffer, RHIFence* signalFence = nullptr) = 0;

    /**
     * @brief Submit a single command buffer with full synchronization
     * @param commandBuffer Command buffer to submit
     * @param waitSemaphore Semaphore to wait on before execution (optional)
     * @param signalSemaphore Semaphore to signal after execution (optional)
     * @param signalFence Fence to signal after execution (optional)
     *
     * Convenience method for frame rendering with semaphore sync.
     */
    virtual void submit(RHICommandBuffer* commandBuffer,
                       RHISemaphore* waitSemaphore,
                       RHISemaphore* signalSemaphore,
                       RHIFence* signalFence = nullptr) = 0;

    /**
     * @brief Wait for all operations on this queue to complete
     *
     * Blocks the CPU until the queue is idle.
     */
    virtual void waitIdle() = 0;

    /**
     * @brief Get the queue type
     * @return Queue type
     */
    virtual QueueType getType() const = 0;
};

} // namespace rhi
