#include <rhi/webgpu/WebGPURHIBindGroup.hpp>
#include <rhi/webgpu/WebGPURHIDevice.hpp>
#include <rhi/webgpu/WebGPURHIBuffer.hpp>
#include <rhi/webgpu/WebGPURHITexture.hpp>
#include <rhi/webgpu/WebGPURHISampler.hpp>
#include <stdexcept>
#include <vector>

namespace RHI {
namespace WebGPU {

// ============================================================================
// WebGPURHIBindGroupLayout Implementation
// ============================================================================

WebGPURHIBindGroupLayout::WebGPURHIBindGroupLayout(WebGPURHIDevice* device,
                                                   const BindGroupLayoutDesc& desc)
    : m_device(device)
{
    std::vector<WGPUBindGroupLayoutEntry> wgpuEntries;
    wgpuEntries.reserve(desc.entries.size());

    for (const auto& entry : desc.entries) {
        WGPUBindGroupLayoutEntry wgpuEntry{};
        wgpuEntry.binding = entry.binding;
        wgpuEntry.visibility = static_cast<WGPUShaderStageFlags>(ToWGPUShaderStage(entry.visibility));

        // Set binding type
        switch (entry.type) {
            case rhi::BindingType::UniformBuffer:
                wgpuEntry.buffer.type = WGPUBufferBindingType_Uniform;
                wgpuEntry.buffer.hasDynamicOffset = entry.hasDynamicOffset;
                wgpuEntry.buffer.minBindingSize = entry.minBufferBindingSize;
                break;

            case rhi::BindingType::StorageBuffer:
                wgpuEntry.buffer.type = WGPUBufferBindingType_Storage;
                wgpuEntry.buffer.hasDynamicOffset = entry.hasDynamicOffset;
                wgpuEntry.buffer.minBindingSize = entry.minBufferBindingSize;
                break;

            case rhi::BindingType::Sampler:
                wgpuEntry.sampler.type = WGPUSamplerBindingType_Filtering;
                break;

            case rhi::BindingType::NonFilteringSampler:
                wgpuEntry.sampler.type = WGPUSamplerBindingType_NonFiltering;
                break;

            case rhi::BindingType::ComparisonSampler:
                wgpuEntry.sampler.type = WGPUSamplerBindingType_Comparison;
                break;

            case rhi::BindingType::SampledTexture:
                wgpuEntry.texture.sampleType = WGPUTextureSampleType_Float;
                wgpuEntry.texture.viewDimension = ToWGPUTextureViewDimension(entry.textureViewDimension);
                wgpuEntry.texture.multisampled = false;
                break;

            case rhi::BindingType::DepthTexture:
                wgpuEntry.texture.sampleType = WGPUTextureSampleType_Depth;
                wgpuEntry.texture.viewDimension = ToWGPUTextureViewDimension(entry.textureViewDimension);
                wgpuEntry.texture.multisampled = false;
                break;

            case rhi::BindingType::StorageTexture:
#if defined(__EMSCRIPTEN__) && EMSCRIPTEN_VERSION_LESS_THAN(3, 1, 60)
                wgpuEntry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
#else
                wgpuEntry.storageTexture.access = entry.storageTextureReadOnly
                    ? WGPUStorageTextureAccess_ReadOnly
                    : WGPUStorageTextureAccess_WriteOnly;
#endif
                wgpuEntry.storageTexture.format = ToWGPUFormat(entry.storageTextureFormat);
                wgpuEntry.storageTexture.viewDimension = ToWGPUTextureViewDimension(entry.textureViewDimension);
                break;
        }

        wgpuEntries.push_back(wgpuEntry);
    }

    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.label = desc.label;
    layoutDesc.entryCount = static_cast<uint32_t>(wgpuEntries.size());
    layoutDesc.entries = wgpuEntries.data();

    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device->getWGPUDevice(), &layoutDesc);
    if (!m_bindGroupLayout) {
        throw std::runtime_error("Failed to create WebGPU bind group layout");
    }
}

WebGPURHIBindGroupLayout::~WebGPURHIBindGroupLayout() {
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
}

WebGPURHIBindGroupLayout::WebGPURHIBindGroupLayout(WebGPURHIBindGroupLayout&& other) noexcept
    : m_device(other.m_device)
    , m_bindGroupLayout(other.m_bindGroupLayout)
{
    other.m_bindGroupLayout = nullptr;
}

WebGPURHIBindGroupLayout& WebGPURHIBindGroupLayout::operator=(WebGPURHIBindGroupLayout&& other) noexcept {
    if (this != &other) {
        if (m_bindGroupLayout) {
            wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        }

        m_device = other.m_device;
        m_bindGroupLayout = other.m_bindGroupLayout;

        other.m_bindGroupLayout = nullptr;
    }
    return *this;
}

// ============================================================================
// WebGPURHIBindGroup Implementation
// ============================================================================

WebGPURHIBindGroup::WebGPURHIBindGroup(WebGPURHIDevice* device, const BindGroupDesc& desc)
    : m_device(device)
{
    auto* webgpuLayout = static_cast<WebGPURHIBindGroupLayout*>(desc.layout);

    std::vector<WGPUBindGroupEntry> wgpuEntries;
    wgpuEntries.reserve(desc.entries.size());

    for (const auto& entry : desc.entries) {
        WGPUBindGroupEntry wgpuEntry{};
        wgpuEntry.binding = entry.binding;

        // Set resource based on type
        if (entry.buffer) {
            auto* webgpuBuffer = static_cast<WebGPURHIBuffer*>(entry.buffer);
            wgpuEntry.buffer = webgpuBuffer->getWGPUBuffer();
            wgpuEntry.offset = entry.bufferOffset;
            wgpuEntry.size = entry.bufferSize > 0 ? entry.bufferSize : entry.buffer->getSize();
        } else if (entry.sampler) {
            auto* webgpuSampler = static_cast<WebGPURHISampler*>(entry.sampler);
            wgpuEntry.sampler = webgpuSampler->getWGPUSampler();
        } else if (entry.textureView) {
            auto* webgpuTextureView = static_cast<WebGPURHITextureView*>(entry.textureView);
            wgpuEntry.textureView = webgpuTextureView->getWGPUTextureView();
        }

        wgpuEntries.push_back(wgpuEntry);
    }

    WGPUBindGroupDescriptor bindGroupDesc{};
    bindGroupDesc.label = desc.label;
    bindGroupDesc.layout = webgpuLayout->getWGPUBindGroupLayout();
    bindGroupDesc.entryCount = static_cast<uint32_t>(wgpuEntries.size());
    bindGroupDesc.entries = wgpuEntries.data();

    m_bindGroup = wgpuDeviceCreateBindGroup(m_device->getWGPUDevice(), &bindGroupDesc);
    if (!m_bindGroup) {
        throw std::runtime_error("Failed to create WebGPU bind group");
    }
}

WebGPURHIBindGroup::~WebGPURHIBindGroup() {
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }
}

WebGPURHIBindGroup::WebGPURHIBindGroup(WebGPURHIBindGroup&& other) noexcept
    : m_device(other.m_device)
    , m_bindGroup(other.m_bindGroup)
{
    other.m_bindGroup = nullptr;
}

WebGPURHIBindGroup& WebGPURHIBindGroup::operator=(WebGPURHIBindGroup&& other) noexcept {
    if (this != &other) {
        if (m_bindGroup) {
            wgpuBindGroupRelease(m_bindGroup);
        }

        m_device = other.m_device;
        m_bindGroup = other.m_bindGroup;

        other.m_bindGroup = nullptr;
    }
    return *this;
}

} // namespace WebGPU
} // namespace RHI
