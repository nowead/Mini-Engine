#include <rhi/vulkan/VulkanRHISync.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>

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

// ============================================================================
// VulkanRHITimelineSemaphore Implementation
// ============================================================================

VulkanRHITimelineSemaphore::VulkanRHITimelineSemaphore(VulkanRHIDevice* device, uint64_t initialValue)
    : m_device(device)
    , m_semaphore(nullptr)
{
    vk::SemaphoreTypeCreateInfo timelineInfo;
    timelineInfo.semaphoreType = vk::SemaphoreType::eTimeline;
    timelineInfo.initialValue = initialValue;

    vk::SemaphoreCreateInfo semaphoreInfo;
    semaphoreInfo.pNext = &timelineInfo;

    m_semaphore = vk::raii::Semaphore(m_device->getVkDevice(), semaphoreInfo);
}

VulkanRHITimelineSemaphore::~VulkanRHITimelineSemaphore() {
    // RAII handles cleanup
}

VulkanRHITimelineSemaphore::VulkanRHITimelineSemaphore(VulkanRHITimelineSemaphore&& other) noexcept
    : m_device(other.m_device)
    , m_semaphore(std::move(other.m_semaphore))
{
}

VulkanRHITimelineSemaphore& VulkanRHITimelineSemaphore::operator=(VulkanRHITimelineSemaphore&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_semaphore = std::move(other.m_semaphore);
    }
    return *this;
}

uint64_t VulkanRHITimelineSemaphore::getCompletedValue() const {
    return m_semaphore.getCounterValue();
}

void VulkanRHITimelineSemaphore::wait(uint64_t value, uint64_t timeout) {
    vk::SemaphoreWaitInfo waitInfo;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &(*m_semaphore);
    waitInfo.pValues = &value;

    auto result = m_device->getVkDevice().waitSemaphores(waitInfo, timeout);
    if (result != vk::Result::eSuccess && result != vk::Result::eTimeout) {
        throw std::runtime_error("Timeline semaphore wait failed");
    }
}

void VulkanRHITimelineSemaphore::signal(uint64_t value) {
    vk::SemaphoreSignalInfo signalInfo;
    signalInfo.semaphore = *m_semaphore;
    signalInfo.value = value;

    m_device->getVkDevice().signalSemaphore(signalInfo);
}

} // namespace Vulkan
} // namespace RHI
