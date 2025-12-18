# Layered Architecture to RHI Architecture Refactoring Strategy

## Overview

This document establishes a strategy for transitioning Mini-Engine's current Layered Architecture to an RHI (Render Hardware Interface) architecture.

### Goals

- **Multi-Backend Support**: Enable support for additional backends beyond Vulkan, such as Direct3D 12, Metal, etc.
- **Platform Independence**: Ensure upper layers don't depend on specific graphics APIs through abstraction
- **Extensibility**: Allow new graphics API backends to be added in a plugin-like manner
- **Performance**: Minimize abstraction overhead, aiming for zero-cost abstraction

### Current Architecture (Layered Architecture)

```
Application Layer
├── Application (window, input, main loop)
└── ImGuiManager (UI overlay)

Rendering Layer
└── Renderer (orchestrates managers + owns rendering components)
    ├── Uses: ResourceManager, SceneManager
    └── Owns: Swapchain, Pipeline, SyncManager

Scene/Resource Layer
├── ResourceManager (asset loading, caching)
├── SceneManager (scene graph)
├── Mesh, Camera
└── Loaders (OBJ, FDF)

Core Layer
├── VulkanDevice
├── CommandManager
├── VulkanBuffer
└── VulkanImage
```

**Problems**:
- Tightly coupled to Vulkan (VulkanDevice, VulkanBuffer, VulkanImage, etc.)
- Switching to other graphics APIs requires complete code rewrite
- Difficult to apply platform-specific optimizations

---

## Target Architecture (RHI Architecture)

```
Application Layer
├── Application
└── ImGuiManager

High-Level Rendering Layer
└── Renderer (API independent)
    ├── Uses: ResourceManager, SceneManager
    └── Uses: RHI Interface (abstraction layer)

RHI Abstraction Layer (Platform-Agnostic)
├── RHIDevice (abstract interface)
├── RHIBuffer (abstract interface)
├── RHITexture (abstract interface)
├── RHIShader (abstract interface)
├── RHIPipeline (abstract interface)
├── RHICommandBuffer (abstract interface)
├── RHISwapchain (abstract interface)
└── RHIFactory (backend creation factory)

Backend Implementation Layer (Platform-Specific)
├── Vulkan Backend
│   ├── VulkanRHIDevice
│   ├── VulkanRHIBuffer
│   ├── VulkanRHITexture
│   ├── VulkanRHIShader
│   ├── VulkanRHIPipeline
│   ├── VulkanRHICommandBuffer
│   └── VulkanRHISwapchain
├── D3D12 Backend (future)
│   └── ...
└── Metal Backend (future)
    └── ...

Scene/Resource Layer
├── ResourceManager (uses RHI)
├── SceneManager
├── Mesh, Camera
└── Loaders
```

---

## Refactoring Phases

### Phase 1: RHI Interface Design

**Goal**: Define platform-independent abstract interfaces

**Work Content**:
1. Create `src/rhi/` directory
2. Define core RHI interfaces:
   - `RHIDevice.hpp` - Device abstraction
   - `RHIQueue.hpp` - Queue management abstraction (Graphics/Compute/Transfer)
   - `RHIBuffer.hpp` - Buffer abstraction
   - `RHITexture.hpp` - Texture/image abstraction
   - `RHISampler.hpp` - Sampler abstraction
   - `RHIShader.hpp` - Shader module abstraction
   - `RHIPipeline.hpp` - Graphics/compute pipeline abstraction
   - `RHIRenderPass.hpp` - Render pass and framebuffer abstraction
   - `RHIBindGroup.hpp` - Resource binding abstraction (Descriptor Set/Bind Group)
   - `RHICommandBuffer.hpp` - Command buffer abstraction
   - `RHISwapchain.hpp` - Swapchain abstraction
   - `RHISync.hpp` - Synchronization primitives (Fence, Semaphore)
   - `RHITypes.hpp` - Common enumerations and structure definitions
   - `RHICapabilities.hpp` - Feature query interface

**Design Principles**:
- Pure virtual interface (virtual function-based)
- Include only minimal common functionality
- Provide platform-specific extensions through separate interfaces
- Maintain RAII pattern (automatic resource cleanup)
- **Reference WebGPU-style API** (modern and cross-platform friendly)

**Core Interface Design**:

