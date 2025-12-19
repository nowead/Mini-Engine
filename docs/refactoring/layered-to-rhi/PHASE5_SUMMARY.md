# Phase 5: Scene Layer RHI Migration - Summary

**Phase**: Phase 5 of 11
**Status**: ⏳ **PLANNED**
**Estimated Duration**: 2-3 days
**Estimated LOC**: ~230 lines

---

## Overview

Phase 5에서는 Scene Layer (Mesh, ResourceManager, SceneManager)를 RHI 추상화 레이어로 마이그레이션합니다. 현재 Renderer에 존재하는 중복 버퍼를 제거하고, Mesh가 직접 RHI 버퍼를 소유하도록 변경합니다.

**핵심 목표**:
- Mesh 클래스의 VulkanBuffer → RHIBuffer 마이그레이션
- ResourceManager의 VulkanImage → RHITexture 마이그레이션
- Renderer의 중복 버퍼 제거
- CommandManager의 RHI 기반 재구현 (완전 제거)

---

## Current State Analysis

### Component Dependencies

```
┌─────────────────────────────────────────────────────────────┐
│                         Renderer                             │
│  - rhiVertexBuffer (DUPLICATE)                              │
│  - rhiIndexBuffer (DUPLICATE)                               │
│  - rhiDepthImage ✅                                          │
│  - rhiUniformBuffers ✅                                      │
└─────────────────────────────────────────────────────────────┘
                          ▲
                          │ uses
                          │
┌─────────────────────────┴────────────────────────────────────┐
│                    SceneManager                              │
│  - meshes (vector<Mesh>)                                    │
└──────────────────────────────────────────────────────────────┘
                          ▲
                          │ owns
                          │
┌─────────────────────────┴────────────────────────────────────┐
│                         Mesh                                 │
│  - VulkanBuffer* vertexBuffer ❌ (LEGACY)                    │
│  - VulkanBuffer* indexBuffer ❌ (LEGACY)                     │
│  - CommandManager& commandManager ❌                         │
└──────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                   ResourceManager                            │
│  - VulkanImage* textureCache ❌ (LEGACY)                     │
│  - CommandManager& commandManager ❌                         │
└─────────────────────────────────────────────────────────────┘
```

### Issues to Resolve

| Issue | Component | Description | Priority |
|-------|-----------|-------------|----------|
| **Buffer Duplication** | Renderer + Mesh | Same mesh data exists twice in memory | 🔴 Critical |
| **Legacy Buffers** | Mesh | Still using VulkanBuffer instead of RHIBuffer | 🔴 Critical |
| **Legacy Textures** | ResourceManager | Still using VulkanImage instead of RHITexture | 🔴 Critical |
| **CommandManager** | Mesh, ResourceManager | Vulkan-specific, blocks WebGPU support | 🟡 High |

---

## Tasks Breakdown

### Task 5.1: Mesh - VulkanBuffer → RHIBuffer ✅ P0

**Goal**: Migrate Mesh's internal buffers to RHI

**Files to Modify**:
- `src/scene/Mesh.hpp` (~30 lines)
- `src/scene/Mesh.cpp` (~60 lines)

**Changes**:
```cpp
// BEFORE
class Mesh {
    VulkanDevice& device;
    CommandManager& commandManager;
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
};

// AFTER
class Mesh {
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;
    std::unique_ptr<rhi::RHIBuffer> vertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> indexBuffer;
public:
    rhi::RHIBuffer* getVertexBuffer() const { return vertexBuffer.get(); }
    rhi::RHIBuffer* getIndexBuffer() const { return indexBuffer.get(); }
};
```

**Key Implementation Details**:
1. Remove `VulkanDevice&` and `CommandManager&` dependencies
2. Add `rhi::RHIDevice*` and `rhi::RHIQueue*` dependencies
3. Change buffer types from `VulkanBuffer` to `RHIBuffer`
4. Add public getter methods for RHI buffers
5. Update `createBuffers()` to use RHI buffer creation
6. Remove legacy `bind()` and `draw()` methods (now handled by Renderer)

