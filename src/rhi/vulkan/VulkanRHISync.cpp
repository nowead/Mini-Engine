#include "VulkanRHISync.hpp"
#include "VulkanRHIDevice.hpp"

namespace RHI {
namespace Vulkan {

// ============================================================================
// VulkanRHIFence Implementation
// ============================================================================

VulkanRHIFence::VulkanRHIFence(VulkanRHIDevice* device, bool signaled)
    : m_device(device)
    , m_fence(nullptr)
{
    vk::FenceCreateInfo fenceInfo;
    if (signaled) {
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
    }

    m_fence = vk::raii::Fence(m_device->getVkDevice(), fenceInfo);
}

VulkanRHIFence::~VulkanRHIFence() {
    // RAII handles cleanup automatically
}

VulkanRHIFence::VulkanRHIFence(VulkanRHIFence&& other) noexcept
    : m_device(other.m_device)
    , m_fence(std::move(other.m_fence))
{
}

VulkanRHIFence& VulkanRHIFence::operator=(VulkanRHIFence&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_fence = std::move(other.m_fence);
    }
    return *this;
}

bool VulkanRHIFence::wait(uint64_t timeout) {
    vk::Result result = m_device->getVkDevice().waitForFences(*m_fence, VK_TRUE, timeout);

    if (result == vk::Result::eSuccess) {
        return true;
    } else if (result == vk::Result::eTimeout) {
        return false;
    } else {
        throw std::runtime_error("Fence wait failed");
    }
}

bool VulkanRHIFence::isSignaled() const {
    // Use a zero timeout wait to check status
    vk::Result result = m_device->getVkDevice().waitForFences(*m_fence, VK_TRUE, 0);
    return result == vk::Result::eSuccess;
}

void VulkanRHIFence::reset() {
    m_device->getVkDevice().resetFences(*m_fence);
}

// ============================================================================
// VulkanRHISemaphore Implementation
// ============================================================================

VulkanRHISemaphore::VulkanRHISemaphore(VulkanRHIDevice* device)
    : m_device(device)
    , m_semaphore(nullptr)
{
    vk::SemaphoreCreateInfo semaphoreInfo;
    m_semaphore = vk::raii::Semaphore(m_device->getVkDevice(), semaphoreInfo);
}

VulkanRHISemaphore::~VulkanRHISemaphore() {
    // RAII handles cleanup automatically
}

VulkanRHISemaphore::VulkanRHISemaphore(VulkanRHISemaphore&& other) noexcept
    : m_device(other.m_device)
    , m_semaphore(std::move(other.m_semaphore))
{
}

VulkanRHISemaphore& VulkanRHISemaphore::operator=(VulkanRHISemaphore&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_semaphore = std::move(other.m_semaphore);
    }
    return *this;
}

} // namespace Vulkan
} // namespace RHI
