# Phase 8: Cross-Platform Rendering Support

This document describes the cross-platform support implementation in Phase 8 of the refactoring process.

## Goal

Enable dual rendering paths to support different Vulkan versions and platform capabilities without code duplication.

## Overview

### Motivation
- Support development on WSL/llvmpipe (Vulkan 1.1 only)
- Support production on macOS via MoltenVK (Vulkan 1.3)
- Support Windows with native Vulkan 1.3
- Maintain single codebase for all platforms
- Demonstrate architecture's platform abstraction capability

### Platform Support Matrix
- **Linux (WSL/llvmpipe)**: Vulkan 1.1 with traditional render passes
- **macOS (MoltenVK)**: Vulkan 1.3 with dynamic rendering
- **Windows**: Vulkan 1.3 with dynamic rendering

---

## Changes

### 1. VulkanDevice - Platform-Specific Extensions

**Modified**: `src/core/VulkanDevice.hpp/.cpp`

**Platform-Specific Extension Management**:
```cpp
void VulkanDevice::createLogicalDevice() {
    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    #ifndef __linux__
    // macOS/Windows: Add Vulkan 1.3 extensions
    deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);

    // macOS-specific: MoltenVK portability subset
    #ifdef __APPLE__
    deviceExtensions.push_back("VK_KHR_portability_subset");
    #endif
    #endif

    // Create logical device with platform-specific extensions
    // ...
}
```

**Platform-Specific Features**:
```cpp
vk::PhysicalDeviceFeatures deviceFeatures{};
#ifndef __linux__
deviceFeatures.shaderDrawParameters = true;
#endif
deviceFeatures.samplerAnisotropy = true;
```

---

### 2. VulkanSwapchain - Render Pass Support

**Modified**: `src/rendering/VulkanSwapchain.hpp/.cpp`

**Added Render Pass for Linux**:
```cpp
class VulkanSwapchain {
public:
    // Existing methods...

    #ifdef __linux__
    vk::RenderPass getRenderPass() const;
    #endif

private:
    #ifdef __linux__
    vk::raii::RenderPass renderPass = nullptr;
    void createRenderPass();
    #endif
};
```

**Render Pass Creation** (Linux only):
```cpp
#ifdef __linux__
void VulkanSwapchain::createRenderPass() {
    vk::AttachmentDescription colorAttachment{
        .format = imageFormat,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::ePresentSrcKHR
    };

    vk::AttachmentDescription depthAttachment{
        .format = depthFormat,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

    vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
        .pDepthStencilAttachment = &depthAttachmentRef
    };

    renderPass = vk::raii::RenderPass(device.getDevice(), renderPassInfo);
}
#endif
```

---

### 3. VulkanPipeline - Dual Path Creation

**Modified**: `src/rendering/VulkanPipeline.hpp/.cpp`

**Constructor with Optional Render Pass**:
```cpp
VulkanPipeline::VulkanPipeline(VulkanDevice& device,
                               VulkanSwapchain& swapchain,
                               const std::string& shaderPath,
                               vk::Format depthFormat,
                               vk::RenderPass renderPass)  // Optional on macOS/Windows
{
    createDescriptorSetLayout();
    createPipelineLayout();

    #ifdef __linux__
    createGraphicsPipeline(shaderPath, swapchain.getFormat(), depthFormat, renderPass);
    #else
    createGraphicsPipeline(shaderPath, swapchain.getFormat(), depthFormat, nullptr);
    #endif
}
```

**Platform-Specific Pipeline Creation**:
```cpp
void VulkanPipeline::createGraphicsPipeline(..., vk::RenderPass renderPass) {
    // Common configuration...

    #ifdef __linux__
    // Traditional render pass
    vk::GraphicsPipelineCreateInfo pipelineInfo{
        .renderPass = renderPass,
        .subpass = 0
    };
    #else
    // Dynamic rendering
    vk::PipelineRenderingCreateInfo renderingInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat
    };
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineChain = {
        pipelineInfo, renderingInfo
    };
    #endif
}
```

