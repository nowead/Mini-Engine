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

## Phase 8: Legacy Code Cleanup

### Issue 8: Segmentation Fault After Legacy Wrapper Deletion

**Date**: 2025-12-21
**Severity**: Critical (Application crash)
**Status**: ✅ Resolved

**Description**:
After deleting legacy Vulkan wrapper classes (VulkanBuffer, VulkanImage, VulkanPipeline, VulkanSwapchain, SyncManager), the application crashed with segmentation fault during initialization.

**Error Messages**:
```
[Vulkan] Validation Error: [ VUID-VkFramebufferCreateInfo-attachmentCount-00876 ]
pCreateInfo->attachmentCount 1 does not match attachmentCount of 2

[Vulkan] Validation Error: [ VUID-VkClearDepthStencilValue-depth-00022 ]
pRenderPassBegin->pClearValues[1].depthStencil.depth is invalid

[Vulkan] Validation Error: [ VUID-VkRenderPassBeginInfo-clearValueCount-00902 ]
clearValueCount is 1 but there must be at least 2 entries

Segmentation fault (core dumped)
```

**Root Cause**:
1. Incorrect initialization order in `Renderer` constructor
2. `createRHIDepthResources()` was called before swapchain creation
3. When `createRHIDepthResources()` executed, `rhiBridge->getSwapchain()` returned null
4. Depth image was not created (early return)
5. Later, `createRHIPipeline()` created framebuffers expecting depth attachment
6. Framebuffer attachment count mismatch caused validation errors and crash

**Investigation Steps**:
1. Traced validation errors back to framebuffer creation
2. Discovered depth image was null despite creation call
3. Found `createRHIDepthResources()` returned early due to null swapchain
4. Identified initialization order as root cause

**Resolution**:
Changed initialization order to create swapchain before depth resources:

**Before (Broken)**:
```cpp
Renderer::Renderer(...) {
    device = std::make_unique<VulkanDevice>(...);
    rhiBridge = std::make_unique<rendering::RendererBridge>(...);

    // ❌ Swapchain not created yet!
    createRHIDepthResources();  // Returns early - swapchain is null
    createRHIUniformBuffers();
    createRHIBindGroups();
    createRHIPipeline();        // Creates framebuffers without depth attachment
}
```

**After (Fixed)**:
```cpp
Renderer::Renderer(...) {
    device = std::make_unique<VulkanDevice>(...);
    rhiBridge = std::make_unique<rendering::RendererBridge>(...);

    // ✅ Create swapchain first (needed for depth resources)
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);

    // Now depth resources can get correct dimensions from swapchain
    createRHIDepthResources();
    createRHIUniformBuffers();
    createRHIBindGroups();
    createRHIPipeline();
}
```

**Files Modified**:
- `src/rendering/Renderer.cpp`: Fixed constructor initialization order

**Key Takeaway**:
Resource creation must follow dependency order:
1. Device → Bridge → Swapchain
2. Swapchain → Depth Resources
3. All resources → Pipeline (which references them)

---

### Issue 9: Semaphore Signaling Validation Warnings

**Date**: 2025-12-21
**Severity**: Low (Non-blocking validation warnings)
**Status**: ✅ Resolved

**Description**:
After Phase 8 cleanup, validation layer reported continuous semaphore signaling warnings during rendering.

**Error Message**:
```
[Vulkan] Validation Error: [ VUID-vkQueueSubmit-pCommandBuffers-00065 ]
vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] (VkSemaphore 0xee647e0000000009[])
is being signaled by VkQueue 0x5b940156fba0[], but it was previously signaled by
VkQueue 0x5b940156fba0[] and has not since been waited on.
```

**Root Cause**:
1. Fence synchronization in `RendererBridge::beginFrame()` was not strict enough
2. Semaphore was being signaled again before previous signal was consumed
3. Timeline of events:
   - Frame N: Signal semaphore
   - Frame N+1: Fence wait (too late - semaphore already signaled)
   - Frame N+1: Signal semaphore again ← Validation error

**Investigation**:
```cpp
// RendererBridge.cpp - Original (problematic)
bool RendererBridge::beginFrame() {
    // Acquire image first
    auto result = m_swapchain->acquireNextImage(...);

    // Wait for fence AFTER acquiring image
    m_inFlightFences[m_currentFrame]->wait();  // ← Too late!
    m_inFlightFences[m_currentFrame]->reset();

    return true;
}
```

**Resolution**:
Wait for fence **before** acquiring next image to ensure previous frame completed:

```cpp
// RendererBridge.cpp - Fixed
bool RendererBridge::beginFrame() {
    // ✅ Wait for fence FIRST to ensure previous frame completed
    m_inFlightFences[m_currentFrame]->wait(UINT64_MAX);
    m_inFlightFences[m_currentFrame]->reset();

    // Now safe to acquire next image and reuse semaphores
    auto result = m_swapchain->acquireNextImage(...);

    return true;
}
```

**Files Modified**:
- `src/rendering/RendererBridge.cpp`: Moved fence wait before image acquisition

**Verification**:
After fix, no more semaphore warnings:
```bash
❯ make run
[RendererBridge] Initialized with Vulkan backend
[Renderer] RHI Pipeline created successfully
[Renderer] RHI buffers uploaded: 23200 vertices, 92168 indices
# ✅ No semaphore warnings!
```

---

### Issue 10: SPIR-V Version Mismatch on Linux

**Date**: 2025-12-26
**Severity**: High (Blocks Linux runtime)
**Status**: ✅ Resolved

**Description**:
After completing RHI migration, validation layers reported SPIR-V version incompatibility errors on Linux. Shaders compiled with SPIR-V 1.5 were being loaded on Vulkan 1.1 environment (lavapipe/llvmpipe).

**Error Message**:
```
[Vulkan] Validation Error: [ VUID-VkShaderModuleCreateInfo-pCode-08737 ]
vkCreateShaderModule(): pCreateInfo->pCode (spirv-val produced an error):
Invalid SPIR-V binary version 1.5 for target environment SPIR-V 1.3 (under Vulkan 1.1 semantics).
```

**Root Cause**:
1. CMakeLists.txt was configured to compile shaders with `-profile spirv_1_3` for Linux
2. However, pre-compiled `fdf.spv` shader was compiled with SPIR-V 1.5
3. FDF mode (enabled by default) loaded the old `fdf.spv` instead of `slang.spv`
4. Vulkan 1.1 only supports SPIR-V 1.0, 1.1, 1.2, and 1.3

**Investigation**:
```bash
# Check SPIR-V version in compiled shader
$ od -A x -t x1z -N 24 shaders/fdf.spv
000000 03 02 23 07 00 05 01 00  # 00 05 01 00 = version 1.5
```

**Resolution**:
Recompiled `fdf.spv` with SPIR-V 1.3 target:

```bash
cd shaders
$HOME/1.3.296.0/x86_64/bin/slangc fdf.slang \
    -target spirv \
    -profile spirv_1_3 \
    -emit-spirv-directly \
    -fvk-use-entrypoint-name \
    -entry vertMain \
    -entry fragMain \
    -Wno-41012 \
    -o fdf.spv
```

**Files Modified**:
- `shaders/fdf.spv`: Recompiled with SPIR-V 1.3
- Note: `CMakeLists.txt` already had correct configuration (line 181-184)

**Verification**:
```bash
# Check new SPIR-V version
$ od -A x -t x1z -N 24 shaders/fdf.spv
000000 03 02 23 07 00 03 01 00  # 00 03 01 00 = version 1.3 ✅

# Run application - no more SPIR-V version errors
$ make run-only
[RendererBridge] Initialized with Vulkan backend
[Renderer] RHI Pipeline created successfully
# ✅ No SPIR-V version errors!
```

**Key Takeaway**:
- Always verify SPIR-V versions in pre-compiled shaders match target Vulkan version
- SPIR-V 1.3 is the maximum version for Vulkan 1.1
- Use `spirv_1_3` profile for Linux builds to ensure compatibility

---

### Issue 11: Semaphore Double Signaling Without Wait

**Date**: 2025-12-26
**Severity**: Medium (Validation errors during rendering)
**Status**: ✅ Resolved

**Description**:
Validation layers reported semaphore synchronization errors where semaphores were being signaled multiple times without being waited on. Additionally, present operations were waiting on semaphores that had no way to be signaled.

**Error Messages**:
```
[Vulkan] Validation Error: [ VUID-vkQueueSubmit-pCommandBuffers-00065 ]
vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] (VkSemaphore 0xee647e0000000009[])
is being signaled by VkQueue 0x57c3fec7eba0[], but it was previously signaled by
VkQueue 0x57c3fec7eba0[] and has not since been waited on.

[Vulkan] Validation Error: [ VUID-vkQueuePresentKHR-pWaitSemaphores-03268 ]
vkQueuePresentKHR(): pPresentInfo->pWaitSemaphores[0] queue (VkQueue 0x57c3fec7eba0[])
is waiting on semaphore (VkSemaphore 0xe88693000000000c[]) that has no way to be signaled.
```

