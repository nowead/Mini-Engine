# Phase 8: RHI Directory Refactoring - Modular Architecture

**Date**: 2025-12-21
**Status**: ✅ COMPLETED
**Phase**: 8 of 11 (RHI Migration)
**Actual Duration**: 2 days
**Priority**: High

---

## Executive Summary

Phase 8은 현재 단일 폴더에 혼재된 RHI 코드를 **언리얼 엔진 스타일의 모듈 분리 구조**로 리팩토링합니다. 이를 통해 코드의 확장성과 유지보수성을 크게 향상시킵니다.

### Key Objectives
- 🎯 **Public/Private 분리**: 외부 노출 헤더와 내부 구현 명확히 분리
- 🎯 **모듈화**: RHI 추상 계층과 백엔드 구현을 독립 모듈로 분리
- 🎯 **확장성**: WebGPU, Metal, D3D12 백엔드 추가 용이한 구조
- 🎯 **업계 표준**: 언리얼 엔진과 동일한 아키텍처 패턴 적용

### Architecture Benefits
- ✅ 상용 엔진(Unreal, Unity)과 동일한 아키텍처 패턴
- ✅ CMake 멀티 모듈 구성으로 독립적 빌드/테스트 가능
- ✅ 의존성 역전 원칙(DIP), 개방-폐쇄 원칙(OCP) 적용
- ✅ 새로운 백엔드 추가 시 기존 코드 수정 불필요

---

## Current State Analysis

### Current Directory Structure (문제점)

```
src/rhi/
├── RHI.hpp                    ❌ Public/Private 구분 없음
├── RHIBindGroup.hpp
├── RHIBuffer.hpp
├── RHICapabilities.hpp
├── RHICommandBuffer.hpp
├── RHIDevice.hpp
├── RHIFactory.cpp             ❌ .cpp 파일이 헤더와 혼재
├── RHIFactory.hpp
├── RHIPipeline.hpp
├── RHIQueue.hpp
├── RHIRenderPass.hpp
├── RHISampler.hpp
├── RHIShader.hpp
├── RHISwapchain.hpp
├── RHISync.hpp
├── RHITexture.hpp
├── RHITypes.hpp
└── vulkan/                    ⚠️ 백엔드만 분리됨
    ├── VulkanCommon.cpp
    ├── VulkanCommon.hpp
    ├── VulkanMemoryAllocator.cpp
    ├── VulkanRHIBindGroup.cpp
    ├── VulkanRHIBindGroup.hpp
    ├── VulkanRHIBuffer.cpp
    ├── VulkanRHIBuffer.hpp
    ├── VulkanRHICapabilities.cpp
    ├── VulkanRHICapabilities.hpp
    ├── VulkanRHICommandEncoder.cpp
    ├── VulkanRHICommandEncoder.hpp
    ├── VulkanRHIDevice.cpp
    ├── VulkanRHIDevice.hpp
    ├── VulkanRHIPipeline.cpp
    ├── VulkanRHIPipeline.hpp
    ├── VulkanRHIQueue.cpp
    ├── VulkanRHIQueue.hpp
    ├── VulkanRHISampler.cpp
    ├── VulkanRHISampler.hpp
    ├── VulkanRHIShader.cpp
    ├── VulkanRHIShader.hpp
    ├── VulkanRHISwapchain.cpp
    ├── VulkanRHISwapchain.hpp
    ├── VulkanRHISync.cpp
    ├── VulkanRHISync.hpp
    ├── VulkanRHITexture.cpp
    └── VulkanRHITexture.hpp
```

### Identified Problems

| 문제 | 설명 | 영향 |
|------|------|------|
| **Public/Private 혼재** | 모든 헤더가 동일 레벨에 위치 | 캡슐화 위반, API 경계 불명확 |
| **hpp/cpp 혼재** | 구현 파일이 인터페이스와 같은 폴더 | 빌드 구조 불명확 |
| **단일 모듈** | 추상 계층과 구현이 같은 빌드 타겟 | 의존성 관리 어려움 |
| **확장성 부족** | 새 백엔드 추가 시 구조 복잡화 | WebGPU/Metal 추가 어려움 |

---

## Target Architecture

### Target Directory Structure (Option A: Unreal Style)

