# Phase 11: Dear ImGui Integration for Debugging UI

This document describes the Dear ImGui integration in Phase 11.

## Goal

Integrate Dear ImGui library to provide real-time debugging UI and runtime controls while maintaining the established 4-layer RAII architecture.

## Overview

### Motivation
- Provide visual feedback for camera state and FdF parameters
- Enable runtime configuration without recompilation
- Demonstrate UI overlay integration without compromising architecture
- Follow industry-standard practices for game engine debugging tools

### Key Achievement
Complete ImGui integration with platform-specific rendering (dynamic rendering on macOS, render pass on Linux) while maintaining pure RAII and zero memory leaks.

---

## Problem Statement

### Challenges Faced

1. **Vulkan RAII Dispatcher Version Mismatch**
   - vcpkg's prebuilt ImGui compiled with different Vulkan header version
   - Assertion failure: `m_dispatcher->getVkHeaderVersion() == VK_HEADER_VERSION`

2. **Command Buffer State Management**
   - ImGui needs to render into same command buffer as main rendering
   - Proper lifecycle management required (begin → main render → ImGui render → end)

3. **Platform-Specific Rendering**
   - Linux: Requires traditional render pass
   - macOS: Requires dynamic rendering (MoltenVK limitation)

4. **Resource Cleanup Order**
   - ImGuiManager depends on Renderer's Vulkan resources
   - C++ destructor order critical for avoiding segfaults

5. **Architectural Layer Placement**
   - UI overlay belongs in Application layer (peer to Renderer)
   - Application orchestrates Renderer + ImGuiManager

---

## Solutions

### 1. Vulkan Header Version Mismatch - volk Meta-Loader

**vcpkg.json modification**:
```json
{
  "dependencies": [
    "volk",  // CRITICAL: Resolves header version mismatch
    {
      "name": "imgui",
      "features": ["glfw-binding", "vulkan-binding"]
    }
  ]
}
```

**Why this works**:
- volk is a Vulkan meta-loader that dynamically loads Vulkan functions at runtime
- vcpkg's ImGui build is configured to use volk when available
- volk bypasses header version checks by loading functions dynamically

---

### 2. Command Buffer Lifecycle - Callback-Based Rendering

**Renderer provides callback-based drawFrame()**:
```cpp
// Renderer.hpp
void drawFrame(std::function<void(const vk::raii::CommandBuffer&, uint32_t)> imguiRenderCallback = nullptr);
```

**Renderer::drawFrame() orchestrates full lifecycle**:
```cpp
void Renderer::drawFrame(std::function<void(const vk::raii::CommandBuffer&, uint32_t)> imguiCallback) {
    // 1-4: Acquire image, wait fence, begin command buffer, record main rendering
    recordCommandBuffer(imageIndex);

    // 5: Record ImGui (if provided)
    if (imguiCallback) {
        imguiCallback(commandManager->getCommandBuffer(currentFrame), imageIndex);
    }

    // 6: End command buffer (ONCE, after all rendering)
    commandManager->getCommandBuffer(currentFrame).end();

    // 7-8: Submit and present
}
```

**Application calls Renderer with ImGui callback**:
```cpp
void Application::mainLoop() {
    renderer->drawFrame([this](const vk::raii::CommandBuffer& cmd, uint32_t imageIndex) {
        imguiManager->newFrame();
        imguiManager->renderUI(*camera, USE_FDF_MODE, ...);
        imguiManager->render(cmd, imageIndex);
    });
}
```

---

### 3. Platform-Specific ImGui Rendering

**ImGuiManager::render() implementation**:
```cpp
void ImGuiManager::render(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) {
    ImGui::Render();
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);

    #ifdef __linux__
    // Linux: Main rendering already started render pass, just draw into it
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
    #else
    // macOS/Windows: Create separate dynamic rendering pass for ImGui
    vk::RenderingAttachmentInfo colorAttachment{
        .imageView = swapchain.getImageViews()[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,  // Load existing content (overlay)
        .storeOp = vk::AttachmentStoreOp::eStore
    };

    vk::RenderingInfo renderingInfo{ /* ... */ };
    commandBuffer.beginRendering(renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
    commandBuffer.endRendering();
    #endif
}
```

