# Phase 4: Rendering Layer Refactoring

This document describes the implementation of rendering layer classes in Phase 4 refactoring.

## Progress
**- **Phase 4.1**: SyncManager (Synchronization primitives)
**- **Phase 4.2**: CommandManager (Command pool and buffers)
**- **Phase 4.3**: VulkanSwapchain (Swapchain management)
**- **Phase 4.4**: VulkanPipeline (Graphics pipeline)

---

# Phase 4.1: SyncManager Implementation

This section describes the implementation of the SyncManager class.

## Overview

The SyncManager class encapsulates Vulkan synchronization primitives (semaphores and fences) that coordinate rendering operations between CPU and GPU across multiple frames in flight.

## Motivation

### Before SyncManager
In `main.cpp`, synchronization objects were managed manually:
```cpp
// Member variables scattered in HelloTriangleApplication
std::vector<vk::raii::Semaphore> presentCompleteSemaphore;
std::vector<vk::raii::Semaphore> renderFinishedSemaphore;
std::vector<vk::raii::Fence> inFlightFences;
uint32_t semaphoreIndex = 0;
uint32_t currentFrame = 0;

// Manual creation in createSyncObjects()
void createSyncObjects() {
    presentCompleteSemaphore.reserve(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphore.reserve(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.reserve(MAX_FRAMES_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        presentCompleteSemaphore.emplace_back(*device, semaphoreInfo);
        renderFinishedSemaphore.emplace_back(*device, semaphoreInfo);
        inFlightFences.emplace_back(*device, fenceInfo);
    }
}

// Manual fence operations in drawFrame()
device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX);
device.resetFences(*inFlightFences[currentFrame]);
```

**Problems**:
- Synchronization logic mixed with rendering code
- No encapsulation of semaphore/fence lifecycle
- Error-prone indexing between frame indices and image indices
- Difficult to understand the relationship between different sync primitives

### After SyncManager
```cpp
// In main.cpp - clean interface
std::unique_ptr<SyncManager> syncManager;

// Initialization
syncManager = std::make_unique<SyncManager>(
    *vulkanDevice,
    MAX_FRAMES_IN_FLIGHT,
    swapChainImages.size()
);

// Usage in drawFrame()
syncManager->waitForFence(currentFrame);
auto [result, imageIndex] = swapChain.acquireNextImage(
    UINT64_MAX,
    syncManager->getImageAvailableSemaphore(currentFrame),
    nullptr
);
syncManager->resetFence(currentFrame);

// Submit with proper synchronization
vk::Semaphore waitSemaphores[] = {
    syncManager->getImageAvailableSemaphore(currentFrame)
};
vk::Semaphore signalSemaphores[] = {
    syncManager->getRenderFinishedSemaphore(imageIndex)  // Note: imageIndex, not currentFrame
};
```

**Benefits**:
- Clear separation of concerns
- Encapsulated synchronization logic
- Self-documenting API (waitForFence, resetFence)
- Correct semaphore allocation per swapchain image

## Implementation Details

### Class Structure

#### Header: `src/rendering/SyncManager.hpp`
```cpp
class SyncManager {
public:
    SyncManager(VulkanDevice& device, uint32_t maxFramesInFlight, uint32_t swapchainImageCount);
    ~SyncManager() = default;

    // Disable copy, enable move construction only
    SyncManager(const SyncManager&) = delete;
    SyncManager& operator=(const SyncManager&) = delete;
    SyncManager(SyncManager&&) = default;
    SyncManager& operator=(SyncManager&&) = delete;

    // Accessors
    vk::Semaphore getImageAvailableSemaphore(uint32_t frameIndex) const;
    vk::Semaphore getRenderFinishedSemaphore(uint32_t imageIndex) const;
    vk::Fence getInFlightFence(uint32_t frameIndex) const;

    // Fence operations
    void waitForFence(uint32_t frameIndex);
    void resetFence(uint32_t frameIndex);

    uint32_t getMaxFramesInFlight() const { return maxFramesInFlight; }

private:
    VulkanDevice& device;
    uint32_t maxFramesInFlight;

    std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
};
```

### Key Design Decisions

#### 1. Separate Semaphore Counts
The critical insight: **frames in flight ≠ swapchain images**

```cpp
// Image available semaphores: one per frame in flight
for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    imageAvailableSemaphores.emplace_back(device.getDevice(), semaphoreInfo);
    inFlightFences.emplace_back(device.getDevice(), fenceInfo);
}

// Render finished semaphores: one per swapchain image
for (uint32_t i = 0; i < swapchainImageCount; i++) {
    renderFinishedSemaphores.emplace_back(device.getDevice(), semaphoreInfo);
}
```

**Rationale**:
- **Image available semaphores** (2): Coordinate CPU frame submission
  - Index by `currentFrame` (0 or 1 with MAX_FRAMES_IN_FLIGHT=2)
  - Signal when swapchain image becomes available

- **Render finished semaphores** (3): Coordinate swapchain presentation
  - Index by `imageIndex` (0, 1, or 2 for 3 swapchain images)
  - Signal when rendering to specific swapchain image completes
  - Each swapchain image needs its own semaphore to avoid reuse conflicts

- **In-flight fences** (2): Coordinate CPU-GPU synchronization
  - Index by `currentFrame`
  - Ensure CPU doesn't submit new work before GPU finishes previous frame

