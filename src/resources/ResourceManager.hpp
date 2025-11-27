#pragma once

#include "src/core/VulkanDevice.hpp"
#include "src/resources/VulkanImage.hpp"
#include "src/resources/VulkanBuffer.hpp"
#include "src/rendering/CommandManager.hpp"

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
 * - Staging buffer creation
 * - Layout transitions
 */
class ResourceManager {
public:
    ResourceManager(VulkanDevice& device, CommandManager& commandManager);
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
    VulkanImage* loadTexture(const std::string& path);

    /**
     * @brief Get texture by path (if already loaded)
     * @return Pointer to texture or nullptr if not loaded
     */
    VulkanImage* getTexture(const std::string& path);

    /**
     * @brief Clear all cached resources
     */
    void clearCache();

private:
    VulkanDevice& device;
    CommandManager& commandManager;

    // Resource cache
    std::unordered_map<std::string, std::unique_ptr<VulkanImage>> textureCache;

    // Helper for uploading texture data
    std::unique_ptr<VulkanImage> uploadTexture(
        unsigned char* pixels,
        int width,
        int height,
        int channels);
};
