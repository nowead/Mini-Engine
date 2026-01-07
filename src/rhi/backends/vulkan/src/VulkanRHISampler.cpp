#include <rhi/vulkan/VulkanRHISampler.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>

namespace RHI {
namespace Vulkan {

VulkanRHISampler::VulkanRHISampler(VulkanRHIDevice* device, const SamplerDesc& desc)
    : m_device(device)
    , m_sampler(nullptr)
{
    vk::SamplerCreateInfo samplerInfo;

    // Filtering
    samplerInfo.magFilter = ToVkFilter(desc.magFilter);
    samplerInfo.minFilter = ToVkFilter(desc.minFilter);
    samplerInfo.mipmapMode = ToVkSamplerMipmapMode(desc.mipmapFilter);

    // Address modes
    samplerInfo.addressModeU = ToVkSamplerAddressMode(desc.addressModeU);
    samplerInfo.addressModeV = ToVkSamplerAddressMode(desc.addressModeV);
    samplerInfo.addressModeW = ToVkSamplerAddressMode(desc.addressModeW);

    // LOD
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.minLod = desc.lodMinClamp;
    samplerInfo.maxLod = desc.lodMaxClamp;

    // Anisotropic filtering
    samplerInfo.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;

    // Comparison (for shadow sampling)
    samplerInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = ToVkCompareOp(desc.compareOp);

    // Border color
    // Note: Vulkan has predefined border colors, RHI's ClearColorValue needs mapping
    // For now, use transparent black as default
    samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;

    // Unnormalized coordinates (always false for standard samplers)
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    // Create sampler
    m_sampler = vk::raii::Sampler(m_device->getVkDevice(), samplerInfo);
}

VulkanRHISampler::~VulkanRHISampler() {
    // RAII handles cleanup automatically
}

VulkanRHISampler::VulkanRHISampler(VulkanRHISampler&& other) noexcept
    : m_device(other.m_device)
    , m_sampler(std::move(other.m_sampler))
{
}

VulkanRHISampler& VulkanRHISampler::operator=(VulkanRHISampler&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_sampler = std::move(other.m_sampler);
    }
    return *this;
}

} // namespace Vulkan
} // namespace RHI