**Root Cause**:
1. `RHISwapchain::present()` interface had no parameter to wait on render completion semaphore
2. `getRenderFinishedSemaphore()` was incorrectly using `m_currentImageIndex` instead of `m_currentFrame`
3. Frame synchronization requires per-frame semaphores, not per-swapchain-image semaphores
4. Timeline of problematic events:
   - Submit command buffer → signals `renderFinishedSemaphore[currentFrame]`
   - Present operation → waits on nothing (missing synchronization)
   - Next frame → signals same semaphore again without wait

**Investigation**:
```cpp
// RendererBridge.hpp - Original (problematic)
rhi::RHISemaphore* getRenderFinishedSemaphore() const {
    return m_renderFinishedSemaphores[m_currentImageIndex].get();  // ❌ Wrong index!
}

// VulkanRHISwapchain.cpp - Original (missing sync)
void VulkanRHISwapchain::present() {
    vk::PresentInfoKHR presentInfo;
    // ... no wait semaphores ❌
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &(*m_swapchain);
}
```

**Resolution**:

1. **Updated RHISwapchain interface** to accept wait semaphore:
```cpp
// src/rhi/include/rhi/RHISwapchain.hpp
virtual void present(RHISemaphore* waitSemaphore = nullptr) = 0;
```

2. **Implemented semaphore wait in present**:
```cpp
// src/rhi-vulkan/src/VulkanRHISwapchain.cpp
void VulkanRHISwapchain::present(rhi::RHISemaphore* waitSemaphore) {
    vk::Semaphore vkWaitSemaphore = VK_NULL_HANDLE;
    if (waitSemaphore) {
        auto* vulkanSemaphore = static_cast<VulkanRHISemaphore*>(waitSemaphore);
        vkWaitSemaphore = vulkanSemaphore->getVkSemaphore();
    }

    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
    presentInfo.pWaitSemaphores = waitSemaphore ? &vkWaitSemaphore : nullptr;
    // ...
}
```

3. **Fixed semaphore indexing**:
```cpp
// src/rendering/RendererBridge.hpp
rhi::RHISemaphore* getRenderFinishedSemaphore() const {
    return m_renderFinishedSemaphores[m_currentFrame].get();  // ✅ Use frame index
}
```

4. **Updated present call** to pass render finished semaphore:
```cpp
// src/rendering/RendererBridge.cpp
void RendererBridge::endFrame() {
    // Present with render finished semaphore to ensure rendering is complete
    m_swapchain->present(m_renderFinishedSemaphores[m_currentFrame].get());

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
```

**Files Modified**:
- `src/rhi/include/rhi/RHISwapchain.hpp`: Added waitSemaphore parameter to present()
- `src/rhi-vulkan/include/rhi-vulkan/VulkanRHISwapchain.hpp`: Updated override signature
- `src/rhi-vulkan/src/VulkanRHISwapchain.cpp`: Implemented wait semaphore in present
- `src/rendering/RendererBridge.hpp`: Fixed getRenderFinishedSemaphore() indexing
- `src/rendering/RendererBridge.cpp`: Pass semaphore to present()

**Synchronization Flow (Fixed)**:
```
Frame N:
  1. beginFrame() → acquireNextImage() signals imageAvailable[N]
  2. submit() waits on imageAvailable[N], signals renderFinished[N]
  3. present() waits on renderFinished[N] ✅
  4. Advance to frame N+1

Frame N+1:
  1. Wait for fence[N+1] to ensure frame N+1-2 completed
  2. Can now safely reuse semaphores[N+1]
```

**Verification**:
```bash
❯ make run-only
[RendererBridge] Initialized with Vulkan backend
[Renderer] RHI Pipeline created successfully
[Renderer] RHI buffers uploaded: 23200 vertices (742400 bytes), 92168 indices (368672 bytes)
# ✅ No semaphore synchronization errors!
```

**Key Takeaway**:
- Use `m_currentFrame` for frame-in-flight resources (semaphores, fences, command buffers)
- Use `m_currentImageIndex` only for swapchain-specific resources (framebuffers, image views)
- Present must wait on render completion semaphore to ensure GPU work finishes before displaying
- Proper synchronization chain: imageAvailable → render → renderFinished → present

---

## Future Issues

*Issues discovered during later phases will be documented here*

---

**Document Maintenance**:
- Add new issues as they are discovered
- Update status when issues are resolved
- Include code snippets and error messages for reference
- Document workarounds and alternative solutions considered
