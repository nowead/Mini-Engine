# Architecture Analysis: 7-Layer vs 4-Layer

## Executive Summary

After completing Phase 7 of the refactoring, we achieved a "perfect" 18-line main.cpp with 7 distinct architectural layers. However, a deeper analysis using **software architecture quality metrics** reveals critical structural limitations in the current design.

This document presents a **quantitative evaluation** of the current architecture and justifies the migration to a cleaner 4-layer design.

---

## Current Architecture (7-Layer)

```
Layer 1: Entry Point (main.cpp)
Layer 2: Application Layer (Application)
Layer 3: Renderer Layer (Renderer)
Layer 4: Scene Layer (Mesh, OBJLoader)
Layer 5: Rendering Layer (Swapchain, Pipeline, Command, Sync)
Layer 6: Resource Layer (VulkanBuffer, VulkanImage)
Layer 7: Core Layer (VulkanDevice)
```

### Current Renderer Implementation

```cpp
class Renderer {
private:
    // 7 direct dependencies
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanPipeline> pipeline;
    std::unique_ptr<CommandManager> commandManager;
    std::unique_ptr<SyncManager> syncManager;

    // Direct resource ownership
    std::unique_ptr<VulkanImage> depthImage;
    std::unique_ptr<VulkanImage> textureImage;
    std::unique_ptr<Mesh> mesh;
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;

    // Descriptor management
    vk::raii::DescriptorPool descriptorPool;
    std::vector<vk::raii::DescriptorSet> descriptorSets;

public:
    void loadModel(const std::string& path);
    void loadTexture(const std::string& path);
    void drawFrame();
    void waitIdle();
    void handleFramebufferResize();
};
```

**Problem**: Renderer directly manages 9+ subsystems and has 13+ public/private methods with mixed responsibilities.

---

## Architecture Quality Metrics Analysis

### 1. Cohesion (응집도) - FAILED ❌

**Definition**: How closely related and focused are the responsibilities within a class?

#### Current Renderer Responsibilities

| Responsibility | Evidence | SRP Violation? |
|----------------|----------|----------------|
| **Vulkan device lifecycle** | `device = std::make_unique<VulkanDevice>(...)` | ✅ Acceptable |
| **Swapchain management** | `swapchain->recreate()`, `handleFramebufferResize()` | ✅ Acceptable |
| **Pipeline management** | `pipeline = std::make_unique<VulkanPipeline>(...)` | ✅ Acceptable |
| **Command recording** | `recordCommandBuffer(imageIndex)` | ✅ Acceptable |
| **Synchronization** | `syncManager->waitForFence(...)` | ✅ Acceptable |
| **Resource loading** | `loadModel()`, `loadTexture()` | ❌ **Resource management** |
| **Texture upload** | 30-line `loadTexture()` with staging buffer | ❌ **Resource management** |
| **Descriptor management** | `createDescriptorPool()`, `updateDescriptorSets()` | ⚠️ Borderline |
| **Rendering coordination** | `drawFrame()` 60-line orchestration | ✅ Acceptable |

**Analysis**:
- Renderer handles **8 different responsibilities**
- Resource loading (`loadModel`, `loadTexture`) violates Single Responsibility Principle
- `loadTexture()` contains 30+ lines of staging buffer logic - this is resource management, not rendering

**Cohesion Score**: **6/10** (Mixed responsibilities)

#### Measurement: Lack of Cohesion of Methods (LCOM)

Using LCOM4 metric to measure cohesion:

```
Methods in Renderer:
- Group A (Rendering): drawFrame, recordCommandBuffer, recreateSwapchain
- Group B (Resources): loadModel, loadTexture, createUniformBuffers
- Group C (Descriptors): createDescriptorPool, updateDescriptorSets
- Group D (Lifecycle): constructor, waitIdle

Each group accesses DIFFERENT member variables
→ Low cohesion (LCOM4 = 4 disconnected groups)
```

**Ideal**: LCOM4 = 1 (all methods work on same data)
**Current**: LCOM4 = 4 (4 separate concerns)

---

### 2. Coupling (결합도) - FAILED ❌

