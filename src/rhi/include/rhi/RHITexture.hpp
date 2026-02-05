#pragma once

#include "RHITypes.hpp"
#include <memory>
#include <cstdint>

namespace rhi {

// Forward declaration
class RHITextureView;

/**
 * @brief Texture creation descriptor
 */
struct TextureDesc {
    Extent3D size;                              // Texture dimensions
    uint32_t mipLevelCount = 1;                 // Number of mip levels
    uint32_t sampleCount = 1;                   // Sample count for MSAA (1, 2, 4, 8, etc.)
    uint32_t arrayLayerCount = 1;               // Number of array layers (6 for cubemaps)
    bool isCubemap = false;                     // Whether this is a cubemap texture
    TextureDimension dimension = TextureDimension::Texture2D;  // Texture dimension
    TextureFormat format = TextureFormat::RGBA8Unorm;          // Pixel format
    TextureUsage usage = TextureUsage::Sampled;                // Usage flags
    const char* label = nullptr;                // Optional debug label

    TextureDesc() = default;
    TextureDesc(uint32_t width, uint32_t height, TextureFormat fmt = TextureFormat::RGBA8Unorm)
        : size(width, height, 1), format(fmt) {}
};

/**
 * @brief Texture view creation descriptor
 */
struct TextureViewDesc {
    TextureFormat format = TextureFormat::Undefined;  // Format (Undefined = use texture format)
    TextureViewDimension dimension = TextureViewDimension::View2D;  // View dimension
    uint32_t baseMipLevel = 0;      // First mip level accessible in the view
    uint32_t mipLevelCount = 1;     // Number of mip levels accessible
    uint32_t baseArrayLayer = 0;    // First array layer accessible in the view
    uint32_t arrayLayerCount = 1;   // Number of array layers accessible
    const char* label = nullptr;    // Optional debug label

    TextureViewDesc() = default;
};

/**
 * @brief Texture view interface
 *
 * A texture view represents a specific view into a texture resource,
 * defining which mip levels, array layers, and format to use.
 */
class RHITextureView {
public:
    virtual ~RHITextureView() = default;

    /**
     * @brief Get the format of this view
     * @return Texture format
     */
    virtual TextureFormat getFormat() const = 0;

    /**
     * @brief Get the dimension of this view
     * @return Texture view dimension
     */
    virtual TextureViewDimension getDimension() const = 0;
};

/**
 * @brief Texture interface for GPU image resources
 *
 * Textures represent multidimensional image data on the GPU that can be
 * sampled in shaders or used as render targets.
 */
class RHITexture {
public:
    virtual ~RHITexture() = default;

    /**
     * @brief Create a view of this texture
     * @param desc View creation descriptor
     * @return Unique pointer to the created texture view
     *
     * Multiple views can be created for the same texture with different
     * formats, mip levels, or array layers.
     */
    virtual std::unique_ptr<RHITextureView> createView(const TextureViewDesc& desc) = 0;

    /**
     * @brief Create a default view of the entire texture
     * @return Unique pointer to the created texture view
     */
    virtual std::unique_ptr<RHITextureView> createDefaultView() = 0;

    /**
     * @brief Get the size of the texture
     * @return Texture dimensions
     */
    virtual Extent3D getSize() const = 0;

    /**
     * @brief Get the format of the texture
     * @return Texture format
     */
    virtual TextureFormat getFormat() const = 0;

    /**
     * @brief Get the number of mip levels
     * @return Mip level count
     */
    virtual uint32_t getMipLevelCount() const = 0;

    /**
     * @brief Get the sample count (for MSAA textures)
     * @return Sample count
     */
    virtual uint32_t getSampleCount() const = 0;

    /**
     * @brief Get the texture dimension
     * @return Texture dimension
     */
    virtual TextureDimension getDimension() const = 0;

    /**
     * @brief Get the number of array layers
     * @return Array layer count (6 for cubemaps)
     */
    virtual uint32_t getArrayLayerCount() const = 0;

    /**
     * @brief Check if this is a cubemap texture
     * @return True if cubemap
     */
    virtual bool isCubemap() const = 0;
};

} // namespace rhi
