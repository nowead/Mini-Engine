# Phase 4: Rendering Layer Refactoring

This document describes the rendering layer implementation in Phase 4 of the refactoring process.

## Goal

Extract rendering infrastructure (synchronization, commands, swapchain, pipeline) into dedicated classes to separate rendering concerns from application logic.

## Overview

### Before Phase 4
- Synchronization primitives managed manually in main.cpp
- Command pool and buffers scattered across code
- Swapchain management mixed with rendering logic
- Pipeline creation embedded in main application
- No clear separation of rendering responsibilities

### After Phase 4
- SyncManager for synchronization primitives
- CommandManager for command buffer management
- VulkanSwapchain for presentation management
- VulkanPipeline for graphics pipeline
- Clean rendering layer architecture

---

## Phase 4.1: SyncManager Implementation

### Problem

Synchronization objects (semaphores, fences) were managed manually with error-prone indexing and no clear lifecycle management.

**Before**:
```cpp
// In main.cpp - scattered member variables
std::vector<vk::raii::Semaphore> presentCompleteSemaphore;
std::vector<vk::raii::Semaphore> renderFinishedSemaphore;
std::vector<vk::raii::Fence> inFlightFences;
uint32_t semaphoreIndex = 0;
uint32_t currentFrame = 0;

// Manual creation and management
void createSyncObjects() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        presentCompleteSemaphore.emplace_back(*device, semaphoreInfo);
        renderFinishedSemaphore.emplace_back(*device, semaphoreInfo);
        inFlightFences.emplace_back(*device, fenceInfo);
    }
}
```

### Solution

**Created**: `src/rendering/SyncManager.hpp/.cpp`

**Class Interface**:
```cpp
class SyncManager {
public:
    SyncManager(VulkanDevice& device, uint32_t maxFramesInFlight, uint32_t swapchainImageCount);
    ~SyncManager() = default;

    vk::Semaphore getImageAvailableSemaphore(uint32_t frameIndex) const;
    vk::Semaphore getRenderFinishedSemaphore(uint32_t imageIndex) const;
    vk::Fence getInFlightFence(uint32_t frameIndex) const;

    void waitForFence(uint32_t frameIndex);
    void resetFence(uint32_t frameIndex);
};
```

**Key Design Decision - Separate Semaphore Counts**:

The critical insight: **frames in flight ≠ swapchain images**

```cpp
// Image available semaphores: one per frame in flight (e.g., 2)
for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    imageAvailableSemaphores.emplace_back(device.getDevice(), semaphoreInfo);
    inFlightFences.emplace_back(device.getDevice(), fenceInfo);
}

// Render finished semaphores: one per swapchain image (e.g., 3)
for (uint32_t i = 0; i < swapchainImageCount; i++) {
    renderFinishedSemaphores.emplace_back(device.getDevice(), semaphoreInfo);
}
```

**Rationale**:
- **Image available semaphores** (2): Coordinate CPU frame submission
- **Render finished semaphores** (3): Coordinate swapchain presentation
- **In-flight fences** (2): Coordinate CPU-GPU synchronization

### Integration

**In main.cpp**:
```cpp
// Removed: ~30 lines of manual synchronization management
// Added:
std::unique_ptr<SyncManager> syncManager;
syncManager = std::make_unique<SyncManager>(*vulkanDevice, MAX_FRAMES_IN_FLIGHT, swapChainImages.size());

// Usage:
syncManager->waitForFence(currentFrame);
syncManager->getImageAvailableSemaphore(currentFrame);
syncManager->getRenderFinishedSemaphore(imageIndex);  // Critical: imageIndex not currentFrame
```

### Reflection

**What Worked Well**:
- Clear separation of synchronization from rendering logic
- Self-documenting API prevented indexing errors
- RAII cleanup automatic

**Challenges**:
- Initial validation error from using frameIndex instead of imageIndex for render-finished semaphores
- Understanding the subtle difference between frame-in-flight and swapchain image indices

**Impact on Later Phases**:
- Established pattern for rendering component encapsulation
- Foundation for Phase 6 Renderer integration

---

## Phase 4.2: CommandManager Implementation

### Problem

Command pool and buffers managed manually with repeated single-time command patterns scattered across codebase.

**Before**:
```cpp
// In main.cpp - scattered variables
vk::raii::CommandPool commandPool = nullptr;
std::vector<vk::raii::CommandBuffer> commandBuffers;

// Single-time utilities duplicated everywhere
std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands() { /* ... */ }
void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer) { /* ... */ }
```

### Solution

**Created**: `src/rendering/CommandManager.hpp/.cpp` (later moved to `src/core/` in Phase 8)