```cpp
// rhi/RHITypes.hpp - Common type definitions
enum class RHIBackendType {
    Vulkan,
    WebGPU,
    D3D12,
    Metal
};

enum class QueueType {
    Graphics,
    Compute,
    Transfer
};

enum class BufferUsage : uint32_t {
    None           = 0,
    Vertex         = 1 << 0,
    Index          = 1 << 1,
    Uniform        = 1 << 2,
    Storage        = 1 << 3,
    CopySrc        = 1 << 4,
    CopyDst        = 1 << 5,
    Indirect       = 1 << 6,
    MapRead        = 1 << 7,
    MapWrite       = 1 << 8
};

enum class TextureUsage : uint32_t {
    None           = 0,
    Sampled        = 1 << 0,
    Storage        = 1 << 1,
    RenderTarget   = 1 << 2,
    DepthStencil   = 1 << 3,
    CopySrc        = 1 << 4,
    CopyDst        = 1 << 5
};

enum class TextureFormat {
    Undefined,
    // 8-bit
    R8Unorm, R8Snorm, R8Uint, R8Sint,
    // 16-bit
    R16Uint, R16Sint, R16Float,
    RG8Unorm, RG8Snorm, RG8Uint, RG8Sint,
    // 32-bit
    R32Uint, R32Sint, R32Float,
    RG16Uint, RG16Sint, RG16Float,
    RGBA8Unorm, RGBA8UnormSrgb, RGBA8Snorm, RGBA8Uint, RGBA8Sint,
    BGRA8Unorm, BGRA8UnormSrgb,
    // 64-bit
    RG32Uint, RG32Sint, RG32Float,
    RGBA16Uint, RGBA16Sint, RGBA16Float,
    // 128-bit
    RGBA32Uint, RGBA32Sint, RGBA32Float,
    // Depth/Stencil
    Depth16Unorm, Depth24Plus, Depth24PlusStencil8, Depth32Float
};

enum class ShaderStage : uint32_t {
    None     = 0,
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
    All      = Vertex | Fragment | Compute
};

struct Extent3D {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
};

// rhi/RHIDevice.hpp
class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    // Resource creation
    virtual std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) = 0;
    virtual std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) = 0;
    virtual std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) = 0;
    virtual std::unique_ptr<RHIShader> createShader(const ShaderDesc& desc) = 0;

    // Pipeline creation
    virtual std::unique_ptr<RHIBindGroupLayout> createBindGroupLayout(
        const BindGroupLayoutDesc& desc) = 0;
    virtual std::unique_ptr<RHIBindGroup> createBindGroup(
        const BindGroupDesc& desc) = 0;
    virtual std::unique_ptr<RHIPipelineLayout> createPipelineLayout(
        const PipelineLayoutDesc& desc) = 0;
    virtual std::unique_ptr<RHIRenderPipeline> createRenderPipeline(
        const RenderPipelineDesc& desc) = 0;
    virtual std::unique_ptr<RHIComputePipeline> createComputePipeline(
        const ComputePipelineDesc& desc) = 0;

    // Command encoding
    virtual std::unique_ptr<RHICommandEncoder> createCommandEncoder() = 0;

    // Queue access
    virtual RHIQueue* getQueue(QueueType type) = 0;

    // Synchronization
    virtual void waitIdle() = 0;

    // Feature query
    virtual const RHICapabilities& getCapabilities() const = 0;
    virtual RHIBackendType getBackendType() const = 0;
};

// rhi/RHIQueue.hpp
class RHIQueue {
public:
    virtual ~RHIQueue() = default;

    virtual void submit(const std::vector<RHICommandBuffer*>& commandBuffers,
                       RHIFence* signalFence = nullptr) = 0;
    virtual void submit(RHICommandBuffer* commandBuffer,
                       RHIFence* signalFence = nullptr) = 0;

    virtual void waitIdle() = 0;

    // Presentation (Graphics queue only)
    virtual void present(RHISwapchain* swapchain) = 0;
};

// rhi/RHIBuffer.hpp
struct BufferDesc {
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::None;
    bool mappedAtCreation = false;
    const char* label = nullptr;
};

class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;

    virtual void* map() = 0;
    virtual void* mapRange(uint64_t offset, uint64_t size) = 0;
    virtual void unmap() = 0;
    virtual void write(const void* data, uint64_t size, uint64_t offset = 0) = 0;

    virtual uint64_t getSize() const = 0;
    virtual BufferUsage getUsage() const = 0;
};

// rhi/RHITexture.hpp
struct TextureDesc {
    Extent3D size;
    uint32_t mipLevelCount = 1;
    uint32_t sampleCount = 1;
    TextureFormat format = TextureFormat::RGBA8Unorm;
    TextureUsage usage = TextureUsage::Sampled;
    const char* label = nullptr;
};

class RHITexture {
public:
    virtual ~RHITexture() = default;

    virtual std::unique_ptr<RHITextureView> createView(
        const TextureViewDesc& desc) = 0;

    virtual Extent3D getSize() const = 0;
    virtual TextureFormat getFormat() const = 0;
    virtual uint32_t getMipLevelCount() const = 0;
};

// rhi/RHIBindGroup.hpp (WebGPU style)
struct BindGroupEntry {
    uint32_t binding;
    RHIBuffer* buffer = nullptr;      // Buffer binding
    uint64_t offset = 0;
    uint64_t size = 0;
    RHISampler* sampler = nullptr;    // Sampler binding
    RHITextureView* textureView = nullptr;  // Texture binding
};

struct BindGroupDesc {
    RHIBindGroupLayout* layout;
    std::vector<BindGroupEntry> entries;
    const char* label = nullptr;
};

class RHIBindGroup {
public:
    virtual ~RHIBindGroup() = default;
};

// rhi/RHICommandBuffer.hpp
class RHICommandEncoder {
public:
    virtual ~RHICommandEncoder() = default;

    // Begin render pass
    virtual std::unique_ptr<RHIRenderPassEncoder> beginRenderPass(
        const RenderPassDesc& desc) = 0;

    // Begin compute pass
    virtual std::unique_ptr<RHIComputePassEncoder> beginComputePass() = 0;

    // Copy operations
    virtual void copyBufferToBuffer(
        RHIBuffer* src, uint64_t srcOffset,
        RHIBuffer* dst, uint64_t dstOffset,
        uint64_t size) = 0;

    virtual void copyBufferToTexture(
        const BufferTextureCopyInfo& src,
        const TextureCopyInfo& dst,
        const Extent3D& copySize) = 0;

    virtual void copyTextureToBuffer(
        const TextureCopyInfo& src,
        const BufferTextureCopyInfo& dst,
        const Extent3D& copySize) = 0;

    // Finish command buffer
    virtual std::unique_ptr<RHICommandBuffer> finish() = 0;
};

class RHIRenderPassEncoder {
public:
    virtual ~RHIRenderPassEncoder() = default;

    virtual void setPipeline(RHIRenderPipeline* pipeline) = 0;
    virtual void setBindGroup(uint32_t index, RHIBindGroup* bindGroup,
                             const std::vector<uint32_t>& dynamicOffsets = {}) = 0;

    virtual void setVertexBuffer(uint32_t slot, RHIBuffer* buffer,
                                uint64_t offset = 0) = 0;
    virtual void setIndexBuffer(RHIBuffer* buffer, IndexFormat format,
                               uint64_t offset = 0) = 0;

    virtual void setViewport(float x, float y, float width, float height,
                            float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
    virtual void setScissorRect(uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                     uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                            uint32_t firstIndex = 0, int32_t baseVertex = 0,
                            uint32_t firstInstance = 0) = 0;

    virtual void end() = 0;
};

// rhi/RHISync.hpp
class RHIFence {
public:
    virtual ~RHIFence() = default;

    virtual void wait(uint64_t timeout = UINT64_MAX) = 0;
    virtual bool isSignaled() const = 0;
    virtual void reset() = 0;
};

class RHISemaphore {
public:
    virtual ~RHISemaphore() = default;
    // Semaphores are used for GPU-to-GPU synchronization, cannot be directly manipulated by CPU
};

// rhi/RHICapabilities.hpp
struct RHILimits {
    uint32_t maxTextureDimension2D = 8192;
    uint32_t maxTextureArrayLayers = 256;
    uint32_t maxBindGroups = 4;
    uint32_t maxBindingsPerBindGroup = 1000;
    uint32_t maxUniformBufferBindingSize = 65536;
    uint32_t maxStorageBufferBindingSize = 134217728;
    uint32_t maxVertexBuffers = 8;
    uint32_t maxVertexAttributes = 16;
    uint32_t maxColorAttachments = 8;
    uint32_t maxComputeWorkgroupSizeX = 256;
    uint32_t maxComputeWorkgroupSizeY = 256;
    uint32_t maxComputeWorkgroupSizeZ = 64;
};

struct RHIFeatures {
    bool depthClipControl = false;
    bool depth32FloatStencil8 = false;
    bool timestampQuery = false;
    bool textureCompressionBC = false;
    bool textureCompressionETC2 = false;
    bool textureCompressionASTC = false;
    bool indirectFirstInstance = false;
    bool shaderFloat16 = false;
    bool rayTracing = false;
    bool meshShaders = false;
};

class RHICapabilities {
public:
    virtual ~RHICapabilities() = default;

    virtual const RHILimits& getLimits() const = 0;
    virtual const RHIFeatures& getFeatures() const = 0;
    virtual bool isFormatSupported(TextureFormat format, TextureUsage usage) const = 0;
};
```

**Expected Result**:
- 14 header files created in `src/rhi/` directory
- Modern API design in WebGPU style
- Platform-independent interface completed

---

### Phase 2: Vulkan Backend Implementation

**Goal**: Convert existing Vulkan code to RHI interface implementations

**Work Content**:
1. Create `src/rhi/vulkan/` directory
2. Write Vulkan implementations for each RHI interface:
   - `VulkanRHIDevice` - Wraps VulkanDevice
   - `VulkanRHIQueue` - Vulkan Queue management
   - `VulkanRHIBuffer` - Wraps VulkanBuffer
   - `VulkanRHITexture` - Wraps VulkanImage
   - `VulkanRHISampler` - Sampler management
   - `VulkanRHIShader` - Shader module management
   - `VulkanRHIBindGroup` - Wraps Descriptor Set
   - `VulkanRHIPipeline` - Wraps VulkanPipeline
   - `VulkanRHICommandEncoder` - Command Buffer encoding
   - `VulkanRHIRenderPassEncoder` - Render pass encoding
   - `VulkanRHISwapchain` - Wraps VulkanSwapchain
   - `VulkanRHIFence/Semaphore` - Synchronization primitives

**Vulkan-RHI Mapping Table**:

| RHI Concept | Vulkan Concept |
|---------|-------------|
| `RHIDevice` | `VkDevice` + `VkPhysicalDevice` + `VkInstance` |
| `RHIQueue` | `VkQueue` |
| `RHIBuffer` | `VkBuffer` + `VkDeviceMemory` |
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

**Migration Strategy**:
- Move existing classes into RHI implementation internals
- Expose public API through RHI interfaces
- Separate Vulkan-specific features into extension interfaces

**Example Code**:
```cpp
// rhi/vulkan/VulkanRHIDevice.hpp
class VulkanRHIDevice : public RHIDevice {
public:
    VulkanRHIDevice(const DeviceCreateInfo& createInfo);
    ~VulkanRHIDevice() override;

    // RHIDevice interface implementation
    std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) override;
    std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) override;
    std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) override;
    std::unique_ptr<RHIShader> createShader(const ShaderDesc& desc) override;
    std::unique_ptr<RHIBindGroupLayout> createBindGroupLayout(
        const BindGroupLayoutDesc& desc) override;
    std::unique_ptr<RHIBindGroup> createBindGroup(const BindGroupDesc& desc) override;
    std::unique_ptr<RHIPipelineLayout> createPipelineLayout(
        const PipelineLayoutDesc& desc) override;
    std::unique_ptr<RHIRenderPipeline> createRenderPipeline(
        const RenderPipelineDesc& desc) override;
    std::unique_ptr<RHICommandEncoder> createCommandEncoder() override;
    RHIQueue* getQueue(QueueType type) override;
    void waitIdle() override;
    const RHICapabilities& getCapabilities() const override;
    RHIBackendType getBackendType() const override { return RHIBackendType::Vulkan; }

    // Vulkan-specific extensions
    vk::Device getNativeDevice() const { return device_; }
    vk::PhysicalDevice getPhysicalDevice() const { return physicalDevice_; }
    vk::Instance getInstance() const { return instance_; }
    VmaAllocator getAllocator() const { return allocator_; }

private:
    vk::Instance instance_;
    vk::PhysicalDevice physicalDevice_;
    vk::Device device_;
    VmaAllocator allocator_;  // Recommended to use VMA (Vulkan Memory Allocator)

    std::unique_ptr<VulkanRHIQueue> graphicsQueue_;
    std::unique_ptr<VulkanRHIQueue> computeQueue_;
    std::unique_ptr<VulkanRHIQueue> transferQueue_;

    VulkanRHICapabilities capabilities_;
    vk::DescriptorPool descriptorPool_;
};

// rhi/vulkan/VulkanRHIBuffer.hpp
class VulkanRHIBuffer : public RHIBuffer {
public:
    VulkanRHIBuffer(VulkanRHIDevice& device, const BufferDesc& desc);
    ~VulkanRHIBuffer() override;

    void* map() override;
    void* mapRange(uint64_t offset, uint64_t size) override;
    void unmap() override;
    void write(const void* data, uint64_t size, uint64_t offset = 0) override;
    uint64_t getSize() const override { return size_; }
    BufferUsage getUsage() const override { return usage_; }

    // Vulkan-specific
    vk::Buffer getNativeBuffer() const { return buffer_; }

private:
    VulkanRHIDevice& device_;
    vk::Buffer buffer_;
    VmaAllocation allocation_;
    uint64_t size_;
    BufferUsage usage_;
    void* mappedPtr_ = nullptr;
};

// rhi/vulkan/VulkanRHICommandEncoder.hpp
class VulkanRHICommandEncoder : public RHICommandEncoder {
public:
    VulkanRHICommandEncoder(VulkanRHIDevice& device);
    ~VulkanRHICommandEncoder() override;

    std::unique_ptr<RHIRenderPassEncoder> beginRenderPass(
        const RenderPassDesc& desc) override;
    std::unique_ptr<RHIComputePassEncoder> beginComputePass() override;

    void copyBufferToBuffer(RHIBuffer* src, uint64_t srcOffset,
                           RHIBuffer* dst, uint64_t dstOffset,
                           uint64_t size) override;
    void copyBufferToTexture(const BufferTextureCopyInfo& src,
                            const TextureCopyInfo& dst,
                            const Extent3D& copySize) override;

    std::unique_ptr<RHICommandBuffer> finish() override;

private:
    VulkanRHIDevice& device_;
    vk::CommandBuffer commandBuffer_;
    vk::CommandPool commandPool_;
    bool isRecording_ = false;
};
```