#### 2. RAII and Move Semantics
```cpp
// Using vk::raii types for automatic cleanup
std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
std::vector<vk::raii::Fence> inFlightFences;

// Copy deleted (prevents double-free)
SyncManager(const SyncManager&) = delete;
SyncManager& operator=(const SyncManager&) = delete;

// Move construction allowed (ownership transfer)
SyncManager(SyncManager&&) = default;

// Move assignment deleted (reference member prevents it)
SyncManager& operator=(SyncManager&&) = delete;
```

#### 3. Fence Abstraction
```cpp
void SyncManager::waitForFence(uint32_t frameIndex) {
    while (vk::Result::eTimeout == device.getDevice().waitForFences(
        *inFlightFences[frameIndex], vk::True, UINT64_MAX)) {
        // Wait until fence is signaled
    }
}

void SyncManager::resetFence(uint32_t frameIndex) {
    device.getDevice().resetFences(*inFlightFences[frameIndex]);
}
```

Simplifies fence operations while maintaining timeout handling.

## Bug Fix: Validation Layer Error

### The Problem
Initial implementation created only `MAX_FRAMES_IN_FLIGHT` (2) render-finished semaphores:

```cpp
// WRONG: Only 2 semaphores for 3 swapchain images
for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    renderFinishedSemaphores.emplace_back(device.getDevice(), semaphoreInfo);
}
```

**Validation error**:
```
vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] is being signaled by VkQueue,
but it may still be in use by VkSwapchainKHR.
```

**Why it failed**:
1. Frame 0 renders to image 0, signals semaphore 0
2. Swapchain takes ownership of semaphore 0 for presentation
3. Frame 1 renders to image 2, signals semaphore 1 - OK
4. Frame 0 (next loop) tries to render to image 2, wants to signal semaphore 0
5. But semaphore 0 is still held by swapchain from presenting image 0!

### The Solution
Create one render-finished semaphore per swapchain image:

```cpp
// CORRECT: 3 semaphores for 3 swapchain images
renderFinishedSemaphores.reserve(swapchainImageCount);
for (uint32_t i = 0; i < swapchainImageCount; i++) {
    renderFinishedSemaphores.emplace_back(device.getDevice(), semaphoreInfo);
}

// Use imageIndex (not currentFrame) to get render finished semaphore
vk::Semaphore signalSemaphores[] = {
    syncManager->getRenderFinishedSemaphore(imageIndex)
};
```

Now each swapchain image has its dedicated semaphore, preventing reuse conflicts.

## Integration Changes

### Files Modified

#### 1. `src/rendering/SyncManager.hpp` (Created)
- Class declaration with full API
- Documentation for parameters (frameIndex vs imageIndex)

#### 2. `src/rendering/SyncManager.cpp` (Created)
- Constructor with separated semaphore allocation
- Fence operation implementations
- Removed obsolete `createSyncObjects()` method

#### 3. `main.cpp`
**Removed**:
- `std::vector<vk::raii::Semaphore> presentCompleteSemaphore`
- `std::vector<vk::raii::Semaphore> renderFinishedSemaphore`
- `std::vector<vk::raii::Fence> inFlightFences`
- `uint32_t semaphoreIndex`
- `void createSyncObjects()` function

**Added**:
- `std::unique_ptr<SyncManager> syncManager;`
- Line 130: `syncManager = std::make_unique<SyncManager>(*vulkanDevice, MAX_FRAMES_IN_FLIGHT, swapChainImages.size());`

**Modified**:
- Line 714: `syncManager->waitForFence(currentFrame);`
- Line 717: `syncManager->getImageAvailableSemaphore(currentFrame)`
- Line 728: `syncManager->resetFence(currentFrame);`
- Line 733: `syncManager->getImageAvailableSemaphore(currentFrame)`
- Line 734: `syncManager->getRenderFinishedSemaphore(imageIndex)` ← Critical fix

#### 4. `CMakeLists.txt`
Added SyncManager to build:
```cmake
# Rendering classes
src/rendering/SyncManager.cpp
src/rendering/SyncManager.hpp
```

## Code Metrics

### Lines Removed from main.cpp
- Synchronization member variables: ~5 lines
- `createSyncObjects()` function: ~20 lines
- Manual fence operations: ~4 lines
- **Total**: ~29 lines removed

### Lines Added
- `SyncManager.hpp`: ~49 lines
- `SyncManager.cpp`: ~52 lines
- **Total**: ~101 lines (but in dedicated, reusable class)

### Complexity Reduction
- **Before**: Synchronization logic scattered across main.cpp
- **After**: Centralized in SyncManager with clear API

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
Application runs without validation errors
Proper frame synchronization verified
No semaphore reuse conflicts

---

# Phase 4.2: CommandManager Implementation

This section describes the implementation of the CommandManager class.

## Overview

The CommandManager class encapsulates Vulkan command pool and command buffer management, providing a clean interface for command recording and single-time command execution.

## Motivation

