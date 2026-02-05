# Mini-Engine AAA Upgrade Roadmap

**Goal**: Elevate the engine from toy-project level to a tech demo with **PBR visuals** and **GPU-Driven optimization**, proving production-ready engine development capability.

**Progress**: Phase 1.2 Complete (2026-02-06)

---

## Week 1: Rendering Pipeline Modernization (PBR & IBL)

**Core Objective**: Remove legacy Phong shading and build a Physically Based Rendering (PBR) pipeline.

### 1.1 Fragment Shader: Cook-Torrance BRDF -- COMPLETE

**Target File**: `shaders/building.frag.glsl`, `shaders/building.wgsl` (full replacement of existing logic)

**Tasks**:

- [x] **Input Restructuring**: Renamed `Vertex.color` to `Vertex.normal`. Extended per-instance data (48 bytes) with metallic, roughness, AO parameters. Updated vertex shaders (GLSL + WGSL) with new pass-through attributes.
- [x] **Direct Lighting Implementation**:
  - Distribution (D): Trowbridge-Reitz GGX
  - Geometry (G): Smith's Schlick-GGX
  - Fresnel (F): Fresnel-Schlick approximation
- [x] **Color Space**: sRGB-to-linear conversion on albedo input (`pow(2.2)`). ACES Filmic tone mapping with configurable exposure. Vulkan: hardware sRGB via `BGRA8UnormSrgb`. WebGPU: manual gamma correction in WGSL shader.
- [x] **Pipeline Updates**: Instance buffer stride 40 -> 48 bytes in both Renderer and ShadowRenderer. Added exposure to UBO and ImGui control with lighting presets.

### 1.2 IBL (Image Based Lighting) Integration -- COMPLETE

**Target Files**: `src/rendering/IBLManager.hpp/.cpp`, `src/rendering/Renderer.cpp`, `shaders/building.frag.glsl`, `shaders/building.wgsl`

**Tasks**:

- [x] **RHI Cubemap Support**: Added `TextureDesc.arrayLayerCount` and `isCubemap` flag. Updated Vulkan (`VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, cubemap view creation, multi-layer transitions) and WebGPU backends (`depthOrArrayLayers` distinction, cubemap views). Added `textureViewDimension` to `BindGroupLayoutEntry` for configurable texture view dimensions in bind group layout.
- [x] **Vulkan Bind Group Fix**: Fixed descriptor type selection (StorageTexture → `eStorageImage` + `GENERAL` layout). Layout entries now stored in `VulkanRHIBindGroupLayout` for correct descriptor creation.
- [x] **HDR Loader**: Added `ResourceManager::loadHDRTexture()` using `stbi_loadf()` with `RGBA32Float` format for equirectangular HDR maps.
- [x] **IBLManager**: New `IBLManager` class managing 4 GPU textures:
  - Environment Cubemap (512x512x6, RGBA16Float)
  - Irradiance Map (32x32x6, RGBA16Float) — hemisphere convolution
  - Prefiltered Environment Map (128x128x6, RGBA16Float, 5 mips) — roughness-based specular
  - BRDF LUT (512x512, RG16Float) — split-sum BRDF integration
- [x] **Compute Shaders** (4 new shaders, GLSL + WGSL):
  - `brdf_lut.comp`: Hammersley sequence + GGX importance sampling for BRDF integration
  - `equirect_to_cubemap.comp`: Equirectangular → cubemap face mapping
  - `irradiance_map.comp`: Cosine-weighted hemisphere convolution
  - `prefilter_env.comp`: Per-mip roughness-based GGX prefiltering (5 dispatches)
- [x] **Pipeline Integration**: Expanded building bind group (bindings 3-6: irradiance cubemap, prefiltered cubemap, BRDF LUT, IBL sampler). First use of RHI compute pipeline in production.
- [x] **Shader Integration**: IBL ambient replaces flat ambient in both GLSL and WGSL: `fresnelSchlickRoughness` for IBL Fresnel, irradiance diffuse, prefiltered specular + BRDF LUT split-sum. Smart fallback: detects empty cubemap and uses flat ambient when no HDR env is loaded.
- [x] **API**: Added `Renderer::loadEnvironmentMap(path)` for runtime HDR loading with full IBL pipeline re-initialization.

---

## Week 2: GPU-Driven Rendering (Optimization)

**Core Objective**: Eliminate CPU bottlenecks (DrawCall overhead, Scene Traversal) and transition to GPU-driven rendering.

### 2.1 Data-Oriented Scene Structure

**Target Files**: `src/scene/SceneGraph.cpp`, `src/rendering/Renderer.cpp`

**Tasks**:

- [ ] **SSBO Introduction**: Deprecate per-object UniformBuffer update approach.
- [ ] **Global Object Buffer**: Create and manage a single SSBO (ObjectDataBuffer) containing WorldMatrix and BoundingBox for all objects.

### 2.2 Compute Shader Culling

**Target Files**: `shaders/cull.comp.glsl` (new), `src/scene/Quadtree.cpp` (to be removed)

**Tasks**:

- [ ] **Logic Migration**: Remove CPU `Quadtree::query` and migrate to Compute Shader.
- [ ] **Parallel Culling**: Perform Frustum Culling in parallel using GlobalInvocationID.
- [ ] **Output Generation**: Write only visible object indices to VisibleIndexBuffer (using atomicAdd).
- [ ] **Command Generation**: Fill `VkDrawIndexedIndirectCommand` structs directly in the Compute Shader.

### 2.3 Indirect Draw Transition

**Target File**: `src/rendering/BatchRenderer.cpp`

**Tasks**:

- [ ] **API Change**: Remove `vkCmdDrawIndexed` calls in loop and replace with a single `vkCmdDrawIndexedIndirect` call.
- [ ] **Synchronization**: Add `VkBufferMemoryBarrier` between Compute Shader (Culling) write and Graphics Pipeline (Vertex) read.

---

## Week 3: Architecture Deep Dive (Memory & Sync)

**Core Objective**: Demonstrate systems-level understanding (memory, synchronization) as an engine developer.

### 3.1 Memory Aliasing (Resource Management)

**Target File**: `src/rhi/backends/vulkan/src/VulkanMemoryAllocator.cpp`

**Tasks**:

- [ ] **Transient Resource Identification**: Identify resources not used simultaneously (e.g., Shadow Map vs Post-process Buffer).
- [ ] **Aliasing Implementation**: Use VMA's `VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT` to share the same `VkDeviceMemory` region.
- [ ] **Memory Savings Report**: Capture GPU memory usage comparison data before and after aliasing.

### 3.2 Async Compute (Parallelism)

**Target Files**: `src/rhi/backends/vulkan/src/VulkanRHIQueue.cpp`, `src/rendering/Renderer.cpp`

**Tasks**:

- [ ] **Queue Family Separation**: Acquire a Compute Queue separate from the Graphics Queue.
- [ ] **Pipeline Separation**: Execute Culling (Compute) and Shadow Rendering (Graphics) in parallel.
- [ ] **Explicit Sync**: Introduce Timeline Semaphores for precise synchronization between Compute completion and Graphics draw timing.

---

## Week 4: Profiling & Proof

**Core Objective**: Prove performance improvements with numbers and data (portfolio packaging).

### 4.1 Profiling Data

**Tools**: RenderDoc, Nsight Graphics

**Tasks**:

- [ ] **Draw Call Comparison**: Capture proof of (Before) hundreds of draw calls to (After) 1 draw call (Indirect Draw).
- [ ] **GPU Timing**: Measure exact elapsed time (ms) for Culling and Rendering passes using `vkCmdWriteTimestamp`.
- [ ] **Stress Test**: Create frame rate comparison graph when rendering 100,000 objects.

### 4.2 Demo & Documentation

**Tasks**:

- [ ] **Tech Demo Video**: Demonstrate PBR material variation (metallic/non-metallic) and large-scale object rendering.
- [ ] **README Update**: Revise project description from "Vulkan toy project" to **"PBR & GPU-Driven Engine"**.

---

*Created: 2026-02-05*
*Last Updated: 2026-02-06*
