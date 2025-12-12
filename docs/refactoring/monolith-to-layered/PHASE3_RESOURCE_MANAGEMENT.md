# Phase 3: Resource Management Refactoring

This document describes the resource management extraction in Phase 3 of the refactoring process.

## Goal

Abstract buffer and image management into reusable RAII classes to eliminate code duplication and improve resource safety.

## Overview

### Before Phase 3
- Manual buffer creation with separate buffer/memory management
- Repeated buffer creation patterns for vertex, index, uniform, staging
- Manual image creation with separate image/memory/view management
- Many member variables for buffers and images
- Helper functions scattered in main.cpp

### After Phase 3
- Unified VulkanBuffer class for all buffer types
- Unified VulkanImage class for all image types
- RAII-based automatic resource management
- Significantly reduced member variables
- Cleaner main.cpp

---

## Changes

### 1. Created `VulkanBuffer` Class

**Files Created**:
- `src/resources/VulkanBuffer.hpp`
- `src/resources/VulkanBuffer.cpp`

**Purpose**: Unified buffer management for all buffer types (Vertex, Index, Uniform, Staging)

**Class Interface**:
```cpp
class VulkanBuffer {
public:
    VulkanBuffer(
        VulkanDevice& device,
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties);

    ~VulkanBuffer() = default; // RAII handles cleanup

    // Disable copy, enable move
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = default;
    VulkanBuffer& operator=(VulkanBuffer&&) = delete;

    // Operations
    void map();
    void unmap();
    void copyData(const void* data, vk::DeviceSize size);
    void copyFrom(VulkanBuffer& srcBuffer, const vk::raii::CommandBuffer& cmdBuffer);

    // Accessors
    vk::Buffer getHandle() const;
    vk::DeviceSize getSize() const;
    void* getMappedData();
};
```

**Usage Comparison**:

Before (manual management):
```cpp
void createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(data, vertices.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    createBuffer(bufferSize,
                 vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal,
                 vertexBuffer, vertexBufferMemory);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
}
```

After (RAII):
```cpp
void createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // Create staging buffer
    VulkanBuffer stagingBuffer(*vulkanDevice, bufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    stagingBuffer.map();
    stagingBuffer.copyData(vertices.data(), bufferSize);
    stagingBuffer.unmap();

    // Create device-local vertex buffer
    vertexBuffer = std::make_unique<VulkanBuffer>(*vulkanDevice, bufferSize,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Copy from staging to vertex buffer
    auto commandBuffer = commandManager->beginSingleTimeCommands();
    vertexBuffer->copyFrom(stagingBuffer, *commandBuffer);
    commandManager->endSingleTimeCommands(*commandBuffer);
}
```

---

### 2. Created `VulkanImage` Class

**Files Created**:
- `src/resources/VulkanImage.hpp`
- `src/resources/VulkanImage.cpp`

**Purpose**: Unified image, image view, and sampler management

**Class Interface**:
```cpp
class VulkanImage {
public:
    VulkanImage(
        VulkanDevice& device,
        uint32_t width,
        uint32_t height,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        vk::ImageAspectFlags aspectFlags);

    ~VulkanImage() = default; // RAII handles cleanup

    // Disable copy, enable move
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&&) = default;
    VulkanImage& operator=(VulkanImage&&) = delete;

    // Operations
    void transitionLayout(
        const vk::raii::CommandBuffer& cmdBuffer,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout);

    void copyFromBuffer(
        const vk::raii::CommandBuffer& cmdBuffer,
        VulkanBuffer& buffer);

    void createSampler(
        vk::Filter magFilter = vk::Filter::eLinear,
        vk::Filter minFilter = vk::Filter::eLinear,
        vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat);

    // Accessors
    vk::Image getImage() const;
    vk::ImageView getImageView() const;
    vk::Sampler getSampler() const;
};
```

**Key Feature**: Image view is automatically created in the constructor - no separate `createImageView()` call needed.

**Usage Comparison**:

Before (manual management):
```cpp
void createDepthResources() {
    vk::Format depthFormat = findDepthFormat();

    createImage(swapChainExtent.width, swapChainExtent.height, depthFormat,
                vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, depthImage, depthImageMemory);

    depthImageView = createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

void createTextureImageView() {
    textureImageView = createImageView(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor);
}

void createTextureSampler() {
    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    vk::SamplerCreateInfo samplerInfo{ /* ... */ };
    textureSampler = vk::raii::Sampler(device, samplerInfo);
}
```

