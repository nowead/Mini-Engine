#pragma once

#include "WebGPUCommon.hpp"
#include <rhi/RHIQueue.hpp>

namespace RHI {
namespace WebGPU {

class WebGPURHIDevice;

/**
 * @brief WebGPU implementation of RHIQueue
 *
 * WebGPU has a single unified queue that handles all operations.
 */
class WebGPURHIQueue : public rhi::RHIQueue {
public:
    WebGPURHIQueue(WebGPURHIDevice* device, WGPUQueue queue);
    ~WebGPURHIQueue() override = default;

    // RHIQueue interface
    void submit(const rhi::SubmitInfo& submitInfo) override;
    void submit(rhi::RHICommandBuffer* commandBuffer, rhi::RHIFence* signalFence = nullptr) override;
    void submit(rhi::RHICommandBuffer* commandBuffer,
                rhi::RHISemaphore* waitSemaphore,
                rhi::RHISemaphore* signalSemaphore,
                rhi::RHIFence* signalFence = nullptr) override;

    void waitIdle() override;
    rhi::QueueType getType() const override { return rhi::QueueType::Graphics; }

    // WebGPU-specific
    WGPUQueue getWGPUQueue() { return m_queue; }

private:
    WebGPURHIDevice* m_device;
    WGPUQueue m_queue;
};

} // namespace WebGPU
} // namespace RHI
