# Phase 2: Device Management Refactoring

This document describes the device management extraction in Phase 2 of the refactoring process.

## Goal

Encapsulate Vulkan instance, physical device, logical device, and queue management into a dedicated class.

## Overview

### Before Phase 2
- Instance, physical device, and logical device managed in main.cpp
- Queue management mixed with rendering code
- Utility functions (findMemoryType, findSupportedFormat) scattered
- Debug messenger setup intertwined with instance creation

### After Phase 2
- Clean VulkanDevice class encapsulating all device management
- Explicit initialization sequence
- Utility functions properly encapsulated
- Significantly cleaner main.cpp

---

## Changes

### 1. Created `VulkanDevice` Class

**Files Created**:
- `src/core/VulkanDevice.hpp`
- `src/core/VulkanDevice.cpp`

**Responsibilities**:
- Vulkan instance creation with validation layers
- Debug messenger setup
- Physical device selection
- Logical device creation
- Queue management (graphics queue)
- Utility functions (memory type finding, format support)

**Class Interface**:
```cpp
class VulkanDevice {
public:
    VulkanDevice(const std::vector<const char*>& validationLayers, bool enableValidation);
    ~VulkanDevice() = default;

    // Disable copy, enable move
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = default;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    void createSurface(GLFWwindow* window);
    void createLogicalDevice();

    // Accessors
    vk::raii::Context& getContext();
    vk::raii::Instance& getInstance();
    vk::raii::PhysicalDevice& getPhysicalDevice();
    vk::raii::Device& getDevice();
    vk::raii::Queue& getGraphicsQueue();
    vk::raii::SurfaceKHR& getSurface();
    uint32_t getGraphicsQueueFamily() const;

    // Utilities
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    vk::Format findSupportedFormat(
        const std::vector<vk::Format>& candidates,
        vk::ImageTiling tiling,
        vk::FormatFeatureFlags features) const;
};
```

### 2. Initialization Order Fix

**Problem**: Logical device creation requires surface to exist, but original code created device in constructor before surface was available.

**Solution**: Explicit three-step initialization sequence

**Before**: Implicit initialization with order dependency bug
```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();           // Surface created
    pickPhysicalDevice();
    createLogicalDevice();     // Needs surface - works by luck
    createSwapChain();
}
```

**After**: Explicit initialization with correct order
```cpp
void initVulkan() {
    // Step 1: Create device (handles instance, debug messenger, physical device)
    vulkanDevice = std::make_unique<VulkanDevice>(validationLayers, enableValidationLayers);

    // Step 2: Create surface (needs window and instance)
    vulkanDevice->createSurface(window);

    // Step 3: Create logical device (needs surface for queue family selection)
    vulkanDevice->createLogicalDevice();

    // Step 4: Continue with other initialization
    createSwapChain();
}
```

**Why This Matters**:
- Physical device selection needs to check surface support
- Queue family selection requires surface for present support
- Explicit steps make dependencies clear and prevent bugs

### 3. Key Implementation Points

**Constructor**:
- Creates Vulkan instance with validation layers
- Sets up debug messenger if validation enabled
- Picks suitable physical device

**createSurface**:
- Uses GLFW for cross-platform surface creation
- Wraps VkSurfaceKHR in RAII wrapper
- Must be called before createLogicalDevice

**createLogicalDevice**:
- Finds queue family that supports both graphics AND present
- Creates single logical device with one queue
- Enables required extensions (swapchain, anisotropic filtering)

**Utility Functions**:
- `findMemoryType`: Finds suitable memory type for buffer/image allocation
- `findSupportedFormat`: Checks device format support for depth buffers, etc.

---

## Integration Changes

### main.cpp Simplification

**Member Variables Before**:
```cpp
vk::raii::Context context;
vk::raii::Instance instance = nullptr;
vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
vk::raii::SurfaceKHR surface = nullptr;
vk::raii::PhysicalDevice physicalDevice = nullptr;
vk::raii::Device device = nullptr;
vk::raii::Queue queue = nullptr;
uint32_t queueIndex = ~0;
```

**Member Variables After**:
```cpp
std::unique_ptr<VulkanDevice> vulkanDevice;
```

**Functions Removed from main.cpp**:
- `createInstance()`
- `setupDebugMessenger()`
- `createSurface()`
- `pickPhysicalDevice()`
- `createLogicalDevice()`
- `findMemoryType()`
- `findSupportedFormat()`
- `getRequiredExtensions()`
- `debugCallback()`

**Reference Updates**:
All direct references updated to use VulkanDevice getters:
```cpp
// Before
device.createBuffer(/* ... */);
physicalDevice.getMemoryProperties();
queue.submit(/* ... */);

// After
vulkanDevice->getDevice().createBuffer(/* ... */);
vulkanDevice->getPhysicalDevice().getMemoryProperties();
vulkanDevice->getGraphicsQueue().submit(/* ... */);
```

---

## Reflection

### What Worked Well
- **Explicit initialization order**: The three-step initialization (device → surface → logical device) made dependencies crystal clear and eliminated subtle bugs
- **Clean encapsulation**: Having all device management in one class made it obvious where to look when debugging device-related issues
- **Utility consolidation**: Moving `findMemoryType` and `findSupportedFormat` into VulkanDevice made them easily accessible throughout the codebase

### Challenges
- **Initialization sequencing**: Had to split initialization across constructor and separate methods due to surface dependency. This felt awkward at first but proved necessary.
- **Reference updates**: Updating all references from `device` to `vulkanDevice->getDevice()` was tedious. Used a Python script to automate most of it.
- **Move semantics**: Initially forgot to properly handle move construction, causing compilation errors when storing VulkanDevice in containers

### Impact on Later Phases
- **Phase 3 (Resources)**: The utility functions (`findMemoryType`, `findSupportedFormat`) became essential for VulkanBuffer and VulkanImage RAII wrappers
- **Phase 4 (Rendering)**: Queue accessor made it easy to separate command submission logic
- **Cross-platform work**: Having device management centralized made it easier to add platform-specific device selection logic later

---

## Testing

### Build
```bash
cmake --build build
```
Build successful with no warnings. All references correctly updated.

### Runtime
```bash
./build/vulkanGLFW
```
Application ran correctly with VulkanDevice. Instance and device created successfully. Validation layers working. Queue selection correct for graphics and present.

---

## Summary

Phase 2 extracted device management into VulkanDevice class:
- Encapsulated instance, physical device, logical device, and queue management
- Established explicit initialization sequence to prevent order bugs
- Cleaned up main.cpp significantly
- Created reusable utility functions for memory and format queries

This phase removed a large chunk of boilerplate from main.cpp and established a pattern of explicit initialization that was followed in subsequent phases.

---

*Phase 2 Complete*
*Previous: Phase 1 - Utility Layer*
*Next: Phase 3 - Resource Management*
