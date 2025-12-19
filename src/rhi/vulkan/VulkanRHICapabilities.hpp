#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Bring RHI types into scope
using rhi::RHICapabilities;
using rhi::RHILimits;
using rhi::RHIFeatures;
using rhi::TextureFormat;
using rhi::TextureUsage;
using rhi::TextureDimension;

/**
 * @brief Vulkan implementation of RHICapabilities
 *
 * Queries and exposes hardware/API capabilities for the Vulkan backend.
 */
class VulkanRHICapabilities : public RHICapabilities {
public:
    /**
     * @brief Query capabilities from physical device
     */
    explicit VulkanRHICapabilities(const vk::raii::PhysicalDevice& physicalDevice);
    ~VulkanRHICapabilities() override = default;

    // RHICapabilities interface
    const RHILimits& getLimits() const override { return m_limits; }
    const RHIFeatures& getFeatures() const override { return m_features; }

    bool isFormatSupported(TextureFormat format, TextureUsage usage) const override;
    bool isSampleCountSupported(TextureFormat format, uint32_t sampleCount) const override;

private:
    void queryLimits(const vk::raii::PhysicalDevice& physicalDevice);
    void queryFeatures(const vk::raii::PhysicalDevice& physicalDevice);

    RHILimits m_limits{};
    RHIFeatures m_features{};
    vk::PhysicalDeviceProperties m_deviceProperties;
    vk::PhysicalDeviceFeatures m_deviceFeatures;
};

} // namespace Vulkan
} // namespace RHI