**Acceptance Criteria**:
- [ ] Mesh uses RHIBuffer internally
- [ ] No VulkanDevice or VulkanBuffer dependencies
- [ ] Public getters for RHI buffers available
- [ ] Compiles without errors
- [ ] OBJ and FDF loading still works

---

### Task 5.2: Mesh - Update Constructor & Dependencies ✅ P0

**Goal**: Update Mesh constructor to accept RHI dependencies

**Files to Modify**:
- `src/scene/Mesh.hpp` (~10 lines)
- `src/scene/Mesh.cpp` (~20 lines)
- `src/scene/SceneManager.cpp` (~15 lines)

**Changes**:
```cpp
// Mesh.hpp
class Mesh {
public:
    Mesh(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    Mesh(rhi::RHIDevice* device, rhi::RHIQueue* queue,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
};

// SceneManager.cpp - Update mesh creation
meshes.emplace_back(rhiDevice, graphicsQueue);
```

**Acceptance Criteria**:
- [ ] Constructor accepts RHI dependencies
- [ ] SceneManager passes correct RHI pointers
- [ ] All mesh creation sites updated

---

### Task 5.3: Renderer - Remove Duplicate Buffers ✅ P0

**Goal**: Remove `rhiVertexBuffer` and `rhiIndexBuffer` from Renderer, use Mesh's buffers instead

**Files to Modify**:
- `src/rendering/Renderer.hpp` (~20 lines)
- `src/rendering/Renderer.cpp` (~60 lines)

**Changes to Remove**:
```cpp
// Renderer.hpp - DELETE THESE
std::unique_ptr<rhi::RHIBuffer> rhiVertexBuffer;  // ❌ Remove
std::unique_ptr<rhi::RHIBuffer> rhiIndexBuffer;   // ❌ Remove
size_t rhiIndexCount = 0;                         // ❌ Remove

// Methods to delete
void createRHIVertexIndexBuffers();  // ❌ Remove entire method
```

**Changes to Update**:
```cpp
// Renderer.cpp - drawFrameRHI()
// BEFORE
if (rhiVertexBuffer && rhiIndexBuffer && rhiIndexCount > 0) {
    renderPass->setVertexBuffer(0, rhiVertexBuffer.get(), 0);
    renderPass->setIndexBuffer(rhiIndexBuffer.get(), rhi::IndexFormat::Uint32, 0);
    renderPass->drawIndexed(rhiIndexCount, 1, 0, 0, 0);
}

// AFTER
auto* mesh = sceneManager.getActiveMesh();  // Assume SceneManager provides this
if (mesh && mesh->hasData()) {
    renderPass->setVertexBuffer(0, mesh->getVertexBuffer(), 0);
    renderPass->setIndexBuffer(mesh->getIndexBuffer(), rhi::IndexFormat::Uint32, 0);
    renderPass->drawIndexed(mesh->getIndexCount(), 1, 0, 0, 0);
}
```

**Memory Impact**:
- **Before**: Mesh data stored in VulkanBuffer (legacy) + RHIBuffer (duplicate) = 2x memory
- **After**: Mesh data stored in RHIBuffer only = 1x memory

**Example** (with current test model):
- Vertex data: 742 KB × 2 = 1.48 MB → 742 KB (saved 742 KB)
- Index data: 369 KB × 2 = 738 KB → 369 KB (saved 369 KB)
- **Total savings**: ~1.11 MB per mesh

**Acceptance Criteria**:
- [ ] No duplicate buffers in Renderer
- [ ] Renderer uses Mesh's RHI buffers directly
- [ ] Memory usage reduced by ~50% for mesh data
- [ ] Rendering output identical to before

---

### Task 5.4: ResourceManager - VulkanImage → RHITexture ✅ P0