---

### 4. Renderer - Platform-Specific Rendering

**Modified**: `src/rendering/Renderer.hpp/.cpp`

**Initialization**:
```cpp
Renderer::Renderer(...) {
    // ... device, swapchain creation ...

    pipeline = std::make_unique<VulkanPipeline>(
        *device, *swapchain, "shaders/slang.spv", findDepthFormat(),
        #ifdef __linux__
        swapchain->getRenderPass()
        #else
        nullptr
        #endif
    );
}
```

**Command Recording**:
```cpp
void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    commandBuffer.begin({});

    #ifdef __linux__
    // Traditional render pass
    vk::RenderPassBeginInfo renderPassInfo{
        .renderPass = swapchain->getRenderPass(),
        .framebuffer = framebuffers[imageIndex],
        .renderArea = { {0, 0}, swapchain->getExtent() },
        .clearValueCount = 2,
        .pClearValues = clearValues
    };
    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Rendering commands...

    commandBuffer.endRenderPass();
    #else
    // Dynamic rendering
    vk::RenderingAttachmentInfo colorAttachment{
        .imageView = swapchain->getImageViews()[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = { {0, 0}, swapchain->getExtent() },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment
    };
    commandBuffer.beginRendering(renderingInfo);

    // Rendering commands...

    commandBuffer.endRendering();
    #endif

    commandBuffer.end();
}
```

---

### 5. CMakeLists - Platform-Specific Shader Compilation

**Modified**: `CMakeLists.txt`

**Shader Compilation Paths**:
```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux: SPIR-V 1.3 (Vulkan 1.1)
    set(SPIRV_VERSION "spirv_1_3")
else()
    # macOS/Windows: SPIR-V 1.4 (Vulkan 1.3)
    set(SPIRV_VERSION "spirv_1_4")
endif()

add_custom_command(
    OUTPUT ${SHADERS_DIR}/slang.spv
    COMMAND ${SLANG_COMPILER} -profile ${SPIRV_VERSION} -target spirv
            -entry vertexMain -stage vertex
            -entry fragmentMain -stage fragment
            ${PROJECT_SOURCE_DIR}/shaders/slang.slang
            -o ${SHADERS_DIR}/slang.spv
    DEPENDS ${PROJECT_SOURCE_DIR}/shaders/slang.slang
)
```

---

## Reflection

### What Worked Well
- Single codebase supports all platforms
- Compile-time selection ensures zero runtime overhead
- Architecture cleanly abstracted platform differences
- Preprocessor directives isolated to affected components

### Challenges
- Understanding MoltenVK limitations and requirements
- Ensuring correct extension and feature combinations
- Managing dual pipeline creation paths
- Testing across all platforms

### Impact on Later Phases
- Phase 9: Cross-platform support inherited by managers
- Phase 11: ImGui integration required platform-specific rendering paths
- Foundation for future platform-specific optimizations

---

## Testing

### Linux (WSL)
```bash
cmake --build build
./build/vulkanGLFW
```
Render pass path validated successfully.

### macOS
```bash
cmake --build build
./build/vulkanGLFW
```
Dynamic rendering path validated successfully.

---

## Summary

Phase 8 successfully added cross-platform rendering support:

**Modified Components**:
- VulkanDevice: Platform-specific extensions and features
- VulkanSwapchain: Render pass support for Linux
- VulkanPipeline: Dual pipeline creation paths
- Renderer: Platform-specific command recording
- CMakeLists: Platform-specific shader compilation

**Key Achievements**:
- Single codebase for multiple platforms
- Clean platform abstraction
- Zero runtime overhead
- Maintained existing architecture

**Platform Coverage**:
- Linux (Vulkan 1.1): Traditional render passes
- macOS (Vulkan 1.3): Dynamic rendering
- Windows (Vulkan 1.3): Dynamic rendering

---

*Phase 8 Complete*
*Previous: Phase 7 - Application Layer*
*Next: Phase 9 - Subsystem Separation (Enhancement)*