**Expected Result**:
- Existing Vulkan code wrapped in RHI implementations
- 12 implementation files created in `src/rhi/vulkan/` directory
- All existing functionality maintained

---

### Phase 3: RHI Factory Pattern Implementation

**Goal**: Implement factory to enable runtime backend selection

**Work Content**:
1. Write `RHIFactory.hpp/cpp`
2. Define backend type enumeration (Vulkan, WebGPU, D3D12, Metal)
3. Create RHIDevice through factory methods
4. Support compile-time backend selection (#ifdef)
5. Adapter (GPU) enumeration functionality

**Example Code**:
```cpp
// rhi/RHIFactory.hpp
struct AdapterInfo {
    std::string name;
    std::string vendor;
    uint64_t dedicatedVideoMemory;
    RHIBackendType backendType;
    bool isDiscreteGPU;
    bool isIntegratedGPU;
};

struct DeviceCreateInfo {
    RHIBackendType preferredBackend = RHIBackendType::Vulkan;
    bool enableValidation = false;
    bool preferDiscreteGPU = true;
    std::vector<const char*> requiredExtensions;
    void* windowHandle = nullptr;  // GLFW window, HWND, etc.
    const char* applicationName = "Mini-Engine";
};

class RHIFactory {
public:
    // List of available backends
    static std::vector<RHIBackendType> getAvailableBackends();

    // Enumerate adapters (GPUs)
    static std::vector<AdapterInfo> enumerateAdapters(RHIBackendType backend);

    // Create device
    static std::unique_ptr<RHIDevice> createDevice(const DeviceCreateInfo& createInfo);

    // Select default backend (per platform)
    static RHIBackendType getDefaultBackend();

    // Check backend support
    static bool isBackendSupported(RHIBackendType backend);

    // Create swapchain (after device creation)
    static std::unique_ptr<RHISwapchain> createSwapchain(
        RHIDevice* device,
        const SwapchainDesc& desc);
};

// rhi/RHIFactory.cpp
std::vector<RHIBackendType> RHIFactory::getAvailableBackends() {
    std::vector<RHIBackendType> backends;

    #ifdef RHI_VULKAN_SUPPORT
    backends.push_back(RHIBackendType::Vulkan);
    #endif

    #ifdef RHI_WEBGPU_SUPPORT
    backends.push_back(RHIBackendType::WebGPU);
    #endif

    #ifdef RHI_D3D12_SUPPORT
    backends.push_back(RHIBackendType::D3D12);
    #endif

    #ifdef RHI_METAL_SUPPORT
    backends.push_back(RHIBackendType::Metal);
    #endif

    return backends;
}

std::unique_ptr<RHIDevice> RHIFactory::createDevice(const DeviceCreateInfo& createInfo) {
    RHIBackendType backend = createInfo.preferredBackend;

    // Use default backend if requested backend is not supported
    if (!isBackendSupported(backend)) {
        backend = getDefaultBackend();
    }

    switch (backend) {
        case RHIBackendType::Vulkan:
            #ifdef RHI_VULKAN_SUPPORT
            return std::make_unique<VulkanRHIDevice>(createInfo);
            #else
            throw std::runtime_error("Vulkan backend not supported in this build");
            #endif

        case RHIBackendType::WebGPU:
            #ifdef RHI_WEBGPU_SUPPORT
            return std::make_unique<WebGPURHIDevice>(createInfo);
            #else
            throw std::runtime_error("WebGPU backend not supported in this build");
            #endif

        case RHIBackendType::D3D12:
            #ifdef RHI_D3D12_SUPPORT
            return std::make_unique<D3D12RHIDevice>(createInfo);
            #else
            throw std::runtime_error("D3D12 backend not supported in this build");
            #endif

        case RHIBackendType::Metal:
            #ifdef RHI_METAL_SUPPORT
            return std::make_unique<MetalRHIDevice>(createInfo);
            #else
            throw std::runtime_error("Metal backend not supported in this build");
            #endif

        default:
            throw std::runtime_error("Unknown RHI backend");
    }
}

RHIBackendType RHIFactory::getDefaultBackend() {
    #if defined(__EMSCRIPTEN__)
        return RHIBackendType::WebGPU;  // WebAssembly environment
    #elif defined(_WIN32)
        #ifdef RHI_D3D12_SUPPORT
        return RHIBackendType::D3D12;
        #else
        return RHIBackendType::Vulkan;
        #endif
    #elif defined(__APPLE__)
        #ifdef RHI_METAL_SUPPORT
        return RHIBackendType::Metal;
        #else
        return RHIBackendType::Vulkan;  // MoltenVK
        #endif
    #else  // Linux, etc.
        return RHIBackendType::Vulkan;
    #endif
}
```

**Usage Example**:
```cpp
// Application initialization
void Application::initialize() {
    // Print available backends
    auto backends = RHIFactory::getAvailableBackends();
    for (auto backend : backends) {
        std::cout << "Available: " << getBackendName(backend) << std::endl;
    }

    // Create device
    DeviceCreateInfo createInfo{};
    createInfo.preferredBackend = RHIBackendType::Vulkan;
    createInfo.enableValidation = true;
    createInfo.windowHandle = glfwGetWin32Window(window_);  // or X11/Wayland

    device_ = RHIFactory::createDevice(createInfo);

    // Create swapchain
    SwapchainDesc swapchainDesc{};
    swapchainDesc.width = 1280;
    swapchainDesc.height = 720;
    swapchainDesc.format = TextureFormat::BGRA8Unorm;
    swapchainDesc.presentMode = PresentMode::Mailbox;

    swapchain_ = RHIFactory::createSwapchain(device_.get(), swapchainDesc);
}
```

**Expected Result**:
- Backends can be swapped like plugins
- Platform-specific default backend automatic selection
- GPU enumeration and selection functionality support

---

### Phase 4: Renderer Layer RHI Migration

**Goal**: Convert Renderer to use only RHI interfaces

**Work Content**:
1. Refactor `Renderer.hpp/cpp`:
   - Change `VulkanDevice*` → `RHIDevice*`
   - Change `VulkanBuffer*` → `RHIBuffer*`
   - Change `VulkanImage*` → `RHITexture*`
   - Change `VulkanPipeline*` → `RHIPipeline*`
2. Remove or conditionally compile Vulkan-specific code
3. Replace with RHI abstraction

**Before**:
```cpp
class Renderer {
private:
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanPipeline> pipeline;
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
    std::unique_ptr<VulkanImage> depthImage;
};
```

**After**:
```cpp
class Renderer {
private:
    std::unique_ptr<RHIDevice> device;
    std::unique_ptr<RHISwapchain> swapchain;
    std::unique_ptr<RHIPipeline> pipeline;
    std::vector<std::unique_ptr<RHIBuffer>> uniformBuffers;
    std::unique_ptr<RHITexture> depthImage;
};
```

**Expected Result**:
- Renderer becomes platform-independent
- Vulkan-specific code isolated to backend

---

### Phase 5: ResourceManager & SceneManager Migration

**Goal**: Convert resource/scene managers to use RHI

**Work Content**:
1. Refactor `ResourceManager`:
   - `VulkanImage*` → `RHITexture*`
   - `VulkanBuffer*` → `RHIBuffer*`
   - Texture loading logic uses RHI interfaces
2. Refactor `SceneManager`:
   - Mesh class uses RHIBuffer
   - Material class uses RHITexture

**Before**:
```cpp
class ResourceManager {
private:
    std::unordered_map<std::string, std::unique_ptr<VulkanImage>> textureCache;
};
```

**After**:
```cpp
class ResourceManager {
private:
    std::unordered_map<std::string, std::unique_ptr<RHITexture>> textureCache;
};
```

**Expected Result**:
- Entire engine uses RHI abstraction
- Platform-independent resource management

---

### Phase 6: ImGuiManager RHI Migration

**Goal**: Convert ImGui to render through RHI

**Work Content**:
1. Refactor `ImGuiManager`:
   - Perform ImGui backend initialization through RHI
   - Conditionally compile Vulkan-specific ImGui initialization
2. Add platform-specific ImGui backend selection logic

**Example Code**:
```cpp
void ImGuiManager::initializeBackend(RHIDevice* device) {
    // Check RHI backend type
    if (auto* vulkanDevice = dynamic_cast<VulkanRHIDevice*>(device)) {
        #ifdef RHI_VULKAN_SUPPORT
        ImGui_ImplVulkan_InitInfo info = {};
        info.Instance = vulkanDevice->getNativeDevice().getInstance();
        // ... Vulkan initialization
        #endif
    }
    else if (auto* d3d12Device = dynamic_cast<D3D12RHIDevice*>(device)) {
        #ifdef RHI_D3D12_SUPPORT
        // D3D12 ImGui initialization
        #endif
    }
}
```

**Expected Result**:
- ImGui works on various backends
- Conditional compilation per backend

---

### Phase 7: Testing and Verification

**Goal**: Verify functional equivalence after refactoring

**Test Items**:
1. **Functional Tests**:
   - OBJ model loading and rendering
   - FDF wireframe rendering
   - ImGui UI overlay
   - Camera control
   - Texture mapping

2. **Performance Tests**:
   - Measure RHI abstraction overhead
   - Compare frame time (before/after refactoring)
   - Compare memory usage

3. **Platform Tests**:
   - Linux (Vulkan)
   - macOS (Vulkan/Metal)
   - Windows (Vulkan/D3D12)

4. **Build Tests**:
   - Update CMake build system
   - Verify conditional compilation per backend
   - Confirm cross-platform builds

**Regression Tests**:
- Verify identical rendering output as before refactoring
- No Vulkan Validation Layer errors
- No memory leaks (Valgrind/ASAN)

**Expected Result**:
- All functionality works correctly
- Performance degradation within 5% (zero-cost abstraction goal)

---

## Directory Structure Changes

### Before Refactoring
```
src/
├── Application.hpp/cpp
├── main.cpp
├── core/
│   ├── VulkanDevice.hpp/cpp
│   ├── CommandManager.hpp/cpp
│   └── PlatformConfig.hpp
├── rendering/
│   ├── Renderer.hpp/cpp
│   ├── VulkanSwapchain.hpp/cpp
│   ├── VulkanPipeline.hpp/cpp
│   └── SyncManager.hpp/cpp
├── resources/
│   ├── VulkanBuffer.hpp/cpp
│   ├── VulkanImage.hpp/cpp
│   └── ResourceManager.hpp/cpp
├── scene/
│   ├── Mesh.hpp/cpp
│   ├── Camera.hpp/cpp
│   ├── Material.hpp/cpp
│   └── SceneManager.hpp/cpp
├── loaders/
│   ├── OBJLoader.hpp/cpp
│   ├── FDFLoader.hpp/cpp
│   └── TextureLoader.hpp/cpp
├── ui/
│   └── ImGuiManager.hpp/cpp
└── utils/
    ├── VulkanCommon.hpp
    ├── Vertex.hpp
    └── FileUtils.hpp
```

### After Refactoring
```
src/
├── Application.hpp/cpp
├── main.cpp
├── rhi/                            # NEW: RHI abstraction layer
│   ├── RHIDevice.hpp
│   ├── RHIQueue.hpp                # NEW: Queue abstraction
│   ├── RHIBuffer.hpp
│   ├── RHITexture.hpp
│   ├── RHISampler.hpp              # NEW: Sampler abstraction
│   ├── RHIShader.hpp
│   ├── RHIBindGroup.hpp            # NEW: Bind group abstraction
│   ├── RHIPipeline.hpp
│   ├── RHIRenderPass.hpp           # NEW: Render pass abstraction
│   ├── RHICommandBuffer.hpp
│   ├── RHISwapchain.hpp
│   ├── RHISync.hpp                 # NEW: Fence/Semaphore abstraction
│   ├── RHITypes.hpp
│   ├── RHICapabilities.hpp         # NEW: Feature query
│   ├── RHIFactory.hpp/cpp
│   ├── vulkan/                     # Vulkan backend
│   │   ├── VulkanRHIDevice.hpp/cpp
│   │   ├── VulkanRHIQueue.hpp/cpp
│   │   ├── VulkanRHIBuffer.hpp/cpp
│   │   ├── VulkanRHITexture.hpp/cpp
│   │   ├── VulkanRHISampler.hpp/cpp
│   │   ├── VulkanRHIShader.hpp/cpp
│   │   ├── VulkanRHIBindGroup.hpp/cpp
│   │   ├── VulkanRHIPipeline.hpp/cpp
│   │   ├── VulkanRHIRenderPass.hpp/cpp
│   │   ├── VulkanRHICommandEncoder.hpp/cpp
│   │   ├── VulkanRHISwapchain.hpp/cpp
│   │   ├── VulkanRHISync.hpp/cpp
│   │   └── internal/               # Vulkan internal implementation
│   │       ├── VulkanDevice.hpp/cpp
│   │       ├── VulkanBuffer.hpp/cpp
│   │       ├── VulkanImage.hpp/cpp
│   │       ├── VulkanSwapchain.hpp/cpp
│   │       ├── VulkanPipeline.hpp/cpp
│   │       ├── CommandManager.hpp/cpp
│   │       └── SyncManager.hpp/cpp
│   ├── webgpu/                     # WebGPU backend (Phase 8)
│   │   ├── WebGPURHIDevice.hpp/cpp
│   │   ├── WebGPURHIQueue.hpp/cpp
│   │   ├── WebGPURHIBuffer.hpp/cpp
│   │   ├── WebGPURHITexture.hpp/cpp
│   │   ├── WebGPURHISampler.hpp/cpp
│   │   ├── WebGPURHIShader.hpp/cpp
│   │   ├── WebGPURHIBindGroup.hpp/cpp
│   │   ├── WebGPURHIPipeline.hpp/cpp
│   │   ├── WebGPURHICommandEncoder.hpp/cpp
│   │   ├── WebGPURHISwapchain.hpp/cpp
│   │   └── WebGPUHelpers.hpp       # WebGPU utilities
│   ├── d3d12/                      # D3D12 backend (Phase 9)
│   │   └── ...
│   └── metal/                      # Metal backend (Phase 10)
│       └── ...
├── rendering/
│   └── Renderer.hpp/cpp            # Uses RHI
├── resources/
│   └── ResourceManager.hpp/cpp     # Uses RHI
├── scene/
│   ├── Mesh.hpp/cpp                # Uses RHI
│   ├── Camera.hpp/cpp
│   ├── Material.hpp/cpp            # Uses RHI
│   └── SceneManager.hpp/cpp
├── loaders/
│   ├── OBJLoader.hpp/cpp
│   ├── FDFLoader.hpp/cpp
│   └── TextureLoader.hpp/cpp
├── ui/
│   └── ImGuiManager.hpp/cpp        # Uses RHI
├── utils/
│   ├── Vertex.hpp
│   └── FileUtils.hpp
└── core/
    └── PlatformConfig.hpp
```

**Major Changes**:
- Added `src/rhi/` directory (RHI abstraction)
- Vulkan-specific code moved to `rhi/vulkan/internal/`
- Upper layers use RHI interfaces
- `utils/VulkanCommon.hpp` moved to Vulkan internals

---

## CMake Build System Updates

### Backend Selection Options

```cmake
# Top-level CMakeLists.txt
option(RHI_VULKAN_SUPPORT "Enable Vulkan RHI backend" ON)
option(RHI_WEBGPU_SUPPORT "Enable WebGPU RHI backend" OFF)
option(RHI_D3D12_SUPPORT "Enable Direct3D 12 RHI backend" OFF)
option(RHI_METAL_SUPPORT "Enable Metal RHI backend" OFF)

# WebGPU implementation selection (Dawn vs wgpu-native)
set(WEBGPU_IMPLEMENTATION "DAWN" CACHE STRING "WebGPU implementation (DAWN or WGPU)")
set_property(CACHE WEBGPU_IMPLEMENTATION PROPERTY STRINGS "DAWN" "WGPU")

# Platform-specific default backend
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

# RHI abstraction layer (always built)
add_library(RHI STATIC
    src/rhi/RHIFactory.cpp
)
target_include_directories(RHI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/rhi)

# Vulkan backend
if(RHI_VULKAN_SUPPORT)
    find_package(Vulkan REQUIRED)

    add_library(RHIVulkan STATIC
        src/rhi/vulkan/VulkanRHIDevice.cpp
        src/rhi/vulkan/VulkanRHIQueue.cpp
        src/rhi/vulkan/VulkanRHIBuffer.cpp
        src/rhi/vulkan/VulkanRHITexture.cpp
        src/rhi/vulkan/VulkanRHISampler.cpp
        src/rhi/vulkan/VulkanRHIShader.cpp
        src/rhi/vulkan/VulkanRHIBindGroup.cpp
        src/rhi/vulkan/VulkanRHIPipeline.cpp
        src/rhi/vulkan/VulkanRHIRenderPass.cpp
        src/rhi/vulkan/VulkanRHICommandEncoder.cpp
        src/rhi/vulkan/VulkanRHISwapchain.cpp
        src/rhi/vulkan/VulkanRHISync.cpp
        src/rhi/vulkan/internal/VulkanDevice.cpp
        src/rhi/vulkan/internal/VulkanBuffer.cpp
        src/rhi/vulkan/internal/VulkanImage.cpp
        # ... other Vulkan implementations
    )
    target_link_libraries(RHIVulkan PUBLIC Vulkan::Vulkan RHI)
    target_compile_definitions(RHIVulkan PUBLIC RHI_VULKAN_SUPPORT)

    # VMA (Vulkan Memory Allocator) recommended
    # Fetch VMA using FetchContent
    include(FetchContent)
    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.0.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    target_link_libraries(RHIVulkan PRIVATE VulkanMemoryAllocator)
endif()

# WebGPU backend
if(RHI_WEBGPU_SUPPORT)
    add_library(RHIWebGPU STATIC
        src/rhi/webgpu/WebGPURHIDevice.cpp
        src/rhi/webgpu/WebGPURHIQueue.cpp
        src/rhi/webgpu/WebGPURHIBuffer.cpp
        src/rhi/webgpu/WebGPURHITexture.cpp
        src/rhi/webgpu/WebGPURHISampler.cpp
        src/rhi/webgpu/WebGPURHIShader.cpp
        src/rhi/webgpu/WebGPURHIBindGroup.cpp
        src/rhi/webgpu/WebGPURHIPipeline.cpp
        src/rhi/webgpu/WebGPURHICommandEncoder.cpp
        src/rhi/webgpu/WebGPURHISwapchain.cpp
    )
    target_link_libraries(RHIWebGPU PUBLIC RHI)
    target_compile_definitions(RHIWebGPU PUBLIC RHI_WEBGPU_SUPPORT)

    if(EMSCRIPTEN)
        # Emscripten built-in WebGPU
        target_compile_options(RHIWebGPU PUBLIC -sUSE_WEBGPU=1)
        target_link_options(RHIWebGPU PUBLIC -sUSE_WEBGPU=1)
    else()
        # Native WebGPU implementation
        if(WEBGPU_IMPLEMENTATION STREQUAL "DAWN")
            # Use Dawn (Google)
            find_package(Dawn REQUIRED)
            target_link_libraries(RHIWebGPU PRIVATE dawn::webgpu_dawn)
        else()
            # Use wgpu-native (Mozilla)
            find_package(wgpu REQUIRED)
            target_link_libraries(RHIWebGPU PRIVATE wgpu::wgpu)
        endif()
    endif()
endif()

# D3D12 backend
if(RHI_D3D12_SUPPORT)
    add_library(RHID3D12 STATIC
        src/rhi/d3d12/D3D12RHIDevice.cpp
        # ... D3D12 implementation
    )
    target_link_libraries(RHID3D12 PUBLIC d3d12.lib dxgi.lib dxguid.lib RHI)
    target_compile_definitions(RHID3D12 PUBLIC RHI_D3D12_SUPPORT)

    # D3D12 Memory Allocator recommended
    FetchContent_Declare(
        D3D12MemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git
        GIT_TAG v2.0.1
    )
    FetchContent_MakeAvailable(D3D12MemoryAllocator)
    target_link_libraries(RHID3D12 PRIVATE D3D12MemoryAllocator)
endif()

# Metal backend
if(RHI_METAL_SUPPORT)
    add_library(RHIMetal STATIC
        src/rhi/metal/MetalRHIDevice.mm
        # ... Metal implementation (Objective-C++)
    )
    target_link_libraries(RHIMetal PUBLIC
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        RHI
    )
    target_compile_definitions(RHIMetal PUBLIC RHI_METAL_SUPPORT)
endif()

# RHI Factory links all backends
if(RHI_VULKAN_SUPPORT)
    target_link_libraries(RHI PUBLIC RHIVulkan)
endif()
if(RHI_WEBGPU_SUPPORT)
    target_link_libraries(RHI PUBLIC RHIWebGPU)
endif()
if(RHI_D3D12_SUPPORT)
    target_link_libraries(RHI PUBLIC RHID3D12)
endif()
if(RHI_METAL_SUPPORT)
    target_link_libraries(RHI PUBLIC RHIMetal)
endif()

# Main engine target
add_executable(MiniEngine
    src/main.cpp
    src/Application.cpp
    src/rendering/Renderer.cpp
    # ... other sources
)
target_link_libraries(MiniEngine PRIVATE RHI)

# WebAssembly build settings
if(EMSCRIPTEN)
    set_target_properties(MiniEngine PROPERTIES
        SUFFIX ".html"
    )
    target_link_options(MiniEngine PRIVATE
        -sUSE_WEBGPU=1
        -sUSE_GLFW=3
        -sALLOW_MEMORY_GROWTH=1
        -sASYNCIFY=1  # WebGPU async support
        --preload-file ${CMAKE_SOURCE_DIR}/shaders@/shaders
        --preload-file ${CMAKE_SOURCE_DIR}/models@/models
        --preload-file ${CMAKE_SOURCE_DIR}/textures@/textures
    )
endif()
```

**Build Examples**:
```bash
# Vulkan backend only (default)
cmake -B build -DRHI_VULKAN_SUPPORT=ON

# Vulkan + WebGPU dual backend (Dawn)
cmake -B build -DRHI_VULKAN_SUPPORT=ON -DRHI_WEBGPU_SUPPORT=ON -DWEBGPU_IMPLEMENTATION=DAWN

# Vulkan + WebGPU dual backend (wgpu-native)
cmake -B build -DRHI_VULKAN_SUPPORT=ON -DRHI_WEBGPU_SUPPORT=ON -DWEBGPU_IMPLEMENTATION=WGPU

# WebAssembly build (Emscripten)
emcmake cmake -B build-wasm
cmake --build build-wasm

# Windows with D3D12 + Vulkan dual backend
cmake -B build -DRHI_VULKAN_SUPPORT=ON -DRHI_D3D12_SUPPORT=ON

# macOS with Metal + Vulkan dual backend
cmake -B build -DRHI_VULKAN_SUPPORT=ON -DRHI_METAL_SUPPORT=ON
```

---

## Key Design Decisions

### 1. Pure Virtual Interface vs Template

**Choice**: Pure Virtual Interface

**Reasons**:
- Runtime backend switching possible
- Easy to support plugin system
- Convenient debugging and profiling
- ABI stability (shared library possible)

**Trade-offs**:
- Virtual function call overhead (~5% performance degradation)
- Limited compile-time optimization compared to template metaprogramming

### 2. Maintain RAII vs Raw Pointer

**Choice**: Maintain RAII (std::unique_ptr)

**Reasons**:
- Preserve strengths of existing architecture
- Guarantee memory safety
- Exception safety

**Implementation**:
```cpp
// RHI interface is abstract class
class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;  // RAII support
    // ...
};

// Factory methods return unique_ptr
std::unique_ptr<RHIBuffer> RHIDevice::createBuffer(...) {
    return std::make_unique<VulkanRHIBuffer>(...);
}
```

### 3. Platform Extension Methods

**Problem**: Need to access platform-specific features like Vulkan's VkCommandBuffer, D3D12's ID3D12GraphicsCommandList, etc.

**Solution**: Extension Interface Pattern

```cpp
// Platform-independent interface
class RHICommandBuffer {
public:
    virtual void draw(uint32_t vertexCount) = 0;
    // ...
};

// Vulkan extension interface
class VulkanRHICommandBufferExt {
public:
    virtual vk::CommandBuffer getNativeCommandBuffer() = 0;
};

// Vulkan implementation inherits both
class VulkanRHICommandBuffer
    : public RHICommandBuffer
    , public VulkanRHICommandBufferExt
{
public:
    void draw(uint32_t vertexCount) override;
    vk::CommandBuffer getNativeCommandBuffer() override;
};

// Usage example (when Vulkan-specific features needed)
void useVulkanFeature(RHICommandBuffer* cmd) {
    if (auto* vulkanCmd = dynamic_cast<VulkanRHICommandBufferExt*>(cmd)) {
        vk::CommandBuffer nativeCmd = vulkanCmd->getNativeCommandBuffer();
        // Vulkan-specific work
    }
}
```

### 4. Shader Cross-Compilation

**Problem**: Vulkan uses SPIR-V, D3D12 uses DXIL, Metal uses MSL, WebGPU uses WGSL

**Solution**: Unified shader pipeline

```
[Source Shader]
     |
     v
  ┌─────────────────────────────────────┐
  │ Slang / HLSL / GLSL (source language) │
  └─────────────────────────────────────┘
     |
     v (slangc / glslc / dxc)
  ┌─────────────────────────────────────┐
  │         SPIR-V (IR)                  │
  └─────────────────────────────────────┘
     |
     +────────────+────────────+────────────+
     |            |            |            |
     v            v            v            v
 [Vulkan]   [SPIRV-Cross] [SPIRV-Cross] [Naga/Tint]
 (direct use)    |            |            |
                v            v            v
             [HLSL]       [MSL]        [WGSL]
                |            |            |
                v            v            v
            [D3D12]      [Metal]     [WebGPU]
```

**Recommended Workflow**:

```cpp
// rhi/RHIShader.hpp
struct ShaderSource {
    enum class Language {
        SPIRV,      // Binary SPIR-V
        WGSL,       // WebGPU Shading Language
        HLSL,       // High Level Shading Language
        GLSL,       // OpenGL Shading Language
        Slang       // Slang (recommended source language)
    };

    Language language;
    std::vector<uint8_t> code;  // Binary or text
    std::string entryPoint = "main";
    ShaderStage stage;
};

class RHIShader {
public:
    virtual ~RHIShader() = default;

    // Generic load function - converts internally if needed
    virtual bool load(const ShaderSource& source) = 0;
};

// Vulkan uses SPIR-V directly
class VulkanRHIShader : public RHIShader {
    bool load(const ShaderSource& source) override {
        if (source.language == ShaderSource::Language::SPIRV) {
            // Load SPIR-V directly
            return loadSPIRV(source.code);
        }
        // Other languages need to convert to SPIR-V
        auto spirv = compileToSPIRV(source);
        return loadSPIRV(spirv);
    }
};

// WebGPU uses WGSL or SPIR-V (Dawn only)
class WebGPURHIShader : public RHIShader {
    bool load(const ShaderSource& source) override {
        if (source.language == ShaderSource::Language::WGSL) {
            return loadWGSL(source.code);
        }
        if (source.language == ShaderSource::Language::SPIRV) {
            // Convert SPIR-V → WGSL (Naga/Tint)
            std::string wgsl = convertSPIRVToWGSL(source.code);
            return loadWGSL(wgsl);
        }
        // Other language → SPIR-V → WGSL
        auto spirv = compileToSPIRV(source);
        std::string wgsl = convertSPIRVToWGSL(spirv);
        return loadWGSL(wgsl);
    }
};

// D3D12 uses HLSL/DXIL
class D3D12RHIShader : public RHIShader {
    bool load(const ShaderSource& source) override {
        if (source.language == ShaderSource::Language::HLSL) {
            return compileHLSL(source.code);
        }
        if (source.language == ShaderSource::Language::SPIRV) {
            // Convert SPIR-V → HLSL (SPIRV-Cross)
            std::string hlsl = convertSPIRVToHLSL(source.code);
            return compileHLSL(hlsl);
        }
        // Other language → SPIR-V → HLSL
        auto spirv = compileToSPIRV(source);
        std::string hlsl = convertSPIRVToHLSL(spirv);
        return compileHLSL(hlsl);
    }
};
```

**Shader Management Utilities**:

```cpp
// utils/ShaderCompiler.hpp
class ShaderCompiler {
public:
    // SPIR-V compilation (wrapping slangc, glslc, dxc)
    static std::vector<uint32_t> compileToSPIRV(
        const std::string& source,
        ShaderSource::Language srcLang,
        ShaderStage stage);

    // SPIR-V conversion (wrapping SPIRV-Cross)
    static std::string convertSPIRVToHLSL(const std::vector<uint32_t>& spirv);
    static std::string convertSPIRVToMSL(const std::vector<uint32_t>& spirv);
    static std::string convertSPIRVToWGSL(const std::vector<uint32_t>& spirv);

    // Precompiled shader cache
    static void setCacheDirectory(const std::string& path);
    static bool loadCached(const std::string& key, std::vector<uint32_t>& spirv);
    static void saveToCache(const std::string& key, const std::vector<uint32_t>& spirv);
};
```

**Dependencies**:
- **Slang** (recommended): Unified shader language, supports SPIR-V/HLSL/MSL/WGSL output
- **SPIRV-Cross**: SPIR-V → HLSL/MSL conversion
- **Naga** (wgpu): SPIR-V ↔ WGSL conversion
- **Tint** (Dawn): SPIR-V ↔ WGSL conversion

---

## Expected Code Changes

| Phase | Added Lines | Modified Lines | Deleted Lines | File Count |
|-------|----------|----------|----------|---------|
| Phase 1: RHI Interface | +1200 | 0 | 0 | +14 |
| Phase 2: Vulkan Backend | +2500 | ~1500 | ~200 | +24 (7 existing moved) |
| Phase 3: RHI Factory | +300 | 0 | 0 | +2 |
| Phase 4: Renderer Migration | +100 | ~500 | ~50 | ~2 |
| Phase 5: Resource/Scene Migration | +50 | ~300 | ~30 | ~4 |
| Phase 6: ImGui Migration | +100 | ~200 | ~20 | ~2 |
| Phase 7: Testing | +500 | 0 | 0 | +10 (test code) |
| Phase 8: WebGPU Backend | +2000 | ~100 | 0 | +12 |
| **Total (Phase 1-7)** | **+4750** | **~2500** | **~300** | **+58** |
| **Total (Phase 1-8 with WebGPU)** | **+6750** | **~2600** | **~300** | **+70** |

**Expected Development Time**:
- Phase 1-7 (Vulkan RHI): 4-6 weeks (single developer)
- Phase 8 (WebGPU): Additional 2-3 weeks

---

## Performance Considerations

### Zero-Cost Abstraction Strategies

1. **Inline Optimization**:
   ```cpp
   // Small functions defined inline in headers
   class RHIBuffer {
   public:
       virtual size_t getSize() const = 0;  // Virtual (small overhead)

       // Utility functions are inline
       inline bool isValid() const { return getSize() > 0; }
   };
   ```

2. **Batch Operations**:
   ```cpp
   // Batch processing instead of single calls
   virtual void drawInstanced(const DrawParams* params, uint32_t count) = 0;
   ```

3. **Compile-Time Dispatch** (optimized builds):
   ```cpp
   #ifdef RHI_STATIC_BACKEND_VULKAN
   // Remove runtime polymorphism, compile-time decision
   using RHIDevice = VulkanRHIDevice;
   using RHIBuffer = VulkanRHIBuffer;
   #endif
   ```

### Expected Performance Impact

- **Abstraction Overhead**: 3-5% (virtual function calls)
- **Memory Overhead**: <1% (vtable pointers)
- **Optimized Builds**: Nearly zero overhead (inlining + devirtualization)

---

## Migration Checklist

### Phase 1: RHI Interface Design
- [ ] Create `src/rhi/` directory
- [ ] Define `RHITypes.hpp` common types/enumerations
- [ ] Define `RHIDevice.hpp` interface
- [ ] Define `RHIQueue.hpp` interface
- [ ] Define `RHIBuffer.hpp` interface
- [ ] Define `RHITexture.hpp` interface
- [ ] Define `RHISampler.hpp` interface
- [ ] Define `RHIShader.hpp` interface
- [ ] Define `RHIBindGroup.hpp` interface
- [ ] Define `RHIPipeline.hpp` interface
- [ ] Define `RHIRenderPass.hpp` interface
- [ ] Define `RHICommandBuffer.hpp` interface
- [ ] Define `RHISwapchain.hpp` interface
- [ ] Define `RHISync.hpp` interface
- [ ] Define `RHICapabilities.hpp` feature query
- [ ] Document interfaces (Doxygen)

### Phase 2: Vulkan Backend Implementation
- [ ] Create `src/rhi/vulkan/` directory
- [ ] Create `src/rhi/vulkan/internal/` directory
- [ ] Move existing Vulkan code to `internal/`
- [ ] Implement `VulkanRHIDevice`
- [ ] Implement `VulkanRHIQueue`
- [ ] Implement `VulkanRHIBuffer`
- [ ] Implement `VulkanRHITexture`
- [ ] Implement `VulkanRHISampler`
- [ ] Implement `VulkanRHIShader`
- [ ] Implement `VulkanRHIBindGroup`
- [ ] Implement `VulkanRHIPipeline`
- [ ] Implement `VulkanRHIRenderPass`
- [ ] Implement `VulkanRHICommandEncoder`
- [ ] Implement `VulkanRHISwapchain`
- [ ] Implement `VulkanRHISync` (Fence/Semaphore)
- [ ] Unit test Vulkan backend

### Phase 3: RHI Factory Implementation
- [ ] Write `RHIFactory.hpp/cpp`
- [ ] Define `RHIBackendType` enumeration
- [ ] Implement `createDevice()` factory method
- [ ] Implement `enumerateAdapters()`
- [ ] Platform-specific default backend selection logic
- [ ] Add CMake backend selection options
- [ ] Backend availability check function

### Phase 4: Renderer Migration
- [ ] Change `Renderer.hpp` to RHI types
- [ ] Change `Renderer.cpp` to RHI methods
- [ ] Remove/conditionally compile Vulkan-specific code
- [ ] Verify build success
- [ ] Test rendering

### Phase 5: Resource/Scene Migration
- [ ] Convert `ResourceManager` to RHI
- [ ] Convert `SceneManager` to RHI
- [ ] Convert `Mesh` class to RHI
- [ ] Convert `Material` class to RHI
- [ ] Test resource loading

### Phase 6: ImGui Migration
- [ ] Convert `ImGuiManager` to RHI
- [ ] Backend-specific ImGui initialization logic
- [ ] Test ImGui rendering

### Phase 7: Testing & Verification
- [ ] Functional test (OBJ rendering)
- [ ] Functional test (FDF rendering)
- [ ] Functional test (ImGui UI)
- [ ] Performance benchmark (frame time)
- [ ] Memory profiling (leak check)
- [ ] Platform test (Linux)
- [ ] Platform test (macOS)
- [ ] Platform test (Windows)
- [ ] Update documentation

### Phase 8: WebGPU Backend Implementation
- [ ] Create `src/rhi/webgpu/` directory
- [ ] Integrate Dawn or wgpu-native
- [ ] Implement `WebGPURHIDevice`
- [ ] Implement `WebGPURHIQueue`
- [ ] Implement `WebGPURHIBuffer`
- [ ] Implement `WebGPURHITexture`
- [ ] Implement `WebGPURHISampler`
- [ ] Implement `WebGPURHIShader` (WGSL support)
- [ ] Implement `WebGPURHIBindGroup`
- [ ] Implement `WebGPURHIPipeline`
- [ ] Implement `WebGPURHICommandEncoder`
- [ ] Implement `WebGPURHISwapchain`
- [ ] SPIR-V → WGSL conversion pipeline
- [ ] Unit test WebGPU backend
- [ ] Test WebAssembly (Emscripten) build
- [ ] Test in browsers (Chrome/Firefox)

---

## Future Expansion Plans

### WebGPU Backend (Phase 8) - High Priority

**Why Choose WebGPU**:
- **Cross-Platform**: Supports both browser (WebAssembly) and native environments
- **Modern API**: Common abstraction of Vulkan/D3D12/Metal, natural fit for RHI design
- **Dawn/wgpu-native**: High-performance native WebGPU implementations available
- **Future-Oriented**: W3C standard, the future of browser graphics

**WebGPU-RHI Mapping Table**:

| RHI Concept | WebGPU Concept |
|---------|-------------|
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

**Implementation Priority**:
1. WebGPURHIDevice - Dawn/wgpu-native initialization
2. WebGPURHIQueue - Queue management (WebGPU has single queue)
3. WebGPURHIBuffer - Buffer creation and mapping
4. WebGPURHITexture - Texture and view management
5. WebGPURHISampler - Sampler management
6. WebGPURHIBindGroup - Bind groups (1:1 mapping with RHI)
7. WebGPURHIShader - WGSL shader modules
8. WebGPURHIPipeline - Render/compute pipelines
9. WebGPURHICommandEncoder - Command encoding
10. WebGPURHISwapchain - Surface-based swapchain

**Core Implementation Code**:
```cpp
// rhi/webgpu/WebGPURHIDevice.hpp
class WebGPURHIDevice : public RHIDevice {
public:
    WebGPURHIDevice(const DeviceCreateInfo& createInfo);
    ~WebGPURHIDevice() override;

    // RHIDevice interface implementation
    std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) override;
    std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) override;
    std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) override;
    std::unique_ptr<RHIShader> createShader(const ShaderDesc& desc) override;
    std::unique_ptr<RHIBindGroupLayout> createBindGroupLayout(
        const BindGroupLayoutDesc& desc) override;
    std::unique_ptr<RHIBindGroup> createBindGroup(const BindGroupDesc& desc) override;
    std::unique_ptr<RHIRenderPipeline> createRenderPipeline(
        const RenderPipelineDesc& desc) override;
    std::unique_ptr<RHICommandEncoder> createCommandEncoder() override;
    RHIQueue* getQueue(QueueType type) override;
    void waitIdle() override;
    RHIBackendType getBackendType() const override { return RHIBackendType::WebGPU; }

    // WebGPU-specific
    WGPUDevice getNativeDevice() const { return device_; }
    WGPUAdapter getAdapter() const { return adapter_; }

private:
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    std::unique_ptr<WebGPURHIQueue> queue_;
    WebGPURHICapabilities capabilities_;
};

// rhi/webgpu/WebGPURHIBuffer.hpp
class WebGPURHIBuffer : public RHIBuffer {
public:
    WebGPURHIBuffer(WebGPURHIDevice& device, const BufferDesc& desc);
    ~WebGPURHIBuffer() override;

    void* map() override;
    void* mapRange(uint64_t offset, uint64_t size) override;
    void unmap() override;
    void write(const void* data, uint64_t size, uint64_t offset = 0) override;
    uint64_t getSize() const override { return size_; }
    BufferUsage getUsage() const override { return usage_; }

    // WebGPU-specific
    WGPUBuffer getNativeBuffer() const { return buffer_; }

private:
    WebGPURHIDevice& device_;
    WGPUBuffer buffer_ = nullptr;
    uint64_t size_;
    BufferUsage usage_;
};

// rhi/webgpu/WebGPURHICommandEncoder.hpp
class WebGPURHICommandEncoder : public RHICommandEncoder {
public:
    WebGPURHICommandEncoder(WebGPURHIDevice& device);
    ~WebGPURHICommandEncoder() override;

    std::unique_ptr<RHIRenderPassEncoder> beginRenderPass(
        const RenderPassDesc& desc) override;
    std::unique_ptr<RHIComputePassEncoder> beginComputePass() override;

    void copyBufferToBuffer(RHIBuffer* src, uint64_t srcOffset,
                           RHIBuffer* dst, uint64_t dstOffset,
                           uint64_t size) override;
    void copyBufferToTexture(const BufferTextureCopyInfo& src,
                            const TextureCopyInfo& dst,
                            const Extent3D& copySize) override;

    std::unique_ptr<RHICommandBuffer> finish() override;

private:
    WebGPURHIDevice& device_;
    WGPUCommandEncoder encoder_ = nullptr;
};

// rhi/webgpu/WebGPURHIRenderPassEncoder.hpp
class WebGPURHIRenderPassEncoder : public RHIRenderPassEncoder {
public:
    WebGPURHIRenderPassEncoder(WGPURenderPassEncoder encoder);
    ~WebGPURHIRenderPassEncoder() override;

    void setPipeline(RHIRenderPipeline* pipeline) override;
    void setBindGroup(uint32_t index, RHIBindGroup* bindGroup,
                     const std::vector<uint32_t>& dynamicOffsets = {}) override;
    void setVertexBuffer(uint32_t slot, RHIBuffer* buffer,
                        uint64_t offset = 0) override;
    void setIndexBuffer(RHIBuffer* buffer, IndexFormat format,
                       uint64_t offset = 0) override;
    void setViewport(float x, float y, float width, float height,
                    float minDepth = 0.0f, float maxDepth = 1.0f) override;
    void setScissorRect(uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
             uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                    uint32_t firstIndex = 0, int32_t baseVertex = 0,
                    uint32_t firstInstance = 0) override;
    void end() override;

private:
    WGPURenderPassEncoder encoder_ = nullptr;
};
```

**Shader Cross-Compilation (for WebGPU)**:
```cpp
// WebGPU accepts WGSL or SPIR-V as input
class WebGPURHIShader : public RHIShader {
public:
    // Load WGSL directly (recommended)
    bool loadFromWGSL(const std::string& wgslSource, ShaderStage stage);

    // Convert from SPIR-V to WGSL (using Naga/Tint)
    bool loadFromSPIRV(const std::vector<uint32_t>& spirv, ShaderStage stage) {
        // Use Dawn's Tint or wgpu's Naga to convert SPIR-V → WGSL
        // Or Dawn directly supports SPIR-V (experimental)
        std::string wgsl = convertSPIRVToWGSL(spirv);
        return loadFromWGSL(wgsl, stage);
    }

private:
    WGPUShaderModule module_ = nullptr;
};
```

**WebAssembly Build Support**:
```cmake
# WebGPU + Emscripten build
if(EMSCRIPTEN)
    set(RHI_WEBGPU_SUPPORT ON)
    set(RHI_VULKAN_SUPPORT OFF)  # Vulkan not available on Emscripten

    # Emscripten WebGPU flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sUSE_WEBGPU=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sUSE_WEBGPU=1")
endif()
```

**Core Challenges**:
- Choosing and integrating Dawn (Google) or wgpu-native (Mozilla)
- SPIR-V → WGSL conversion pipeline (Tint/Naga)
- Handling async API (many WebGPU functions are async)
- Buffer mapping model differences (WebGPU uses async mapping)
- WebAssembly build and testing

**Expected Development Time**: 2-3 weeks

---

### Direct3D 12 Backend (Phase 9)

**D3D12-RHI Mapping Table**:

| RHI Concept | D3D12 Concept |
|---------|------------|
| `RHIDevice` | `ID3D12Device` + `IDXGIAdapter` + `IDXGIFactory` |
| `RHIQueue` | `ID3D12CommandQueue` |
| `RHIBuffer` | `ID3D12Resource` (Buffer) |
| `RHITexture` | `ID3D12Resource` (Texture) |
| `RHITextureView` | `D3D12_CPU_DESCRIPTOR_HANDLE` (SRV/UAV/RTV/DSV) |
| `RHISampler` | `D3D12_CPU_DESCRIPTOR_HANDLE` (Sampler) |
| `RHIShader` | `ID3DBlob` (DXIL) |
| `RHIBindGroupLayout` | Part of Root Signature |
| `RHIBindGroup` | Descriptor Table |
| `RHIPipelineLayout` | `ID3D12RootSignature` |
| `RHIRenderPipeline` | `ID3D12PipelineState` |
| `RHICommandEncoder` | `ID3D12GraphicsCommandList` |
| `RHISwapchain` | `IDXGISwapChain4` |
| `RHIFence` | `ID3D12Fence` |

**Implementation Priority**:
1. D3D12RHIDevice
2. D3D12RHICommandBuffer
3. D3D12RHIBuffer
4. D3D12RHITexture
5. D3D12RHISwapchain
6. D3D12RHIPipeline
7. D3D12RHIShader

**Core Challenges**:
- Command Queue management (Graphics/Compute/Copy)
- Descriptor Heap management
- Root Signature design
- PSO (Pipeline State Object) caching
- SPIRV-Cross HLSL conversion verification

### Metal Backend (Phase 10)

**Metal-RHI Mapping Table**:

| RHI Concept | Metal Concept |
|---------|-----------|
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

**Implementation Priority**:
1. MetalRHIDevice
2. MetalRHICommandBuffer
3. MetalRHIBuffer
4. MetalRHITexture
5. MetalRHISwapchain
6. MetalRHIPipeline
7. MetalRHIShader

**Core Challenges**:
- MTLDevice integration
- Command Encoder management
- Resource Hazard Tracking
- SPIRV-Cross MSL conversion verification
- MetalKit integration

### Ray Tracing Extension (Phase 11)

**RHI Interface Extension**:
```cpp
class RHIDevice {
public:
    virtual bool supportsRayTracing() const = 0;
    virtual std::unique_ptr<RHIAccelerationStructure>
        createAccelerationStructure(const ASDesc& desc) = 0;
};

class RHICommandBuffer {
public:
    virtual void traceRays(const RayTracingPipelineState& pso,
                          uint32_t width, uint32_t height) = 0;
};
```

**Backend-Specific Implementation**:
- Vulkan: VK_KHR_ray_tracing_pipeline
- D3D12: DXR (DirectX Raytracing)
- Metal: Metal Ray Tracing (M1+)

---

## Risk Factors and Mitigation

### Risk 1: Performance Degradation

**Risk Level**: Medium

**Impact**:
- Virtual function call overhead
- Reduced inlining opportunities

**Mitigation**:
- Identify hotspots through profiling
- Consider CRTP pattern for hot path functions
- Compile-time backend selection in optimized builds

### Risk 2: API Inconsistency

**Risk Level**: High

**Impact**:
- Conceptual differences between Vulkan/D3D12/Metal (e.g., descriptor management)
- Difficult to set abstraction level

**Mitigation**:
- Include only minimal common functionality in RHI
- Provide platform-specific features through extension interfaces
- Analyze reference cases (Unreal RHI, BGFX, Diligent Engine)

### Risk 3: Increased Development Time

**Risk Level**: Medium

**Impact**:
- More code changes than expected
- Expanded test scope

**Mitigation**:
- Phased incremental migration
- Regression testing after each phase
- Build automated testing

### Risk 4: Maintenance Burden

**Risk Level**: Low

**Impact**:
- Difficult backend-specific bug tracking
- Increased codebase

**Mitigation**:
- Thorough documentation
- Backend-specific unit tests
- Build CI/CD pipeline

---

## Reference Architectures

### Unreal Engine RHI
- Abstraction level: Medium (absorbs platform differences while maintaining performance)
- Backends: Vulkan, D3D11, D3D12, Metal
- Pattern: Virtual interface + Platform-specific extensions

### BGFX
- Abstraction level: High (unified API, internally optimized)
- Backends: Vulkan, D3D9/11/12, Metal, OpenGL, WebGPU
- Pattern: C API + internal C++ implementation

### Diligent Engine
- Abstraction level: Medium-High
- Backends: Vulkan, D3D11, D3D12, Metal, OpenGL, WebGPU
- Pattern: Pure virtual interface + Smart pointers

### WebGPU (wgpu / Dawn)
- Abstraction level: High (spec-based unified API)
- Backends: Vulkan, D3D12, Metal (internal implementation)
- Pattern: C API (webgpu.h) + modern resource model

### wgpu-rs (Rust)
- Abstraction level: High
- Backends: Vulkan, D3D12, Metal, WebGPU
- Pattern: WebGPU spec-based + safe Rust API

**Mini-Engine Choice**: **WebGPU-style API + Diligent Engine Pattern**

**Reasons**:
1. WebGPU API is modern and cross-platform friendly
2. Pure virtual interface provides flexibility
3. RAII + Smart pointers ensure safety
4. BindGroup model naturally maps to Vulkan/D3D12/Metal/WebGPU

---

## Conclusion

This refactoring strategy presents a roadmap for transforming Mini-Engine from a Vulkan-only engine to a **multi-backend cross-platform engine**.

### Achieving Core Goals
1. **Platform Independence**: Isolate upper layers through RHI abstraction
2. **Extensibility**: Add new backends like plugins
3. **Maintain RAII**: Preserve strengths of existing architecture
4. **Performance**: Target zero-cost abstraction (overhead <5%)
5. **Web Support**: Enable browser deployment through WebGPU

### Phased Execution
- **Phase 1-3**: Build RHI foundation (2 weeks)
- **Phase 4-6**: Migrate existing code (2 weeks)
- **Phase 7**: Testing and verification (1 week)
- **Phase 8**: WebGPU backend implementation (2-3 weeks)
- **Phase 9-11**: D3D12/Metal/RayTracing (optional, 2-3 weeks each)

### Backend Priority
1. **Vulkan** (Phase 1-7): Base backend, Linux/Windows/macOS (MoltenVK)
2. **WebGPU** (Phase 8): Web deployment + modern cross-platform
3. **D3D12** (Phase 9): Windows native performance optimization
4. **Metal** (Phase 10): macOS/iOS native performance optimization

### Expected Outcomes
- WebGPU support enables direct execution in web browsers
- Direct3D 12 support improves Windows native performance
- Metal support improves macOS/iOS native performance
- Latest technologies like Ray Tracing can be supported per backend

### Design Key Points
- **WebGPU-style API**: Modern and clear resource binding model
- **CommandEncoder/RenderPassEncoder pattern**: Explicit command recording
- **BindGroup-based resource binding**: 1:1 mapping with Vulkan Descriptor Sets
- **Synchronization primitive abstraction**: GPU synchronization management with Fence/Semaphore

---

**Document Version**: 2.0
**Date**: 2025-12-18
**Major Changes**:
- Extended RHI interface (added Queue, BindGroup, Sync, Capabilities)
- Added WebGPU backend design (Phase 8)
- Improved shader cross-compilation pipeline
- Added CMake build system WebGPU/Emscripten support
**Next Review**: After Phase 1 completion
