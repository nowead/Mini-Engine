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
     * @brief Get texture by path (if already loaded)
     * @return Pointer to texture or nullptr if not loaded
     */
    rhi::RHITexture* getTexture(const std::string& path);

    /**
     * @brief Clear all cached resources
     */
    void clearCache();

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
};
