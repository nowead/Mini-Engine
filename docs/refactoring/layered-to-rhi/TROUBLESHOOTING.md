# RHI Migration - Troubleshooting & Issues

**Document Version**: 1.1
**Created**: 2025-12-19
**Last Updated**: 2025-12-22

---

## Overview

This document tracks issues, blockers, unexpected challenges, and their resolutions encountered during the RHI migration process.

---

## Phase 2: Vulkan Backend Implementation

### Issue 1: VMA Dependency Integration

**Date**: 2025-12-19
**Severity**: Low
**Status**: ✅ Resolved

**Description**:
VMA (Vulkan Memory Allocator) was not previously integrated into the project. Phase 2 requires VMA for efficient GPU memory management in the Vulkan backend.

**Impact**:
- Blocks implementation of VulkanRHIBuffer and VulkanRHITexture
- Required before any resource allocation code can be written

**Resolution**:
1. Added `vulkan-memory-allocator` to `vcpkg.json` dependencies
2. Added `find_package(VulkanMemoryAllocator CONFIG REQUIRED)` to CMakeLists.txt
3. Linked VMA: `GPUOpen::VulkanMemoryAllocator` to target

**Files Modified**:
- `vcpkg.json`: Added `vulkan-memory-allocator` dependency
- `CMakeLists.txt`: Added find_package and target_link_libraries

**Next Steps**:
- Run CMake configure to download and integrate VMA
- Verify VMA headers are accessible

---

## Phase 1: RHI Interface Design

No issues encountered. Phase 1 completed successfully with 100% of objectives met.

---

### Issue 2: Slang Shader Compilation Failure

**Date**: 2025-12-19
**Severity**: Medium (Blocks full build)
**Status**: ⚠️ Workaround Applied

**Description**:
During `make build-only`, the slangc shader compilation step fails with "Subprocess killed" error. However, manual execution of the same slangc command succeeds without errors.

**Error Message**:
```
FAILED: [code=1] /Users/mindaewon/projects/Mini-Engine/shaders/slang.spv
cd /Users/mindaewon/projects/Mini-Engine/shaders && /opt/homebrew/bin/cmake -E env DYLD_LIBRARY_PATH=/opt/homebrew/opt/vulkan-loader/lib: /usr/local/bin/slangc /Users/mindaewon/projects/Mini-Engine/shaders/shader.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -Wno-41012 -o slang.spv
Subprocess killed
```

**Impact**:
- Full project build fails
- Cannot compile and test Vulkan RHI implementations integrated with the main project
- Phase 2 integration testing is blocked

**Investigation**:
1. slangc exists at `/usr/local/bin/slangc`
2. Manual execution of the same command succeeds without errors
3. The issue appears to be related to CMake's custom command execution or environment setup
4. Possible cause: DYLD_LIBRARY_PATH conflicts or subprocess timeout

**Workaround**:
- Vulkan RHI classes can still be implemented and header-checked independently
- Manual shader precompilation may bypass the build issue
- Consider disabling shader compilation target temporarily for RHI development

**Next Steps**:
- Investigate CMake custom command timeout settings
- Check DYLD_LIBRARY_PATH conflicts
- Consider switching to precompiled shaders during migration
- May need to update CMakeLists.txt shader compilation logic

**Priority**: Should be resolved before Phase 7 (Testing & Verification)

---

## Phase 8: Directory Refactoring

### Issue 3: Linux Dynamic Rendering Not Supported

**Date**: 2025-12-22
**Severity**: High (Blocks Linux runtime)
**Status**: ✅ Resolved

**Description**:
After completing the directory refactoring, the application crashed on Linux (WSL2 with lavapipe) with dynamic rendering validation errors. The RHI was using `vkCmdBeginRendering`/`vkCmdEndRendering` which requires Vulkan 1.3 or the `VK_KHR_dynamic_rendering` extension.

**Error Message**:
```
Validation Error: [ VUID-vkCmdBeginRendering-dynamicRendering-06446 ]
vkCmdBeginRendering requires VK_KHR_dynamic_rendering or Vulkan 1.3

Assertion `("dynamicRendering is not enabled on the device", false)` failed.
```

**Root Cause**:
- Linux with lavapipe/llvmpipe software renderer only supports Vulkan 1.1
- Dynamic rendering is a Vulkan 1.3 feature
- macOS (MoltenVK) and Windows with modern GPUs support Vulkan 1.3
- The RHI was designed assuming Vulkan 1.3 availability

**Resolution**:
Implemented platform-specific render pass handling using `#ifdef __linux__`:

1. **VulkanRHICommandEncoder**: Use traditional `beginRenderPass()`/`endRenderPass()` on Linux
2. **RHIRenderPass.hpp**: Added `nativeRenderPass` and `nativeFramebuffer` fields to `RenderPassDesc`
3. **VulkanRHISwapchain**: Implemented `createFramebuffers()` and `getFramebuffer()` for Linux

**Files Modified**:
- `src/rhi/include/rhi/RHIRenderPass.hpp`
- `src/rhi-vulkan/include/rhi-vulkan/VulkanRHICommandEncoder.hpp`
- `src/rhi-vulkan/src/VulkanRHICommandEncoder.cpp`
- `src/rhi-vulkan/include/rhi-vulkan/VulkanRHISwapchain.hpp`
- `src/rhi-vulkan/src/VulkanRHISwapchain.cpp`
- `src/rendering/Renderer.cpp`