**Class Interface**:
```cpp
class CommandManager {
public:
    CommandManager(VulkanDevice& device, uint32_t queueFamilyIndex, uint32_t maxFramesInFlight);
    ~CommandManager() = default;

    vk::raii::CommandBuffer& getCommandBuffer(uint32_t frameIndex);

    std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();
    void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer);
};
```

**Single-Time Commands Pattern**:
```cpp
// Encapsulated staging operations
std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands() {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    auto commandBuffer = std::make_unique<vk::raii::CommandBuffer>(
        std::move(vk::raii::CommandBuffers(device.getDevice(), allocInfo).front())
    );
    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };
    commandBuffer->begin(beginInfo);
    return commandBuffer;
}

void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer) {
    commandBuffer.end();
    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffer
    };
    device.getGraphicsQueue().submit(submitInfo);
    device.getGraphicsQueue().waitIdle();
}
```

### Integration

**In main.cpp**:
```cpp
// Removed: ~30 lines of command management code
// Added:
std::unique_ptr<CommandManager> commandManager;
commandManager = std::make_unique<CommandManager>(*vulkanDevice, vulkanDevice->getGraphicsQueueFamily(), MAX_FRAMES_IN_FLIGHT);

// Usage:
commandManager->getCommandBuffer(currentFrame).begin({});
auto commandBuffer = commandManager->beginSingleTimeCommands();
commandManager->endSingleTimeCommands(*commandBuffer);
```

### Reflection

**What Worked Well**:
- Frame-indexed access simplified command buffer management
- Single-time command pattern centralized
- Clean interface reduced duplication

**Challenges**:
- Determining proper ownership semantics for single-time buffers

**Impact on Later Phases**:
- Phase 8: Moved to core/ layer for better architecture (Foundation classes need command buffers)
- Essential for all resource upload operations

---

## Phase 4.3: VulkanSwapchain Implementation

### Problem

Swapchain state fragmented across multiple member variables with complex recreation logic mixed with rendering.

**Before**:
```cpp
// In main.cpp - fragmented state
vk::raii::SwapchainKHR swapChain = nullptr;
std::vector<vk::Image> swapChainImages;
vk::SurfaceFormatKHR swapChainSurfaceFormat;
vk::Extent2D swapChainExtent;
std::vector<vk::raii::ImageView> swapChainImageViews;

// Manual creation, cleanup, recreation (~70 lines of code)
void createSwapChain() { /* ... */ }
void createImageViews() { /* ... */ }
void cleanupSwapChain() { /* ... */ }
void recreateSwapChain() { /* ... */ }
static uint32_t chooseSwapMinImageCount(...) { /* ... */ }
static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(...) { /* ... */ }
vk::Extent2D chooseSwapExtent(...) { /* ... */ }
```

### Solution

**Created**: `src/rendering/VulkanSwapchain.hpp/.cpp`

**Class Interface**:
```cpp
class VulkanSwapchain {
public:
    VulkanSwapchain(VulkanDevice& device, GLFWwindow* window);
    ~VulkanSwapchain() = default;

    void recreate();
    void cleanup();

    std::pair<vk::Result, uint32_t> acquireNextImage(
        uint64_t timeout, vk::Semaphore semaphore, vk::Fence fence = nullptr);

    vk::SwapchainKHR getSwapchain() const;
    const std::vector<vk::Image>& getImages() const;
    vk::ImageView getImageView(uint32_t index) const;
    vk::Format getFormat() const;
    vk::Extent2D getExtent() const;
    uint32_t getImageCount() const;

private:
    void createSwapchain();
    void createImageViews();

    static uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);
    static vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
};
```

**Encapsulated Configuration**:
```cpp
// Configuration logic hidden as private implementation
uint32_t VulkanSwapchain::chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities) {
    auto imageCount = std::max(3u, capabilities.minImageCount);
    if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < imageCount)) {
        imageCount = capabilities.maxImageCount;
    }
    return imageCount;
}

vk::SurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return formats[0];
}
```

### Integration

**In main.cpp**:
```cpp
// Removed: ~70 lines of swapchain management
// Added:
std::unique_ptr<VulkanSwapchain> swapchain;
swapchain = std::make_unique<VulkanSwapchain>(*vulkanDevice, window);

// Usage:
swapchain->getExtent()
swapchain->getFormat()
swapchain->acquireNextImage(timeout, semaphore)
swapchain->recreate()
```

### Reflection

**What Worked Well**:
- All swapchain state unified in single class
- Recreation simplified to single method call
- Clean accessor API

**Challenges**:
- Handling window minimization edge cases
- Ensuring proper cleanup order during recreation