```
src/
├── rhi/                               # 📦 RHI Abstract Interface Module
│   ├── include/rhi/                   # Public Headers (외부에서 #include <rhi/...>)
│   │   ├── RHI.hpp                    # Convenience header
│   │   ├── Types.hpp                  # Enums, flags, structures
│   │   ├── Forward.hpp                # Forward declarations
│   │   ├── Device.hpp                 # RHIDevice interface
│   │   ├── Buffer.hpp                 # RHIBuffer interface
│   │   ├── Texture.hpp                # RHITexture interface
│   │   ├── Sampler.hpp                # RHISampler interface
│   │   ├── Shader.hpp                 # RHIShader interface
│   │   ├── BindGroup.hpp              # RHIBindGroup interface
│   │   ├── Pipeline.hpp               # RHIPipeline interface
│   │   ├── RenderPass.hpp             # RHIRenderPass interface
│   │   ├── CommandBuffer.hpp          # RHICommandEncoder interface
│   │   ├── Swapchain.hpp              # RHISwapchain interface
│   │   ├── Queue.hpp                  # RHIQueue interface
│   │   ├── Sync.hpp                   # RHIFence, RHISemaphore interface
│   │   ├── Capabilities.hpp           # RHICapabilities interface
│   │   └── Factory.hpp                # RHIFactory
│   ├── src/                           # Private Implementation
│   │   └── Factory.cpp
│   └── CMakeLists.txt                 # rhi module build
│
├── rhi-vulkan/                        # 📦 Vulkan Backend Module
│   ├── include/rhi-vulkan/            # Public Vulkan-specific headers (optional)
│   │   └── VulkanExtensions.hpp       # Vulkan extension access (if needed)
│   ├── src/                           # Private Implementation
│   │   ├── Common.hpp                 # Internal shared header
│   │   ├── Common.cpp                 # Internal shared implementation
│   │   ├── MemoryAllocator.cpp        # VMA integration
│   │   ├── Device.hpp                 # VulkanRHIDevice
│   │   ├── Device.cpp
│   │   ├── Buffer.hpp                 # VulkanRHIBuffer
│   │   ├── Buffer.cpp
│   │   ├── Texture.hpp                # VulkanRHITexture
│   │   ├── Texture.cpp
│   │   ├── Sampler.hpp                # VulkanRHISampler
│   │   ├── Sampler.cpp
│   │   ├── Shader.hpp                 # VulkanRHIShader
│   │   ├── Shader.cpp
│   │   ├── BindGroup.hpp              # VulkanRHIBindGroup
│   │   ├── BindGroup.cpp
│   │   ├── Pipeline.hpp               # VulkanRHIPipeline
│   │   ├── Pipeline.cpp
│   │   ├── CommandEncoder.hpp         # VulkanRHICommandEncoder
│   │   ├── CommandEncoder.cpp
│   │   ├── Swapchain.hpp              # VulkanRHISwapchain
│   │   ├── Swapchain.cpp
│   │   ├── Queue.hpp                  # VulkanRHIQueue
│   │   ├── Queue.cpp
│   │   ├── Sync.hpp                   # VulkanRHISync (Fence, Semaphore)
│   │   ├── Sync.cpp
│   │   ├── Capabilities.hpp           # VulkanRHICapabilities
│   │   └── Capabilities.cpp
│   └── CMakeLists.txt                 # rhi-vulkan module build
│
├── rhi-webgpu/                        # 📦 WebGPU Backend Module (Phase 9+)
│   ├── include/rhi-webgpu/
│   ├── src/
│   └── CMakeLists.txt
│
├── core/                              # Existing core module
├── rendering/                         # Rendering layer (depends on rhi)
├── scene/                             # Scene layer (depends on rhi)
├── resources/                         # Resource layer (depends on rhi)
└── ...
```

### Dependency Graph

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                         │
│                     (Application, ImGuiManager)                  │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                       High-Level Rendering                       │
│              (Renderer, ResourceManager, SceneManager)           │
└─────────────────────────────────┬───────────────────────────────┘
                                  │ depends on
                                  ▼
                    ┌─────────────────────────────┐
                    │           rhi               │  ← Abstract Interface Only
                    │   (Pure Virtual Classes)    │
                    └─────────────────┬───────────┘
                                      │ implemented by
           ┌──────────────────────────┼──────────────────────────┐
           ▼                          ▼                          ▼
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│   rhi-vulkan    │      │   rhi-webgpu    │      │   rhi-metal     │
│  (VK_KHR_...)   │      │  (Dawn/wgpu)    │      │  (MTL...)       │
└────────┬────────┘      └────────┬────────┘      └────────┬────────┘
         │                        │                        │
         ▼                        ▼                        ▼
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│   Vulkan SDK    │      │   WebGPU Impl   │      │   Metal SDK     │
└─────────────────┘      └─────────────────┘      └─────────────────┘
```

---

## File Migration Plan

### Task 8.1: Create New Directory Structure

```bash
# Create rhi module directories
mkdir -p src/rhi/include/rhi
mkdir -p src/rhi/src