### Before CommandManager
In `main.cpp`, command pool and buffers were managed manually:
```cpp
// Member variables
vk::raii::CommandPool commandPool = nullptr;
std::vector<vk::raii::CommandBuffer> commandBuffers;

// Manual creation
void createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = vulkanDevice->getGraphicsQueueFamily()
    };
    commandPool = vk::raii::CommandPool(vulkanDevice->getDevice(), poolInfo);
}

void createCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT
    };
    commandBuffers = vk::raii::CommandBuffers(vulkanDevice->getDevice(), allocInfo);
}

// Single-time command utilities scattered in main.cpp
std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands() {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    auto commandBuffer = std::make_unique<vk::raii::CommandBuffer>(
        std::move(vk::raii::CommandBuffers(vulkanDevice->getDevice(), allocInfo).front())
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
    vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
    vulkanDevice->getGraphicsQueue().waitIdle();
}

// Direct access everywhere
commandBuffers[currentFrame].begin({});
commandBuffers[currentFrame].bindPipeline(...);
```

**Problems**:
- Command management logic mixed with rendering code
- No encapsulation of command pool lifecycle
- Repeated command buffer allocation code
- Single-time commands scattered throughout the codebase

### After CommandManager
```cpp
// In main.cpp - clean interface
std::unique_ptr<CommandManager> commandManager;

// Initialization
commandManager = std::make_unique<CommandManager>(
    *vulkanDevice,
    vulkanDevice->getGraphicsQueueFamily(),
    MAX_FRAMES_IN_FLIGHT
);

// Usage - frame command buffers
commandManager->getCommandBuffer(currentFrame).begin({});
commandManager->getCommandBuffer(currentFrame).bindPipeline(...);

// Usage - single-time commands
auto commandBuffer = commandManager->beginSingleTimeCommands();
vertexBuffer->copyFrom(stagingBuffer, *commandBuffer);
commandManager->endSingleTimeCommands(*commandBuffer);
```

**Benefits**:
- Clear separation of command management from rendering logic
- Encapsulated command pool and buffer lifecycle
- Centralized single-time command utilities
- Simplified API with frame-indexed access

## Implementation Details

### Class Structure

#### Header: `src/rendering/CommandManager.hpp`
```cpp
class CommandManager {
public:
    CommandManager(VulkanDevice& device, uint32_t queueFamilyIndex, uint32_t maxFramesInFlight);
    ~CommandManager() = default;

    // Disable copy, enable move construction only
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;
    CommandManager(CommandManager&&) = default;
    CommandManager& operator=(CommandManager&&) = delete;

    // Command buffer access
    vk::raii::CommandBuffer& getCommandBuffer(uint32_t frameIndex);
    const vk::raii::CommandBuffer& getCommandBuffer(uint32_t frameIndex) const;

    // Single-time command utilities
    std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();
    void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer);

    // Command pool access
    vk::CommandPool getCommandPool() const { return *commandPool; }

private:
    VulkanDevice& device;
    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;

    void createCommandPool(uint32_t queueFamilyIndex);
    void createCommandBuffers(uint32_t count);
};
```

### Key Design Decisions

#### 1. Constructor Initialization
All command resources created in constructor:
```cpp
CommandManager::CommandManager(VulkanDevice& device, uint32_t queueFamilyIndex, uint32_t maxFramesInFlight)
    : device(device) {
    createCommandPool(queueFamilyIndex);
    createCommandBuffers(maxFramesInFlight);
}
```

#### 2. Frame-Indexed Access
Simple getter API for per-frame command buffers:
```cpp
vk::raii::CommandBuffer& CommandManager::getCommandBuffer(uint32_t frameIndex) {
    return commandBuffers[frameIndex];
}
```

Allows clean usage:
```cpp
commandManager->getCommandBuffer(currentFrame).begin({});
commandManager->getCommandBuffer(currentFrame).bindPipeline(...);
```

#### 3. Single-Time Commands
Encapsulated pattern for staging operations:
```cpp
std::unique_ptr<vk::raii::CommandBuffer> CommandManager::beginSingleTimeCommands() {
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

void CommandManager::endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffer
    };

    device.getGraphicsQueue().submit(submitInfo);
    device.getGraphicsQueue().waitIdle();
}
```

Used for buffer/image transfers:
```cpp
// Texture upload
auto commandBuffer = commandManager->beginSingleTimeCommands();
textureImage->transitionLayout(*commandBuffer, ...);
textureImage->copyFromBuffer(*commandBuffer, stagingBuffer);
commandManager->endSingleTimeCommands(*commandBuffer);

// Vertex buffer upload
auto commandBuffer = commandManager->beginSingleTimeCommands();
vertexBuffer->copyFrom(stagingBuffer, *commandBuffer);
commandManager->endSingleTimeCommands(*commandBuffer);
```

## Integration Changes

### Files Created

#### 1. `src/rendering/CommandManager.hpp` (Created)
- Class declaration with command pool and buffer management API
- Documentation for queue family and frame count parameters

#### 2. `src/rendering/CommandManager.cpp` (Created)
- Constructor with automatic pool and buffer creation
- Getter implementations for frame-indexed access
- Single-time command utilities

### Files Modified

#### 3. `main.cpp`
**Removed**:
- `vk::raii::CommandPool commandPool` member variable
- `std::vector<vk::raii::CommandBuffer> commandBuffers` member variable
- `void createCommandPool()` function (~5 lines)
- `void createCommandBuffers()` function (~5 lines)
- `std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands()` function (~10 lines)
- `void endSingleTimeCommands(vk::raii::CommandBuffer&)` function (~7 lines)

**Added**:
- `#include "src/rendering/CommandManager.hpp"`
- `std::unique_ptr<CommandManager> commandManager;`
- Line 117: `commandManager = std::make_unique<CommandManager>(*vulkanDevice, vulkanDevice->getGraphicsQueueFamily(), MAX_FRAMES_IN_FLIGHT);`

