#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHIBuffer;
using rhi::BufferDesc;
using rhi::BufferUsage;

/**
 * @brief Vulkan implementation of RHIBuffer
 *
 * Uses VMA (Vulkan Memory Allocator) for efficient memory management.
 */
class VulkanRHIBuffer : public RHIBuffer {
public:
    /**
     * @brief Create buffer with VMA
     */
    VulkanRHIBuffer(VulkanRHIDevice* device, const BufferDesc& desc);
    ~VulkanRHIBuffer() override;

    // Non-copyable, movable
    VulkanRHIBuffer(const VulkanRHIBuffer&) = delete;
    VulkanRHIBuffer& operator=(const VulkanRHIBuffer&) = delete;
    VulkanRHIBuffer(VulkanRHIBuffer&&) noexcept;
    VulkanRHIBuffer& operator=(VulkanRHIBuffer&&) noexcept;

    // RHIBuffer interface
    void* map() override;
    void* mapRange(uint64_t offset, uint64_t size) override;
    void unmap() override;
    void write(const void* data, uint64_t size, uint64_t offset = 0) override;
    uint64_t getSize() const override { return m_size; }
    BufferUsage getUsage() const override { return m_usage; }

    // Vulkan-specific accessors
    VkBuffer getVkBuffer() const { return m_buffer; }
    VmaAllocation getVmaAllocation() const { return m_allocation; }

private:
    VulkanRHIDevice* m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_allocationInfo{};

    uint64_t m_size = 0;
    BufferUsage m_usage = BufferUsage::None;
    void* m_mappedData = nullptr;
};

} // namespace Vulkan
} // namespace RHI
