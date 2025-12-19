#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHIQueue;
using rhi::QueueType;
using rhi::SubmitInfo;
using rhi::RHICommandBuffer;
using rhi::RHIFence;
using rhi::RHISemaphore;

/**
 * @brief Vulkan implementation of RHIQueue
 *
 * Wraps vk::raii::Queue for command submission and synchronization.
 */
class VulkanRHIQueue : public RHIQueue {
public:
    /**
     * @brief Create queue wrapper
     */
    VulkanRHIQueue(VulkanRHIDevice* device,
                   vk::raii::Queue& queue,
                   uint32_t queueFamilyIndex,
                   QueueType type);

    ~VulkanRHIQueue() override = default;

    // RHIQueue interface
    void submit(const SubmitInfo& submitInfo) override;
    void submit(RHICommandBuffer* commandBuffer, RHIFence* signalFence = nullptr) override;
    void submit(RHICommandBuffer* commandBuffer,
               RHISemaphore* waitSemaphore,
               RHISemaphore* signalSemaphore,
               RHIFence* signalFence = nullptr) override;
    void waitIdle() override;
    QueueType getType() const override { return m_type; }

    // Vulkan-specific accessors
    vk::raii::Queue& getVkQueue() { return m_queue; }
    uint32_t getQueueFamilyIndex() const { return m_queueFamilyIndex; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::Queue& m_queue;
    uint32_t m_queueFamilyIndex;
    QueueType m_type;
};

} // namespace Vulkan
} // namespace RHI
