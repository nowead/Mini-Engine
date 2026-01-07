#include <rhi/webgpu/WebGPURHISampler.hpp>
#include <rhi/webgpu/WebGPURHIDevice.hpp>
#include <stdexcept>

namespace RHI {
namespace WebGPU {

WebGPURHISampler::WebGPURHISampler(WebGPURHIDevice* device, const SamplerDesc& desc)
    : m_device(device)
{
    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.label = desc.label;

    // Filtering
    samplerDesc.magFilter = ToWGPUFilterMode(desc.magFilter);
    samplerDesc.minFilter = ToWGPUFilterMode(desc.minFilter);
    samplerDesc.mipmapFilter = ToWGPUMipmapFilterMode(desc.mipmapFilter);

    // Address modes
    samplerDesc.addressModeU = ToWGPUAddressMode(desc.addressModeU);
    samplerDesc.addressModeV = ToWGPUAddressMode(desc.addressModeV);
    samplerDesc.addressModeW = ToWGPUAddressMode(desc.addressModeW);

    // LOD
    samplerDesc.lodMinClamp = desc.lodMinClamp;
    samplerDesc.lodMaxClamp = desc.lodMaxClamp;

    // Anisotropic filtering
    if (desc.anisotropyEnable) {
        samplerDesc.maxAnisotropy = static_cast<uint16_t>(desc.maxAnisotropy);
    } else {
        samplerDesc.maxAnisotropy = 1;
    }

    // Comparison (for shadow sampling)
    if (desc.compareEnable) {
        samplerDesc.compare = ToWGPUCompareFunc(desc.compareOp);
    } else {
        samplerDesc.compare = WGPUCompareFunction_Undefined;
    }

    // Create sampler
    m_sampler = wgpuDeviceCreateSampler(m_device->getWGPUDevice(), &samplerDesc);
    if (!m_sampler) {
        throw std::runtime_error("Failed to create WebGPU sampler");
    }
}

WebGPURHISampler::~WebGPURHISampler() {
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
}

WebGPURHISampler::WebGPURHISampler(WebGPURHISampler&& other) noexcept
    : m_device(other.m_device)
    , m_sampler(other.m_sampler)
{
    other.m_sampler = nullptr;
}

WebGPURHISampler& WebGPURHISampler::operator=(WebGPURHISampler&& other) noexcept {
    if (this != &other) {
        if (m_sampler) {
            wgpuSamplerRelease(m_sampler);
        }

        m_device = other.m_device;
        m_sampler = other.m_sampler;

        other.m_sampler = nullptr;
    }
    return *this;
}

} // namespace WebGPU
} // namespace RHI
