# Phase 5: Scene Layer - Mesh Class

This document describes the Mesh class implementation in Phase 5 of the refactoring process.

## Goal

Extract mesh data and buffer management into a dedicated Mesh class, separating geometry data from the main application logic.

## Overview

### Before Phase 5
- Vertex and index data stored as raw vectors in main.cpp
- Buffer creation logic scattered in createVertexBuffer() and createIndexBuffer()
- OBJ loading code mixed with application logic (loadModel())
- Repetitive buffer creation patterns
- No abstraction for renderable geometry

### After Phase 5
- Clean Mesh class encapsulating geometry data and buffers
- OBJLoader utility for file loading
- Simple bind() and draw() interface
- Reusable across different model files
- Foundation for material system (Phase 6)

---

## Changes

### 1. Created `Mesh` Class

**Files Created**:
- `src/scene/Mesh.hpp`
- `src/scene/Mesh.cpp`

**Purpose**: Encapsulate vertex/index data and GPU buffers for renderable geometry

**Class Interface**:
```cpp
class Mesh {
public:
    // Constructors
    Mesh(VulkanDevice& device, CommandManager& commandManager);
    Mesh(VulkanDevice& device, CommandManager& commandManager,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);

    // Disable copy, enable move
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = default;
    Mesh& operator=(Mesh&&) = delete;

    // Loading
    void loadFromOBJ(const std::string& filename);
    void setData(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Rendering
    void bind(const vk::raii::CommandBuffer& commandBuffer) const;
    void draw(const vk::raii::CommandBuffer& commandBuffer) const;

    // Queries
    size_t getVertexCount() const { return vertices.size(); }
    size_t getIndexCount() const { return indices.size(); }
    bool hasData() const { return !vertices.empty() && !indices.empty(); }

private:
    VulkanDevice& device;
    CommandManager& commandManager;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;

    void createBuffers();
};
```

#### Implementation Highlights

**Constructor with Data**:
```cpp
Mesh::Mesh(VulkanDevice& device, CommandManager& commandManager,
           const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : device(device), commandManager(commandManager),
      vertices(vertices), indices(indices) {

    if (hasData()) {
        createBuffers();
    }
}
```

**Buffer Creation** (encapsulates staging buffer pattern):
```cpp
void Mesh::createBuffers() {
    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("Cannot create buffers for empty mesh");
    }

    // Create vertex buffer
    vk::DeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();

    VulkanBuffer vertexStagingBuffer(device, vertexBufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    vertexStagingBuffer.map();
    vertexStagingBuffer.copyData(vertices.data(), vertexBufferSize);
    vertexStagingBuffer.unmap();

    vertexBuffer = std::make_unique<VulkanBuffer>(device, vertexBufferSize,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    auto commandBuffer = commandManager.beginSingleTimeCommands();
    vertexBuffer->copyFrom(vertexStagingBuffer, *commandBuffer);
    commandManager.endSingleTimeCommands(*commandBuffer);

    // Create index buffer (similar pattern)
    // ...
}
```

**Simple Rendering Interface**:
```cpp
void Mesh::bind(const vk::raii::CommandBuffer& commandBuffer) const {
    if (!hasData()) {
        throw std::runtime_error("Cannot bind empty mesh");
    }
    commandBuffer.bindVertexBuffers(0, vertexBuffer->getHandle(), {0});
    commandBuffer.bindIndexBuffer(indexBuffer->getHandle(), 0, vk::IndexType::eUint32);
}

void Mesh::draw(const vk::raii::CommandBuffer& commandBuffer) const {
    if (!hasData()) {
        throw std::runtime_error("Cannot draw empty mesh");
    }
    commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
}
```

**Benefits**:
- Encapsulates geometry data and GPU buffers together
- Clean bind/draw interface
- Automatic buffer management with RAII
- Exception safety with error checking

---

### 2. Created `OBJLoader` Utility

**Files Created**:
- `src/loaders/OBJLoader.hpp`
- `src/loaders/OBJLoader.cpp`