**Definition**: How dependent is a class on implementation details of other classes?

#### Dependency Analysis

```cpp
// Current: Renderer directly accesses low-level details
void Renderer::drawFrame() {
    // Line 107: Direct access to SyncManager internals
    syncManager->waitForFence(currentFrame);

    // Line 110-113: Swapchain details exposed
    auto [result, imageIndex] = swapchain->acquireNextImage(
        UINT64_MAX,
        syncManager->getImageAvailableSemaphore(currentFrame),
        nullptr);

    // Line 131-144: Manual synchronization setup
    vk::PipelineStageFlags waitDestinationStageMask(...);
    vk::Semaphore waitSemaphores[] = {
        syncManager->getImageAvailableSemaphore(currentFrame)
    };
    vk::Semaphore signalSemaphores[] = {
        syncManager->getRenderFinishedSemaphore(imageIndex)
    };

    // Line 144: Direct Vulkan API call
    device->getGraphicsQueue().submit(submitInfo,
        syncManager->getInFlightFence(currentFrame));

    // Line 155: Another direct Vulkan API call
    device->getGraphicsQueue().presentKHR(presentInfoKHR);
}
```

**Problems**:
1. Renderer knows about **Semaphore objects** (low-level synchronization primitives)
2. Renderer knows about **Queue submission details** (should be encapsulated)
3. Renderer knows about **image indices** and **fence states** (internal state leakage)

**Coupling Type**: **Content Coupling** (worst type)
- Renderer accesses internal state of SyncManager (`getImageAvailableSemaphore`)
- Renderer constructs Vulkan structures manually (tight coupling to Vulkan API)

**Coupling Score**: **4/10** (High coupling to implementation details)

#### Desired: Interface Coupling

```cpp
// Better design: Renderer only knows high-level interfaces
void Renderer::drawFrame() {
    renderingSystem->beginFrame();
    renderingSystem->render(sceneManager->getScene());
    renderingSystem->endFrame();
}
```

---

### 3. Extensibility (확장성) - FAILED ❌

**Test**: How many files need modification to add a new feature?

#### Scenario 1: Add Shadow Mapping (Multi-pass rendering)

**Current architecture requires**:
1. Modify `Renderer.hpp` - Add shadow pass members
2. Modify `Renderer.cpp` - Add shadow pass initialization (constructor)
3. Modify `Renderer::drawFrame()` - Insert shadow pass before main pass
4. Modify `Renderer::recordCommandBuffer()` - Add shadow pass recording
5. Modify `VulkanPipeline` - Create second pipeline for shadow
6. Modify `createDescriptorSets()` - Add shadow map texture binding

**Files modified**: 6
**Lines changed**: ~200
**Risk**: High (touching core rendering loop)

**4-Layer architecture requires**:
1. Create `ShadowPass.cpp` (new file)
2. Register pass in `RenderingSystem` (1 line)

**Files modified**: 2
**Lines changed**: ~80
**Risk**: Low (no existing code touched)

**Extensibility Score**: **5/10** (New features require modifying core classes)

---

#### Scenario 2: Add New Resource Type (Cubemap)

**Current architecture**:
```cpp
// Renderer.hpp - Add new member
std::unique_ptr<VulkanImage> cubemapImage;

// Renderer.cpp - Add new load method
void Renderer::loadCubemap(const std::string& path) {
    // 50+ lines of loading logic
    cubemapImage = std::make_unique<VulkanImage>(...);
    updateDescriptorSets();  // Modify existing function
}
```

**Files modified**: 2 (Renderer.hpp, Renderer.cpp)
**Lines changed**: ~60

**4-Layer architecture**:
```cpp
// ResourceManager.cpp - Add to existing load method
auto cubemap = resourceManager->loadCubemap(path);
```

**Files modified**: 1 (ResourceManager.cpp)
**Lines changed**: ~20

**Extensibility Score for Scenario 2**: **4/10**

**Overall Extensibility**: **4.5/10** (Poor)

---

### 4. Testability (테스트 가능성) - FAILED ❌

