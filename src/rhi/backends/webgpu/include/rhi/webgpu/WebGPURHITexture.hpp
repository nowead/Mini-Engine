#pragma once

#include "WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHITextureView;
using rhi::RHITexture;
using rhi::TextureViewDesc;
using rhi::TextureDesc;
using rhi::TextureFormat;
using rhi::TextureDimension;
using rhi::TextureViewDimension;
using rhi::Extent3D;
using rhi::TextureUsage;

// Forward declaration for friend class
class WebGPURHISwapchain;

/**
 * @brief WebGPU implementation of RHITextureView
 */
class WebGPURHITextureView : public RHITextureView {
public:
    WebGPURHITextureView(WebGPURHIDevice* device,
                         WGPUTexture texture,
                         const TextureViewDesc& desc);
    ~WebGPURHITextureView() override;

    // Non-copyable, movable
    WebGPURHITextureView(const WebGPURHITextureView&) = delete;
    WebGPURHITextureView& operator=(const WebGPURHITextureView&) = delete;
    WebGPURHITextureView(WebGPURHITextureView&&) noexcept;
    WebGPURHITextureView& operator=(WebGPURHITextureView&&) noexcept;

    // RHITextureView interface
    TextureFormat getFormat() const override { return m_format; }
    TextureViewDimension getDimension() const override { return m_dimension; }

    // WebGPU-specific accessors
    WGPUTextureView getWGPUTextureView() const { return m_textureView; }

private:
    friend class WebGPURHISwapchain;

    // Internal constructor for swapchain (takes ownership of existing texture view)
    WebGPURHITextureView(WebGPURHIDevice* device,
                         WGPUTextureView textureView,
                         TextureFormat format,
                         TextureViewDimension dimension,
                         bool ownsView = true);

    WebGPURHIDevice* m_device;
    WGPUTextureView m_textureView = nullptr;
    TextureFormat m_format;
    TextureViewDimension m_dimension;
    bool m_ownsTextureView = true;
};

/**
 * @brief WebGPU implementation of RHITexture
 *
 * WebGPU textures have automatic memory management (no VMA needed).
 */
class WebGPURHITexture : public RHITexture {
public:
    /**
     * @brief Create texture with WebGPU
     */
    WebGPURHITexture(WebGPURHIDevice* device, const TextureDesc& desc);
    ~WebGPURHITexture() override;

    // Non-copyable, movable
    WebGPURHITexture(const WebGPURHITexture&) = delete;
    WebGPURHITexture& operator=(const WebGPURHITexture&) = delete;
    WebGPURHITexture(WebGPURHITexture&&) noexcept;
    WebGPURHITexture& operator=(WebGPURHITexture&&) noexcept;

    // RHITexture interface
    std::unique_ptr<RHITextureView> createView(const TextureViewDesc& desc) override;
    std::unique_ptr<RHITextureView> createDefaultView() override;
    TextureFormat getFormat() const override { return m_format; }
    TextureDimension getDimension() const override { return m_dimension; }
    Extent3D getSize() const override { return m_size; }
    uint32_t getMipLevelCount() const override { return m_mipLevels; }
    uint32_t getSampleCount() const override { return m_sampleCount; }
    uint32_t getArrayLayerCount() const override { return m_arrayLayerCount; }
    bool isCubemap() const override { return m_isCubemap; }

    // WebGPU-specific accessors
    WGPUTexture getWGPUTexture() const { return m_texture; }

private:
    WebGPURHIDevice* m_device;
    WGPUTexture m_texture = nullptr;

    TextureFormat m_format;
    TextureDimension m_dimension;
    Extent3D m_size;
    uint32_t m_mipLevels;
    uint32_t m_sampleCount;
    uint32_t m_arrayLayerCount;
    bool m_isCubemap;
    TextureUsage m_usage;
};

} // namespace WebGPU
} // namespace RHI