**Modified** (all `commandBuffers[currentFrame]` → `commandManager->getCommandBuffer(currentFrame)`):
- Line 542: `recordCommandBuffer()` - begin
- Line 577: pipeline barrier for depth
- Lines 605-613: rendering commands (begin, bind, draw, end)
- Line 624: end command buffer
- Line 659: pipeline barrier for layout transition
- Line 700: reset command buffer
- Line 713: submit command buffer

**Modified** (single-time commands):
- Line 352: texture image transitions
- Line 434: vertex buffer copy
- Line 457: index buffer copy

#### 4. `CMakeLists.txt`
Added CommandManager to build:
```cmake
# Rendering classes
src/rendering/SyncManager.cpp
src/rendering/SyncManager.hpp
src/rendering/CommandManager.cpp
src/rendering/CommandManager.hpp
```

## Code Metrics

### Lines Removed from main.cpp
- Command pool/buffer member variables: ~2 lines
- `createCommandPool()` function: ~5 lines
- `createCommandBuffers()` function: ~5 lines
- `beginSingleTimeCommands()` function: ~10 lines
- `endSingleTimeCommands()` function: ~7 lines
- **Total**: ~29 lines removed

### Lines Added
- `CommandManager.hpp`: ~52 lines
- `CommandManager.cpp`: ~68 lines
- **Total**: ~120 lines (in dedicated, reusable class)

### Complexity Reduction
- **Before**: Command management scattered across main.cpp
- **After**: Centralized in CommandManager with clean API
- **Usage sites**: 15+ locations now use simple `commandManager->getCommandBuffer()`

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
Application runs without validation errors
Command buffer recording works correctly
Single-time commands execute properly for staging operations

## Summary

The CommandManager class successfully encapsulates command pool and buffer management, providing:
- Unified interface for frame-based command buffer access
- Centralized single-time command execution pattern
- Proper RAII semantics with automatic cleanup
- Foundation for pipeline refactoring

Combined with SyncManager, the rendering layer is becoming well-structured and maintainable.

---

# Phase 4.3: VulkanSwapchain Implementation

This section describes the implementation of the VulkanSwapchain class.

## Overview

The VulkanSwapchain class encapsulates Vulkan swapchain management, including creation, recreation, image view management, and configuration utilities.

## Motivation

### Before VulkanSwapchain
In `main.cpp`, swapchain management was scattered across multiple variables and functions:
```cpp
// Member variables scattered throughout the class
vk::raii::SwapchainKHR swapChain = nullptr;
std::vector<vk::Image> swapChainImages;
vk::SurfaceFormatKHR swapChainSurfaceFormat;
vk::Extent2D swapChainExtent;
std::vector<vk::raii::ImageView> swapChainImageViews;

// Manual creation functions
void createSwapChain() {
    auto surfaceCapabilities = vulkanDevice->getPhysicalDevice().getSurfaceCapabilitiesKHR(*vulkanDevice->getSurface());
    swapChainExtent = chooseSwapExtent(surfaceCapabilities);
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(vulkanDevice->getPhysicalDevice().getSurfaceFormatsKHR(*vulkanDevice->getSurface()));

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *vulkanDevice->getSurface(),
        .minImageCount = chooseSwapMinImageCount(surfaceCapabilities),
        .imageFormat = swapChainSurfaceFormat.format,
        .imageColorSpace = swapChainSurfaceFormat.colorSpace,
        .imageExtent = swapChainExtent,
        // ... more configuration
    };

    swapChain = vk::raii::SwapchainKHR(vulkanDevice->getDevice(), swapChainCreateInfo);
    swapChainImages = swapChain.getImages();
}

void createImageViews() {
    vk::ImageViewCreateInfo imageViewCreateInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = swapChainSurfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    };
    for (auto image : swapChainImages) {
        imageViewCreateInfo.image = image;
        swapChainImageViews.emplace_back(vulkanDevice->getDevice(), imageViewCreateInfo);
    }
}

void cleanupSwapChain() {
    swapChainImageViews.clear();
    swapChain = nullptr;
}

void recreateSwapChain() {
    // Wait for window to be visible
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vulkanDevice->getDevice().waitIdle();
    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createDepthResources();
}

// Helper functions scattered in class
static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);
static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes);
vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

// Usage scattered everywhere
swapChainExtent.width
swapChainSurfaceFormat.format
swapChainImageViews[imageIndex]
swapChainImages[imageIndex]
swapChain.acquireNextImage(...)
```

**Problems**:
- Swapchain state fragmented across 5+ member variables
- Manual lifecycle management (create, cleanup, recreate)
- Helper functions polluting main class interface
- Configuration logic mixed with rendering code
- Direct access to internal state everywhere

### After VulkanSwapchain
```cpp
// In main.cpp - single, clean interface
std::unique_ptr<VulkanSwapchain> swapchain;

// Initialization
swapchain = std::make_unique<VulkanSwapchain>(*vulkanDevice, window);

// Recreation
swapchain->recreate();

// Usage - clean accessor methods
swapchain->getExtent()
swapchain->getFormat()
swapchain->getImageView(imageIndex)
swapchain->getImages()[imageIndex]
swapchain->acquireNextImage(timeout, semaphore)
swapchain->getImageCount()
```