# Create rhi-vulkan module directories
mkdir -p src/rhi-vulkan/include/rhi-vulkan
mkdir -p src/rhi-vulkan/src
```

### Task 8.2: Move RHI Abstract Interface Headers

| Source | Destination | Notes |
|--------|-------------|-------|
| `src/rhi/RHI.hpp` | `src/rhi/include/rhi/RHI.hpp` | Convenience header |
| `src/rhi/RHITypes.hpp` | `src/rhi/include/rhi/Types.hpp` | Rename, remove prefix |
| `src/rhi/RHIDevice.hpp` | `src/rhi/include/rhi/Device.hpp` | Rename |
| `src/rhi/RHIBuffer.hpp` | `src/rhi/include/rhi/Buffer.hpp` | Rename |
| `src/rhi/RHITexture.hpp` | `src/rhi/include/rhi/Texture.hpp` | Rename |
| `src/rhi/RHISampler.hpp` | `src/rhi/include/rhi/Sampler.hpp` | Rename |
| `src/rhi/RHIShader.hpp` | `src/rhi/include/rhi/Shader.hpp` | Rename |
| `src/rhi/RHIBindGroup.hpp` | `src/rhi/include/rhi/BindGroup.hpp` | Rename |
| `src/rhi/RHIPipeline.hpp` | `src/rhi/include/rhi/Pipeline.hpp` | Rename |
| `src/rhi/RHIRenderPass.hpp` | `src/rhi/include/rhi/RenderPass.hpp` | Rename |
| `src/rhi/RHICommandBuffer.hpp` | `src/rhi/include/rhi/CommandBuffer.hpp` | Rename |
| `src/rhi/RHISwapchain.hpp` | `src/rhi/include/rhi/Swapchain.hpp` | Rename |
| `src/rhi/RHIQueue.hpp` | `src/rhi/include/rhi/Queue.hpp` | Rename |
| `src/rhi/RHISync.hpp` | `src/rhi/include/rhi/Sync.hpp` | Rename |
| `src/rhi/RHICapabilities.hpp` | `src/rhi/include/rhi/Capabilities.hpp` | Rename |
| `src/rhi/RHIFactory.hpp` | `src/rhi/include/rhi/Factory.hpp` | Rename |
| `src/rhi/RHIFactory.cpp` | `src/rhi/src/Factory.cpp` | Move to src/ |

### Task 8.3: Move Vulkan Backend Files

| Source | Destination | Notes |
|--------|-------------|-------|
| `src/rhi/vulkan/VulkanCommon.hpp` | `src/rhi-vulkan/src/Common.hpp` | Internal header |
| `src/rhi/vulkan/VulkanCommon.cpp` | `src/rhi-vulkan/src/Common.cpp` | |
| `src/rhi/vulkan/VulkanMemoryAllocator.cpp` | `src/rhi-vulkan/src/MemoryAllocator.cpp` | |
| `src/rhi/vulkan/VulkanRHIDevice.hpp` | `src/rhi-vulkan/src/Device.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHIDevice.cpp` | `src/rhi-vulkan/src/Device.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHIBuffer.hpp` | `src/rhi-vulkan/src/Buffer.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHIBuffer.cpp` | `src/rhi-vulkan/src/Buffer.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHITexture.hpp` | `src/rhi-vulkan/src/Texture.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHITexture.cpp` | `src/rhi-vulkan/src/Texture.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHISampler.hpp` | `src/rhi-vulkan/src/Sampler.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHISampler.cpp` | `src/rhi-vulkan/src/Sampler.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHIShader.hpp` | `src/rhi-vulkan/src/Shader.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHIShader.cpp` | `src/rhi-vulkan/src/Shader.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHIBindGroup.hpp` | `src/rhi-vulkan/src/BindGroup.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHIBindGroup.cpp` | `src/rhi-vulkan/src/BindGroup.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHIPipeline.hpp` | `src/rhi-vulkan/src/Pipeline.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHIPipeline.cpp` | `src/rhi-vulkan/src/Pipeline.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHICommandEncoder.hpp` | `src/rhi-vulkan/src/CommandEncoder.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHICommandEncoder.cpp` | `src/rhi-vulkan/src/CommandEncoder.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHISwapchain.hpp` | `src/rhi-vulkan/src/Swapchain.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHISwapchain.cpp` | `src/rhi-vulkan/src/Swapchain.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHIQueue.hpp` | `src/rhi-vulkan/src/Queue.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHIQueue.cpp` | `src/rhi-vulkan/src/Queue.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHISync.hpp` | `src/rhi-vulkan/src/Sync.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHISync.cpp` | `src/rhi-vulkan/src/Sync.cpp` | Rename |
| `src/rhi/vulkan/VulkanRHICapabilities.hpp` | `src/rhi-vulkan/src/Capabilities.hpp` | Rename |
| `src/rhi/vulkan/VulkanRHICapabilities.cpp` | `src/rhi-vulkan/src/Capabilities.cpp` | Rename |

### Task 8.4: Create New Forward Declaration Header

**File**: `src/rhi/include/rhi/Forward.hpp`

```cpp
#pragma once

