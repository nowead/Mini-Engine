#include "VulkanRHIQueue.hpp"
#include "VulkanRHIDevice.hpp"
#include "VulkanRHICommandEncoder.hpp"
#include "VulkanRHISync.hpp"

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
    // Convert RHI command buffers to Vulkan command buffers
    std::vector<vk::CommandBuffer> vkCommandBuffers;
    vkCommandBuffers.reserve(submitInfo.commandBuffers.size());

    for (auto* cmdBuffer : submitInfo.commandBuffers) {
        auto* vulkanCmdBuffer = static_cast<VulkanRHICommandBuffer*>(cmdBuffer);
        if (vulkanCmdBuffer) {
            vkCommandBuffers.push_back(vulkanCmdBuffer->getVkCommandBuffer());
        }
    }

    // Convert wait semaphores
    std::vector<vk::Semaphore> vkWaitSemaphores;
    std::vector<vk::PipelineStageFlags> vkWaitStages;
    vkWaitSemaphores.reserve(submitInfo.waitSemaphores.size());
    vkWaitStages.reserve(submitInfo.waitSemaphores.size());

    for (auto* semaphore : submitInfo.waitSemaphores) {
        auto* vulkanSemaphore = static_cast<VulkanRHISemaphore*>(semaphore);
        if (vulkanSemaphore) {
            vkWaitSemaphores.push_back(vulkanSemaphore->getVkSemaphore());
            vkWaitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        }
    }

    // Convert signal semaphores
    std::vector<vk::Semaphore> vkSignalSemaphores;
    vkSignalSemaphores.reserve(submitInfo.signalSemaphores.size());

    for (auto* semaphore : submitInfo.signalSemaphores) {
        auto* vulkanSemaphore = static_cast<VulkanRHISemaphore*>(semaphore);
        if (vulkanSemaphore) {
            vkSignalSemaphores.push_back(vulkanSemaphore->getVkSemaphore());
        }
    }

    // Convert fence
    vk::Fence vkFence = nullptr;
    if (submitInfo.signalFence) {
        auto* vulkanFence = static_cast<VulkanRHIFence*>(submitInfo.signalFence);
        if (vulkanFence) {
            vkFence = vulkanFence->getVkFence();
        }
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

    m_queue.submit(vkSubmitInfo, vkFence);
}

void VulkanRHIQueue::submit(RHICommandBuffer* commandBuffer, RHIFence* signalFence) {
    if (!commandBuffer) {
        return;
    }

    auto* vulkanCmdBuffer = static_cast<VulkanRHICommandBuffer*>(commandBuffer);
    vk::CommandBuffer vkCmdBuffer = vulkanCmdBuffer->getVkCommandBuffer();

    vk::Fence vkFence = nullptr;
    if (signalFence) {
        auto* vulkanFence = static_cast<VulkanRHIFence*>(signalFence);
        vkFence = vulkanFence->getVkFence();
    }

    vk::SubmitInfo vkSubmitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &vkCmdBuffer
    };

    m_queue.submit(vkSubmitInfo, vkFence);
}

void VulkanRHIQueue::submit(RHICommandBuffer* commandBuffer,
                           RHISemaphore* waitSemaphore,
                           RHISemaphore* signalSemaphore,
                           RHIFence* signalFence) {
    if (!commandBuffer) {
        return;
    }

    auto* vulkanCmdBuffer = static_cast<VulkanRHICommandBuffer*>(commandBuffer);
    vk::CommandBuffer vkCmdBuffer = vulkanCmdBuffer->getVkCommandBuffer();

    vk::Semaphore vkWaitSemaphore = nullptr;
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    if (waitSemaphore) {
        auto* vulkanSemaphore = static_cast<VulkanRHISemaphore*>(waitSemaphore);
        vkWaitSemaphore = vulkanSemaphore->getVkSemaphore();
    }

    vk::Semaphore vkSignalSemaphore = nullptr;
    if (signalSemaphore) {
        auto* vulkanSemaphore = static_cast<VulkanRHISemaphore*>(signalSemaphore);
        vkSignalSemaphore = vulkanSemaphore->getVkSemaphore();
    }

    vk::Fence vkFence = nullptr;
    if (signalFence) {
        auto* vulkanFence = static_cast<VulkanRHIFence*>(signalFence);
        vkFence = vulkanFence->getVkFence();
    }

    vk::SubmitInfo vkSubmitInfo{
        .waitSemaphoreCount = waitSemaphore ? 1u : 0u,
        .pWaitSemaphores = waitSemaphore ? &vkWaitSemaphore : nullptr,
        .pWaitDstStageMask = waitSemaphore ? &waitStage : nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkCmdBuffer,
        .signalSemaphoreCount = signalSemaphore ? 1u : 0u,
        .pSignalSemaphores = signalSemaphore ? &vkSignalSemaphore : nullptr
    };

    m_queue.submit(vkSubmitInfo, vkFence);
}

void VulkanRHIQueue::waitIdle() {
    m_queue.waitIdle();
}

} // namespace Vulkan
} // namespace RHI