**Goal**: Migrate texture cache from VulkanImage to RHITexture

**Files to Modify**:
- `src/resources/ResourceManager.hpp` (~30 lines)
- `src/resources/ResourceManager.cpp` (~80 lines)

**Changes**:
```cpp
// ResourceManager.hpp
class ResourceManager {
public:
    ResourceManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);

    rhi::RHITexture* loadTexture(const std::string& path);  // Changed return type
    rhi::RHITexture* getTexture(const std::string& path);   // Changed return type

private:
    rhi::RHIDevice* rhiDevice;  // Changed from VulkanDevice&
    rhi::RHIQueue* graphicsQueue;  // Added

    std::unordered_map<std::string, std::unique_ptr<rhi::RHITexture>> textureCache;

    std::unique_ptr<rhi::RHITexture> uploadTexture(
        unsigned char* pixels, int width, int height, int channels);
};
```

**Implementation Details**:
```cpp
// ResourceManager.cpp - uploadTexture()
std::unique_ptr<rhi::RHITexture> ResourceManager::uploadTexture(
    unsigned char* pixels, int width, int height, int channels) {

    vk::DeviceSize imageSize = width * height * channels;

    // 1. Create staging buffer (RHI)
    rhi::BufferDesc stagingDesc{};
    stagingDesc.size = imageSize;
    stagingDesc.usage = rhi::BufferUsage::CopySrc;
    stagingDesc.memoryUsage = rhi::MemoryUsage::Upload;
    auto stagingBuffer = rhiDevice->createBuffer(stagingDesc);

    // 2. Copy pixel data to staging buffer
    void* mapped = stagingBuffer->map();
    memcpy(mapped, pixels, imageSize);
    stagingBuffer->unmap();

    // 3. Create texture (RHI)
    rhi::TextureDesc textureDesc{};
    textureDesc.dimension = rhi::TextureDimension::Texture2D;
    textureDesc.format = rhi::TextureFormat::RGBA8Unorm_sRGB;
    textureDesc.width = width;
    textureDesc.height = height;
    textureDesc.usage = rhi::TextureUsage::CopyDst | rhi::TextureUsage::TextureBinding;
    auto texture = rhiDevice->createTexture(textureDesc);

    // 4. Copy staging buffer to texture (using RHI CommandEncoder)
    auto encoder = rhiDevice->createCommandEncoder();
    encoder->begin();

    // Transition to CopyDst
    encoder->textureBarrier(texture.get(),
        rhi::TextureLayout::Undefined,
        rhi::TextureLayout::CopyDst);

    // Copy buffer to texture
    rhi::BufferTextureCopyInfo copyInfo{};
    copyInfo.buffer = stagingBuffer.get();
    copyInfo.bytesPerRow = width * channels;

    rhi::TextureCopyInfo texInfo{};
    texInfo.texture = texture.get();

    rhi::Extent3D extent{width, height, 1};
    encoder->copyBufferToTexture(copyInfo, texInfo, extent);

    // Transition to ShaderReadOnly
    encoder->textureBarrier(texture.get(),
        rhi::TextureLayout::CopyDst,
        rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();

    // 5. Submit and wait
    graphicsQueue->submit(cmdBuffer.get());
    graphicsQueue->waitIdle();

    return texture;
}
```

**Acceptance Criteria**:
- [ ] ResourceManager uses RHITexture
- [ ] No VulkanDevice or VulkanImage dependencies
- [ ] Texture loading works correctly
- [ ] Texture cache functional
- [ ] No memory leaks

---

### Task 5.5: SceneManager - Update to RHI Types ✅ P1

**Goal**: Update SceneManager to use RHI types

**Files to Modify**:
- `src/scene/SceneManager.hpp` (~10 lines)
- `src/scene/SceneManager.cpp` (~20 lines)