**Purpose**: Static utility class for loading OBJ files using tinyobjloader

**Class Interface**:
```cpp
class OBJLoader {
public:
    static void load(const std::string& filename,
                    std::vector<Vertex>& vertices,
                    std::vector<uint32_t>& indices);

private:
    OBJLoader() = delete;  // Static utility class
};
```

**Implementation**:
```cpp
void OBJLoader::load(const std::string& filename,
                     std::vector<Vertex>& vertices,
                     std::vector<uint32_t>& indices) {

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str())) {
        throw std::runtime_error("Failed to load OBJ file: " + filename + "\n" + warn + err);
    }

    vertices.clear();
    indices.clear();

    // Vertex deduplication
    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            // Position
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            // Texture coordinates
            if (index.texcoord_index >= 0) {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]  // Flip Y
                };
            } else {
                vertex.texCoord = {0.0f, 0.0f};
            }

            // Default color
            vertex.color = {1.0f, 1.0f, 1.0f};

            // Deduplicate
            if (!uniqueVertices.contains(vertex)) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }
}
```

**Key Features**:
- Vertex deduplication for optimal performance
- Texture coordinate Y-flip for Vulkan
- Default white color for vertices
- Better error messages with filename

---

## Integration Changes

### Files Modified

#### main.cpp

**Includes Added**:
```cpp
#include "src/scene/Mesh.hpp"
```

**Includes Removed**:
```cpp
#define TINYOBJLOADER_IMPLEMENTATION  // Moved to OBJLoader.cpp
#include <tiny_obj_loader.h>          // No longer needed in main
```

**Member Variables Removed**:
```cpp
std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
std::unique_ptr<VulkanBuffer> vertexBuffer;
std::unique_ptr<VulkanBuffer> indexBuffer;
```

**Member Variables Added**:
```cpp
std::unique_ptr<Mesh> mesh;
```

**Functions Removed** (~90 lines total):
- `loadModel()` (~40 lines) - OBJ loading with vertex deduplication
- `createVertexBuffer()` (~25 lines) - Staging buffer and copy
- `createIndexBuffer()` (~25 lines) - Staging buffer and copy

**Functions Added** (~3 lines):
```cpp
void createMesh() {
    mesh = std::make_unique<Mesh>(*vulkanDevice, *commandManager);
    mesh->loadFromOBJ(MODEL_PATH);
}
```

**Rendering Updated** (in `recordCommandBuffer()`):
```cpp
// Before: Manual buffer binding and draw
commandBuffer.bindVertexBuffers(0, vertexBuffer->getHandle(), {0});
commandBuffer.bindIndexBuffer(indexBuffer->getHandle(), 0, vk::IndexType::eUint32);
commandBuffer.bindDescriptorSets(...);
commandBuffer.drawIndexed(indices.size(), 1, 0, 0, 0);

// After: Clean mesh interface
mesh->bind(commandBuffer);
commandBuffer.bindDescriptorSets(...);
mesh->draw(commandBuffer);
```

#### CMakeLists.txt

**Added**:
```cmake
# Scene classes
src/scene/Mesh.cpp
src/scene/Mesh.hpp
# Loader classes
src/loaders/OBJLoader.cpp
src/loaders/OBJLoader.hpp
```

---

## Code Metrics

### Lines Removed from main.cpp
- Member variables (vertices, indices, buffers): ~4 lines
- loadModel() function: ~40 lines
- createVertexBuffer() function: ~25 lines
- createIndexBuffer() function: ~25 lines
- tinyobjloader include: ~2 lines
- **Total**: ~96 lines removed

### Lines Added
- Mesh.hpp: ~88 lines
- Mesh.cpp: ~98 lines
- OBJLoader.hpp: ~27 lines
- OBJLoader.cpp: ~60 lines
- main.cpp (createMesh): ~3 lines
- **Total**: ~276 lines (in reusable classes)