**Impact on Later Phases**:
- Phase 8: Added render pass support for Linux cross-platform compatibility
- Essential for Phase 6 Renderer integration

---

## Phase 4.4: VulkanPipeline Implementation

### Problem

Pipeline creation scattered across multiple functions with complex configuration mixed into main application logic.

**Before**:
```cpp
// In main.cpp - fragmented pipeline state
vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
vk::raii::PipelineLayout pipelineLayout = nullptr;
vk::raii::Pipeline graphicsPipeline = nullptr;

// Manual creation (~110 lines)
void createDescriptorSetLayout() { /* ... */ }
void createGraphicsPipeline() { /* ... */ }  // ~95 lines
vk::raii::ShaderModule createShaderModule(...) { /* ... */ }
```

### Solution

**Created**: `src/rendering/VulkanPipeline.hpp/.cpp`

**Class Interface**:
```cpp
class VulkanPipeline {
public:
    VulkanPipeline(VulkanDevice& device,
                   const VulkanSwapchain& swapchain,
                   const std::string& shaderPath,
                   vk::Format depthFormat);
    ~VulkanPipeline() = default;

    vk::Pipeline getPipeline() const;
    vk::PipelineLayout getPipelineLayout() const;
    vk::DescriptorSetLayout getDescriptorSetLayout() const;

    void bind(const vk::raii::CommandBuffer& commandBuffer) const;

private:
    void createDescriptorSetLayout();
    void createPipelineLayout();
    void createGraphicsPipeline(const std::string& shaderPath,
                                vk::Format colorFormat,
                                vk::Format depthFormat);
    vk::raii::ShaderModule createShaderModule(const std::vector<char>& code);
};
```

**Constructor-Driven Initialization**:
```cpp
VulkanPipeline::VulkanPipeline(VulkanDevice& device,
                               const VulkanSwapchain& swapchain,
                               const std::string& shaderPath,
                               vk::Format depthFormat)
    : device(device) {
    createDescriptorSetLayout();
    createPipelineLayout();
    createGraphicsPipeline(shaderPath, swapchain.getFormat(), depthFormat);
}
```

**Graphics Pipeline Configuration** (simplified):
```cpp
void VulkanPipeline::createGraphicsPipeline(...) {
    // Load shader
    vk::raii::ShaderModule shaderModule = createShaderModule(FileUtils::readFile(shaderPath));

    // Configure pipeline stages (vertex input, rasterization, etc.)
    // ...

    // Create with dynamic rendering support
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        { /* ... */ },
        { .colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat }
    };

    graphicsPipeline = vk::raii::Pipeline(device.getDevice(), nullptr, pipelineCreateInfoChain.get<>());
}
```

### Integration

**In main.cpp**:
```cpp
// Removed: ~110 lines of pipeline creation
// Added:
std::unique_ptr<VulkanPipeline> pipeline;
pipeline = std::make_unique<VulkanPipeline>(*vulkanDevice, *swapchain, "shaders/slang.spv", findDepthFormat());

// Usage:
pipeline->bind(commandBuffer)
pipeline->getPipelineLayout()
pipeline->getDescriptorSetLayout()
```

### Reflection

**What Worked Well**:
- All pipeline state unified
- Constructor ensures correct initialization order
- Shader module management internalized

**Challenges**:
- Balancing configurability vs. simplicity
- Supporting both dynamic rendering and traditional render passes

**Impact on Later Phases**:
- Phase 10: Extended to support topology modes (TriangleList/LineList) for FdF wireframe rendering
- Foundation for all rendering operations

---

## Testing

### Build
```bash
cmake --build build
```
All components compiled successfully with no warnings.

### Runtime
```bash
./build/vulkanGLFW
```
Application ran without validation errors. All rendering components functioned correctly with proper synchronization and resource management.

---

## Summary

Phase 4 successfully extracted rendering infrastructure into four dedicated classes:

1. **SyncManager**: CPU-GPU and GPU-GPU synchronization primitives
2. **CommandManager**: Command pool and buffer management with single-time command pattern
3. **VulkanSwapchain**: Presentation surface management with automatic recreation
4. **VulkanPipeline**: Graphics pipeline with full state configuration

**Key Achievements**:
- Removed ~210+ lines from main.cpp
- Clear separation of rendering concerns
- RAII-based automatic cleanup
- Foundation for Phase 6 Renderer integration

**Architecture Impact**:
The rendering layer now provides clean abstractions for all core rendering operations, enabling the high-level Renderer class in Phase 6.

---

*Phase 4 Complete*
*Previous: Phase 3 - Resource Management*
*Next: Phase 5 - Scene Layer (Mesh Class)*