After (RAII):
```cpp
void createDepthResources() {
    vk::Format depthFormat = findDepthFormat();

    depthImage = std::make_unique<VulkanImage>(*vulkanDevice,
        swapChainExtent.width, swapChainExtent.height,
        depthFormat, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageAspectFlagBits::eDepth);
    // Image view created automatically!
}

void createTextureImageView() {
    // No longer needed - image view created in VulkanImage constructor!
}

void createTextureSampler() {
    textureImage->createSampler();  // One line!
}
```

---

## Integration Changes

### main.cpp Simplification

**Member Variables Before**:
```cpp
vk::raii::Buffer vertexBuffer = nullptr;
vk::raii::DeviceMemory vertexBufferMemory = nullptr;
vk::raii::Buffer indexBuffer = nullptr;
vk::raii::DeviceMemory indexBufferMemory = nullptr;
std::vector<vk::raii::Buffer> uniformBuffers;
std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
std::vector<void*> uniformBuffersMapped;
vk::raii::Image depthImage = nullptr;
vk::raii::DeviceMemory depthImageMemory = nullptr;
vk::raii::ImageView depthImageView = nullptr;
vk::raii::Image textureImage = nullptr;
vk::raii::DeviceMemory textureImageMemory = nullptr;
vk::raii::ImageView textureImageView = nullptr;
vk::raii::Sampler textureSampler = nullptr;
```

**Member Variables After**:
```cpp
std::unique_ptr<VulkanBuffer> vertexBuffer;
std::unique_ptr<VulkanBuffer> indexBuffer;
std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
std::unique_ptr<VulkanImage> depthImage;
std::unique_ptr<VulkanImage> textureImage;
```

**Functions Removed from main.cpp**:
- `createBuffer()`
- `copyBuffer()`
- `createImage()`
- `createImageView()`
- `transitionImageLayout()`
- `copyBufferToImage()`

**Functions Simplified**:
- `createVertexBuffer()` - significantly shorter
- `createIndexBuffer()` - significantly shorter
- `createUniformBuffers()` - much simpler
- `createDepthResources()` - one constructor call
- `createTextureImage()` - shorter
- `createTextureImageView()` - eliminated (automatic)
- `createTextureSampler()` - one line
- `updateUniformBuffer()` - uses `getMappedData()`
- `recordCommandBuffer()` - uses getters

---

## Reflection

### What Worked Well
- **RAII cleanup**: Never had to worry about manual destruction. When an object went out of scope or was reset, everything cleaned up automatically. This eliminated a whole class of bugs.
- **Automatic image view creation**: Having the image view created in the VulkanImage constructor was brilliant - eliminated a repetitive step and made it impossible to forget.
- **Unified interface**: Using the same VulkanBuffer class for vertex, index, uniform, and staging buffers simplified the code significantly. No more copy-pasting similar code.
- **Persistent mapping**: Uniform buffers stayed mapped throughout their lifetime, avoiding repeated map/unmap calls

### Challenges
- **Move semantics**: Initially struggled with move-only types when storing VulkanBuffer/VulkanImage in containers. Had to use `std::unique_ptr` and `std::make_unique` everywhere.
- **Reference invalidation**: Had to be careful about VulkanDevice reference lifetime. The device must outlive all buffers/images referencing it.
- **Initialization order**: Smart pointers needed to be initialized in the right order during startup to avoid accessing null device references

### Impact on Later Phases
- **Phase 4 (Rendering)**: The clean buffer/image interface made it easy to refactor swapchain and pipeline management
- **Phase 5 (Scene)**: Mesh class could directly use VulkanBuffer without worrying about memory management
- **Phase 10 (FdF)**: When adding FdF wireframe support, creating new vertex/index buffers was trivial - just construct VulkanBuffer

---

## Testing

### Build
```bash
cmake --build build
```
Build successful with no warnings

### Runtime
```bash
./build/vulkanGLFW
```
Application ran correctly. Vertex and index buffers worked. Uniform buffers updated correctly. Depth buffer rendered properly. Texture loading and sampling worked.

---

## Summary

Phase 3 extracted resource management into reusable RAII classes:
- **VulkanBuffer**: Unified buffer management for all buffer types
- **VulkanImage**: Unified image management with automatic image view creation

Main improvements:
- Dramatically reduced member variables
- Eliminated helper functions
- Shorter buffer/image creation code
- Automatic cleanup with RAII
- Type-safe interfaces

This phase completed the core foundation of the refactoring. With utility layer (Phase 1), device management (Phase 2), and resource management (Phase 3) now modular, the codebase was ready for rendering layer extraction.

---

*Phase 3 Complete*
*Previous: Phase 2 - Device Management*
*Next: Phase 4 - Rendering Layer*