**Test**: Can we unit test rendering logic without a GPU?

#### Current Architecture

```cpp
TEST(RendererTest, DrawFrame) {
    // Problem 1: Requires real GLFW window
    GLFWwindow* window = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);

    // Problem 2: Requires real Vulkan device
    std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
    Renderer renderer(window, layers, true);  // ← Fails without GPU

    // Problem 3: Cannot mock subsystems
    renderer.drawFrame();  // ← Actual GPU rendering happens

    // Problem 4: Cannot verify internal state
    // No way to check if syncManager was called correctly
}
```

**Issues**:
1. ❌ Concrete dependencies (cannot inject mocks)
2. ❌ Requires GPU hardware
3. ❌ Requires GLFW window system
4. ❌ No interfaces to mock
5. ❌ Integration test only, not unit test

**Testability Score**: **3/10** (Integration tests only)

---

#### 4-Layer Architecture (Mockable)

```cpp
// Interface abstraction
class IRenderingSystem {
    virtual void beginFrame() = 0;
    virtual void render(Scene& scene) = 0;
    virtual void endFrame() = 0;
};

class Renderer {
    std::unique_ptr<IRenderingSystem> renderingSystem;
public:
    // Dependency injection for testing
    Renderer(std::unique_ptr<IRenderingSystem> rs)
        : renderingSystem(std::move(rs)) {}
};

// Test with mock
class MockRenderingSystem : public IRenderingSystem {
    void render(Scene& scene) override {
        renderCallCount++;
    }
    int renderCallCount = 0;
};

TEST(RendererTest, DrawFrame) {
    auto mock = std::make_unique<MockRenderingSystem>();
    MockRenderingSystem* mockPtr = mock.get();
    Renderer renderer(std::move(mock));

    renderer.drawFrame();

    EXPECT_EQ(mockPtr->renderCallCount, 1);  // ✅ Verifiable!
}
```

**Testability Score (4-Layer)**: **9/10** (Full unit testing)

---

### 5. Complexity (복잡도) - BORDERLINE ⚠️

#### Cyclomatic Complexity

**Definition**: Number of independent paths through code (measures decision points)

**Current `Renderer::drawFrame()` complexity**:

```cpp
void Renderer::drawFrame() {
    syncManager->waitForFence(currentFrame);               // 1

    auto [result, imageIndex] = swapchain->acquireNextImage(...);

    if (result == vk::Result::eErrorOutOfDateKHR) {        // +1
        recreateSwapchain();
        return;
    }
    if (result != vk::Result::eSuccess &&                  // +2
        result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error(...);
    }

    updateUniformBuffer(currentFrame);
    syncManager->resetFence(currentFrame);
    commandManager->getCommandBuffer(currentFrame).reset();
    recordCommandBuffer(imageIndex);

    // Submit logic...
    device->getGraphicsQueue().submit(...);
    result = device->getGraphicsQueue().presentKHR(...);

    if (result == vk::Result::eErrorOutOfDateKHR ||        // +2
        result == vk::Result::eSuboptimalKHR) {
        recreateSwapchain();
    } else if (result != vk::Result::eSuccess) {           // +1
        throw std::runtime_error(...);
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
// Cyclomatic Complexity = 7
```

**Complexity = 7** (Moderate - acceptable but not simple)

**Ideal**: CC < 5 (simple functions)

---

**Current `Renderer::loadTexture()` complexity**:

```cpp
void Renderer::loadTexture(const std::string& texturePath) {
    // stbi_load
    if (!pixels) {                                         // +1
        throw std::runtime_error(...);
    }

    // Create staging buffer
    stagingBuffer.map();
    stagingBuffer.copyData(pixels, imageSize);
    stagingBuffer.unmap();

    // Create texture image
    textureImage = std::make_unique<VulkanImage>(...);

    // Command buffer recording...
    auto commandBuffer = commandManager->beginSingleTimeCommands();
    textureImage->transitionLayout(...);
    textureImage->copyFromBuffer(...);
    textureImage->transitionLayout(...);
    commandManager->endSingleTimeCommands(*commandBuffer);

    textureImage->createSampler();
    updateDescriptorSets();
}
// Cyclomatic Complexity = 2 (simple but TOO LONG)
```

