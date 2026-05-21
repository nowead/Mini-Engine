#include "ResourceManager.hpp"

// stb_image is needed on both backends: native loadTexture reads from disk,
// and WASM AssetImporter::decodeImage uses stbi_load_from_memory on glTF
// embedded images. STB_IMAGE_IMPLEMENTATION is defined HERE (single TU) so
// the symbols exist for both build paths. The on-disk loadTexture body
// stays ifdef-guarded below because there's no host filesystem on WASM.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>
#include <cstring>
#include <iostream>

ResourceManager::ResourceManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : rhiDevice(device), graphicsQueue(queue) {}

rhi::RHITexture* ResourceManager::loadTexture(const std::string& path) {
#ifndef __EMSCRIPTEN__
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
#else
    std::cerr << "[ResourceManager] Texture loading not supported in WASM build: " << path << std::endl;
    return nullptr;
#endif
}

rhi::RHITexture* ResourceManager::loadHDRTexture(const std::string& path) {
#ifndef __EMSCRIPTEN__
    // Check cache first
    auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        return it->second.get();
    }

    // Load HDR image from disk (no vertical flip — Vulkan UV origin is top-left)
    int texWidth, texHeight, texChannels;
    float* pixels = stbi_loadf(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("Failed to load HDR texture: " + path);
    }

    std::cout << "[ResourceManager] Loaded HDR: " << path << " (" << texWidth << "x" << texHeight
              << ", " << texChannels << " channels)" << std::endl;

    // Check if data is non-zero
    bool hasData = false;
    for (int i = 0; i < texWidth * texHeight * 4 && i < 1000; ++i) {
        if (pixels[i] > 0.001f) {
            hasData = true;
            break;
        }
    }
    std::cout << "[ResourceManager] HDR data check: " << (hasData ? "OK (non-zero)" : "WARNING (all zero)") << std::endl;

    // Upload to GPU as RGBA16Float
    auto texture = uploadHDRTexture(pixels, texWidth, texHeight);

    stbi_image_free(pixels);

    // Cache and return
    rhi::RHITexture* result = texture.get();
    textureCache[path] = std::move(texture);
    return result;
#else
    std::cerr << "[ResourceManager] HDR texture loading not supported in WASM build: " << path << std::endl;
    return nullptr;
#endif
}

rhi::RHITexture* ResourceManager::getTexture(const std::string& path) {
    auto it = textureCache.find(path);
    return (it != textureCache.end()) ? it->second.get() : nullptr;
}

void ResourceManager::clearCache() {
    textureCache.clear();
}

std::unique_ptr<rhi::RHITexture> ResourceManager::uploadRGBA8FromMemory(
    const uint8_t*     pixels,
    uint32_t           width,
    uint32_t           height,
    rhi::TextureFormat format) {

    if (!pixels || width == 0 || height == 0) return nullptr;

    const uint64_t imageSize = static_cast<uint64_t>(width) * height * 4u;

    // Staging buffer
    rhi::BufferDesc stagingDesc{};
    stagingDesc.size  = imageSize;
    stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto stagingBuffer = rhiDevice->createBuffer(stagingDesc);
    if (!stagingBuffer) return nullptr;

    void* mapped = stagingBuffer->map();
    std::memcpy(mapped, pixels, imageSize);
    stagingBuffer->unmap();

    // Texture
    rhi::TextureDesc textureDesc{};
    textureDesc.size          = rhi::Extent3D{width, height, 1};
    textureDesc.dimension     = rhi::TextureDimension::Texture2D;
    textureDesc.format        = format;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount   = 1;
    textureDesc.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
    auto texture = rhiDevice->createTexture(textureDesc);
    if (!texture) return nullptr;

    // Staging -> Texture copy
    auto encoder = rhiDevice->createCommandEncoder();

    encoder->transitionTextureLayout(texture.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::TransferDst);

    // NOTE: bytesPerRow / rowsPerImage = 0 means tightly packed.
    rhi::BufferTextureCopyInfo bufferCopyInfo{};
    bufferCopyInfo.buffer       = stagingBuffer.get();
    bufferCopyInfo.offset       = 0;
    bufferCopyInfo.bytesPerRow  = 0;
    bufferCopyInfo.rowsPerImage = 0;

    rhi::TextureCopyInfo textureCopyInfo{};
    textureCopyInfo.texture  = texture.get();
    textureCopyInfo.mipLevel = 0;
    textureCopyInfo.origin   = {0, 0, 0};
    textureCopyInfo.aspect   = 0;  // color

    rhi::Extent3D copySize{width, height, 1};
    encoder->copyBufferToTexture(bufferCopyInfo, textureCopyInfo, copySize);

    encoder->transitionTextureLayout(texture.get(),
                                     rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();
    graphicsQueue->submit(cmdBuffer.get());
    graphicsQueue->waitIdle();

    return texture;
}

// Legacy entrypoint preserved for the on-disk loadTexture path. Always
// uploads as sRGB (matches prior behavior). New code paths should call
// uploadRGBA8FromMemory directly with an explicit format.
std::unique_ptr<rhi::RHITexture> ResourceManager::uploadTexture(
    unsigned char* pixels,
    int width,
    int height,
    int channels) {
    (void)channels;  // legacy parameter; only RGBA8 (channels=4) is supported
    return uploadRGBA8FromMemory(pixels,
                                 static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height),
                                 rhi::TextureFormat::RGBA8UnormSrgb);
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
