#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHIFence;
using rhi::RHISemaphore;
using rhi::RHITimelineSemaphore;

/**
 * @brief Vulkan implementation of RHIFence
 *
 * Wraps vk::Fence for CPU-GPU synchronization.
 */
class VulkanRHIFence : public RHIFence {
public:
    /**
     * @brief Create fence
     * @param device Device to create fence on
     * @param signaled Initial state (true = signaled, false = unsignaled)
     */
    VulkanRHIFence(VulkanRHIDevice* device, bool signaled = false);
    ~VulkanRHIFence() override;

    // Non-copyable, movable
    VulkanRHIFence(const VulkanRHIFence&) = delete;
    VulkanRHIFence& operator=(const VulkanRHIFence&) = delete;
    VulkanRHIFence(VulkanRHIFence&&) noexcept;
    VulkanRHIFence& operator=(VulkanRHIFence&&) noexcept;

    // RHIFence interface
    bool wait(uint64_t timeout = UINT64_MAX) override;
    bool isSignaled() const override;
    void reset() override;

    // Vulkan-specific accessors
    vk::Fence getVkFence() const { return *m_fence; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::Fence m_fence;
};

/**
 * @brief Vulkan implementation of RHISemaphore
 *
 * Wraps vk::Semaphore for GPU-GPU synchronization.
 * Semaphores are opaque objects used only in queue submissions.
 */
class VulkanRHISemaphore : public RHISemaphore {
public:
    /**
     * @brief Create semaphore
     */
    explicit VulkanRHISemaphore(VulkanRHIDevice* device);
    ~VulkanRHISemaphore() override;

    // Non-copyable, movable
    VulkanRHISemaphore(const VulkanRHISemaphore&) = delete;
    VulkanRHISemaphore& operator=(const VulkanRHISemaphore&) = delete;
    VulkanRHISemaphore(VulkanRHISemaphore&&) noexcept;
    VulkanRHISemaphore& operator=(VulkanRHISemaphore&&) noexcept;

    // Vulkan-specific accessors
    vk::Semaphore getVkSemaphore() const { return *m_semaphore; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::Semaphore m_semaphore;
};

/**
 * @brief Vulkan implementation of RHITimelineSemaphore
 *
 * Wraps VkSemaphore with VK_SEMAPHORE_TYPE_TIMELINE for fine-grained
 * CPU-GPU and GPU-GPU synchronization across async compute and graphics queues.
 */
class VulkanRHITimelineSemaphore : public RHITimelineSemaphore {
public:
    VulkanRHITimelineSemaphore(VulkanRHIDevice* device, uint64_t initialValue = 0);
    ~VulkanRHITimelineSemaphore() override;

    VulkanRHITimelineSemaphore(const VulkanRHITimelineSemaphore&) = delete;
    VulkanRHITimelineSemaphore& operator=(const VulkanRHITimelineSemaphore&) = delete;
    VulkanRHITimelineSemaphore(VulkanRHITimelineSemaphore&&) noexcept;
    VulkanRHITimelineSemaphore& operator=(VulkanRHITimelineSemaphore&&) noexcept;

    uint64_t getCompletedValue() const override;
    void wait(uint64_t value, uint64_t timeout = UINT64_MAX) override;
    void signal(uint64_t value) override;

    vk::Semaphore getVkSemaphore() const { return *m_semaphore; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::Semaphore m_semaphore;
};

} // namespace Vulkan
} // namespace RHI