**Problem**: Low CC but **high line count** (30+ lines) - indicates **low cohesion** (resource management in rendering class)

**Complexity Score**: **6/10** (Acceptable but improvable)

---

### 6. Maintainability Index

**Calculation**:
```
MI = 171 - 5.2 * ln(HV) - 0.23 * CC - 16.2 * ln(LOC)

Where:
- HV = Halstead Volume (vocabulary size)
- CC = Cyclomatic Complexity
- LOC = Lines of Code
```

**Current Renderer.cpp**:
- LOC = ~300 lines
- CC (average) = 6
- HV (estimated) = ~500

```
MI = 171 - 5.2 * ln(500) - 0.23 * 6 - 16.2 * ln(300)
MI = 171 - 32.3 - 1.38 - 92.5
MI ≈ 45
```

**Interpretation**:
- MI > 85: Highly maintainable ✅
- MI 65-85: Moderately maintainable ⚠️
- MI < 65: Difficult to maintain ❌

**Current MI = 45** → **Difficult to maintain** ❌

**Maintainability Score**: **4/10** (Below industry standard)

---

## Summary Scorecard

| Metric | Current (7-Layer) | Target (4-Layer) | Status |
|--------|-------------------|------------------|--------|
| **1. Cohesion** | 6/10 | 9/10 | ❌ Failed |
| **2. Coupling** | 4/10 | 8/10 | ❌ Failed |
| **3. Extensibility** | 4.5/10 | 9/10 | ❌ Failed |
| **4. Testability** | 3/10 | 9/10 | ❌ Failed |
| **5. Complexity** | 6/10 | 7/10 | ⚠️ Borderline |
| **6. Maintainability** | 4/10 | 8/10 | ❌ Failed |
| **TOTAL** | **27.5/60** | **50/60** | ❌ **45.8% FAILED** |

**Conclusion**: The current 7-layer architecture **fails 5 out of 6 quality metrics**.

---

## Root Cause Analysis

### Why Did We Fail?

#### 1. **Incomplete Layer Separation**

**Problem**: Layers are defined by *file structure*, not by *actual separation of concerns*.

```
Current reality:
┌─────────────────────────────────────────┐
│             Renderer                    │
│  - Rendering subsystem management  ✅   │
│  - Resource loading               ❌   │  ← Wrong layer!
│  - Descriptor management          ⚠️   │  ← Should be in Pipeline
│  - Vulkan API calls               ❌   │  ← Too low-level
└─────────────────────────────────────────┘
```

The "Scene Layer" (Mesh) is separate, but the "Resource Layer" (VulkanImage, VulkanBuffer) is **bypassed** by Renderer directly loading textures.

**Layer violation**: Renderer (Layer 3) directly accesses ResourceManager responsibilities.

---

#### 2. **God Object Pattern in Renderer**

**Definition**: A class that knows too much or does too much.

**Evidence**:
```cpp
class Renderer {
    // 9 member objects
    // 13 public/private methods
    // 8 different responsibilities
    // 300+ lines of implementation
};
```

**Classic God Object symptoms**:
- ✅ Too many dependencies (9)
- ✅ Too many methods (13)
- ✅ Too many responsibilities (8)
- ✅ Difficult to test (requires GPU)
- ✅ High coupling (accesses internals of 5+ classes)

**Diagnosis**: **Renderer is a God Object** ❌

---

#### 3. **Missing Subsystem Abstraction**

**Current**: Renderer directly owns and orchestrates 5 rendering subsystems

```cpp
class Renderer {
    VulkanSwapchain* swapchain;
    VulkanPipeline* pipeline;
    CommandManager* commandManager;
    SyncManager* syncManager;
    // ...plus resource management
    // ...plus descriptor management
};
```

**Problem**: No abstraction between "high-level rendering" and "low-level Vulkan subsystems"

**Missing layer**: `RenderingSystem` to encapsulate these 5 subsystems

---

#### 4. **Resource Management Responsibility Violation**