**Changes**:
```cpp
// SceneManager.hpp
class SceneManager {
public:
    SceneManager(rhi::RHIDevice* device, rhi::RHIQueue* queue,
                 ResourceManager& resourceManager);

    Mesh* getActiveMesh() const;  // Add getter for Renderer

private:
    rhi::RHIDevice* rhiDevice;  // Changed from VulkanDevice&
    rhi::RHIQueue* graphicsQueue;  // Added
};
```

**Acceptance Criteria**:
- [ ] SceneManager uses RHI types
- [ ] Mesh creation uses RHI dependencies
- [ ] Model loading (OBJ, FDF) works

---

### Task 5.6: CommandManager - Complete Removal ✅ P0

**Goal**: Remove CommandManager entirely, replace with direct RHI usage

**Decision**: **Choice 1 - Complete Removal** (selected on 2025-12-20)

**Files to Modify**:
- `src/scene/Mesh.cpp` (~40 lines) - staging operations
- `src/resources/ResourceManager.cpp` (~30 lines) - texture upload
- `src/ui/ImGuiManager.cpp` (~20 lines) - font texture upload

**Files to Delete**:
- `src/core/CommandManager.hpp` ❌
- `src/core/CommandManager.cpp` ❌
- Update `CMakeLists.txt` to remove CommandManager sources

**Migration Pattern**:

```cpp
// BEFORE (CommandManager)
auto commandBuffer = commandManager.beginSingleTimeCommands();
buffer->copyData(*commandBuffer, stagingBuffer, size);
commandManager.endSingleTimeCommands(*commandBuffer);

// AFTER (Direct RHI)
auto encoder = rhiDevice->createCommandEncoder();
encoder->begin();
encoder->copyBuffer(stagingBuffer, buffer, size);
auto cmdBuffer = encoder->finish();
graphicsQueue->submit(cmdBuffer.get());
graphicsQueue->waitIdle();
```

**Benefits**:
- ✅ Complete Vulkan decoupling
- ✅ Direct RHI usage (no wrapper layer)
- ✅ Consistent with Phase 4 patterns
- ✅ WebGPU/D3D12/Metal ready

**Challenges**:
- ⚠️ More boilerplate code (5 lines vs 2 lines)
- ⚠️ Multiple file modifications

**Acceptance Criteria**:
- [ ] CommandManager.hpp deleted
- [ ] CommandManager.cpp deleted
- [ ] All CommandManager usages replaced with RHI
- [ ] CMakeLists.txt updated
- [ ] Compiles without errors
- [ ] All staging operations work correctly

---

### Task 5.7: Integration Testing ✅ P0

**Goal**: Verify all components work together after migration

**Test Cases**:

| Test | Description | Expected Result |
|------|-------------|-----------------|
| **OBJ Loading** | Load `models/viking_room.obj` | Model renders correctly |
| **FDF Loading** | Load `models/fdf/42.fdf` | Wireframe renders correctly |
| **Texture Loading** | Load `viking_room.png` | Texture displays correctly |
| **Multiple Meshes** | Load 2+ models | All render without corruption |
| **Memory Usage** | Check memory after loading | ~50% reduction in mesh data |
| **Performance** | Measure frame time | < 5% increase |

**Validation**:
```bash
# Run application
./vulkanGLFW models/viking_room.obj

# Check for Vulkan validation errors (should be 0)
# Verify visual output matches pre-Phase-5 baseline
# Profile memory usage
```

**Acceptance Criteria**:
- [ ] All test cases pass
- [ ] No Vulkan validation errors
- [ ] Visual output identical to Phase 4
- [ ] Memory usage reduced by ~1 MB per mesh
- [ ] Frame time within 5% of baseline

---

## Phase Completion Checklist

### Code Changes
- [ ] Task 5.1: Mesh buffer migration complete
- [ ] Task 5.2: Mesh constructor updated
- [ ] Task 5.3: Renderer duplicate buffers removed
- [ ] Task 5.4: ResourceManager texture migration complete
- [ ] Task 5.5: SceneManager updated to RHI types
- [ ] Task 5.6: CommandManager removed entirely
- [ ] Task 5.7: Integration tests passing

