#include "VulkanRHIQueue.hpp"
#include "VulkanRHIDevice.hpp"

namespace RHI {
namespace Vulkan {

VulkanRHIQueue::VulkanRHIQueue(VulkanRHIDevice* device,
                               vk::raii::Queue& queue,
                               uint32_t queueFamilyIndex,
                               QueueType type)
    : m_device(device)
    , m_queue(queue)
    , m_queueFamilyIndex(queueFamilyIndex)
    , m_type(type)
{
}

void VulkanRHIQueue::submit(const SubmitInfo& submitInfo) {
    // Convert RHI submit info to Vulkan submit info
    std::vector<vk::CommandBuffer> vkCommandBuffers;
    vkCommandBuffers.reserve(submitInfo.commandBuffers.size());

    for (auto* cmdBuffer : submitInfo.commandBuffers) {
        // TODO: Extract VkCommandBuffer from RHICommandBuffer
        // For now, this is a placeholder
    }

    std::vector<vk::Semaphore> vkWaitSemaphores;
    std::vector<vk::PipelineStageFlags> vkWaitStages;
    vkWaitSemaphores.reserve(submitInfo.waitSemaphores.size());
    vkWaitStages.reserve(submitInfo.waitSemaphores.size());

    for (auto* semaphore : submitInfo.waitSemaphores) {
        // TODO: Extract VkSemaphore from RHISemaphore
        vkWaitStages.push_back(vk::PipelineStageFlagBits::eAllCommands);
    }

    std::vector<vk::Semaphore> vkSignalSemaphores;
    vkSignalSemaphores.reserve(submitInfo.signalSemaphores.size());

    for (auto* semaphore : submitInfo.signalSemaphores) {
        // TODO: Extract VkSemaphore from RHISemaphore
    }

    vk::SubmitInfo vkSubmitInfo{
        .waitSemaphoreCount = static_cast<uint32_t>(vkWaitSemaphores.size()),
        .pWaitSemaphores = vkWaitSemaphores.data(),
        .pWaitDstStageMask = vkWaitStages.data(),
        .commandBufferCount = static_cast<uint32_t>(vkCommandBuffers.size()),
        .pCommandBuffers = vkCommandBuffers.data(),
        .signalSemaphoreCount = static_cast<uint32_t>(vkSignalSemaphores.size()),
        .pSignalSemaphores = vkSignalSemaphores.data()
    };

    // TODO: Get VkFence from submitInfo.signalFence if present
    m_queue.submit(vkSubmitInfo, nullptr);
}

void VulkanRHIQueue::submit(RHICommandBuffer* commandBuffer, RHIFence* signalFence) {
    // Convert to SubmitInfo and call the main submit method
    SubmitInfo submitInfo;
    submitInfo.commandBuffers.push_back(commandBuffer);
    submitInfo.signalFence = signalFence;
    submit(submitInfo);
}

void VulkanRHIQueue::waitIdle() {
    m_queue.waitIdle();
}

} // namespace Vulkan
} // namespace RHI