```cpp
// Renderer.cpp:62-103 (42 lines)
void Renderer::loadTexture(const std::string& texturePath) {
    // Image file I/O
    stbi_uc* pixels = stbi_load(...);

    // Staging buffer creation
    VulkanBuffer stagingBuffer(...);
    stagingBuffer.map();
    stagingBuffer.copyData(pixels, imageSize);

    // Image creation
    textureImage = std::make_unique<VulkanImage>(...);

    // Layout transitions
    textureImage->transitionLayout(...);
    textureImage->copyFromBuffer(...);

    // Descriptor updates
    updateDescriptorSets();
}
```

**This function contains 4 different concerns**:
1. File I/O (`stbi_load`)
2. Buffer management (staging buffer)
3. Image management (texture creation)
4. Descriptor management

**Solution**: Extract to `ResourceManager` class

---

## Proposed 4-Layer Architecture

### Clean Separation of Concerns

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                    │
│  ┌───────────────────────────────────────────────────┐  │
│  │           main.cpp + Application                  │  │
│  │  - Window management                              │  │
│  │  - Event loop                                     │  │
│  └───────────────────────────────────────────────────┘  │
└────────────────────┬────────────────────────────────────┘
                     │ delegates to
┌────────────────────▼────────────────────────────────────┐
│                    Renderer Layer                       │
│  ┌──────────────────────────────────────────────────┐   │
│  │               Renderer (Facade)                  │   │
│  │  - Coordinates subsystems                        │   │
│  │  - High-level API only                           │   │
│  └──────────────────────────────────────────────────┘   │
└───┬─────────────────┬─────────────────┬────────────────┘
    │                 │                 │
┌───▼───────────┐ ┌───▼───────────┐ ┌──▼──────────────┐
│ Rendering     │ │    Scene      │ │   Resource      │
│   System      │ │   Manager     │ │   Manager       │
│               │ │               │ │                 │
│ - Swapchain   │ │ - Meshes      │ │ - Textures      │
│ - Pipeline    │ │ - Camera      │ │ - Buffers       │
│ - Commands    │ │ - Materials   │ │ - Loading       │
│ - Sync        │ │               │ │                 │
└───┬───────────┘ └───┬───────────┘ └──┬──────────────┘
    │                 │                 │
    └─────────────────┴─────────────────┘
                      │
┌─────────────────────▼─────────────────────────────────┐
│                   Core Layer                          │
│  - VulkanDevice                                       │
│  - VulkanBuffer (RAII)                                │
│  - VulkanImage (RAII)                                 │
│  - PlatformConfig                                     │
└───────────────────────────────────────────────────────┘
```

### New Renderer (Simplified)

```cpp
class Renderer {
private:
    // Only 3 high-level subsystems
    std::unique_ptr<RenderingSystem> renderingSystem;
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<ResourceManager> resourceManager;

public:
    // Clean high-level API
    void loadModel(const std::string& path) {
        sceneManager->loadMesh(path);
    }

    void loadTexture(const std::string& path) {
        resourceManager->loadTexture(path);
    }

    void drawFrame() {
        renderingSystem->beginFrame();
        renderingSystem->render(sceneManager->getScene());
        renderingSystem->endFrame();
    }
};
```

**Benefits**:
- ✅ 3 dependencies (vs 9)
- ✅ Single responsibility: Coordination
- ✅ No low-level Vulkan API calls
- ✅ Easily testable with mocks
- ✅ Each method < 5 lines

---

### New RenderingSystem (Encapsulation)

```cpp
class RenderingSystem {
private:
    VulkanDevice& device;
    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanPipeline> pipeline;
    std::unique_ptr<CommandManager> commandManager;
    std::unique_ptr<SyncManager> syncManager;

public:
    void beginFrame() {
        syncManager->wait();
        currentImage = swapchain->acquireImage();
    }

    void render(Scene& scene) {
        auto& cmd = commandManager->getCurrentBuffer();
        cmd.begin();
        pipeline->bind(cmd);
        scene.draw(cmd);
        cmd.end();
    }