**Key Code Pattern**:
```cpp
// VulkanRHICommandEncoder.cpp
void VulkanRHICommandEncoder::beginRenderPass(const RenderPassDesc& desc) {
#ifdef __linux__
    // Linux (Vulkan 1.1): Traditional render pass
    if (desc.nativeRenderPass && desc.nativeFramebuffer) {
        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = /* cast from nativeRenderPass */;
        renderPassInfo.framebuffer = /* cast from nativeFramebuffer */;
        m_commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        m_usesTraditionalRenderPass = true;
    }
#else
    // macOS/Windows (Vulkan 1.3): Dynamic rendering
    m_commandBuffer.beginRendering(renderingInfo);
#endif
}
```

---

### Issue 4: Pipeline Creation Requires RenderPass on Linux

**Date**: 2025-12-22
**Severity**: High (Blocks Linux build)
**Status**: ✅ Resolved

**Description**:
After fixing dynamic rendering, pipeline creation failed on Linux because `VkGraphicsPipelineCreateInfo::renderPass` was `VK_NULL_HANDLE`.

**Error Message**:
```
Validation Error: [ VUID-VkGraphicsPipelineCreateInfo-dynamicRendering-06576 ]
If the dynamicRendering feature is not enabled, renderPass must not be VK_NULL_HANDLE
```

**Root Cause**:
- Vulkan 1.3 dynamic rendering allows pipeline creation without a render pass
- Vulkan 1.1 requires a valid render pass at pipeline creation time
- The swapchain (which owns the render pass) wasn't created before the pipeline

**Resolution**:
1. Added `nativeRenderPass` field to `RenderPipelineDesc`
2. Modified `VulkanRHIPipeline::createGraphicsPipeline()` to use render pass on Linux
3. Ensured swapchain is created before pipeline in `Renderer::createRHIPipeline()`

**Files Modified**:
- `src/rhi/include/rhi/RHIPipeline.hpp`
- `src/rhi-vulkan/src/VulkanRHIPipeline.cpp`
- `src/rendering/Renderer.cpp`

**Key Code Pattern**:
```cpp
// Renderer.cpp
void Renderer::createRHIPipeline() {
    // Swapchain must exist before pipeline on Linux (for render pass)
    if (!m_rhiSwapchain) {
        createSwapchain();
    }

    rhi::RenderPipelineDesc desc{};
#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<VulkanRHISwapchain*>(m_rhiSwapchain.get());
    if (vulkanSwapchain) {
        desc.nativeRenderPass = reinterpret_cast<void*>(
            static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass()));
    }
#endif
    m_rhiPipeline = m_rhiDevice->createRenderPipeline(desc);
}
```

---

### Issue 5: Image Layout Transition Conflicts

**Date**: 2025-12-22
**Severity**: Medium (Runtime validation warnings)
**Status**: ✅ Resolved

**Description**:
After implementing traditional render pass on Linux, validation errors appeared for image layout transitions.

**Error Message**:
```
Validation Error: [ VUID-VkImageMemoryBarrier-oldLayout-01197 ]
oldLayout must be VK_IMAGE_LAYOUT_UNDEFINED or the current layout of the image
```

**Root Cause**:
- Traditional render pass handles image layout transitions automatically via `initialLayout`/`finalLayout`
- The code was also calling `pipelineBarrier()` for manual layout transitions
- This caused conflicts as the image was already transitioned by the render pass

**Resolution**:
Skip manual pipeline barriers on Linux when using traditional render pass:

```cpp
// VulkanRHICommandEncoder.cpp
void VulkanRHICommandEncoder::pipelineBarrier(...) {
#ifdef __linux__
    // Traditional render pass handles layout transitions automatically
    return;
#endif
    m_commandBuffer.pipelineBarrier(...);
}
```

**Note**: This is a temporary workaround. A more robust solution would track render pass state and skip only conflicting barriers.

---

### Issue 6: Type Casting Between vulkan-hpp and C API

**Date**: 2025-12-22
**Severity**: Low (Compile error)
**Status**: ✅ Resolved

**Description**:
Casting `vk::RenderPass` to `void*` for platform-agnostic interfaces caused compilation errors.

**Error Message**:
```
error: invalid static_cast from type 'vk::RenderPass' to type 'void*'
```

**Root Cause**:
- vulkan-hpp types (`vk::RenderPass`) are wrapper classes around C handles
- Direct casting to `void*` doesn't work; need to first cast to C handle type

**Resolution**:
Use double cast pattern:
```cpp
// To void*
reinterpret_cast<void*>(static_cast<VkRenderPass>(vkRenderPass))

// From void*
static_cast<vk::RenderPass>(reinterpret_cast<VkRenderPass>(nativePtr))
```

---

### Issue 7: Namespace Conflicts After Refactoring

**Date**: 2025-12-22
**Severity**: Low (Compile error)
**Status**: ✅ Resolved

**Description**:
After directory refactoring, namespace references became inconsistent.

**Error Message**:
```
error: 'VulkanRHISwapchain' is not a member of 'rhi'
```

**Root Cause**:
- Old code used `rhi::VulkanRHISwapchain`
- New structure uses `RHI::Vulkan::VulkanRHISwapchain`
- Some files had stale namespace references

**Resolution**:
Updated all references to use correct namespace:
```cpp
// Before
rhi::VulkanRHISwapchain* swapchain = ...;

// After
RHI::Vulkan::VulkanRHISwapchain* swapchain = ...;
```

---

## Future Issues

*Issues discovered during later phases will be documented here*

---

**Document Maintenance**:
- Add new issues as they are discovered
- Update status when issues are resolved
- Include code snippets and error messages for reference
- Document workarounds and alternative solutions considered
