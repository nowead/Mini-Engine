# Optimization & Hardware Abstraction: Real Troubleshooting Experiences

This document collects concrete experiences from Mini-Engine development where memory issues were resolved, processing speed was dramatically improved, or hardware differences were successfully abstracted away. Each entry includes the problem, root cause, solution, and lessons learned.

---

## Table of Contents

1. [Performance Optimization](#performance-optimization)
   - [GPU Instancing: 100x Draw Call Reduction](#gpu-instancing-100x-draw-call-reduction)
   - [WASM Binary Size: Eliminating "section too large" Linker Error](#wasm-binary-size-eliminating-section-too-large-linker-error)
2. [Hardware Abstraction](#hardware-abstraction)
   - [Vulkan 1.1 vs 1.3: Dynamic Rendering Not Available on Linux](#vulkan-11-vs-13-dynamic-rendering-not-available-on-linux)
   - [Platform-Specific Pipeline Creation: RenderPass Requirement](#platform-specific-pipeline-creation-renderpass-requirement)
   - [Image Layout Transitions: Manual vs. Automatic Barrier Handling](#image-layout-transitions-manual-vs-automatic-barrier-handling)
   - [Native Handle Type Casting: Bridging C++ Wrappers and void*](#native-handle-type-casting-bridging-c-wrappers-and-void)
   - [WebGPU Storage Texture Format Incompatibility](#webgpu-storage-texture-format-incompatibility)

---

## Performance Optimization

### GPU Instancing: 100x Draw Call Reduction

**Source**: `docs/refactoring/aaa-upgrade/GPU_INSTANCING.md`

#### Problem

Rendering 1,000 building objects issued 1,000 individual draw calls per frame. Each draw call carries CPU-side overhead (state validation, command recording, driver submission). At 1,000 objects the GPU was starved by CPU bottleneck, achieving only ~10 FPS.

```
Without instancing:
  1,000 objects × 1 draw call = 1,000 draw calls → ~10 FPS
```

#### Root Cause

Each object was submitted independently through the render loop. There was no mechanism for the GPU to batch identical geometry with varying per-object transforms.

#### Solution

Introduced GPU instancing via a secondary vertex buffer carrying per-instance data:

```cpp
// Instance buffer layout — rate switches from Vertex to Instance
VertexBufferLayout instanceLayout{};
instanceLayout.stride    = sizeof(InstanceData);
instanceLayout.inputRate = VertexInputRate::Instance;   // Key change
instanceLayout.attributes = { /* modelMatrix columns, color, etc. */ };

// Single draw call for all instances
encoder->draw(vertexCount, instanceCount, 0, 0);
```

The vertex shader receives both per-vertex attributes (`@location(0..n)`) and per-instance attributes (`@location(n+1..m)`) and reconstructs the model matrix on the GPU.

#### Result

| Metric | Before | After |
|--------|--------|-------|
| Draw calls (1,000 objects) | 1,000 | 1 |
| Frame rate | ~10 FPS | ~60 FPS |
| Speedup | — | **~6× wall-clock, ~100× draw calls** |

#### Lessons

- GPU instancing is the single most impactful optimization for scenes with repeated geometry.
- The only CPU cost becomes updating the instance data buffer once per frame (a single `memcpy`).
- `VertexInputRate::Instance` is the RHI-level knob — the rest is shader bookkeeping.

---

### WASM Binary Size: Eliminating "section too large" Linker Error

**Source**: `docs/refactoring/webgpu-backend/WASM_BUILD_TROUBLESHOOTING.md`

#### Problem

The Emscripten linker failed with a fatal error when building the WebAssembly target:

```
wasm-ld: error: section too large
```

The build was completely broken — no `.wasm` output was produced at all.

#### Root Cause

`WebGPUCommon.hpp` defined 25+ small conversion functions (`ToWGPUTextureFormat`, `ToWGPUBlendFactor`, …) as `inline` in the header. Because the header was included in 13+ translation units, the linker saw 13 copies of every function body. WASM's binary section size limit was exceeded before any optimization pass could run.

```
25 functions × 13 TUs ≈ 325 duplicated function bodies in the .wasm section
```

#### Solution

1. **Moved all 25 conversion functions** from `WebGPUCommon.hpp` to `WebGPUCommon.cpp` — one definition, 13 declarations.
2. **Switched compile flags** from `-O2` to `-Oz` (maximum size) and added `-flto` on both compile and link steps.
3. **Stripped debug info** with `-g0` for release builds.

```cmake
target_compile_options(mini_engine_wasm PRIVATE -Oz -flto -g0)
target_link_options   (mini_engine_wasm PRIVATE -Oz -flto)
```

#### Result

| Metric | Before (estimated) | After |
|--------|--------------------|-------|
| `.wasm` size | >500 KB (failed to link) | **156 KB** |
| JS glue size | — | 154 KB |
| Build status | **FAILED** | ✅ Success |

#### Lessons

- `inline` functions in widely-included headers are a hidden code-size multiplier in WASM builds.
- Prefer declaring functions in headers and defining them in a single `.cpp` file for any utility with many call sites.
- `-Oz` + `-flto` together are significantly more effective than either alone for WASM.
- Always verify final `.wasm` size in CI — silent bloat accumulates quickly with heavy header usage.

---

## Hardware Abstraction

### Vulkan 1.1 vs 1.3: Dynamic Rendering Not Available on Linux

**Source**: `docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md` — Issue 3

#### Problem

The RHI's `beginRenderPass()` implementation called `vkCmdBeginRendering` / `vkCmdEndRendering` (Vulkan 1.3 / `VK_KHR_dynamic_rendering`). On Linux (lavapipe software renderer, Vulkan 1.1) every frame produced a validation error and a crash:

```
Validation Error: VUID-vkCmdBeginRendering-dynamicRendering-06446
vkCmdBeginRendering() requires VK_KHR_dynamic_rendering or Vulkan 1.3.
```

#### Root Cause

The initial RHI design targeted macOS/Windows where hardware drivers expose Vulkan 1.3. The Linux CI/development machine ran lavapipe which only supports Vulkan 1.1 and does not implement the dynamic rendering extension.

#### Solution

Added a platform-specific code path inside `VulkanRHICommandEncoder`:

```cpp
void VulkanRHICommandEncoder::beginRenderPass(const RenderPassDesc& desc) {
#ifdef __linux__
    // Traditional VkRenderPass — compatible with Vulkan 1.1
    VkRenderPassBeginInfo info{};
    info.renderPass  = static_cast<VkRenderPass>(desc.nativeRenderPass);
    info.framebuffer = static_cast<VkFramebuffer>(desc.nativeFramebuffer);
    // ...
    vkCmdBeginRenderPass(m_cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
#else
    // Dynamic rendering — Vulkan 1.3 / macOS / Windows
    VkRenderingInfoKHR renderingInfo{};
    // ...
    vkCmdBeginRenderingKHR(m_cmd, &renderingInfo);
#endif
}
```

`RenderPassDesc` was extended with two opaque fields:

```cpp
struct RenderPassDesc {
    // ... existing fields ...
    void* nativeRenderPass   = nullptr;   // VkRenderPass on Linux
    void* nativeFramebuffer  = nullptr;   // VkFramebuffer on Linux
};
```

`VulkanRHISwapchain` exposes `getRenderPass()` and `getFramebuffer(imageIndex)` so callers can populate these fields without depending on Vulkan types directly.

#### Lesson

Hardware abstraction layers must account for **version fragmentation within the same API**. Vulkan 1.1 and 1.3 require fundamentally different render pass approaches. Surfacing platform differences through nullable opaque fields in the descriptor struct kept the RHI interface stable while accommodating both code paths.

---

### Platform-Specific Pipeline Creation: RenderPass Requirement

**Source**: `docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md` — Issue 4

#### Problem

Graphics pipeline creation silently produced an invalid pipeline on Linux because `VkGraphicsPipelineCreateInfo::renderPass` was `VK_NULL_HANDLE`. Downstream draw calls crashed with:

```
Validation Error: pipeline renderPass is VK_NULL_HANDLE
```

#### Root Cause

`VkGraphicsPipelineCreateInfo` requires a valid `renderPass` when dynamic rendering is not active (Vulkan 1.1). On Vulkan 1.3 this field is ignored. The RHI always passed `VK_NULL_HANDLE`, which worked on macOS/Windows but not Linux.

#### Solution

Added `nativeRenderPass` to `RenderPipelineDesc` and a strict initialization order:

```cpp
struct RenderPipelineDesc {
    // ...
    void* nativeRenderPass = nullptr;  // Required on Vulkan 1.1 (Linux)
};
```

```cpp
// Correct order on Linux:
bridge->createSwapchain(width, height, vsync);   // 1. Creates VkRenderPass internally
auto rp = swapchain->getRenderPass();            // 2. Retrieve it
pipelineDesc.nativeRenderPass = reinterpret_cast<void*>(static_cast<VkRenderPass>(rp));
device->createRenderPipeline(pipelineDesc);      // 3. Now safe
```

**Critical ordering rule**: on Linux the swapchain (and its `VkRenderPass`) must exist before any pipeline is created.

#### Lesson

Initialization order bugs are especially subtle in cross-platform code because they only manifest on one platform. Document the required creation order explicitly and enforce it with assertions in debug builds.

---

### Image Layout Transitions: Manual vs. Automatic Barrier Handling

**Source**: `docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md` — Issue 5

#### Problem

Validation errors appeared on Linux after adding manual `pipelineBarrier()` calls for image layout transitions:

```
Validation Error: oldLayout must be VK_IMAGE_LAYOUT_UNDEFINED or
the current layout of the image. Expected PRESENT_SRC_KHR, got UNDEFINED.
```

#### Root Cause

Traditional `VkRenderPass` (used on Linux) automatically inserts image layout transitions for color and depth attachments as part of its subpass dependency model. The code also called `pipelineBarrier()` manually — duplicating the transition and placing the image in an unexpected layout.

On Vulkan 1.3 with dynamic rendering there are no automatic transitions, so manual barriers are required.

#### Solution

Skip manual barriers on Linux when using a traditional render pass:

```cpp
void VulkanRHICommandEncoder::transitionImageLayout(...) {
#ifdef __linux__
    // VkRenderPass owns layout transitions — do not duplicate them.
    return;
#endif
    VkImageMemoryBarrier barrier{};
    // ... fill barrier ...
    vkCmdPipelineBarrier(m_cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
```

#### Lesson

The same logical operation (image layout transition) has two completely different implementation strategies depending on the Vulkan version. An abstraction layer must hide this divergence: callers declare *intent* (transition this image to shader-read), and the backend selects the correct mechanism.

---

### Native Handle Type Casting: Bridging C++ Wrappers and void*

**Source**: `docs/game_logic/TROUBLESHOOTING.md`

#### Problem

The RHI stored native Vulkan handles as `void*` in descriptor structs (e.g., `RenderPassDesc::nativeRenderPass`). Directly casting `vk::RenderPass` (the C++ wrapper type from `vulkan.hpp`) to `void*` failed to compile:

```
error: reinterpret_cast of type 'vk::RenderPass' to 'void*' is not allowed
```

#### Root Cause

`vk::RenderPass` is a C++ value type that wraps an opaque 64-bit integer handle. It is not a pointer, so a direct `reinterpret_cast<void*>` is ill-formed. The underlying C type `VkRenderPass` is `uint64_t` on 64-bit platforms, also not directly castable to `void*`.

#### Solution

A two-step cast pattern that is both portable and well-defined:

```cpp
// C++ wrapper → void*
void* toVoidPtr(vk::RenderPass rp) {
    return reinterpret_cast<void*>(static_cast<VkRenderPass>(rp));
}

// void* → C++ wrapper
vk::RenderPass fromVoidPtr(void* ptr) {
    return static_cast<vk::RenderPass>(reinterpret_cast<VkRenderPass>(ptr));
}
```

`static_cast` converts between the C++ wrapper and its underlying C type; `reinterpret_cast` converts between the integer handle and a pointer.

#### Lesson

When building hardware abstraction that must store backend-specific handles in a generic interface (`void*`), establish a canonical cast helper per handle type. Ad-hoc casts scattered across the codebase lead to subtle pointer-size bugs on 32-bit targets.

---

### WebGPU Storage Texture Format Incompatibility

**Source**: `docs/refactoring/webgpu-backend/WEBGPU_RUNTIME_VALIDATION.md` — Issue 5

#### Problem

The BRDF LUT compute shader bound a storage texture with format `RG16Float`. The WASM/WebGPU build produced a validation error at bind group creation:

```
[WebGPU] Validation error: Storage texture format 'rg16float' is not supported.
Supported formats: rgba8unorm, rgba8snorm, rgba8uint, rgba8sint,
                   rgba16float, r32float, r32uint, r32sint, ...
```

On native Vulkan `RG16Float` as a storage image worked without issue.

#### Root Cause

WebGPU's allowed storage texture formats are a strict subset of Vulkan's. `RG16Float` is valid in Vulkan but is not in WebGPU's permitted list. The two backends have divergent format support tables.

#### Solution

Platform-specific format selection at texture creation:

```cpp
TextureFormat brdfLutFormat =
#ifdef __EMSCRIPTEN__
    TextureFormat::RGBA16Float;   // WebGPU requires 4-component
#else
    TextureFormat::RG16Float;     // Vulkan — tighter packing, no wasted channel
#endif
```

The shader was updated to only read `.rg` regardless of whether it received an `RG` or `RGBA` texture, keeping both paths functionally identical.

#### Lesson

Format support tables differ between Vulkan and WebGPU even for semantically equivalent concepts. When writing cross-backend code, validate each texture format against both APIs' specification and use compile-time selection for unavoidable divergences. Centralizing format choices in a single location makes future backend additions easier.

---

## Summary

| Experience | Category | Impact |
|------------|----------|--------|
| GPU Instancing | Performance | 1,000 draw calls → 1; ~10 FPS → ~60 FPS |
| WASM Binary Size | Memory / Build | >500 KB (link fail) → 156 KB working binary |
| Dynamic Rendering (Vulkan 1.1 vs 1.3) | HW Abstraction | Linux compatibility; cross-platform render pass model |
| Pipeline RenderPass Requirement | HW Abstraction | Prevented silent invalid pipeline on Linux |
| Image Layout Transition Ownership | HW Abstraction | Eliminated double-transition validation errors |
| Native Handle Type Casting | HW Abstraction | Portable `void*` interop pattern across backends |
| WebGPU Storage Texture Formats | HW Abstraction | Cross-backend BRDF LUT compute compatibility |
