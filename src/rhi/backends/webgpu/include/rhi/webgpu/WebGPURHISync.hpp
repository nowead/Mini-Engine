#pragma once

#include "WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHIFence;
using rhi::RHISemaphore;

/**
 * @brief WebGPU implementation of RHIFence
 *
 * WebGPU uses queue callbacks for fence-like synchronization.
 * This implementation uses wgpuQueueOnSubmittedWorkDone.
 */
class WebGPURHIFence : public RHIFence {
public:
    WebGPURHIFence(WebGPURHIDevice* device, bool signaled = false);
    ~WebGPURHIFence() override;

    // RHIFence interface
    bool wait(uint64_t timeout = UINT64_MAX) override;
    bool isSignaled() const override { return m_signaled; }
    void reset() override { m_signaled = false; }

    // WebGPU-specific: called by queue submission
    void onQueueSubmitted(WGPUQueue queue);

private:
    WebGPURHIDevice* m_device;
    bool m_signaled = false;
    WGPUQueue m_lastQueue = nullptr;
};

/**
 * @brief WebGPU implementation of RHISemaphore
 *
 * WebGPU doesn't have explicit semaphores - GPU operations are automatically ordered.
 * This is a stub implementation for API compatibility.
 */
class WebGPURHISemaphore : public RHISemaphore {
public:
    WebGPURHISemaphore(WebGPURHIDevice* device);
    ~WebGPURHISemaphore() override;

    // WebGPU-specific: No-op, semaphores are implicit
private:
    WebGPURHIDevice* m_device;
};

} // namespace WebGPU
} // namespace RHI
