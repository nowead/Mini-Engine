#include "rhi-webgpu/WebGPURHIQueue.hpp"
#include "rhi-webgpu/WebGPURHIDevice.hpp"
#include "rhi-webgpu/WebGPURHICommandEncoder.hpp"
#include "rhi-webgpu/WebGPURHISync.hpp"

namespace RHI {
namespace WebGPU {

WebGPURHIQueue::WebGPURHIQueue(WebGPURHIDevice* device, WGPUQueue queue)
    : m_device(device)
    , m_queue(queue)
{
}

void WebGPURHIQueue::submit(const rhi::SubmitInfo& submitInfo) {
    // Convert RHI command buffers to WebGPU command buffers
    std::vector<WGPUCommandBuffer> wgpuCommandBuffers;
    wgpuCommandBuffers.reserve(submitInfo.commandBuffers.size());

    for (auto* cmdBuffer : submitInfo.commandBuffers) {
        auto* webgpuCmdBuffer = static_cast<WebGPURHICommandBuffer*>(cmdBuffer);
        wgpuCommandBuffers.push_back(webgpuCmdBuffer->getWGPUCommandBuffer());
    }

    // Submit to queue
    if (!wgpuCommandBuffers.empty()) {
        wgpuQueueSubmit(m_queue, wgpuCommandBuffers.size(), wgpuCommandBuffers.data());
    }

    // Handle fence signaling
    if (submitInfo.signalFence) {
        auto* fence = static_cast<WebGPURHIFence*>(submitInfo.signalFence);
        fence->onQueueSubmitted(m_queue);
    }

    // Note: WebGPU doesn't have explicit semaphores like Vulkan
    // Queue operations are automatically ordered
}

void WebGPURHIQueue::submit(rhi::RHICommandBuffer* commandBuffer, rhi::RHIFence* signalFence) {
    rhi::SubmitInfo submitInfo;
    submitInfo.commandBuffers = {commandBuffer};
    submitInfo.signalFence = signalFence;
    submit(submitInfo);
}

void WebGPURHIQueue::submit(rhi::RHICommandBuffer* commandBuffer,
                             rhi::RHISemaphore* waitSemaphore,
                             rhi::RHISemaphore* signalSemaphore,
                             rhi::RHIFence* signalFence) {
    // WebGPU doesn't have explicit semaphores
    // Just submit the command buffer and signal the fence
    rhi::SubmitInfo submitInfo;
    submitInfo.commandBuffers = {commandBuffer};
    submitInfo.signalFence = signalFence;
    submit(submitInfo);
}

void WebGPURHIQueue::waitIdle() {
    // Submit an empty command buffer to flush the queue
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device->getWGPUDevice(), &encoderDesc);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);

    wgpuQueueSubmit(m_queue, 1, &commandBuffer);

    wgpuCommandBufferRelease(commandBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Poll device to process pending work
#ifndef __EMSCRIPTEN__
    wgpuDevicePoll(m_device->getWGPUDevice(), true, nullptr);
#endif
}

} // namespace WebGPU
} // namespace RHI