### Complexity Reduction
- **Before**: Geometry data and buffer management scattered in main.cpp
- **After**: Clean Mesh abstraction with simple bind/draw interface
- **Member Variables**: 4 → 1 (75% reduction)
- **Functions**: 3 → 1 (67% reduction)
- **Lines in main.cpp**: -96 lines (reduced)

---

## Benefits

### 1. Encapsulation
- Geometry data and GPU buffers managed together
- Buffer creation details hidden from main application
- Clear ownership of resources

### 2. Reusability
- Mesh class works with any vertex/index data
- OBJLoader reusable across projects
- Easy to add more loaders (FDF, GLTF, etc.)

### 3. Simplified Rendering
- Single bind() call instead of two buffer binds
- Single draw() call instead of manual drawIndexed
- Clear, intention-revealing interface

### 4. Extensibility
- Foundation for material system
- Easy to add mesh transformations
- Support for multiple meshes per scene

### 5. Maintainability
- OBJ loading logic in dedicated file
- Buffer patterns consolidated
- Easier to debug geometry issues

---

## Testing

### Build
```bash
cmake --build build
```
Build successful with no warnings
OBJLoader compiled separately
Mesh class integrated correctly

### Runtime
```bash
./build/vulkanGLFW
```
Application runs without errors
OBJ file loads correctly
Mesh renders properly
Vertex deduplication working
No validation errors

---

## Architecture Impact

### Scene Layer Introduction

Phase 5 introduces the **Scene Layer**, sitting between the rendering system and application logic:

```
Application (main.cpp)
    ↓ uses
Scene Layer
    ├── Mesh (geometry + buffers)
    └── [Future: Material, Transform, Scene Graph]
    ↓ uses
Rendering Layer
    ├── VulkanPipeline
    ├── VulkanSwapchain
    ├── CommandManager
    └── SyncManager
    ↓ uses
Resource Layer
    ├── VulkanBuffer
    └── VulkanImage
    ↓ uses
Core Layer
    └── VulkanDevice
```

### Comparison

**Before** (monolithic):
```cpp
// In main.cpp
std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
std::unique_ptr<VulkanBuffer> vertexBuffer;
std::unique_ptr<VulkanBuffer> indexBuffer;

loadModel();           // 40 lines
createVertexBuffer();  // 25 lines
createIndexBuffer();   // 25 lines

// Rendering
commandBuffer.bindVertexBuffers(...);
commandBuffer.bindIndexBuffer(...);
commandBuffer.drawIndexed(indices.size(), ...);
```

**After** (modular):
```cpp
// In main.cpp
std::unique_ptr<Mesh> mesh;

createMesh();  // 3 lines

// Rendering
mesh->bind(commandBuffer);
mesh->draw(commandBuffer);
```

---

## Next Steps (Phase 6+)

Phase 5 establishes the scene layer foundation. Future phases can build on this:

### Phase 6: Material System
- Material class (descriptor sets, textures, uniforms)
- Material-Mesh binding
- Multiple materials per scene

### Phase 7: Renderer Integration
- High-level Renderer class
- Automatic descriptor management
- Render queue/sorting

### Phase 8: Scene Graph
- Transform hierarchy
- Multiple mesh instances
- Scene composition

---

## Summary

Phase 5 successfully extracted mesh management into dedicated classes:
- **Mesh class**: Geometry data + GPU buffers with clean bind/draw interface
- **OBJLoader**: Static utility for OBJ file loading with vertex deduplication
- **main.cpp**: Reduced by ~96 lines, cleaner geometry management

Key achievements:
- Introduced Scene Layer in architecture
- Consolidated buffer creation patterns
- Separated file loading from application logic
- Foundation for material system and scene management
- Clean, reusable abstraction for renderable geometry

The refactoring maintains full functionality while significantly improving code organization and extensibility for future features.

---

*Phase 5 Complete*
*Previous: Phase 4 - Rendering Layer*
*Next: Phase 6 - Material System / Renderer Integration*
