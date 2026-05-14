# Phase 5: Scene Layer - Mesh Class

This document describes the Mesh class implementation in Phase 5 of the refactoring process.

## Goal

Extract mesh data and buffer management into a dedicated Mesh class, separating geometry data from the main application logic.

## Overview

### Before Phase 5
- Vertex and index data stored as raw vectors in main.cpp
- Buffer creation logic scattered in createVertexBuffer() and createIndexBuffer()
- OBJ loading code mixed with application logic
- Repetitive buffer creation patterns
- No abstraction for renderable geometry

### After Phase 5
- Clean Mesh class encapsulating geometry data and buffers
- OBJLoader utility for file loading
- Simple bind() and draw() interface
- Reusable across different model files
- Foundation for material system

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
    Mesh(VulkanDevice& device, CommandManager& commandManager);
    Mesh(VulkanDevice& device, CommandManager& commandManager,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);

    void loadFromOBJ(const std::string& filename);
    void setData(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    void bind(const vk::raii::CommandBuffer& commandBuffer) const;
    void draw(const vk::raii::CommandBuffer& commandBuffer) const;

    size_t getVertexCount() const;
    size_t getIndexCount() const;
    bool hasData() const;

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

**Buffer Creation** (encapsulates staging buffer pattern):
```cpp
void Mesh::createBuffers() {
    vk::DeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();

    // Create and upload vertex buffer
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

    // Similar for index buffer...
}
```

**Simple Rendering Interface**:
```cpp
void Mesh::bind(const vk::raii::CommandBuffer& commandBuffer) const {
    commandBuffer.bindVertexBuffers(0, vertexBuffer->getHandle(), {0});
    commandBuffer.bindIndexBuffer(indexBuffer->getHandle(), 0, vk::IndexType::eUint32);
}

void Mesh::draw(const vk::raii::CommandBuffer& commandBuffer) const {
    commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
}
```

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

**Implementation** (vertex deduplication):
```cpp
void OBJLoader::load(const std::string& filename,
                     std::vector<Vertex>& vertices,
                     std::vector<uint32_t>& indices) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str())) {
        throw std::runtime_error("Failed to load OBJ file: " + filename);
    }

    vertices.clear();
    indices.clear();

    // Vertex deduplication
    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};
            vertex.pos = { /* extract position */ };
            vertex.texCoord = { /* extract texcoord, flip Y */ };
            vertex.color = {1.0f, 1.0f, 1.0f};

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
- Texture coordinate Y-flip for Vulkan convention
- Default white color for vertices

---

## Integration Changes

### Files Modified

**main.cpp**:

**Removed**:
- `std::vector<Vertex> vertices;`
- `std::vector<uint32_t> indices;`
- `std::unique_ptr<VulkanBuffer> vertexBuffer;`
- `std::unique_ptr<VulkanBuffer> indexBuffer;`
- `loadModel()` function (~40 lines)
- `createVertexBuffer()` function (~25 lines)
- `createIndexBuffer()` function (~25 lines)

**Added**:
```cpp
std::unique_ptr<Mesh> mesh;

void createMesh() {
    mesh = std::make_unique<Mesh>(*vulkanDevice, *commandManager);
    mesh->loadFromOBJ(MODEL_PATH);
}
```

**Rendering Updated**:
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

**CMakeLists.txt**:
```cmake
# Scene classes
src/scene/Mesh.cpp
src/scene/Mesh.hpp

# Loader classes
src/loaders/OBJLoader.cpp
src/loaders/OBJLoader.hpp
```

---

## Reflection

### What Worked Well
- Geometry data and GPU buffers managed together
- Clean bind/draw interface simplified rendering code
- Buffer creation patterns consolidated
- Vertex deduplication automatic

### Challenges
- Determining correct ownership of staging buffers
- Ensuring proper cleanup order
- Handling empty mesh cases

### Impact on Later Phases
- Phase 10: Extended with FdF loader for wireframe terrains
- Phase 9: SceneManager built on Mesh abstraction
- Foundation for future material system

---

## Testing

### Build
```bash
cmake --build build
```
Build successful with OBJLoader compiled separately.

### Runtime
```bash
./build/vulkanGLFW
```
Application runs correctly. OBJ file loads, mesh renders properly, vertex deduplication working, no validation errors.

---

## Summary

Phase 5 successfully extracted mesh management into dedicated classes:

**Created**:
- **Mesh class**: Geometry data + GPU buffers with clean bind/draw interface
- **OBJLoader**: Static utility for OBJ file loading with vertex deduplication

**Removed from main.cpp**:
- ~96 lines of mesh and buffer management code
- 4 member variables reduced to 1

**Key Achievements**:
- Scene layer established in architecture
- Consolidated buffer creation patterns
- Separated file loading from application logic
- Foundation for scene management system

---

*Phase 5 Complete*
*Previous: Phase 4 - Rendering Layer*
*Next: Phase 6 - Renderer Integration*
