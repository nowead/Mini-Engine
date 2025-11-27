#include "ResourceManager.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>

ResourceManager::ResourceManager(VulkanDevice& device, CommandManager& commandManager)
    : device(device), commandManager(commandManager) {}

VulkanImage* ResourceManager::loadTexture(const std::string& path) {
    // Check cache first
    auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        return it->second.get();
    }

    // Load image from disk
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("Failed to load texture image: " + path);
    }

    // Upload to GPU
    auto texture = uploadTexture(pixels, texWidth, texHeight, 4);

    stbi_image_free(pixels);

    // Cache and return
    VulkanImage* result = texture.get();
    textureCache[path] = std::move(texture);
    return result;
}

VulkanImage* ResourceManager::getTexture(const std::string& path) {
    auto it = textureCache.find(path);
    return (it != textureCache.end()) ? it->second.get() : nullptr;
}

void ResourceManager::clearCache() {
    textureCache.clear();
}

std::unique_ptr<VulkanImage> ResourceManager::uploadTexture(
    unsigned char* pixels,
    int width,
    int height,
    int channels) {

    vk::DeviceSize imageSize = width * height * channels;

    // Create staging buffer
    VulkanBuffer stagingBuffer(device, imageSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    stagingBuffer.map();
    stagingBuffer.copyData(pixels, imageSize);
    stagingBuffer.unmap();

    // Create texture image
    auto texture = std::make_unique<VulkanImage>(device,
        width, height,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageAspectFlagBits::eColor);

    // Transition and copy
    auto commandBuffer = commandManager.beginSingleTimeCommands();
    texture->transitionLayout(*commandBuffer,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal);
    texture->copyFromBuffer(*commandBuffer, stagingBuffer);
    texture->transitionLayout(*commandBuffer,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal);
    commandManager.endSingleTimeCommands(*commandBuffer);

    // Create sampler
    texture->createSampler();

    return texture;
}