**Benefits**:
- All swapchain state encapsulated in one class
- Automatic lifecycle management with RAII
- Configuration logic hidden as private implementation
- Clean, intention-revealing API
- Single source of truth for swapchain properties

## Implementation Details

### Class Structure

#### Header: `src/rendering/VulkanSwapchain.hpp`
```cpp
class VulkanSwapchain {
public:
    VulkanSwapchain(VulkanDevice& device, GLFWwindow* window);
    ~VulkanSwapchain() = default;

    // Disable copy, enable move construction only
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = default;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    // Swapchain operations
    void recreate();
    void cleanup();

    // Image acquisition
    std::pair<vk::Result, uint32_t> acquireNextImage(
        uint64_t timeout,
        vk::Semaphore semaphore,
        vk::Fence fence = nullptr);

    // Accessors
    vk::SwapchainKHR getSwapchain() const { return *swapchain; }
    const std::vector<vk::Image>& getImages() const { return images; }
    vk::ImageView getImageView(uint32_t index) const { return *imageViews[index]; }
    const std::vector<vk::raii::ImageView>& getImageViews() const { return imageViews; }
    vk::Format getFormat() const { return surfaceFormat.format; }
    vk::Extent2D getExtent() const { return extent; }
    uint32_t getImageCount() const { return static_cast<uint32_t>(images.size()); }

private:
    VulkanDevice& device;
    GLFWwindow* window;

    vk::raii::SwapchainKHR swapchain = nullptr;
    std::vector<vk::Image> images;
    std::vector<vk::raii::ImageView> imageViews;
    vk::SurfaceFormatKHR surfaceFormat;
    vk::Extent2D extent;

    void createSwapchain();
    void createImageViews();

    // Helper functions for swapchain configuration
    static uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);
    static vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    static vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& modes);
    vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
};
```

### Key Design Decisions

#### 1. Constructor Initialization
All swapchain resources created in constructor:
```cpp
VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, GLFWwindow* window)
    : device(device), window(window) {
    createSwapchain();
    createImageViews();
}
```

Single construction call replaces multiple setup functions.

#### 2. Encapsulated Configuration Logic
Configuration helpers moved from public static functions to private methods:
```cpp
// BEFORE: Public static functions polluting interface
static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR&);
static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>&);

// AFTER: Private implementation details
private:
    static uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);
    static vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
```

Implementation:
```cpp
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

vk::PresentModeKHR VulkanSwapchain::choosePresentMode(const std::vector<vk::PresentModeKHR>& modes) {
    for (const auto& mode : modes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            return vk::PresentModeKHR::eMailbox;
        }
    }
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanSwapchain::chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != 0xFFFFFFFF) {
        return capabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return {
        std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}
```

#### 3. Simplified Recreation
Complex recreation logic simplified:
```cpp
void VulkanSwapchain::recreate() {
    // Wait for device to be idle before recreating swapchain
    device.getDevice().waitIdle();

    cleanup();
    createSwapchain();
    createImageViews();
}
```

Window minimization handling now done in main.cpp before calling recreate():
```cpp
// In main.cpp
void recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    swapchain->recreate();
    createDepthResources();
}
```

#### 4. Unified Image Acquisition
Swapchain's acquireNextImage wrapped with same signature:
```cpp
std::pair<vk::Result, uint32_t> VulkanSwapchain::acquireNextImage(
    uint64_t timeout,
    vk::Semaphore semaphore,
    vk::Fence fence) {
    return swapchain.acquireNextImage(timeout, semaphore, fence);
}
```

Allows transparent usage:
```cpp
auto [result, imageIndex] = swapchain->acquireNextImage(
    UINT64_MAX,
    syncManager->getImageAvailableSemaphore(currentFrame),
    nullptr);
```

## Integration Changes

### Files Created

#### 1. `src/rendering/VulkanSwapchain.hpp` (Created)
- Class declaration with swapchain management API
- Accessors for extent, format, images, and image views
- Recreation and cleanup methods

#### 2. `src/rendering/VulkanSwapchain.cpp` (Created)
- Constructor with automatic swapchain and image view creation
- Configuration helper implementations
- Recreation and cleanup logic

### Files Modified

#### 3. `main.cpp`
**Removed**:
- `vk::raii::SwapchainKHR swapChain` member variable
- `std::vector<vk::Image> swapChainImages` member variable
- `vk::SurfaceFormatKHR swapChainSurfaceFormat` member variable
- `vk::Extent2D swapChainExtent` member variable
- `std::vector<vk::raii::ImageView> swapChainImageViews` member variable
- `void createSwapChain()` function (~20 lines)
- `void createImageViews()` function (~10 lines)
- `void cleanupSwapChain()` function (~3 lines)
- `static uint32_t chooseSwapMinImageCount()` function (~7 lines)
- `static vk::SurfaceFormatKHR chooseSwapSurfaceFormat()` function (~8 lines)
- `static vk::PresentModeKHR chooseSwapPresentMode()` function (~5 lines)
- `vk::Extent2D chooseSwapExtent()` function (~12 lines)

**Added**:
- `#include "src/rendering/VulkanSwapchain.hpp"`
- `std::unique_ptr<VulkanSwapchain> swapchain;`
- Line 110: `swapchain = std::make_unique<VulkanSwapchain>(*vulkanDevice, window);`

