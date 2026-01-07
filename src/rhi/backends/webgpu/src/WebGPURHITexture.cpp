#include <rhi/webgpu/WebGPURHITexture.hpp>
#include <rhi/webgpu/WebGPURHIDevice.hpp>
#include <stdexcept>

namespace RHI {
namespace WebGPU {

// ============================================================================
// WebGPURHITextureView Implementation
// ============================================================================

WebGPURHITextureView::WebGPURHITextureView(WebGPURHIDevice* device,
                                           WGPUTexture texture,
                                           const TextureViewDesc& desc)
    : m_device(device)
    , m_format(desc.format)
    , m_dimension(desc.dimension)
{
    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.label = desc.label;
    viewDesc.format = ToWGPUFormat(desc.format);
    viewDesc.dimension = ToWGPUTextureViewDimension(desc.dimension);
    viewDesc.baseMipLevel = desc.baseMipLevel;
    viewDesc.mipLevelCount = desc.mipLevelCount;
    viewDesc.baseArrayLayer = desc.baseArrayLayer;
    viewDesc.arrayLayerCount = desc.arrayLayerCount;

    // Aspect is determined automatically by WebGPU based on format
    viewDesc.aspect = WGPUTextureAspect_All;

    m_textureView = wgpuTextureCreateView(texture, &viewDesc);
    if (!m_textureView) {
        throw std::runtime_error("Failed to create WebGPU texture view");
    }
}

// Internal constructor for swapchain
WebGPURHITextureView::WebGPURHITextureView(WebGPURHIDevice* device,
                                           WGPUTextureView textureView,
                                           TextureFormat format,
                                           TextureViewDimension dimension,
                                           bool ownsView)
    : m_device(device)
    , m_textureView(textureView)
    , m_format(format)
    , m_dimension(dimension)
    , m_ownsTextureView(ownsView)
{
}

WebGPURHITextureView::~WebGPURHITextureView() {
    if (m_ownsTextureView && m_textureView) {
        wgpuTextureViewRelease(m_textureView);
        m_textureView = nullptr;
    }
}

WebGPURHITextureView::WebGPURHITextureView(WebGPURHITextureView&& other) noexcept
    : m_device(other.m_device)
    , m_textureView(other.m_textureView)
    , m_format(other.m_format)
    , m_dimension(other.m_dimension)
    , m_ownsTextureView(other.m_ownsTextureView)
{
    other.m_textureView = nullptr;
    other.m_ownsTextureView = false;
}

WebGPURHITextureView& WebGPURHITextureView::operator=(WebGPURHITextureView&& other) noexcept {
    if (this != &other) {
        if (m_ownsTextureView && m_textureView) {
            wgpuTextureViewRelease(m_textureView);
        }

        m_device = other.m_device;
        m_textureView = other.m_textureView;
        m_format = other.m_format;
        m_dimension = other.m_dimension;
        m_ownsTextureView = other.m_ownsTextureView;

        other.m_textureView = nullptr;
        other.m_ownsTextureView = false;
    }
    return *this;
}

// ============================================================================
// WebGPURHITexture Implementation
// ============================================================================

WebGPURHITexture::WebGPURHITexture(WebGPURHIDevice* device, const TextureDesc& desc)
    : m_device(device)
    , m_format(desc.format)
    , m_dimension(desc.dimension)
    , m_size(desc.size)
    , m_mipLevels(desc.mipLevelCount)
    , m_sampleCount(desc.sampleCount)
    , m_usage(desc.usage)
{
    WGPUTextureDescriptor textureDesc{};
    textureDesc.label = desc.label;
    textureDesc.usage = ToWGPUTextureUsage(desc.usage);
    textureDesc.dimension = ToWGPUTextureDimension(desc.dimension);

    textureDesc.size.width = desc.size.width;
    textureDesc.size.height = desc.size.height;
    textureDesc.size.depthOrArrayLayers = desc.size.depth;

    textureDesc.format = ToWGPUFormat(desc.format);
    textureDesc.mipLevelCount = desc.mipLevelCount;
    textureDesc.sampleCount = desc.sampleCount;

    // View formats (allow all compatible formats by default)
    textureDesc.viewFormatCount = 0;
    textureDesc.viewFormats = nullptr;

    m_texture = wgpuDeviceCreateTexture(m_device->getWGPUDevice(), &textureDesc);
    if (!m_texture) {
        throw std::runtime_error("Failed to create WebGPU texture");
    }
}

WebGPURHITexture::~WebGPURHITexture() {
    if (m_texture) {
        wgpuTextureRelease(m_texture);
        m_texture = nullptr;
    }
}

WebGPURHITexture::WebGPURHITexture(WebGPURHITexture&& other) noexcept
    : m_device(other.m_device)
    , m_texture(other.m_texture)
    , m_format(other.m_format)
    , m_dimension(other.m_dimension)
    , m_size(other.m_size)
    , m_mipLevels(other.m_mipLevels)
    , m_sampleCount(other.m_sampleCount)
    , m_usage(other.m_usage)
{
    other.m_texture = nullptr;
}

WebGPURHITexture& WebGPURHITexture::operator=(WebGPURHITexture&& other) noexcept {
    if (this != &other) {
        if (m_texture) {
            wgpuTextureRelease(m_texture);
        }

        m_device = other.m_device;
        m_texture = other.m_texture;
        m_format = other.m_format;
        m_dimension = other.m_dimension;
        m_size = other.m_size;
        m_mipLevels = other.m_mipLevels;
        m_sampleCount = other.m_sampleCount;
        m_usage = other.m_usage;

        other.m_texture = nullptr;
    }
    return *this;
}

std::unique_ptr<RHITextureView> WebGPURHITexture::createView(const TextureViewDesc& desc) {
    // Use texture format if view format is undefined
    TextureViewDesc actualDesc = desc;
    if (actualDesc.format == TextureFormat::Undefined) {
        actualDesc.format = m_format;
    }

    return std::make_unique<WebGPURHITextureView>(m_device, m_texture, actualDesc);
}

std::unique_ptr<RHITextureView> WebGPURHITexture::createDefaultView() {
    // Determine default view dimension from texture dimension
    TextureViewDimension viewDim = TextureViewDimension::View2D;
    switch (m_dimension) {
        case TextureDimension::Texture1D:
            viewDim = TextureViewDimension::View1D;
            break;
        case TextureDimension::Texture2D:
            viewDim = TextureViewDimension::View2D;
            break;
        case TextureDimension::Texture3D:
            viewDim = TextureViewDimension::View3D;
            break;
    }

    TextureViewDesc desc{};
    desc.format = m_format;
    desc.dimension = viewDim;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = m_mipLevels;
    desc.baseArrayLayer = 0;
    desc.arrayLayerCount = m_size.depth; // For 1D/2D, this is array layers; for 3D, this is depth

    return createView(desc);
}

} // namespace WebGPU
} // namespace RHI
