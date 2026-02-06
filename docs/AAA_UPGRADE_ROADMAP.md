# Mini-Engine AAA Upgrade Roadmap

**Goal**: Elevate the engine from toy-project level to a tech demo with **PBR visuals** and **GPU-Driven optimization**, proving production-ready engine development capability.

**Progress**: Phase 2.2+2.3 Complete (2026-02-06)

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

### 2.1 Data-Oriented Scene Structure -- COMPLETE

**Target Files**: `src/rendering/InstancedRenderData.hpp`, `src/game/managers/BuildingManager.hpp/.cpp`, `src/rendering/Renderer.hpp/.cpp`, `src/rendering/ShadowRenderer.hpp/.cpp`, `shaders/building.vert.glsl`, `shaders/building.wgsl`, `shaders/shadow.vert.glsl`, `shaders/shadow.wgsl`, `src/Application.cpp`

**Tasks**:

- [x] **ObjectData Struct**: Replaced 48-byte `InstanceData` (flat position/color/scale) with 128-byte `ObjectData` (mat4 worldMatrix, AABB boundingBoxMin/Max, vec4 colorAndMetallic, vec4 roughnessAOPad). `alignas(16)` for std430 compatibility.
- [x] **SSBO Introduction**: Replaced per-instance vertex attributes (binding 1, 6 attributes) with Storage Buffer Object. Buffer usage changed from `Vertex | MapWrite` to `Storage | MapWrite`. Buffer reuse optimization: only recreate when capacity insufficient, otherwise `write()` into existing buffer.
- [x] **Two Bind Group Architecture**: Set 0 (UBO + textures, unchanged) and Set 1 (SSBO with ObjectData[] array). SSBO bind group cached per-frame with dirty tracking via pointer comparison.
- [x] **Shader Migration (GLSL + WGSL)**: Building and shadow vertex shaders access SSBO via `gl_InstanceIndex` / `@builtin(instance_index)`. World transform computed on CPU as `glm::translate * glm::scale`, stored in mat4. Fragment shaders unchanged (receive interpolated values).
- [x] **Pipeline Updates**: Renderer and ShadowRenderer pipeline layouts include both bind group layouts. Removed instance vertex buffer layout from both pipelines. Memory barrier updated: `eHost→eVertexShader` with `eHostWrite→eShaderRead`.
- [x] **API Rename**: `getInstanceBuffer()` → `getObjectBuffer()`, `updateInstanceBuffer()` → `updateObjectBuffer()`, `markInstanceBufferDirty()` → `markObjectBufferDirty()`.

### 2.2 Compute Shader Culling -- COMPLETE

**Target Files**: `shaders/frustum_cull.comp.glsl` (new), `shaders/frustum_cull.comp.wgsl` (new), `shaders/building.vert.glsl`, `shaders/building.wgsl`, `src/rendering/Renderer.hpp/.cpp`, `CMakeLists.txt`

**Tasks**:

- [x] **Compute Shader (GLSL + WGSL)**: New `frustum_cull.comp` shader with workgroup size 64. Per-thread AABB vs 6 frustum plane test (p-vertex method). `atomicAdd` on indirect draw buffer's `instanceCount` to allocate visible slots.
- [x] **Frustum Plane Extraction**: Griggs-Hartmann method extracts 6 normalized planes (Left/Right/Bottom/Top/Near/Far) from VP matrix. GLM column-major access pattern.
- [x] **Cull Pipeline**: New `createCullingPipeline()` with 4-entry bind group layout (CullUBO, ObjectData, IndirectDrawCommand, VisibleIndices). Per-frame buffers: CullUBO (112 bytes, Uniform|MapWrite), IndirectDraw (20 bytes, Storage|Indirect|MapWrite), VisibleIndices (4*MAX_CULL_OBJECTS bytes, Storage).
- [x] **Vertex Shader Indirection**: Building vertex shaders (GLSL + WGSL) now index via `visibleIndices[gl_InstanceIndex]` → `objectData[actualIndex]`. SSBO bind group layout expanded to 2 bindings (ObjectData + VisibleIndices).
- [x] **Vulkan Barriers**: Pre-compute barrier (`eHost→eComputeShader`) for CullUBO, IndirectDraw, ObjectData writes. Post-compute barrier (`eComputeShader→eDrawIndirect|eVertexShader`) for IndirectDraw and VisibleIndices reads.

### 2.3 Indirect Draw Transition -- COMPLETE

**Target Files**: `src/rendering/Renderer.cpp`

**Tasks**:

- [x] **API Change**: Replaced `drawIndexed(indexCount, instanceCount)` with single `drawIndexedIndirect(indirectBuffer, 0)` for main building render pass. Shadow pass unchanged (direct `drawIndexed`, no frustum culling from light perspective).
- [x] **Compute Dispatch**: `performFrustumCulling()` dispatches `(objectCount+63)/64` workgroups per frame. Resets indirect draw buffer (instanceCount=0) before each dispatch. Cull bind group cached and invalidated on objectBuffer change.

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