**Modified** (swapchain usage):
- Line 124: `syncManager = std::make_unique<SyncManager>(..., swapchain->getImageCount());`
- Line 149: `swapchain->recreate();`
- Line 228: `vk::Format swapchainFormat = swapchain->getFormat();`
- Line 253: `swapchain->getExtent().width, swapchain->getExtent().height`
- Line 534: `swapchain->getImageView(imageIndex)`
- Line 550: `swapchain->getExtent()`
- Line 558-559: `swapchain->getExtent().width/height`
- Line 596: `swapchain->getImages()[imageIndex]`
- Line 623: `swapchain->getExtent().width / height`
- Line 634: `swapchain->acquireNextImage(...)`
- Line 672: `swapchain->getSwapchain()`

#### 4. `CMakeLists.txt`
Added VulkanSwapchain to build:
```cmake
# Rendering classes
src/rendering/SyncManager.cpp
src/rendering/SyncManager.hpp
src/rendering/CommandManager.cpp
src/rendering/CommandManager.hpp
src/rendering/VulkanSwapchain.cpp
src/rendering/VulkanSwapchain.hpp
```

## Code Metrics

### Lines Removed from main.cpp
- Swapchain member variables: ~5 lines
- `createSwapChain()` function: ~20 lines
- `createImageViews()` function: ~10 lines
- `cleanupSwapChain()` function: ~3 lines
- Helper functions (chooseSwap*): ~32 lines
- **Total**: ~70 lines removed

### Lines Added
- `VulkanSwapchain.hpp`: ~68 lines
- `VulkanSwapchain.cpp`: ~117 lines
- **Total**: ~185 lines (in dedicated, reusable class)

### Complexity Reduction
- **Before**: Swapchain state fragmented across 5 variables + 7 functions
- **After**: Single class with clean interface
- **Usage sites**: 10+ locations now use simple `swapchain->get*()`

## Bug Fixes During Integration

### Issue 1: Rvalue Address Error
**Error**: Cannot take address of rvalue `swapchain->getFormat()`
```cpp
// WRONG: Taking address of temporary
.pColorAttachmentFormats = &swapchain->getFormat()
```

**Fix**: Store in variable first
```cpp
vk::Format swapchainFormat = swapchain->getFormat();
.pColorAttachmentFormats = &swapchainFormat
```

### Issue 2: Missing GLFW Header
**Error**: `glfwGetFramebufferSize` undeclared in VulkanSwapchain.cpp

**Fix**: Added GLFW include
```cpp
#include "VulkanSwapchain.hpp"
#include <GLFW/glfw3.h>  // Added for glfwGetFramebufferSize
#include <algorithm>
#include <cassert>
```

### Issue 3: Swapchain Handle Address
**Error**: Taking address of temporary `swapchain->getSwapchain()`
```cpp
// WRONG: Taking address of temporary
.pSwapchains = &swapchain->getSwapchain()
```

**Fix**: Store handle in variable
```cpp
vk::SwapchainKHR swapchainHandle = swapchain->getSwapchain();
.pSwapchains = &swapchainHandle
```

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
Application runs without validation errors
Swapchain creation works correctly
Window resizing triggers proper swapchain recreation
Image acquisition and presentation work seamlessly

## Summary

The VulkanSwapchain class successfully encapsulates swapchain management, providing:
- Unified state management (extent, format, images, views)
- Simplified recreation logic
- Clean accessor API hiding implementation details
- Proper RAII semantics with automatic cleanup
- Foundation for further rendering abstraction

Combined with SyncManager and CommandManager, the rendering layer now has:
- **SyncManager**: CPU-GPU and GPU-GPU synchronization
- **CommandManager**: Command recording and submission
- **VulkanSwapchain**: Presentation surface management

Next step: VulkanPipeline to complete the rendering layer refactoring.

---

# Phase 4.4: VulkanPipeline Implementation

This section describes the implementation of the VulkanPipeline class.

## Overview

The VulkanPipeline class encapsulates Vulkan graphics pipeline management, including descriptor set layout, pipeline layout, and graphics pipeline creation with support for dynamic rendering.

## Motivation

### Before VulkanPipeline
In `main.cpp`, pipeline management was scattered across multiple variables and functions:
```cpp
// Member variables scattered throughout the class
vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
vk::raii::PipelineLayout pipelineLayout = nullptr;
vk::raii::Pipeline graphicsPipeline = nullptr;

// Manual creation functions
void createDescriptorSetLayout() {
    std::array bindings = {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr)
    };
    vk::DescriptorSetLayoutCreateInfo layoutInfo{...};
    descriptorSetLayout = vk::raii::DescriptorSetLayout(vulkanDevice->getDevice(), layoutInfo);
}

void createGraphicsPipeline() {
    // Shader module loading
    vk::raii::ShaderModule shaderModule = createShaderModule(FileUtils::readFile("shaders/slang.spv"));

    // Vertex input configuration
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    // Pipeline state configuration (70+ lines)
    // - Vertex input state
    // - Input assembly state
    // - Viewport state (dynamic)
    // - Rasterization state
    // - Multisampling state
    // - Depth/stencil state
    // - Color blending state
    // - Dynamic state

    // Pipeline layout creation
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{...};
    pipelineLayout = vk::raii::PipelineLayout(vulkanDevice->getDevice(), pipelineLayoutInfo);

    // Graphics pipeline creation with dynamic rendering
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {...};
    graphicsPipeline = vk::raii::Pipeline(vulkanDevice->getDevice(), nullptr, pipelineCreateInfoChain.get<>());
}

vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const {
    vk::ShaderModuleCreateInfo createInfo{...};
    return vk::raii::ShaderModule{vulkanDevice->getDevice(), createInfo};
}

// Usage scattered everywhere
commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, ...);
std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
```