namespace rhi {

// Enums
enum class RHIBackendType;
enum class TextureFormat;
enum class BufferUsage;
// ... etc

// Core interfaces
class RHIDevice;
class RHIBuffer;
class RHITexture;
class RHITextureView;
class RHISampler;
class RHIShader;
class RHIBindGroupLayout;
class RHIBindGroup;
class RHIPipelineLayout;
class RHIPipeline;
class RHICommandEncoder;
class RHIRenderPassEncoder;
class RHICommandBuffer;
class RHISwapchain;
class RHIQueue;
class RHIFence;
class RHISemaphore;
class RHICapabilities;

// Factory
class RHIFactory;

} // namespace rhi
```

---

## CMake Configuration

### Task 8.5: Create `src/rhi/CMakeLists.txt`

```cmake
# =============================================================================
# RHI Abstract Interface Module
# =============================================================================
# This module defines platform-independent graphics API interfaces.
# Upper layers depend only on this module, not on specific backends.
# =============================================================================

add_library(rhi STATIC
    src/Factory.cpp
)

target_include_directories(rhi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(rhi PUBLIC cxx_std_17)

# No platform-specific dependencies - this is pure abstraction
```

### Task 8.6: Create `src/rhi-vulkan/CMakeLists.txt`

```cmake
# =============================================================================
# RHI Vulkan Backend Module
# =============================================================================
# Implements the RHI interfaces using Vulkan API.
# This module is only linked at the application level for backend selection.
# =============================================================================

add_library(rhi-vulkan STATIC
    src/Common.cpp
    src/MemoryAllocator.cpp
    src/Device.cpp
    src/Buffer.cpp
    src/Texture.cpp
    src/Sampler.cpp
    src/Shader.cpp
    src/BindGroup.cpp
    src/Pipeline.cpp
    src/CommandEncoder.cpp
    src/Swapchain.cpp
    src/Queue.cpp
    src/Sync.cpp
    src/Capabilities.cpp
)

target_include_directories(rhi-vulkan
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rhi-vulkan
    PUBLIC
        rhi
    PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        glfw
)

target_compile_features(rhi-vulkan PUBLIC cxx_std_17)

# Vulkan-specific compile definitions
target_compile_definitions(rhi-vulkan PRIVATE
    VK_USE_PLATFORM_XLIB_KHR   # Linux
    # VK_USE_PLATFORM_WIN32_KHR  # Windows
    # VK_USE_PLATFORM_MACOS_MVK  # macOS
)
```

### Task 8.7: Update Root `CMakeLists.txt`

```cmake
# Add RHI modules
add_subdirectory(src/rhi)
add_subdirectory(src/rhi-vulkan)
# add_subdirectory(src/rhi-webgpu)  # Phase 9+

# Main executable links to specific backend
target_link_libraries(${PROJECT_NAME} PRIVATE
    rhi
    rhi-vulkan  # Backend selection at link time
    # Other dependencies...
)
```

---

## Include Path Updates

### Task 8.8: Update Include Statements

All files using RHI must update their include paths:

**Before:**
```cpp
#include "src/rhi/RHI.hpp"
#include "src/rhi/RHIDevice.hpp"
#include "src/rhi/vulkan/VulkanRHIDevice.hpp"
```

**After:**
```cpp
#include <rhi/RHI.hpp>
#include <rhi/Device.hpp>
#include "Device.hpp"  // Internal include within rhi-vulkan
```

### Files Requiring Include Updates

| File | Changes Required |
|------|------------------|
| `src/rendering/Renderer.cpp` | `#include <rhi/RHI.hpp>` |
| `src/rendering/Renderer.hpp` | `#include <rhi/Device.hpp>` |
| `src/rendering/RendererBridge.cpp` | Update all RHI includes |
| `src/rendering/RendererBridge.hpp` | Update all RHI includes |
| `src/scene/Mesh.hpp` | `#include <rhi/RHI.hpp>` |
| `src/scene/Mesh.cpp` | Update includes |
| `src/scene/SceneManager.cpp` | Update includes |
| `src/resources/ResourceManager.cpp` | Update includes |
| `src/ui/ImGuiManager.cpp` | Update includes |
| `tests/rhi_smoke_test.cpp` | Update includes |

---

## Namespace Considerations

### Option A: Keep Current Namespace (Recommended)

Keep `namespace rhi` and class names unchanged to minimize code changes:

```cpp
namespace rhi {
    class RHIDevice { ... };
    class RHIBuffer { ... };
}
```

### Option B: Simplify Class Names (Future Enhancement)

Remove `RHI` prefix since namespace already indicates RHI:

```cpp
namespace rhi {
    class Device { ... };      // Instead of RHIDevice
    class Buffer { ... };      // Instead of RHIBuffer
}
```

**Recommendation**: Keep Option A for Phase 8 to minimize changes. Consider Option B as a separate cleanup phase.

---

## Verification Checklist

### Build Verification

- [ ] `cmake --preset default` succeeds
- [ ] `cmake --build build` compiles without errors
- [ ] All include paths resolve correctly
- [ ] No circular dependencies

### Runtime Verification

- [ ] `./build/vulkanGLFW models/fdf/42.fdf` runs correctly
- [ ] RHI smoke tests pass
- [ ] No Vulkan validation errors
- [ ] ImGui renders correctly

### Code Quality

- [ ] No duplicate files left in old locations
- [ ] All old `src/rhi/` directory removed
- [ ] Include guards updated if renamed
- [ ] Doxygen comments updated with new paths

---

## Rollback Plan

If issues arise during migration:

```bash
# Create backup branch before starting
git checkout -b backup/pre-phase8-refactoring

# If rollback needed
git checkout feat/rhi-migration
git reset --hard backup/pre-phase8-refactoring
```

---

## Timeline

| Task | Description | Duration | Dependencies |
|------|-------------|----------|--------------|
| 8.1 | Create directory structure | 5 min | - |
| 8.2 | Move RHI interface headers | 30 min | 8.1 |
| 8.3 | Move Vulkan backend files | 30 min | 8.1 |
| 8.4 | Create Forward.hpp | 15 min | 8.2 |
| 8.5 | Create rhi/CMakeLists.txt | 15 min | 8.2 |
| 8.6 | Create rhi-vulkan/CMakeLists.txt | 20 min | 8.3 |
| 8.7 | Update root CMakeLists.txt | 15 min | 8.5, 8.6 |
| 8.8 | Update include statements | 1-2 hr | 8.2, 8.3 |
| 8.9 | Build verification | 30 min | All above |
| 8.10 | Runtime verification | 30 min | 8.9 |
| 8.11 | Cleanup old directories | 10 min | 8.10 |

**Total Estimated Time**: 4-5 hours

---

## Success Criteria

### Must Have
- [x] All RHI code compiles in new structure
- [x] Application runs identically to before
- [x] No Vulkan validation errors
- [x] Clear Public/Private separation

### Nice to Have
- [x] Documentation updated
- [ ] README.md updated with new architecture
- [ ] Architecture diagram added

---

## Completion Notes

### Implemented Changes

1. **Directory Structure**: Successfully reorganized to `src/rhi/` (interfaces) and `src/rhi-vulkan/` (implementation)
2. **Public/Private Separation**: All public headers in `include/rhi/` and `include/rhi-vulkan/`
3. **CMake Modules**: Separate `rhi` and `rhi-vulkan` modules with proper dependencies
4. **Cross-Platform Support**: Added Linux Vulkan 1.1 compatibility

### Platform-Specific Implementations

| Feature | Linux (Vulkan 1.1) | macOS/Windows (Vulkan 1.3) |
|---------|-------------------|---------------------------|
| Render Pass | Traditional (`vkCmdBeginRenderPass`) | Dynamic (`vkCmdBeginRendering`) |
| Pipeline Creation | Requires `renderPass` | Uses `pNext` chain |
| Image Barriers | Auto (render pass handles) | Manual pipeline barriers |
| Framebuffers | Required | Not needed |

### Build Results

- **Targets**: 50/50 passed
- **Smoke Tests**: All passing
- **Validation Errors**: None (except legacy renderer semaphore sync - out of scope)

---

## Lessons Learned

### 1. Vulkan Version Compatibility

**Issue**: Linux (especially WSL2 with lavapipe) only supports Vulkan 1.1, while dynamic rendering requires Vulkan 1.3.

**Solution**: Use `#ifdef __linux__` conditionals to provide traditional render pass path:
- `VulkanRHICommandEncoder`: Use `beginRenderPass()`/`endRenderPass()` instead of `beginRendering()`/`endRendering()`
- `VulkanRHIPipeline`: Provide `renderPass` instead of `pNext` rendering info
- `VulkanRHISwapchain`: Create framebuffers for traditional render pass

**Lesson**: Always check Vulkan feature availability at runtime or compile time when supporting multiple platforms.

### 2. Render Pass vs Dynamic Rendering

**Issue**: Traditional render pass handles image layout transitions automatically, but dynamic rendering requires explicit barriers.

**Solution**: Skip manual `pipelineBarrier()` calls on Linux where render pass handles transitions via `initialLayout`/`finalLayout`.

**Lesson**: Understand the differences between render pass automatic transitions and manual barrier control.

### 3. Swapchain Creation Order

**Issue**: Pipeline creation on Linux requires a valid `VkRenderPass`, which comes from the swapchain.

**Solution**: Ensure swapchain is created before pipeline in initialization flow:
```cpp
void createRHIPipeline() {
    if (!m_rhiSwapchain) {
        createSwapchain();  // Must exist for render pass
    }
    // ... create pipeline with render pass from swapchain
}
```

**Lesson**: Be aware of resource creation order dependencies across platforms.

### 4. Type Casting Between vulkan-hpp and C API

**Issue**: Casting `vk::RenderPass` to `void*` for platform-agnostic interfaces.

**Solution**: Use double cast:
```cpp
reinterpret_cast<void*>(static_cast<VkRenderPass>(vkRenderPass))
```

And reverse:
```cpp
static_cast<vk::RenderPass>(reinterpret_cast<VkRenderPass>(nativePtr))
```

**Lesson**: vulkan-hpp types are wrappers around C handles; proper casting sequence is essential.

### 5. Namespace Organization

**Issue**: Namespace conflicts when refactoring (`rhi::VulkanRHISwapchain` vs `RHI::Vulkan::VulkanRHISwapchain`).

**Solution**: Standardize on consistent namespace structure:
- `rhi::` for interfaces
- `RHI::Vulkan::` for Vulkan backend implementations

**Lesson**: Establish namespace conventions early and document them clearly.

---

## Future Extensions (Phase 9+)

With this modular structure, adding new backends becomes straightforward:

```bash
# Adding WebGPU backend
mkdir -p src/rhi-webgpu/include/rhi-webgpu
mkdir -p src/rhi-webgpu/src
# Implement RHI interfaces using Dawn/wgpu
# Add CMakeLists.txt
# Link in application
```

---

## References

- **Unreal Engine RHI**: `Engine/Source/Runtime/RHI/`, `Engine/Source/Runtime/VulkanRHI/`
- **wgpu (Rust)**: `wgpu-hal/src/vulkan/`, `wgpu-hal/src/metal/`
- **Rtrc Engine**: `Source/Rtrc/RHI/`

---

## Appendix: Command Reference

### File Operations

```bash
# Create directories
mkdir -p src/rhi/include/rhi src/rhi/src
mkdir -p src/rhi-vulkan/include/rhi-vulkan src/rhi-vulkan/src

# Move and rename files (example)
mv src/rhi/RHIDevice.hpp src/rhi/include/rhi/Device.hpp
mv src/rhi/vulkan/VulkanRHIDevice.hpp src/rhi-vulkan/src/Device.hpp
mv src/rhi/vulkan/VulkanRHIDevice.cpp src/rhi-vulkan/src/Device.cpp

# Remove old directory after verification
rm -rf src/rhi  # Only after all verification passes!
```

### Build Commands

```bash
# Clean build
rm -rf build
cmake --preset default
cmake --build build

# Run tests
./build/rhi_smoke_test
./build/vulkanGLFW models/fdf/42.fdf
```
