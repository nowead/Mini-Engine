#include "VulkanRHIBuffer.hpp"
#include "VulkanRHIDevice.hpp"

namespace RHI {
namespace Vulkan {

VulkanRHIBuffer::VulkanRHIBuffer(VulkanRHIDevice* device, const BufferDesc& desc)
    : m_device(device)
    , m_size(desc.size)
    , m_usage(desc.usage)
{
    // Convert RHI buffer usage to Vulkan buffer usage
    VkBufferUsageFlags vkUsage = static_cast<VkBufferUsageFlags>(ToVkBufferUsage(desc.usage));

    // Create buffer create info
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = vkUsage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Determine VMA usage based on buffer usage
    VmaAllocationCreateInfo allocInfo{};

    // If buffer needs to be mapped (Uniform, MapRead, MapWrite), use CPU-visible memory
    if (hasFlag(desc.usage, BufferUsage::Uniform) ||
        hasFlag(desc.usage, BufferUsage::MapRead) ||
        hasFlag(desc.usage, BufferUsage::MapWrite)) {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        // Device-local memory for GPU-only buffers (Vertex, Index, Storage)
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    // Create buffer with VMA
    VkResult result = vmaCreateBuffer(
        m_device->getVmaAllocator(),
        &bufferInfo,
        &allocInfo,
        &m_buffer,
        &m_allocation,
        &m_allocationInfo
    );

    CheckVkResult(result, "vmaCreateBuffer");

    // If buffer was created mapped, store the mapped pointer
    if (allocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        m_mappedData = m_allocationInfo.pMappedData;
    }
}

VulkanRHIBuffer::~VulkanRHIBuffer() {
    if (m_buffer != VK_NULL_HANDLE) {
        // Unmap if currently mapped
        if (m_mappedData != nullptr && !(m_allocationInfo.pMappedData)) {
            vmaUnmapMemory(m_device->getVmaAllocator(), m_allocation);
        }

        // Destroy buffer and free memory
        vmaDestroyBuffer(m_device->getVmaAllocator(), m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

VulkanRHIBuffer::VulkanRHIBuffer(VulkanRHIBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_buffer(other.m_buffer)
    , m_allocation(other.m_allocation)
    , m_allocationInfo(other.m_allocationInfo)
    , m_size(other.m_size)
    , m_usage(other.m_usage)
    , m_mappedData(other.m_mappedData)
{
    other.m_buffer = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
    other.m_mappedData = nullptr;
}

VulkanRHIBuffer& VulkanRHIBuffer::operator=(VulkanRHIBuffer&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        if (m_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_device->getVmaAllocator(), m_buffer, m_allocation);
        }

        // Move from other
        m_device = other.m_device;
        m_buffer = other.m_buffer;
        m_allocation = other.m_allocation;
        m_allocationInfo = other.m_allocationInfo;
        m_size = other.m_size;
        m_usage = other.m_usage;
        m_mappedData = other.m_mappedData;

        // Reset other
        other.m_buffer = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
        other.m_mappedData = nullptr;
    }
    return *this;
}

void* VulkanRHIBuffer::map() {
    if (m_mappedData != nullptr) {
        // Already mapped (persistent mapping)
        return m_mappedData;
    }

    void* data = nullptr;
    VkResult result = vmaMapMemory(m_device->getVmaAllocator(), m_allocation, &data);
    CheckVkResult(result, "vmaMapMemory");

    m_mappedData = data;
    return data;
}

void* VulkanRHIBuffer::mapRange(uint64_t offset, uint64_t size) {
    // VMA doesn't support mapping ranges directly, so we map the entire buffer
    // and return a pointer to the offset
    void* data = map();
    return static_cast<uint8_t*>(data) + offset;
}

void VulkanRHIBuffer::unmap() {
    if (m_mappedData != nullptr && m_allocationInfo.pMappedData == nullptr) {
        // Only unmap if it's not a persistent mapping
        vmaUnmapMemory(m_device->getVmaAllocator(), m_allocation);
        m_mappedData = nullptr;
    }
}

void VulkanRHIBuffer::write(const void* data, uint64_t size, uint64_t offset) {
    // Map, write, unmap pattern
    void* mapped = mapRange(offset, size);
    memcpy(mapped, data, size);

    // Flush if memory is not coherent
    vmaFlushAllocation(m_device->getVmaAllocator(), m_allocation, offset, size);

    unmap();
}

} // namespace Vulkan
} // namespace RHI
