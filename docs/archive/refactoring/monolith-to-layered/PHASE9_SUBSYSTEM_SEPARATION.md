# Phase 9: Subsystem Separation - ResourceManager & SceneManager

This document describes the subsystem separation in Phase 9 of the refactoring process.

## Goal

Extract resource loading and scene management from Renderer into dedicated manager classes, transforming the God Object pattern into a clean 4-layer architecture.

## Overview

### Problem
Renderer class had too many responsibilities:
- Low cohesion (mixed rendering, resource loading, scene management)
- High coupling (knows Vulkan implementation details)
- Poor testability (requires GPU)
- Difficult to extend

### Solution
Create two independent managers:
- **ResourceManager**: Texture loading, caching, staging buffer management
- **SceneManager**: Mesh loading, scene graph foundation
- **Renderer**: Directly owns rendering components, uses managers

---

## Changes

### 1. Created `ResourceManager`

**Files Created**:
- `src/resources/ResourceManager.hpp`
- `src/resources/ResourceManager.cpp`

**Purpose**: Centralize resource loading and caching

**Class Interface**:
```cpp
class ResourceManager {
public:
    ResourceManager(VulkanDevice& device, CommandManager& commandManager);

    VulkanImage* loadTexture(const std::string& texturePath);
    VulkanImage* getTexture(const std::string& texturePath);

private:
    VulkanDevice& device;
    CommandManager& commandManager;
    std::unordered_map<std::string, std::unique_ptr<VulkanImage>> textureCache;
};
```

**Key Features**:
- Automatic texture caching
- Staging buffer management internalized
- Returns raw pointers (ResourceManager retains ownership)

---

### 2. Created `SceneManager`

**Files Created**:
- `src/scene/SceneManager.hpp`
- `src/scene/SceneManager.cpp`

**Purpose**: Manage scene content and geometry

**Class Interface**:
```cpp
class SceneManager {
public:
    SceneManager(VulkanDevice& device, CommandManager& commandManager);

    void loadMesh(const std::string& modelPath);
    std::vector<std::unique_ptr<Mesh>>& getMeshes();

private:
    VulkanDevice& device;
    CommandManager& commandManager;
    std::vector<std::unique_ptr<Mesh>> meshes;
};
```

**Key Features**:
- Mesh collection management
- Foundation for scene graph (future enhancement)
- Automatic file format detection

---

### 3. Modified `Renderer`

**Changes**:
- Removed texture/mesh loading logic
- Added ResourceManager and SceneManager members
- Simplified to rendering coordination

**Before**:
```cpp
class Renderer {
private:
    std::unique_ptr<VulkanImage> textureImage;
    std::unique_ptr<Mesh> mesh;

    void loadTexture(const std::string& path);  // ~40 lines
    void loadModel(const std::string& path);    // ~15 lines
};
```

**After**:
```cpp
class Renderer {
private:
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<SceneManager> sceneManager;

    // Direct ownership of rendering components
    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanPipeline> pipeline;
    std::unique_ptr<SyncManager> syncManager;

public:
    void loadTexture(const std::string& path) {
        VulkanImage* texture = resourceManager->loadTexture(path);
        updateDescriptorSets(texture);
    }

    void loadMesh(const std::string& path) {
        sceneManager->loadMesh(path);
    }
};
```

---

## 4-Layer Architecture

```
Application Layer
└── Application

Rendering Layer
└── Renderer (orchestrates managers + owns rendering components)
    ├── Uses: ResourceManager
    ├── Uses: SceneManager
    └── Owns: Swapchain, Pipeline, SyncManager

Scene/Resource Layer
├── ResourceManager (asset loading)
├── SceneManager (scene graph)
├── Mesh
└── Loaders (OBJ, FDF)

Core Layer
├── VulkanDevice
├── CommandManager
├── VulkanBuffer
└── VulkanImage
```

---

## Reflection

### What Worked Well
- Clear separation of concerns achieved
- Renderer simplified to rendering coordination
- Managers independently testable
- Resource caching automatic

### Challenges
- Determining correct ownership semantics
- Managing dependencies between managers
- Ensuring proper cleanup order

### Impact on Later Phases
- Phase 10: SceneManager extended for FdF format support
- Foundation for future features (material system, scene graph)
- Improved testability enables better debugging

---

## Testing

### Build
```bash
cmake --build build
```
Build successful with new manager classes.

### Runtime
```bash
./build/vulkanGLFW
```
Application runs correctly with resource/scene management separated.

---

## Summary

Phase 9 successfully separated subsystems:

**Created**:
- ResourceManager: Texture loading and caching
- SceneManager: Mesh and scene management

**Modified**:
- Renderer: Simplified to rendering coordination

**Removed from Renderer**:
- ~55 lines of resource loading logic
- Resource ownership responsibilities

**Architecture**:
- 4-layer design established
- Renderer owns rendering components directly
- 2 independent managers for resources and scene

---

*Phase 9 Complete*
*Previous: Phase 8 - Cross-Platform Support*
*Next: Phase 10 - FdF Integration (Enhancement)*
