#pragma once

#include <rhi/RHI.hpp>

#include <memory>
#include <string>
#include <unordered_map>

/**
 * @brief Manages loading and caching of GPU resources
 *
 * Responsibilities:
 * - Texture loading from disk
 * - Staging buffer management
 * - Image format conversion
 * - Resource caching (avoid duplicate loads)
 *
 * Hides from Renderer:
 * - stb_image details
 * - Staging buffer creation (via RHI)
 * - Layout transitions (via RHI)
 *
 * Note: Migrated to RHI in Phase 5 (Scene Layer Migration)
 */
class ResourceManager {
public:
    ResourceManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~ResourceManager() = default;

    // Disable copy and move
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) = delete;
    ResourceManager& operator=(ResourceManager&&) = delete;

    /**
     * @brief Load texture from file (with caching)
     * @param path Path to image file
     * @return Pointer to loaded texture (owned by ResourceManager)
     */
    rhi::RHITexture* loadTexture(const std::string& path);

    /**
     * @brief Load HDR texture from file (with caching)
     * @param path Path to .hdr image file
     * @return Pointer to loaded RGBA16Float texture (owned by ResourceManager)
     */
    rhi::RHITexture* loadHDRTexture(const std::string& path);

    /**
     * @brief Get texture by path (if already loaded)
     * @return Pointer to texture or nullptr if not loaded
     */
    rhi::RHITexture* getTexture(const std::string& path);

    /**
     * @brief Clear all cached resources
     */
    void clearCache();

    /**
     * @brief Upload a tightly-packed RGBA8 pixel buffer to a new RHI texture.
     *
     * Used by the asset pipeline to push glTF embedded images that have
     * already been decoded into CPU memory (see AssetImporter::decodeImage)
     * onto the GPU. The format argument selects the color space:
     *   - RGBA8UnormSrgb for albedo / emissive textures (sRGB)
     *   - RGBA8Unorm    for normal / metallicRoughness / occlusion (linear)
     *
     * Caller retains ownership of @p pixels; the function copies into a
     * staging buffer and waits for the upload to complete before returning.
     */
    std::unique_ptr<rhi::RHITexture> uploadRGBA8FromMemory(
        const uint8_t*     pixels,
        uint32_t           width,
        uint32_t           height,
        rhi::TextureFormat format);

private:
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;

    // Resource cache
    std::unordered_map<std::string, std::unique_ptr<rhi::RHITexture>> textureCache;

    // Helper for uploading texture data
    std::unique_ptr<rhi::RHITexture> uploadTexture(
        unsigned char* pixels,
        int width,
        int height,
        int channels);

    // Helper for uploading HDR float texture data
    std::unique_ptr<rhi::RHITexture> uploadHDRTexture(
        float* pixels,
        int width,
        int height);
};