**Problems**:
- Pipeline state fragmented across 3 member variables
- Complex creation logic (~95 lines) in main.cpp
- Pipeline configuration mixed with rendering code
- Shader module management ad-hoc
- No encapsulation of pipeline lifecycle
- Direct access to internal objects everywhere

### After VulkanPipeline
```cpp
// In main.cpp - single, clean interface
std::unique_ptr<VulkanPipeline> pipeline;

// Initialization
pipeline = std::make_unique<VulkanPipeline>(
    *vulkanDevice,
    *swapchain,
    "shaders/slang.spv",
    findDepthFormat()
);

// Usage - clean accessor methods
pipeline->bind(commandBuffer);
pipeline->getPipelineLayout()
pipeline->getDescriptorSetLayout()
```

**Benefits**:
- All pipeline state encapsulated in one class
- Automatic lifecycle management with RAII
- Configuration logic hidden as private implementation
- Clean, intention-revealing API
- Single source of truth for pipeline objects
- Shader module management internalized

## Implementation Details

### Class Structure

#### Header: `src/rendering/VulkanPipeline.hpp`
```cpp
class VulkanPipeline {
public:
    /**
     * @brief Construct graphics pipeline with shader and configuration
     * @param device Vulkan device reference
     * @param swapchain Swapchain for format information
     * @param shaderPath Path to compiled shader SPIR-V file
     * @param depthFormat Depth buffer format
     */
    VulkanPipeline(
        VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        const std::string& shaderPath,
        vk::Format depthFormat);

    ~VulkanPipeline() = default;

    // Disable copy, enable move construction only
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;
    VulkanPipeline(VulkanPipeline&&) = default;
    VulkanPipeline& operator=(VulkanPipeline&&) = delete;

    // Accessors
    vk::Pipeline getPipeline() const { return *graphicsPipeline; }
    vk::PipelineLayout getPipelineLayout() const { return *pipelineLayout; }
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return *descriptorSetLayout; }

    // Bind pipeline to command buffer
    void bind(const vk::raii::CommandBuffer& commandBuffer) const;

private:
    VulkanDevice& device;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;

    void createDescriptorSetLayout();
    void createPipelineLayout();
    void createGraphicsPipeline(
        const std::string& shaderPath,
        vk::Format colorFormat,
        vk::Format depthFormat);

    vk::raii::ShaderModule createShaderModule(const std::vector<char>& code);
};
```

### Key Design Decisions

#### 1. Constructor-Driven Initialization
All pipeline resources created in constructor:
```cpp
VulkanPipeline::VulkanPipeline(
    VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const std::string& shaderPath,
    vk::Format depthFormat)
    : device(device) {

    createDescriptorSetLayout();
    createPipelineLayout();
    createGraphicsPipeline(shaderPath, swapchain.getFormat(), depthFormat);
}
```

Single construction call replaces multiple setup functions and ensures correct initialization order.

#### 2. Descriptor Set Layout Configuration
Encapsulated configuration for uniform buffers and samplers:
```cpp
void VulkanPipeline::createDescriptorSetLayout() {
    std::array bindings = {
        vk::DescriptorSetLayoutBinding(
            0,  // binding
            vk::DescriptorType::eUniformBuffer,
            1,  // count
            vk::ShaderStageFlagBits::eVertex,
            nullptr),
        vk::DescriptorSetLayoutBinding(
            1,  // binding
            vk::DescriptorType::eCombinedImageSampler,
            1,  // count
            vk::ShaderStageFlagBits::eFragment,
            nullptr)
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };

    descriptorSetLayout = vk::raii::DescriptorSetLayout(device.getDevice(), layoutInfo);
}
```

**Rationale**:
- Binding 0: Uniform buffer (MVP matrices) in vertex shader
- Binding 1: Combined image sampler (texture) in fragment shader

#### 3. Graphics Pipeline Configuration
Full pipeline state configuration with dynamic rendering support:
```cpp
void VulkanPipeline::createGraphicsPipeline(
    const std::string& shaderPath,
    vk::Format colorFormat,
    vk::Format depthFormat) {

    // Load shader module
    vk::raii::ShaderModule shaderModule = createShaderModule(FileUtils::readFile(shaderPath));

    // Shader stages
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = shaderModule,
        .pName = "vertMain"
    };
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = shaderModule,
        .pName = "fragMain"
    };

    // Vertex input from Vertex class
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()
    };

    // Input assembly: triangle list topology
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False
    };

    // Viewport state: dynamic viewport and scissor
    vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1
    };

    // Rasterization: back-face culling, counter-clockwise front face
    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f
    };

    // Multisampling: disabled (single sample)
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False
    };

    // Depth/stencil: depth test enabled with less compare op
    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False
    };

    // Color blending: disabled, write all components
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR |
                         vk::ColorComponentFlagBits::eG |
                         vk::ColorComponentFlagBits::eB |
                         vk::ColorComponentFlagBits::eA
    };

    // Dynamic states: viewport and scissor
    std::vector dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    // Create pipeline with dynamic rendering (no render pass)
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = *pipelineLayout,
            .renderPass = nullptr  // Using dynamic rendering
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat,
            .depthAttachmentFormat = depthFormat
        }
    };

    graphicsPipeline = vk::raii::Pipeline(
        device.getDevice(),
        nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
    );
}
```

