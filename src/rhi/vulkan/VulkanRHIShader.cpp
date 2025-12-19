#include "VulkanRHIShader.hpp"
#include "VulkanRHIDevice.hpp"

namespace RHI {
namespace Vulkan {

VulkanRHIShader::VulkanRHIShader(VulkanRHIDevice* device, const ShaderDesc& desc)
    : m_device(device)
    , m_shaderModule(nullptr)
    , m_stage(desc.source.stage)
    , m_entryPoint(desc.source.entryPoint)
{
    // Vulkan primarily supports SPIR-V
    if (desc.source.language != ShaderLanguage::SPIRV) {
        throw std::runtime_error("VulkanRHIShader: Only SPIR-V shaders are supported");
    }

    // Ensure code size is valid and aligned to 4 bytes (SPIR-V requirement)
    if (desc.source.code.empty()) {
        throw std::runtime_error("VulkanRHIShader: Shader code is empty");
    }

    if (desc.source.code.size() % 4 != 0) {
        throw std::runtime_error("VulkanRHIShader: SPIR-V code size must be a multiple of 4 bytes");
    }

    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = desc.source.code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(desc.source.code.data());

    // Create shader module
    m_shaderModule = vk::raii::ShaderModule(m_device->getVkDevice(), createInfo);
}

VulkanRHIShader::~VulkanRHIShader() {
    // RAII handles cleanup automatically
}

VulkanRHIShader::VulkanRHIShader(VulkanRHIShader&& other) noexcept
    : m_device(other.m_device)
    , m_shaderModule(std::move(other.m_shaderModule))
    , m_stage(other.m_stage)
    , m_entryPoint(std::move(other.m_entryPoint))
{
}

VulkanRHIShader& VulkanRHIShader::operator=(VulkanRHIShader&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_shaderModule = std::move(other.m_shaderModule);
        m_stage = other.m_stage;
        m_entryPoint = std::move(other.m_entryPoint);
    }
    return *this;
}

} // namespace Vulkan
} // namespace RHI
