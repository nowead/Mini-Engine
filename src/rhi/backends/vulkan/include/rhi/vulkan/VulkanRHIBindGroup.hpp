#pragma once

#include "VulkanCommon.hpp"
#include <vector>

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHIBindGroupLayout;
using rhi::RHIBindGroup;
using rhi::BindGroupLayoutDesc;
using rhi::BindGroupDesc;

/**
 * @brief Vulkan implementation of RHIBindGroupLayout
 *
 * Wraps vk::DescriptorSetLayout which defines the structure of descriptor sets.
 */
class VulkanRHIBindGroupLayout : public RHIBindGroupLayout {
public:
    /**
     * @brief Create a descriptor set layout
     * @param device Device to create layout on
     * @param desc Layout descriptor
     */
    VulkanRHIBindGroupLayout(VulkanRHIDevice* device, const BindGroupLayoutDesc& desc);
    ~VulkanRHIBindGroupLayout() override;

    // Non-copyable, movable
    VulkanRHIBindGroupLayout(const VulkanRHIBindGroupLayout&) = delete;
    VulkanRHIBindGroupLayout& operator=(const VulkanRHIBindGroupLayout&) = delete;
    VulkanRHIBindGroupLayout(VulkanRHIBindGroupLayout&&) noexcept;
    VulkanRHIBindGroupLayout& operator=(VulkanRHIBindGroupLayout&&) noexcept;

    // Vulkan-specific accessors
    vk::DescriptorSetLayout getVkDescriptorSetLayout() const { return *m_layout; }
    const std::vector<vk::DescriptorPoolSize>& getPoolSizes() const { return m_poolSizes; }
    const std::vector<rhi::BindGroupLayoutEntry>& getEntries() const { return m_entries; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::DescriptorSetLayout m_layout;

    // Pool sizes needed for allocating descriptor sets from this layout
    std::vector<vk::DescriptorPoolSize> m_poolSizes;
    // Original layout entries for bind group creation
    std::vector<rhi::BindGroupLayoutEntry> m_entries;
};

/**
 * @brief Vulkan implementation of RHIBindGroup
 *
 * Wraps vk::DescriptorSet which binds actual resources to the layout.
 * Note: Descriptor sets are allocated from a pool managed by VulkanRHIDevice.
 */
class VulkanRHIBindGroup : public RHIBindGroup {
public:
    /**
     * @brief Create a bind group (descriptor set)
     * @param device Device to create bind group on
     * @param desc Bind group descriptor
     */
    VulkanRHIBindGroup(VulkanRHIDevice* device, const BindGroupDesc& desc);
    ~VulkanRHIBindGroup() override;

    // Non-copyable, movable
    VulkanRHIBindGroup(const VulkanRHIBindGroup&) = delete;
    VulkanRHIBindGroup& operator=(const VulkanRHIBindGroup&) = delete;
    VulkanRHIBindGroup(VulkanRHIBindGroup&&) noexcept;
    VulkanRHIBindGroup& operator=(VulkanRHIBindGroup&&) noexcept;

    // Vulkan-specific accessors
    vk::DescriptorSet getVkDescriptorSet() const { return *m_descriptorSet; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::DescriptorSet m_descriptorSet;
};

} // namespace Vulkan
} // namespace RHI