**Key features**:
- Dynamic rendering support (VK_KHR_dynamic_rendering)
- Single SPIR-V file with multiple entry points (vertMain, fragMain)
- Dynamic viewport and scissor for window resizing
- Depth testing for 3D rendering
- Back-face culling for performance

#### 4. Shader Module Management
Internalized shader loading and compilation:
```cpp
vk::raii::ShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };

    return vk::raii::ShaderModule(device.getDevice(), createInfo);
}
```

Shader modules are RAII-managed and live within the pipeline creation scope.

#### 5. Simplified Binding
Clean command buffer binding:
```cpp
void VulkanPipeline::bind(const vk::raii::CommandBuffer& commandBuffer) const {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
}
```

Single method call replaces verbose pipeline binding code.

## Integration Changes

### Files Created

#### 1. `src/rendering/VulkanPipeline.hpp` (Created)
- Class declaration with pipeline management API
- Accessors for pipeline, layout, and descriptor set layout
- Bind method for command buffer binding

#### 2. `src/rendering/VulkanPipeline.cpp` (Created)
- Constructor with automatic pipeline creation
- Descriptor set layout configuration
- Graphics pipeline state setup
- Shader module loading and management

### Files Modified

#### 3. `main.cpp`
**Removed**:
- `vk::raii::DescriptorSetLayout descriptorSetLayout` member variable
- `vk::raii::PipelineLayout pipelineLayout` member variable
- `vk::raii::Pipeline graphicsPipeline` member variable
- `void createDescriptorSetLayout()` function (~8 lines)
- `void createGraphicsPipeline()` function (~95 lines)
- `vk::raii::ShaderModule createShaderModule()` function (~5 lines)

**Added**:
- `#include "src/rendering/VulkanPipeline.hpp"`
- `std::unique_ptr<VulkanPipeline> pipeline;`
- Line 109: `pipeline = std::make_unique<VulkanPipeline>(*vulkanDevice, *swapchain, "shaders/slang.spv", findDepthFormat());`

**Modified** (pipeline usage):
- Line 344: `pipeline->getDescriptorSetLayout()` (descriptor set allocation)
- Line 458: `pipeline->bind(commandManager->getCommandBuffer(currentFrame))` (pipeline binding)
- Line 463: `pipeline->getPipelineLayout()` (descriptor set binding)

#### 4. `CMakeLists.txt`
Added VulkanPipeline to build:
```cmake
# Rendering classes
src/rendering/SyncManager.cpp
src/rendering/SyncManager.hpp
src/rendering/CommandManager.cpp
src/rendering/CommandManager.hpp
src/rendering/VulkanSwapchain.cpp
src/rendering/VulkanSwapchain.hpp
src/rendering/VulkanPipeline.cpp
src/rendering/VulkanPipeline.hpp
```

## Code Metrics

### Lines Removed from main.cpp
- Pipeline member variables: ~3 lines
- `createDescriptorSetLayout()` function: ~8 lines
- `createGraphicsPipeline()` function: ~95 lines
- `createShaderModule()` function: ~5 lines
- **Total**: ~111 lines removed

### Lines Added
- `VulkanPipeline.hpp`: ~61 lines
- `VulkanPipeline.cpp`: ~189 lines
- **Total**: ~250 lines (in dedicated, reusable class)

### Complexity Reduction
- **Before**: Pipeline state fragmented across 3 variables + 3 functions + ~110 lines
- **After**: Single class with clean interface
- **Usage sites**: 3 locations now use simple `pipeline->get*()` or `pipeline->bind()`

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
Application runs without validation errors
Graphics pipeline binds correctly
Descriptor set layout works with descriptor allocation
Rendering produces correct output with depth testing and culling

## Summary

The VulkanPipeline class successfully encapsulates graphics pipeline management, providing:
- Unified pipeline state management (descriptor layout, pipeline layout, pipeline)
- Simplified initialization with constructor-driven setup
- Clean accessor API hiding implementation details
- Proper RAII semantics with automatic cleanup
- Support for dynamic rendering (VK_KHR_dynamic_rendering)
- Internalized shader module management

### Phase 4 Complete!

With VulkanPipeline, the rendering layer refactoring is now complete. The four major components:

1. **SyncManager**: CPU-GPU and GPU-GPU synchronization
   - Semaphores (image available, render finished)
   - Fences (in-flight frame tracking)

2. **CommandManager**: Command recording and submission
   - Command pool and buffer management
   - Single-time command utilities

3. **VulkanSwapchain**: Presentation surface management
   - Swapchain creation and recreation
   - Image and image view management

4. **VulkanPipeline**: Graphics pipeline configuration
   - Descriptor set layout
   - Pipeline layout
   - Graphics pipeline with full state

**Impact**:
- Removed ~210+ lines from main.cpp
- Added ~650+ lines in well-organized, reusable classes
- Clear separation of concerns
- Improved testability and maintainability
- Foundation for future rendering features

The refactoring successfully transformed monolithic code into a modular, object-oriented architecture while maintaining full functionality and correctness.