### Code Quality
- [ ] No `#include "VulkanBuffer.hpp"` in Scene layer
- [ ] No `#include "VulkanImage.hpp"` in Scene layer
- [ ] No `#include "CommandManager.hpp"` anywhere
- [ ] All public APIs use RHI types
- [ ] No Vulkan types leaked to upper layers

### Documentation
- [ ] Code comments updated
- [ ] Public API documented with Doxygen
- [ ] Phase 5 summary completed
- [ ] Migration decisions documented

### Testing
- [ ] Build succeeds with no warnings
- [ ] OBJ model loading works
- [ ] FDF model loading works
- [ ] Texture loading works
- [ ] No memory leaks (Valgrind/ASAN)
- [ ] Vulkan validation clean (0 errors)

### Git Management
- [ ] Git tag created: `phase5-complete`
- [ ] Commit messages descriptive
- [ ] No unintended file changes

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| **Buffer creation API mismatch** | Medium | High | Reference Phase 4 implementation, test early |
| **Texture upload complexity** | High | High | Implement in small steps, test after each change |
| **CommandManager removal breaks ImGui** | Low | Medium | Defer ImGui changes to Phase 6 if needed |
| **Memory management errors** | Medium | Critical | Use ASAN, verify with Valgrind |
| **Performance regression** | Low | Medium | Benchmark before/after each task |

---

## Rollback Plan

**Git Tags**:
- Before Phase 5: `phase4-complete` ✅
- After Task 5.3: `phase5.3-no-duplicate-buffers`
- After Task 5.4: `phase5.4-texture-migration`
- After Task 5.6: `phase5.6-no-command-manager`
- Phase 5 complete: `phase5-complete`

**Rollback Procedure**:
```bash
# If critical issues arise
git checkout phase4-complete
git branch phase5-failed
git tag phase5-rollback
```

---

## Success Metrics

| Metric | Target | How to Measure |
|--------|--------|----------------|
| **Code Changes** | ~230 lines | Git diff stats |
| **Memory Reduction** | ~1 MB per mesh | Before/after profiling |
| **Performance Overhead** | < 5% | Frame time comparison |
| **Validation Errors** | 0 | Vulkan validation layer |
| **Memory Leaks** | 0 | Valgrind/ASAN |
| **Test Pass Rate** | 100% | Integration test results |

---

## Next Steps After Phase 5

1. **Phase 6: ImGui Migration** (3-4 days)
   - ImGui backend abstraction
   - Vulkan ImGui adapter
   - Integration with RHI render pass

2. **Phase 7: Testing & Cleanup** (1-2 weeks)
   - Unit test suite
   - Performance profiling
   - Legacy code cleanup
   - Documentation completion

3. **Phase 8: WebGPU Backend** (2-3 weeks)
   - WebGPU implementation
   - SPIR-V to WGSL conversion
   - Browser deployment

---

## Key Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2025-12-20 | CommandManager: Choice 1 (Complete Removal) | Prefer direct RHI usage over wrapper layer for simplicity and consistency |
| 2025-12-20 | Buffer ownership: Mesh owns buffers | Eliminate duplication, reduce memory usage |
| 2025-12-20 | Texture cache: RHITexture only | Consistent with buffer migration approach |

---

## References

- [RHI Migration PRD](RHI_MIGRATION_PRD.md) - Overall project plan
- [Phase 4 Summary](PHASE4_SUMMARY.md) - Previous phase
- [RHI Technical Guide](RHI_TECHNICAL_GUIDE.md) - RHI API reference
- [Legacy Code Reference](LEGACY_CODE_REFERENCE.md) - Vulkan code patterns

---

**Last Updated**: 2025-12-20
**Status**: Ready to begin implementation
