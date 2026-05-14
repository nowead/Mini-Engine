# RHI (Render Hardware Interface) - Technical Implementation Guide

**Version**: 1.0
**Last Updated**: 2025-12-19
**Purpose**: Technical reference for implementing the RHI architecture

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Design Principles](#design-principles)
4. [Core Interfaces](#core-interfaces)
5. [Backend Mapping](#backend-mapping)
6. [Implementation Patterns](#implementation-patterns)
7. [Shader Cross-Compilation](#shader-cross-compilation)
8. [Build System Configuration](#build-system-configuration)
9. [Performance Optimization](#performance-optimization)
10. [Reference Architectures](#reference-architectures)

---

## Introduction

### What is RHI?

RHI (Render Hardware Interface) is an abstraction layer that provides a unified API for graphics rendering across multiple backends (Vulkan, WebGPU, Direct3D 12, Metal). It allows upper layers of the engine to remain platform-independent while enabling efficient access to modern graphics APIs.

### Goals

- **Multi-Backend Support**: Enable Vulkan, WebGPU, D3D12, and Metal backends
- **Platform Independence**: Upper layers use only RHI interfaces
- **Extensibility**: Plugin-like backend architecture
- **Performance**: Zero-cost abstraction (< 5% overhead)

### Design Philosophy

The RHI design follows **WebGPU's API model** for several reasons:

1. **Modern**: Designed for modern GPU architectures
2. **Cross-Platform**: Common abstraction of Vulkan/D3D12/Metal
3. **Clean**: Explicit resource management, clear ownership
4. **Future-Proof**: W3C standard, actively developed

---

## Architecture Overview

### From Layered to RHI Architecture

**Before (Layered Architecture)**:
```
Application Layer
├── Application
└── ImGuiManager

Rendering Layer
└── Renderer
    ├── Uses: ResourceManager, SceneManager
    └── Owns: VulkanSwapchain, VulkanPipeline, SyncManager

Core Layer
├── VulkanDevice ❌ (Tightly coupled)
├── VulkanBuffer ❌
└── VulkanImage ❌
```

**Problems**:
- Tightly coupled to Vulkan
- Cannot switch graphics APIs
- Platform-specific code scattered throughout

**After (RHI Architecture)**:
```
Application Layer
├── Application
└── ImGuiManager

High-Level Rendering Layer
└── Renderer (API independent) ✅
    ├── Uses: ResourceManager, SceneManager
    └── Uses: RHI Interface

RHI Abstraction Layer (Platform-Agnostic) ✅
├── RHIDevice (abstract interface)
├── RHIBuffer, RHITexture, RHISampler
├── RHIShader, RHIBindGroup, RHIPipeline
├── RHICommandBuffer, RHIRenderPass
├── RHISwapchain, RHIQueue, RHISync
└── RHIFactory (backend creation)

Backend Implementation Layer (Platform-Specific) ✅
├── Vulkan Backend
├── WebGPU Backend
├── D3D12 Backend (future)
└── Metal Backend (future)
```

**Benefits**:
- Platform independence
- Easy backend switching
- Clean separation of concerns

---

## Design Principles

### 1. Pure Virtual Interfaces

**Rationale**: Runtime polymorphism for backend flexibility

```cpp
class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;
    virtual void* map() = 0;
    virtual void unmap() = 0;
    virtual uint64_t getSize() const = 0;
    // ...
};
```

**Trade-offs**:
- ✅ Runtime backend switching
- ✅ Easy plugin system
- ✅ ABI stability
- ⚠️ Virtual function overhead (~3-5%)

### 2. RAII Pattern

**Rationale**: Automatic resource management, exception safety

```cpp
// Resources are owned by unique_ptr
std::unique_ptr<RHIBuffer> buffer = device->createBuffer(desc);
// Automatically destroyed when going out of scope
```

**Benefits**:
- Guaranteed cleanup
- Exception-safe
- No manual delete
- Clear ownership

### 3. WebGPU-Style Command Encoding

**Rationale**: Explicit, type-safe command recording

```cpp
// Create command encoder
auto encoder = device->createCommandEncoder();

// Begin render pass
auto renderPass = encoder->beginRenderPass(renderPassDesc);
renderPass->setPipeline(pipeline);
renderPass->setBindGroup(0, bindGroup);
renderPass->setVertexBuffer(0, vertexBuffer);
renderPass->draw(vertexCount);
renderPass->end();

// Finish encoding
auto commandBuffer = encoder->finish();

// Submit to queue
queue->submit(commandBuffer.get());
```

**vs Vulkan's immediate mode**:
```cpp
// Vulkan (more verbose, error-prone)
vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
vkCmdDraw(cmd, vertexCount, 1, 0, 0);
vkCmdEndRenderPass(cmd);
```

### 4. Bind Group Model

**Rationale**: Efficient resource binding across all backends

```cpp
// Define layout
auto layout = device->createBindGroupLayout({
    {0, ShaderStage::Vertex, BindingType::UniformBuffer},
    {1, ShaderStage::Fragment, BindingType::SampledTexture},
    {2, ShaderStage::Fragment, BindingType::Sampler}
});

// Create bind group
auto bindGroup = device->createBindGroup({
    layout,
    {
        BindGroupEntry::Buffer(0, uniformBuffer),
        BindGroupEntry::TextureView(1, textureView),
        BindGroupEntry::Sampler(2, sampler)
    }
});
```

**Maps naturally to**:
- Vulkan: Descriptor Sets
- D3D12: Descriptor Tables
- Metal: Argument Buffers
- WebGPU: Bind Groups (1:1)

### 5. Type Safety

**Rationale**: Catch errors at compile time

```cpp
// Strongly typed enumerations
enum class BufferUsage : uint32_t {
    Vertex  = 1 << 0,
    Index   = 1 << 1,
    Uniform = 1 << 2,
    // ...
};

// Bitwise operators for flags
BufferUsage usage = BufferUsage::Vertex | BufferUsage::CopySrc;

// Type-safe checking
if (hasFlag(usage, BufferUsage::Vertex)) {
    // ...
}
```

---

## Core Interfaces

### RHITypes.hpp - Foundation

Common type definitions used throughout RHI:

```cpp
// Backend type enumeration
enum class RHIBackendType {
    Vulkan,
    WebGPU,
    D3D12,
    Metal
};

// Buffer usage flags
enum class BufferUsage : uint32_t {
    None     = 0,
    Vertex   = 1 << 0,
    Index    = 1 << 1,
    Uniform  = 1 << 2,
    Storage  = 1 << 3,
    CopySrc  = 1 << 4,
    CopyDst  = 1 << 5,
    // ...
};

// Texture formats (subset)
enum class TextureFormat {
    RGBA8Unorm,
    RGBA8UnormSrgb,
    BGRA8Unorm,
    Depth24Plus,
    Depth32Float,
    // ...
};

// Common structures
struct Extent3D {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
};
```

**Key Features**:
- 15+ enumerations
- Bitwise operators for flags
- Platform-independent types
- No backend-specific types

### RHIDevice.hpp - Main Interface

Central interface for resource creation:

```cpp
class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    // Resource creation
    virtual std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc&) = 0;
    virtual std::unique_ptr<RHITexture> createTexture(const TextureDesc&) = 0;
    virtual std::unique_ptr<RHISampler> createSampler(const SamplerDesc&) = 0;
    virtual std::unique_ptr<RHIShader> createShader(const ShaderDesc&) = 0;

    // Pipeline creation
    virtual std::unique_ptr<RHIBindGroupLayout> createBindGroupLayout(const BindGroupLayoutDesc&) = 0;
    virtual std::unique_ptr<RHIBindGroup> createBindGroup(const BindGroupDesc&) = 0;
    virtual std::unique_ptr<RHIPipelineLayout> createPipelineLayout(const PipelineLayoutDesc&) = 0;
    virtual std::unique_ptr<RHIRenderPipeline> createRenderPipeline(const RenderPipelineDesc&) = 0;
    virtual std::unique_ptr<RHIComputePipeline> createComputePipeline(const ComputePipelineDesc&) = 0;

    // Command encoding
    virtual std::unique_ptr<RHICommandEncoder> createCommandEncoder() = 0;

    // Synchronization
    virtual std::unique_ptr<RHIFence> createFence(bool signaled = false) = 0;
    virtual std::unique_ptr<RHISemaphore> createSemaphore() = 0;

    // Swapchain
    virtual std::unique_ptr<RHISwapchain> createSwapchain(const SwapchainDesc&) = 0;

    // Queue access
    virtual RHIQueue* getQueue(QueueType type) = 0;

    // Device operations
    virtual void waitIdle() = 0;
    virtual const RHICapabilities& getCapabilities() const = 0;
    virtual RHIBackendType getBackendType() const = 0;
};
```

### RHICommandBuffer.hpp - Command Recording

WebGPU-style encoder pattern:

```cpp
// Main command encoder
class RHICommandEncoder {
public:
    // Begin passes
    virtual std::unique_ptr<RHIRenderPassEncoder> beginRenderPass(const RenderPassDesc&) = 0;
    virtual std::unique_ptr<RHIComputePassEncoder> beginComputePass() = 0;

    // Copy operations
    virtual void copyBufferToBuffer(RHIBuffer* src, uint64_t srcOffset,
                                    RHIBuffer* dst, uint64_t dstOffset,
                                    uint64_t size) = 0;
    virtual void copyBufferToTexture(const BufferTextureCopyInfo&,
                                    const TextureCopyInfo&,
                                    const Extent3D&) = 0;

    // Finish encoding
    virtual std::unique_ptr<RHICommandBuffer> finish() = 0;
};

// Render pass encoder
class RHIRenderPassEncoder {
public:
    virtual void setPipeline(RHIRenderPipeline*) = 0;
    virtual void setBindGroup(uint32_t index, RHIBindGroup*) = 0;
    virtual void setVertexBuffer(uint32_t slot, RHIBuffer*, uint64_t offset = 0) = 0;
    virtual void setIndexBuffer(RHIBuffer*, IndexFormat, uint64_t offset = 0) = 0;
    virtual void setViewport(float x, float y, float w, float h, float minD = 0, float maxD = 1) = 0;
    virtual void setScissorRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                     uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                            uint32_t firstIndex = 0, int32_t baseVertex = 0,
                            uint32_t firstInstance = 0) = 0;
    virtual void end() = 0;
};
```

**Usage Example**:

```cpp
// Create encoder
auto encoder = device->createCommandEncoder();

// Begin render pass
RenderPassDesc passDesc{};
passDesc.colorAttachments = {
    {colorView, LoadOp::Clear, StoreOp::Store, {0.1f, 0.2f, 0.3f, 1.0f}}
};
passDesc.depthStencilAttachment = {depthView, LoadOp::Clear, StoreOp::Store, 1.0f};

auto renderPass = encoder->beginRenderPass(passDesc);
renderPass->setPipeline(pipeline.get());
renderPass->setBindGroup(0, bindGroup.get());
renderPass->setVertexBuffer(0, vertexBuffer.get());
renderPass->setIndexBuffer(indexBuffer.get(), IndexFormat::Uint32);
renderPass->setViewport(0, 0, width, height);
renderPass->setScissorRect(0, 0, width, height);
renderPass->drawIndexed(indexCount);
renderPass->end();

// Finish and submit
auto commandBuffer = encoder->finish();
queue->submit(commandBuffer.get());
```

---

## Backend Mapping

### Vulkan Backend Mapping

| RHI Concept | Vulkan Concept |
|-------------|----------------|
| `RHIDevice` | `VkDevice` + `VkPhysicalDevice` + `VkInstance` |
| `RHIQueue` | `VkQueue` |
| `RHIBuffer` | `VkBuffer` + `VkDeviceMemory` (via VMA) |
| `RHITexture` | `VkImage` + `VkDeviceMemory` |
| `RHITextureView` | `VkImageView` |
| `RHISampler` | `VkSampler` |
| `RHIShader` | `VkShaderModule` |
| `RHIBindGroupLayout` | `VkDescriptorSetLayout` |
| `RHIBindGroup` | `VkDescriptorSet` |
| `RHIPipelineLayout` | `VkPipelineLayout` |
| `RHIRenderPipeline` | `VkPipeline` (Graphics) |
| `RHIComputePipeline` | `VkPipeline` (Compute) |
| `RHICommandEncoder` | `VkCommandBuffer` (Recording) |
| `RHICommandBuffer` | `VkCommandBuffer` (Executable) |
| `RHIRenderPassEncoder` | `VkCommandBuffer` + `VkRenderPass` |
| `RHISwapchain` | `VkSwapchainKHR` |
| `RHIFence` | `VkFence` |
| `RHISemaphore` | `VkSemaphore` |

**Implementation Notes**:
- Use VMA (Vulkan Memory Allocator) for efficient memory management
- Descriptor sets map directly to bind groups
- Render pass compatibility handled internally
- Command buffer pools managed per-queue

### WebGPU Backend Mapping

| RHI Concept | WebGPU Concept |
|-------------|----------------|
| `RHIDevice` | `WGPUDevice` + `WGPUAdapter` + `WGPUInstance` |
| `RHIQueue` | `WGPUQueue` |
| `RHIBuffer` | `WGPUBuffer` |
| `RHITexture` | `WGPUTexture` |
| `RHITextureView` | `WGPUTextureView` |
| `RHISampler` | `WGPUSampler` |
| `RHIShader` | `WGPUShaderModule` (WGSL) |
| `RHIBindGroupLayout` | `WGPUBindGroupLayout` |
| `RHIBindGroup` | `WGPUBindGroup` |
| `RHIPipelineLayout` | `WGPUPipelineLayout` |
| `RHIRenderPipeline` | `WGPURenderPipeline` |
| `RHIComputePipeline` | `WGPUComputePipeline` |
| `RHICommandEncoder` | `WGPUCommandEncoder` |
| `RHICommandBuffer` | `WGPUCommandBuffer` |
| `RHIRenderPassEncoder` | `WGPURenderPassEncoder` |
| `RHISwapchain` | `WGPUSurface` + Configuration |

**Implementation Notes**:
- Nearly 1:1 mapping (RHI based on WebGPU)
- SPIR-V → WGSL conversion via Naga/Tint
- Handle async operations (buffer mapping)
- Single queue model (no separate compute/transfer)

### Direct3D 12 Backend Mapping

| RHI Concept | D3D12 Concept |
|-------------|---------------|
| `RHIDevice` | `ID3D12Device` + `IDXGIAdapter` + `IDXGIFactory` |
| `RHIQueue` | `ID3D12CommandQueue` |
| `RHIBuffer` | `ID3D12Resource` (Buffer) |
| `RHITexture` | `ID3D12Resource` (Texture) |
| `RHITextureView` | `D3D12_CPU_DESCRIPTOR_HANDLE` (SRV/UAV/RTV/DSV) |
| `RHISampler` | `D3D12_CPU_DESCRIPTOR_HANDLE` (Sampler) |
| `RHIShader` | `ID3DBlob` (DXIL bytecode) |
| `RHIBindGroupLayout` | Part of Root Signature |
| `RHIBindGroup` | Descriptor Table |
| `RHIPipelineLayout` | `ID3D12RootSignature` |
| `RHIRenderPipeline` | `ID3D12PipelineState` (Graphics) |
| `RHIComputePipeline` | `ID3D12PipelineState` (Compute) |
| `RHICommandEncoder` | `ID3D12GraphicsCommandList` |
| `RHISwapchain` | `IDXGISwapChain4` |
| `RHIFence` | `ID3D12Fence` |

**Implementation Notes**:
- Use D3D12MA for memory management
- Descriptor heap management required
- Root signature maps to bind group layouts
- PSO (Pipeline State Object) caching essential

### Metal Backend Mapping

| RHI Concept | Metal Concept |
|-------------|---------------|
| `RHIDevice` | `MTLDevice` |
| `RHIQueue` | `MTLCommandQueue` |
| `RHIBuffer` | `MTLBuffer` |
| `RHITexture` | `MTLTexture` |
| `RHISampler` | `MTLSamplerState` |
| `RHIShader` | `MTLLibrary` + `MTLFunction` |
| `RHIBindGroup` | Argument Buffer |
| `RHIPipelineLayout` | - (Metal has no separate layout) |
| `RHIRenderPipeline` | `MTLRenderPipelineState` |
| `RHIComputePipeline` | `MTLComputePipelineState` |
| `RHICommandEncoder` | `MTLCommandBuffer` |
| `RHIRenderPassEncoder` | `MTLRenderCommandEncoder` |
| `RHISwapchain` | `CAMetalLayer` |

**Implementation Notes**:
- Objective-C++ required (.mm files)
- Automatic resource tracking
- SPIR-V → MSL conversion via SPIRV-Cross
- MetalKit integration for window management

---

## Implementation Patterns

### Resource Creation Pattern

```cpp
// 1. Define descriptor
BufferDesc bufferDesc{};
bufferDesc.size = 1024 * sizeof(Vertex);
bufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
bufferDesc.label = "Vertex Buffer";

// 2. Create resource
auto buffer = device->createBuffer(bufferDesc);

// 3. Upload data (if needed)
buffer->write(vertexData, bufferDesc.size);
```

### Pipeline Creation Pattern

```cpp
// 1. Create shaders
auto vertShader = device->createShader({vertexShaderSource, ShaderStage::Vertex});
auto fragShader = device->createShader({fragmentShaderSource, ShaderStage::Fragment});

// 2. Create bind group layout
auto bindGroupLayout = device->createBindGroupLayout({
    {0, ShaderStage::Vertex, BindingType::UniformBuffer},
    {1, ShaderStage::Fragment, BindingType::SampledTexture},
    {2, ShaderStage::Fragment, BindingType::Sampler}
});

// 3. Create pipeline layout
auto pipelineLayout = device->createPipelineLayout({{bindGroupLayout.get()}});

// 4. Create pipeline
RenderPipelineDesc pipelineDesc{};
pipelineDesc.vertexShader = vertShader.get();
pipelineDesc.fragmentShader = fragShader.get();
pipelineDesc.layout = pipelineLayout.get();
pipelineDesc.vertex.buffers = {vertexBufferLayout};
pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
pipelineDesc.colorTargets = {{TextureFormat::BGRA8Unorm}};

auto pipeline = device->createRenderPipeline(pipelineDesc);
```

### Frame Rendering Pattern

```cpp
void renderFrame() {
    // 1. Acquire swapchain image
    auto swapchainView = swapchain->acquireNextImage();

    // 2. Create command encoder
    auto encoder = device->createCommandEncoder();

    // 3. Begin render pass
    RenderPassDesc passDesc{};
    passDesc.colorAttachments = {{swapchainView, LoadOp::Clear, StoreOp::Store}};
    auto renderPass = encoder->beginRenderPass(passDesc);

    // 4. Record commands
    renderPass->setPipeline(pipeline.get());
    renderPass->setBindGroup(0, bindGroup.get());
    renderPass->setVertexBuffer(0, vertexBuffer.get());
    renderPass->draw(vertexCount);
    renderPass->end();

    // 5. Finish encoding
    auto commandBuffer = encoder->finish();

    // 6. Submit and present
    queue->submit(commandBuffer.get());
    swapchain->present();
}
```

---

## Shader Cross-Compilation

### Strategy: SPIR-V as Intermediate Representation

```
Source Shader (GLSL/HLSL/Slang)
        ↓
    [Compile]
        ↓
    SPIR-V (IR)
        ↓
    ┌───┴───┬───────┬───────┐
    ↓       ↓       ↓       ↓
 Vulkan  D3D12   Metal  WebGPU
(native) (HLSL)  (MSL)  (WGSL)
```

### Tools

| Tool | Purpose | Input | Output |
|------|---------|-------|--------|
| **glslc** | GLSL → SPIR-V | GLSL | SPIR-V |
| **dxc** | HLSL → SPIR-V | HLSL | SPIR-V |
| **slangc** | Slang → Multi | Slang | SPIR-V/HLSL/MSL/WGSL |
| **SPIRV-Cross** | SPIR-V → Source | SPIR-V | HLSL/MSL |
| **Naga** (wgpu) | SPIR-V ↔ WGSL | SPIR-V | WGSL |
| **Tint** (Dawn) | SPIR-V ↔ WGSL | SPIR-V | WGSL |

### Recommended Workflow

**Option 1: SPIR-V as Source** (Vulkan-first)
```
GLSL/HLSL → [glslc/dxc] → SPIR-V → Deploy
                              ↓
                    [SPIRV-Cross/Naga/Tint]
                              ↓
                    HLSL/MSL/WGSL (runtime)
```

**Option 2: Slang as Source** (Recommended)
```
Slang → [slangc] → SPIR-V/HLSL/MSL/WGSL → Deploy
```

### Shader Loading Example

```cpp
// RHI shader creation (backend-agnostic)
ShaderSource vertexSource{ShaderLanguage::SPIRV, spirvBytecode, ShaderStage::Vertex};
auto vertShader = device->createShader({vertexSource});

// Backend handles conversion internally:
// - Vulkan: Uses SPIR-V directly
// - WebGPU: Converts SPIR-V → WGSL via Naga/Tint
// - D3D12: Converts SPIR-V → HLSL → DXIL via SPIRV-Cross + dxc
// - Metal: Converts SPIR-V → MSL via SPIRV-Cross
```

---

## Build System Configuration

### CMake Backend Selection

```cmake
# Backend options
option(RHI_VULKAN_SUPPORT "Enable Vulkan RHI backend" ON)
option(RHI_WEBGPU_SUPPORT "Enable WebGPU RHI backend" OFF)
option(RHI_D3D12_SUPPORT "Enable Direct3D 12 RHI backend" OFF)
option(RHI_METAL_SUPPORT "Enable Metal RHI backend" OFF)

# WebGPU implementation (Dawn or wgpu-native)
set(WEBGPU_IMPLEMENTATION "DAWN" CACHE STRING "WebGPU implementation")
set_property(CACHE WEBGPU_IMPLEMENTATION PROPERTY STRINGS "DAWN" "WGPU")

# Platform-specific defaults
if(EMSCRIPTEN)
    set(RHI_DEFAULT_BACKEND "WebGPU")
    set(RHI_WEBGPU_SUPPORT ON CACHE BOOL "" FORCE)
    set(RHI_VULKAN_SUPPORT OFF CACHE BOOL "" FORCE)
elseif(WIN32)
    set(RHI_DEFAULT_BACKEND "D3D12")
elseif(APPLE)
    set(RHI_DEFAULT_BACKEND "Metal")
else()
    set(RHI_DEFAULT_BACKEND "Vulkan")
endif()
```

### Backend Libraries

```cmake
# RHI Core (always built)
add_library(RHI STATIC src/rhi/RHIFactory.cpp)

# Vulkan Backend
if(RHI_VULKAN_SUPPORT)
    find_package(Vulkan REQUIRED)
    add_library(RHIVulkan STATIC
        src/rhi/vulkan/VulkanRHIDevice.cpp
        # ... other Vulkan sources
    )
    target_link_libraries(RHIVulkan PUBLIC Vulkan::Vulkan RHI)
    target_compile_definitions(RHIVulkan PUBLIC RHI_VULKAN_SUPPORT)

    # VMA
    FetchContent_Declare(VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.0.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    target_link_libraries(RHIVulkan PRIVATE VulkanMemoryAllocator)
endif()

# WebGPU Backend
if(RHI_WEBGPU_SUPPORT)
    add_library(RHIWebGPU STATIC
        src/rhi/webgpu/WebGPURHIDevice.cpp
        # ... other WebGPU sources
    )
    target_link_libraries(RHIWebGPU PUBLIC RHI)
    target_compile_definitions(RHIWebGPU PUBLIC RHI_WEBGPU_SUPPORT)

    if(EMSCRIPTEN)
        target_compile_options(RHIWebGPU PUBLIC -sUSE_WEBGPU=1)
        target_link_options(RHIWebGPU PUBLIC -sUSE_WEBGPU=1)
    else()
        if(WEBGPU_IMPLEMENTATION STREQUAL "DAWN")
            find_package(Dawn REQUIRED)
            target_link_libraries(RHIWebGPU PRIVATE dawn::webgpu_dawn)
        else()
            find_package(wgpu REQUIRED)
            target_link_libraries(RHIWebGPU PRIVATE wgpu::wgpu)
        endif()
    endif()
endif()

# Link all backends to RHI
if(RHI_VULKAN_SUPPORT)
    target_link_libraries(RHI PUBLIC RHIVulkan)
endif()
if(RHI_WEBGPU_SUPPORT)
    target_link_libraries(RHI PUBLIC RHIWebGPU)
endif()
```

### Build Examples

```bash
# Vulkan only (default)
cmake -B build -DRHI_VULKAN_SUPPORT=ON

# Vulkan + WebGPU (Dawn)
cmake -B build -DRHI_VULKAN_SUPPORT=ON -DRHI_WEBGPU_SUPPORT=ON -DWEBGPU_IMPLEMENTATION=DAWN

# WebAssembly (Emscripten)
emcmake cmake -B build-wasm
cmake --build build-wasm
# Output: MiniEngine.html, MiniEngine.js, MiniEngine.wasm

# Windows with D3D12
cmake -B build -DRHI_VULKAN_SUPPORT=ON -DRHI_D3D12_SUPPORT=ON

# macOS with Metal
cmake -B build -DRHI_VULKAN_SUPPORT=ON -DRHI_METAL_SUPPORT=ON
```

---

## Performance Optimization

### Zero-Cost Abstraction Strategies

#### 1. Inline Small Functions

```cpp
// Small getters should be inline
class RHIBuffer {
public:
    // This gets inlined by compiler
    inline uint64_t getSize() const { return size_; }

    // Virtual calls remain (necessary for polymorphism)
    virtual void* map() = 0;
};
```

#### 2. Batch Operations

```cpp
// Bad: Individual calls (multiple virtual dispatches)
for (auto& obj : objects) {
    renderPass->draw(obj.vertexCount);
}

// Good: Batch draw (single virtual dispatch)
std::vector<DrawParams> drawCalls;
for (auto& obj : objects) {
    drawCalls.push_back({obj.vertexCount, obj.instanceCount});
}
renderPass->drawBatch(drawCalls);
```

#### 3. Compile-Time Backend Selection (Optional)

For release builds, eliminate virtual calls:

```cpp
#ifdef RHI_STATIC_BACKEND_VULKAN
    // Direct type, no virtual calls
    using RHIDevice = VulkanRHIDevice;
    using RHIBuffer = VulkanRHIBuffer;
    // ... compiler can devirtualize everything
#else
    // Runtime polymorphism (debug/multi-backend builds)
    // ... virtual calls preserved
#endif
```

#### 4. Resource Pooling

```cpp
// Pool frequently created/destroyed resources
class CommandEncoderPool {
    std::vector<std::unique_ptr<RHICommandEncoder>> pool_;

public:
    RHICommandEncoder* acquire() {
        if (pool_.empty()) {
            return device->createCommandEncoder().release();
        }
        auto encoder = pool_.back().release();
        pool_.pop_back();
        return encoder;
    }

    void release(RHICommandEncoder* encoder) {
        pool_.push_back(std::unique_ptr<RHICommandEncoder>(encoder));
    }
};
```

### Performance Metrics

**Target**:
- Virtual function overhead: < 5%
- Memory overhead: < 1% (vtable pointers)
- Startup time: < 10% increase

**Actual (Phase 1)**:
- Compilation overhead: 0% (headers only, no runtime yet)
- Design validated as zero-cost in theory

---

## Reference Architectures

### Unreal Engine RHI

**Design**:
- Virtual interface + platform-specific implementations
- Medium abstraction level

**Backends**:
- Vulkan, D3D11, D3D12, Metal

**Lessons**:
- Extension interfaces for platform-specific features
- Shader cross-compilation infrastructure essential
- Descriptor management can be complex

### BGFX

**Design**:
- High-level abstraction (hides platform differences)
- C API with C++ internals

**Backends**:
- Vulkan, D3D9/11/12, Metal, OpenGL, WebGPU

**Lessons**:
- State caching can hide overhead
- Automatic resource transitions helpful
- Trade-off: simplicity vs. explicit control

### Diligent Engine

**Design**:
- Pure virtual interfaces
- Smart pointers for RAII

**Backends**:
- Vulkan, D3D11, D3D12, Metal, OpenGL, WebGPU

**Lessons**:
- Similar to our approach
- Well-documented, good reference
- Comprehensive but somewhat verbose

### WebGPU (wgpu / Dawn)

**Design**:
- Spec-based unified API
- C API (webgpu.h) + backend implementations

**Backends**:
- Vulkan, D3D12, Metal (internal)

**Lessons**:
- Our design is based on this
- Clean, modern API
- Explicit resource management
- Bind group model works well

**Mini-Engine Choice**: WebGPU-style API + Diligent-style RAII

---

## Leveraging Existing RAII Vulkan Code

### Strategic Reuse Pattern

The existing Vulkan RAII implementation (VulkanDevice, VulkanBuffer, VulkanImage, VulkanSwapchain, VulkanPipeline) represents a well-designed foundation that should be preserved and integrated into the RHI architecture. This section outlines the optimal strategy for leveraging this code.

### Current RAII Code Overview

**Existing Components**:
- `VulkanDevice` (352 lines): Instance, device, and surface management with RAII
- `VulkanBuffer` (60 lines): GPU buffer allocation and mapping with vk::raii
- `VulkanImage` (90 lines): Texture allocation and image view management
- `VulkanSwapchain` (100 lines): Swapchain and presentation logic
- `VulkanPipeline` (150 lines): Graphics pipeline creation and layout management
- `CommandManager` (70 lines): Command pool and buffer allocation
- `SyncManager` (60 lines): Synchronization primitives (fences, semaphores)

**Key Strengths**:
- ✅ Pure RAII pattern with automatic cleanup
- ✅ Error handling via exceptions
- ✅ vk::raii wrapper usage (memory-safe)
- ✅ Well-tested and stable (used in current production code)
- ✅ Clear separation of concerns
- ✅ Comprehensive accessor methods

### Wrapper Pattern Strategy

**Approach**: Wrap existing RAII code into RHI interfaces rather than rewriting

#### Benefits
1. **Reduced Risk**: Proven code, fewer bugs
2. **Faster Implementation**: 50-70% time savings
3. **Code Preservation**: Leverage existing quality
4. **Gradual Migration**: Can refactor incrementally
5. **Known Performance**: Baseline already established

#### Implementation Pattern

```cpp
// EXISTING RAII CODE (kept as-is)
class VulkanDevice {
    vk::raii::Instance instance;
    vk::raii::Device device;
    vk::raii::Queue queue;
    // ... existing implementation
};

// NEW RHI WRAPPER
class VulkanRHIDevice : public RHIDevice {
private:
    std::unique_ptr<VulkanDevice> m_vulkanDevice;  // Wrap existing code
    std::unique_ptr<VulkanRHIQueue> m_queue;

public:
    VulkanRHIDevice(GLFWwindow* window, bool enableValidation)
        : m_vulkanDevice(std::make_unique<VulkanDevice>(
            validationLayers, enableValidation))
    {
        m_vulkanDevice->createSurface(window);
        m_vulkanDevice->createLogicalDevice();
        m_queue = std::make_unique<VulkanRHIQueue>(
            m_vulkanDevice->getGraphicsQueue(), ...);
    }

    // Implement RHI interface by delegating to wrapped object
    std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) override {
        // Convert RHI desc → Vulkan flags
        vk::BufferUsageFlags usage = ToVkBufferUsage(desc.usage);
        vk::MemoryPropertyFlags memProps = ToVkMemoryProperties(desc.usage);

        // Create using existing VulkanBuffer wrapper
        auto buffer = std::make_unique<VulkanBuffer>(
            *m_vulkanDevice, desc.size, usage, memProps);

        // Wrap in RHI interface
        return std::make_unique<VulkanRHIBuffer>(std::move(buffer));
    }

    // ... other factory methods
};
```

### Two Implementation Strategies

#### Strategy A: Wrapper-Only (Minimal Changes) ✅ **Recommended for Phase 2**

**What**: Keep existing Vulkan code, add RHI wrapper layer on top

**Pros**:
- ✅ Minimal changes to existing code
- ✅ Fast implementation
- ✅ Easy to verify correctness
- ✅ Can run old code alongside new

**Cons**:
- ❌ Temporary dual maintenance (Phase 2-7)
- ⚠️ Slight code duplication

**Example**:
```cpp
// Phase 2: Wrapper layer added
VulkanRHIDevice wraps VulkanDevice
VulkanRHIBuffer wraps VulkanBuffer
VulkanRHISwapchain wraps VulkanSwapchain

// Application still uses old code during Phase 3-4
Renderer → VulkanDevice directly
```

#### Strategy B: Refactored Integration (Enhancement) ⏳ **Planned for Phase 3**

**What**: Refactor existing code to remove redundancies, then wrap

**Example Areas for Refactoring**:

```cpp
// BEFORE: Duplicate buffer creation logic
class VulkanBuffer {
    // Manual memory management
    vk::BufferUsageFlags usage;
    vk::MemoryPropertyFlags properties;
    // Manual allocation, deallocation
};

// AFTER: Enhanced with better abstraction
class VulkanBuffer {
    // Better encapsulation
    BufferDesc desc;  // Store RHI descriptor
    void createFromRHIDesc(const BufferDesc& desc);
};
```

### Memory Management Enhancement (VMA Integration)

**Current Situation**:
- Existing VulkanBuffer: Manual memory allocation via vk::raii::DeviceMemory
- New VulkanRHIBuffer: Uses VMA (Vulkan Memory Allocator)

**Problem**: Dual memory management approaches during migration

**Solution: Hybrid Approach for Phase 2-3**

```cpp
// Phase 2: Two parallel implementations
class VulkanBuffer {
    // Existing: Manual memory management
    vk::raii::DeviceMemory memory;
};

class VulkanRHIBuffer {
    // New: VMA-based management
    VmaAllocation allocation;
};

// Phase 3: Gradually migrate VulkanBuffer to use VMA
class VulkanBuffer {
    VmaAllocation allocation;  // ← Migrated to VMA
    // Rest of implementation stays the same
};

// Phase 7: Consolidate into single implementation
class VulkanBuffer {
    // Unified VMA-based approach
    VmaAllocation allocation;
};
```

### Type Conversion Utilities (Critical)

**Need**: Convert between RHI types and Vulkan types

**Implementation Locations**:
- Primary: `src/rhi/vulkan/VulkanCommon.hpp/cpp`
- Usage: All Vulkan backend implementations

**Key Conversions Needed**:

```cpp
// In VulkanCommon.hpp

// Buffer usage conversions
inline vk::BufferUsageFlags ToVkBufferUsage(rhi::BufferUsage usage) {
    vk::BufferUsageFlags result;
    if (hasFlag(usage, rhi::BufferUsage::Vertex))
        result |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (hasFlag(usage, rhi::BufferUsage::Index))
        result |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (hasFlag(usage, rhi::BufferUsage::Uniform))
        result |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (hasFlag(usage, rhi::BufferUsage::CopySrc))
        result |= vk::BufferUsageFlagBits::eTransferSrc;
    if (hasFlag(usage, rhi::BufferUsage::CopyDst))
        result |= vk::BufferUsageFlagBits::eTransferDst;
    return result;
}

// Texture format conversions
inline vk::Format ToVkFormat(rhi::TextureFormat format) {
    switch (format) {
        case rhi::TextureFormat::RGBA8Unorm: return vk::Format::eR8G8B8A8Unorm;
        case rhi::TextureFormat::RGBA8UnormSrgb: return vk::Format::eR8G8B8A8Srgb;
        case rhi::TextureFormat::Depth24Plus: return vk::Format::eD24UnormS8Uint;
        case rhi::TextureFormat::Depth32Float: return vk::Format::eD32Sfloat;
        // ...
    }
}

// Compare operator conversions
inline vk::CompareOp ToVkCompareOp(rhi::CompareOp op) {
    switch (op) {
        case rhi::CompareOp::Never: return vk::CompareOp::eNever;
        case rhi::CompareOp::Less: return vk::CompareOp::eLess;
        case rhi::CompareOp::Equal: return vk::CompareOp::eEqual;
        case rhi::CompareOp::LessOrEqual: return vk::CompareOp::eLessOrEqual;
        // ...
    }
}

// Reverse conversions (Vulkan → RHI)
inline rhi::TextureFormat FromVkFormat(vk::Format format) {
    switch (format) {
        case vk::Format::eR8G8B8A8Unorm: return rhi::TextureFormat::RGBA8Unorm;
        case vk::Format::eD32Sfloat: return rhi::TextureFormat::Depth32Float;
        // ...
    }
}
```

**Existing Utilities to Preserve**:
- VulkanDevice::findMemoryType()
- VulkanDevice::findSupportedFormat()
- These should be wrapped, not reimplemented

### Phase-by-Phase Migration Plan

#### Phase 1 (Completed) ✅
- Define RHI interfaces (no Vulkan code)
- No changes to existing RAII code

#### Phase 2 (Current) ✅
- Create wrapper implementations (VulkanRHIDevice, etc.)
- Keep existing code untouched
- Add type conversions in VulkanCommon

#### Phase 3 (Planned)
- Create RHI factory pattern
- Dual-use: Both old and new code can work
- Applications start switching to RHI interfaces gradually

#### Phase 4-6 (Planned)
- Renderer, ResourceManager, SceneManager migrate to RHI
- Existing Vulkan code still available but deprecated
- No changes to Vulkan code (only usage changes)

#### Phase 7 (Planned)
- Complete unit test coverage
- Verify 100% feature parity
- Mark old code as "internal" (src/core/vulkan_internal/)

#### Post-Phase 7 (Future)
- When confident, optionally:
  - Option A: Keep old code as "internal backend" (safest)
  - Option B: Merge best practices back into unified implementation
  - Option C: Full removal (requires extensive testing)

### Code Quality Considerations

**Maintain Existing Patterns**:
```cpp
// ✅ Keep existing RAII patterns
std::unique_ptr<VulkanBuffer> buffer = std::make_unique<VulkanBuffer>(...);

// ✅ Keep existing error handling
if (result != vk::Result::eSuccess) {
    throw std::runtime_error("Buffer creation failed");
}

// ✅ Keep existing utility functions
device.findMemoryType(...);
device.findSupportedFormat(...);
```

**Avoid Introducing**:
```cpp
// ❌ Raw pointers
VulkanBuffer* buffer = new VulkanBuffer(...);

// ❌ Different error handling
if (result == VK_ERROR_...) { /* ... */ }  // Inconsistent with RAII

// ❌ Reimplementing existing utilities
// Use existing VulkanDevice methods instead
```

### Documentation for Developers

**When Wrapping Old Code**:
1. Preserve original method signatures
2. Keep comments explaining Vulkan semantics
3. Document why wrapper exists (code reuse, safety)
4. Link to original implementation

**Example**:
```cpp
class VulkanRHIBuffer : public RHIBuffer {
private:
    // Wraps src/resources/VulkanBuffer for VMA integration
    // See Phase 2 notes: Wrapper Pattern Strategy
    std::unique_ptr<VulkanBuffer> m_buffer;
};
```

### Summary: RAII Code Reuse Benefits

| Aspect | Benefit | Impact |
|--------|---------|--------|
| **Development Time** | 50-70% faster backend | Phase 2 completes on schedule |
| **Code Quality** | Proven, tested code | Lower risk, fewer bugs |
| **Maintenance** | Existing patterns preserved | Easier to understand |
| **Performance** | Known baseline | No unexpected regressions |
| **Migration Path** | Gradual, not disruptive | Can keep old code available |
| **Documentation** | Existing docs still relevant | Faster onboarding |

---

## Extension Interfaces (Advanced)

For backend-specific features not in RHI core:

```cpp
// Core RHI interface
class RHICommandBuffer {
public:
    virtual void draw(uint32_t vertexCount) = 0;
};

// Vulkan-specific extension
class VulkanRHICommandBufferExt {
public:
    virtual vk::CommandBuffer getNativeCommandBuffer() = 0;
};

// Vulkan implementation
class VulkanRHICommandBuffer
    : public RHICommandBuffer
    , public VulkanRHICommandBufferExt
{
public:
    void draw(uint32_t vertexCount) override;
    vk::CommandBuffer getNativeCommandBuffer() override;
};

// Usage
void useVulkanFeature(RHICommandBuffer* cmd) {
    if (auto* vulkanCmd = dynamic_cast<VulkanRHICommandBufferExt*>(cmd)) {
        vk::CommandBuffer nativeCmd = vulkanCmd->getNativeCommandBuffer();
        // Use Vulkan-specific features
    }
}
```

---

## Conclusion

This guide provides the technical foundation for implementing the RHI architecture. Key principles:

1. **Follow WebGPU model** for API design
2. **Use pure virtual interfaces** for flexibility
3. **Maintain RAII** for safety
4. **Think in terms of bind groups** for resource binding
5. **Use SPIR-V as IR** for shader cross-compilation

For project management aspects (timeline, risks, acceptance criteria), see **RHI_MIGRATION_PRD.md**.

For implementation progress and results, see **PHASE1_SUMMARY.md** and subsequent phase summaries.

---

**Document Version**: 1.0
**Last Updated**: 2025-12-19
**Related Documents**:
- RHI_MIGRATION_PRD.md - Project requirements and management
- PHASE1_SUMMARY.md - Phase 1 completion summary
- RHI source code - src/rhi/*.hpp

**END OF GUIDE**