**Key Points**:
- **Linux**: ImGui renders into main render pass (still active)
- **macOS**: ImGui renders into separate dynamic rendering pass
- **loadOp = eLoad**: Preserves main rendering result, overlays ImGui on top

---

### 4. RAII Resource Cleanup Order

**Correct Member Declaration Order**:
```cpp
class Application {
private:
    // Destruction order: 4 → 3 → 2 → 1
    GLFWwindow* window = nullptr;                // Destroyed LAST (4th)
    std::unique_ptr<Camera> camera;              // Destroyed 3rd
    std::unique_ptr<Renderer> renderer;          // Destroyed 2nd (calls waitIdle)
    std::unique_ptr<ImGuiManager> imguiManager;  // Destroyed FIRST (1st)
};
```

**Why This Works**:
1. `~ImGuiManager()` runs first → cleans up ImGui resources while Renderer still valid
2. `~Renderer()` runs second → calls `device->getDevice().waitIdle()` before cleanup
3. `~Camera()` runs third → no special cleanup needed
4. `glfwDestroyWindow(window)` runs last → manual cleanup in `~Application()`

**Renderer Destructor**:
```cpp
Renderer::~Renderer() {
    if (device) {
        device->getDevice().waitIdle();  // CRITICAL: Wait for GPU
    }
    // All RAII members destroyed automatically in reverse declaration order
}
```

---

## ImGuiManager Implementation

**Files Created**:
- `src/ui/ImGuiManager.hpp`
- `src/ui/ImGuiManager.cpp`

**Class Interface**:
```cpp
class ImGuiManager {
public:
    ImGuiManager(GLFWwindow* window,
                 VulkanDevice& device,
                 VulkanSwapchain& swapchain,
                 CommandManager& commandManager);
    ~ImGuiManager();

    void newFrame();
    void renderUI(Camera& camera, bool isFdfMode, ...);
    void render(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);

private:
    VulkanDevice& device;
    VulkanSwapchain& swapchain;
    CommandManager& commandManager;
    vk::raii::DescriptorPool imguiPool = nullptr;
};
```

**UI Features**:
- FPS counter
- Camera position and distance display
- Projection mode display
- Projection toggle button
- Reset camera button
- Mode toggle button (OBJ/FdF)
- File loading input (prepared for future)

---

## Reflection

### What Worked Well
- volk solved header version mismatch cleanly
- Callback pattern enabled clean command buffer lifecycle
- Platform-specific paths isolated in ImGuiManager
- Pure RAII cleanup with correct member declaration order

### Challenges
- Understanding Vulkan RAII dispatcher version requirements
- Determining proper command buffer lifecycle management
- Managing platform differences (render pass vs. dynamic rendering)
- Ensuring correct destruction order to avoid segfaults

### Impact on Later Phases
- Foundation for runtime configuration UI
- Demonstrates clean UI overlay integration
- Validates RAII architecture robustness

---

## Testing

### Platform Testing
- macOS: Dynamic rendering path works correctly
- Linux: Traditional render pass path works correctly
- ImGui renders on top of main scene (overlay)
- No memory leaks (RAII cleanup verified)
- No segfaults on exit

### Functional Testing
- FPS counter updates correctly
- Camera info displays correctly
- Buttons respond correctly
- Window resize handling works

---

## Summary

Phase 11 successfully integrated Dear ImGui:

**Created**:
- ImGuiManager: UI lifecycle management and rendering

**Modified**:
- Application: ImGuiManager integration, RAII destruction order
- Renderer: Callback-based rendering for ImGui
- vcpkg.json: Added volk and imgui dependencies
- CMakeLists.txt: ImGui build integration

**Key Achievements**:
- Real-time debugging UI overlay
- Platform-specific rendering (dynamic rendering on macOS, render pass on Linux)
- Pure RAII cleanup (member declaration order critical)
- Vulkan header version mismatch solved with volk
- Zero memory leaks, no segfaults

---

*Phase 11 Complete*
*Previous: Phase 10 - FdF Integration*
*All Phases Complete*