    void endFrame() {
        swapchain->submit(commandManager->getCurrentBuffer());
        swapchain->present();
    }
};
```

**Benefits**:
- ✅ Encapsulates all rendering details
- ✅ Renderer doesn't know about Semaphores, Queues, etc.
- ✅ Single responsibility: Frame rendering
- ✅ Easy to add multi-pass (shadow, post-processing)

---

### New ResourceManager (Resource Loading)

```cpp
class ResourceManager {
private:
    VulkanDevice& device;
    CommandManager& commandManager;
    std::unordered_map<std::string, std::unique_ptr<VulkanImage>> textures;

public:
    VulkanImage* loadTexture(const std::string& path) {
        // Check cache
        if (textures.contains(path)) {
            return textures[path].get();
        }

        // Load from disk
        auto pixels = loadImageFile(path);

        // Upload to GPU
        auto texture = uploadTexture(pixels);

        textures[path] = std::move(texture);
        return textures[path].get();
    }

private:
    std::unique_ptr<VulkanImage> uploadTexture(const ImageData& data) {
        // All the staging buffer logic moved here
        VulkanBuffer stagingBuffer(...);
        // ...
        return texture;
    }
};
```

**Benefits**:
- ✅ Resource caching (don't reload same texture)
- ✅ All loading logic in one place
- ✅ Renderer doesn't know about staging buffers
- ✅ Easy to add async loading

---

### New SceneManager (Scene Graph)

```cpp
class SceneManager {
private:
    std::vector<std::unique_ptr<Mesh>> meshes;
    Camera camera;

public:
    void loadMesh(const std::string& path) {
        auto mesh = std::make_unique<Mesh>(device, commandManager);
        mesh->loadFromOBJ(path);
        meshes.push_back(std::move(mesh));
    }

    Scene getScene() const {
        return Scene{meshes, camera};
    }
};
```

---

## Expected Improvements

### Metric Projections

| Metric | Current | After 4-Layer | Improvement |
|--------|---------|---------------|-------------|
| **Cohesion** | 6/10 | 9/10 | +50% ✅ |
| **Coupling** | 4/10 | 8/10 | +100% ✅ |
| **Extensibility** | 4.5/10 | 9/10 | +100% ✅ |
| **Testability** | 3/10 | 9/10 | +200% ✅ |
| **Complexity** | 6/10 | 7/10 | +16% ✅ |
| **Maintainability** | 4/10 | 8/10 | +100% ✅ |
| **TOTAL** | 27.5/60 | 50/60 | **+82%** ✅ |

### Code Changes

| Component | Current LOC | After 4-Layer | Change |
|-----------|-------------|---------------|--------|
| Renderer.cpp | 300 | 80 | **-73%** ✅ |
| RenderingSystem.cpp | 0 | 150 | +150 (new) |
| ResourceManager.cpp | 0 | 120 | +120 (new) |
| SceneManager.cpp | 0 | 80 | +80 (new) |
| **Total** | 300 | 430 | +43% (but modular) |

**Trade-off**: More total lines, but **much better structure**

---

## Conclusion

The current 7-layer architecture **appears clean** (18-line main.cpp) but **fails fundamental quality metrics**:

- ❌ Low cohesion (mixed responsibilities)
- ❌ High coupling (knows too many details)
- ❌ Poor extensibility (modify core for new features)
- ❌ Difficult to test (requires GPU)
- ❌ Low maintainability (MI = 45)

**The "7 layers" are an illusion** - Renderer still acts as a God Object.

**The 4-layer architecture** addresses these issues by:
- ✅ True separation of concerns (Rendering, Scene, Resources)
- ✅ Interface-based design (testable with mocks)
- ✅ Each class has single responsibility
- ✅ Low coupling through abstraction

**Next step**: [PHASE8_SUBSYSTEM_SEPARATION.md](PHASE8_SUBSYSTEM_SEPARATION.md) - Implement the 4-layer migration.

---

*Analysis Date: 2025-01-22*
*Current Architecture: 7-Layer (Phase 7 complete)*
*Recommendation: Migrate to 4-Layer for production quality*
