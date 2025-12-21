#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHIShader;
using rhi::ShaderDesc;
using rhi::ShaderStage;
using rhi::ShaderLanguage;

/**
 * @brief Vulkan implementation of RHIShader
 *
 * Wraps vk::ShaderModule for compiled shader modules.
 * Supports SPIR-V binary input primarily.
 */
class VulkanRHIShader : public RHIShader {
public:
    /**
     * @brief Create shader module from descriptor
     */
    VulkanRHIShader(VulkanRHIDevice* device, const ShaderDesc& desc);
    ~VulkanRHIShader() override;

    // Non-copyable, movable
    VulkanRHIShader(const VulkanRHIShader&) = delete;
    VulkanRHIShader& operator=(const VulkanRHIShader&) = delete;
    VulkanRHIShader(VulkanRHIShader&&) noexcept;
    VulkanRHIShader& operator=(VulkanRHIShader&&) noexcept;

    // RHIShader interface
    ShaderStage getStage() const override { return m_stage; }
    const std::string& getEntryPoint() const override { return m_entryPoint; }

    // Vulkan-specific accessors
    vk::ShaderModule getVkShaderModule() const { return *m_shaderModule; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::ShaderModule m_shaderModule;
    ShaderStage m_stage;
    std::string m_entryPoint;
};

} // namespace Vulkan
} // namespace RHI
