#include "ResourceManager.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>
#include <cstring>

ResourceManager::ResourceManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : rhiDevice(device), graphicsQueue(queue) {}

rhi::RHITexture* ResourceManager::loadTexture(const std::string& path) {
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
    rhi::RHITexture* result = texture.get();
    textureCache[path] = std::move(texture);
    return result;
}

rhi::RHITexture* ResourceManager::loadHDRTexture(const std::string& path) {
    // Check cache first
    auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        return it->second.get();
    }

    // Load HDR image from disk
    stbi_set_flip_vertically_on_load(true);
    int texWidth, texHeight, texChannels;
    float* pixels = stbi_loadf(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    stbi_set_flip_vertically_on_load(false);

    if (!pixels) {
        throw std::runtime_error("Failed to load HDR texture: " + path);
    }

    // Upload to GPU as RGBA16Float
    auto texture = uploadHDRTexture(pixels, texWidth, texHeight);

    stbi_image_free(pixels);

    // Cache and return
    rhi::RHITexture* result = texture.get();
    textureCache[path] = std::move(texture);
    return result;
}

rhi::RHITexture* ResourceManager::getTexture(const std::string& path) {
    auto it = textureCache.find(path);
    return (it != textureCache.end()) ? it->second.get() : nullptr;
}

void ResourceManager::clearCache() {
    textureCache.clear();
}

std::unique_ptr<rhi::RHITexture> ResourceManager::uploadTexture(
    unsigned char* pixels,
    int width,
    int height,
    int channels) {

    uint64_t imageSize = static_cast<uint64_t>(width) * height * channels;

    // ========================================================================
    // Create Staging Buffer
    // ========================================================================
    rhi::BufferDesc stagingDesc{};
    stagingDesc.size = imageSize;
    stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto stagingBuffer = rhiDevice->createBuffer(stagingDesc);

    // Copy pixel data to staging buffer
    void* mapped = stagingBuffer->map();
    std::memcpy(mapped, pixels, imageSize);
    stagingBuffer->unmap();

    // ========================================================================
    // Create Texture
    // ========================================================================
    rhi::TextureDesc textureDesc{};
    textureDesc.size = rhi::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    textureDesc.dimension = rhi::TextureDimension::Texture2D;
    textureDesc.format = rhi::TextureFormat::RGBA8UnormSrgb;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;
    textureDesc.usage = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
    auto texture = rhiDevice->createTexture(textureDesc);

    // ========================================================================
    // Copy Staging Buffer to Texture (Direct RHI usage, no CommandManager)
    // ========================================================================
    auto encoder = rhiDevice->createCommandEncoder();

    // CRITICAL: Transition image layout from UNDEFINED to TRANSFER_DST_OPTIMAL
    encoder->transitionTextureLayout(texture.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::TransferDst);

    // Setup buffer-to-texture copy info
    // NOTE: bytesPerRow is actually "row length in pixels" for Vulkan, not bytes!
    // Setting to 0 means "tightly packed" (use image width)
    rhi::BufferTextureCopyInfo bufferCopyInfo{};
    bufferCopyInfo.buffer = stagingBuffer.get();
    bufferCopyInfo.offset = 0;
    bufferCopyInfo.bytesPerRow = 0;  // 0 = tightly packed (use image width)
    bufferCopyInfo.rowsPerImage = 0; // 0 = tightly packed (use image height)

    rhi::TextureCopyInfo textureCopyInfo{};
    textureCopyInfo.texture = texture.get();
    textureCopyInfo.mipLevel = 0;
    textureCopyInfo.origin = {0, 0, 0};
    textureCopyInfo.aspect = 0;  // Color aspect

    rhi::Extent3D copySize{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    // Copy buffer to texture
    encoder->copyBufferToTexture(bufferCopyInfo, textureCopyInfo, copySize);

    // Transition image layout from TRANSFER_DST_OPTIMAL to SHADER_READ_ONLY_OPTIMAL
    encoder->transitionTextureLayout(texture.get(),
                                     rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();

    // Submit and wait for completion
    graphicsQueue->submit(cmdBuffer.get());
    graphicsQueue->waitIdle();

    // Staging buffer will be automatically destroyed when going out of scope
    return texture;
}

std::unique_ptr<rhi::RHITexture> ResourceManager::uploadHDRTexture(
    float* pixels,
    int width,
    int height) {

    // RGBA16Float = 4 channels * 2 bytes (half float) per channel = 8 bytes per pixel
    // But stbi_loadf gives us 32-bit floats, so staging buffer holds full floats
    // and we convert to half-float for the GPU texture
    uint64_t floatDataSize = static_cast<uint64_t>(width) * height * 4 * sizeof(float);

    // Create staging buffer with full float data
    rhi::BufferDesc stagingDesc{};
    stagingDesc.size = floatDataSize;
    stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto stagingBuffer = rhiDevice->createBuffer(stagingDesc);

    void* mapped = stagingBuffer->map();
    std::memcpy(mapped, pixels, floatDataSize);
    stagingBuffer->unmap();

    // Create texture as RGBA32Float (matches staging data)
    // Using RGBA32Float since we're uploading 32-bit float data directly
    rhi::TextureDesc textureDesc{};
    textureDesc.size = rhi::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    textureDesc.dimension = rhi::TextureDimension::Texture2D;
    textureDesc.format = rhi::TextureFormat::RGBA32Float;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;
    textureDesc.usage = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
    auto texture = rhiDevice->createTexture(textureDesc);

    // Copy staging buffer to texture
    auto encoder = rhiDevice->createCommandEncoder();

    encoder->transitionTextureLayout(texture.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::TransferDst);

    rhi::BufferTextureCopyInfo bufferCopyInfo{};
    bufferCopyInfo.buffer = stagingBuffer.get();
    bufferCopyInfo.offset = 0;
    bufferCopyInfo.bytesPerRow = 0;
    bufferCopyInfo.rowsPerImage = 0;

    rhi::TextureCopyInfo textureCopyInfo{};
    textureCopyInfo.texture = texture.get();
    textureCopyInfo.mipLevel = 0;
    textureCopyInfo.origin = {0, 0, 0};
    textureCopyInfo.aspect = 0;

    rhi::Extent3D copySize{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    encoder->copyBufferToTexture(bufferCopyInfo, textureCopyInfo, copySize);

    encoder->transitionTextureLayout(texture.get(),
                                     rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();

    graphicsQueue->submit(cmdBuffer.get());
    graphicsQueue->waitIdle();

    return texture;
}
