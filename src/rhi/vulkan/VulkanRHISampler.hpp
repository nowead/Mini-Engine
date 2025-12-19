#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHISampler;
using rhi::SamplerDesc;

/**
 * @brief Vulkan implementation of RHISampler
 *
 * Wraps vk::Sampler for texture sampling configuration.
 * Samplers are immutable state objects created from SamplerDesc.
 */
class VulkanRHISampler : public RHISampler {
public:
    /**
     * @brief Create sampler from descriptor
     */
    VulkanRHISampler(VulkanRHIDevice* device, const SamplerDesc& desc);
    ~VulkanRHISampler() override;

    // Non-copyable, movable
    VulkanRHISampler(const VulkanRHISampler&) = delete;
    VulkanRHISampler& operator=(const VulkanRHISampler&) = delete;
    VulkanRHISampler(VulkanRHISampler&&) noexcept;
    VulkanRHISampler& operator=(VulkanRHISampler&&) noexcept;

    // Vulkan-specific accessors
    vk::Sampler getVkSampler() const { return *m_sampler; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::Sampler m_sampler;
};

} // namespace Vulkan
} // namespace RHI
