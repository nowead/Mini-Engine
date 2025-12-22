# RHI Migration - Product Requirements Document

**Project**: Mini-Engine RHI (Render Hardware Interface) Architecture Migration
**Version**: 3.0
**Status**: ✅ **Core Migration Complete (Phase 1-8)**

---

## Executive Summary

This document outlines the migration of Mini-Engine from a Vulkan-only layered architecture to a multi-backend RHI (Render Hardware Interface) architecture. The migration enables cross-platform graphics API support while maintaining performance and code quality.

### Project Goals

- **Multi-Backend Support**: Enable Vulkan, WebGPU, D3D12, and Metal backends
- **Platform Independence**: Abstract graphics API from application layers
- **Web Deployment**: Enable browser execution via WebGPU/WebAssembly
- **Zero-Cost Abstraction**: Maintain performance overhead < 5%
- **Clean Architecture**: Follow industry patterns (Unreal Engine, Unity)

### Current Status

| Phase | Status | Description |
|-------|--------|-------------|
| **Phase 1** | ✅ Complete | RHI Interface Design & Specification |
| **Phase 2** | ✅ Complete | Vulkan Backend Implementation |
| **Phase 3** | ✅ Complete | Factory Pattern & Integration Bridge |
| **Phase 4** | ✅ Complete | Renderer Core Migration |
| **Phase 5** | ✅ Complete | Scene & Resource Layer Migration |
| **Phase 6** | ✅ Complete | ImGui Adapter Integration |
| **Phase 7** | ✅ Complete | Complete Renderer Migration & Cleanup |
| **Phase 8** | ✅ Complete | Legacy Cleanup & Modular Architecture |
| **Phase 9+** | 📋 Planned | WebGPU Backend & Advanced Features |

**🎉 Achievements:**
- ✅ 100% RHI-native rendering pipeline
- ✅ ~890 LOC legacy code removed
- ✅ Modular architecture (src/rhi/ + src/rhi-vulkan/)
- ✅ Zero Vulkan validation errors
- ✅ Cross-platform support (Linux Vulkan 1.1 + macOS/Windows Vulkan 1.3)

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Success Criteria](#success-criteria)
3. [Phase Breakdown](#phase-breakdown)
4. [Dependencies](#dependencies)
5. [Quality Metrics](#quality-metrics)
6. [Risk Management](#risk-management)

---

## Project Overview

### Primary Objectives

1. **Multi-Backend Architecture**
   - Support Vulkan as primary backend
   - Add WebGPU for web deployment
   - Prepare for D3D12 (Windows) and Metal (macOS/iOS) backends

2. **API Independence**
   - Upper layers (Renderer, ResourceManager, SceneManager) become API-agnostic
   - Graphics API details isolated to backend implementations
   - Clean separation of concerns following DIP and OCP principles

3. **Performance Maintenance**
   - Virtual function overhead < 5%
   - No memory overhead beyond vtable pointers
   - Optimize hot paths with compile-time dispatch options

4. **Code Quality**
   - Comprehensive documentation
   - Type-safe interfaces with modern C++17/20 features
   - RAII pattern throughout

### Secondary Objectives

1. Web deployment capability via Emscripten + WebGPU
2. Plugin-like backend architecture for easy API addition
3. Shader cross-compilation pipeline (SPIR-V as IR)
4. Automated testing and CI/CD pipeline

---

## Success Criteria

### Overall Project Success

The migration is considered complete when:

1. **Functionality**: 100% feature parity with pre-migration
2. **Performance**: < 5% overhead vs. direct Vulkan implementation
3. **Quality**: Zero critical bugs, zero memory leaks
4. **Documentation**: Complete API docs + migration guide
5. **Testing**: >80% code coverage, all tests passing
6. **Backends**: Minimum Vulkan + WebGPU working
7. **Platforms**: Linux, macOS, Windows builds successful

### Phase 1-8 Achievements

**Phase 1-2: Foundation** ✅
- 15 RHI interface headers (2,125 LOC)
- 12 Vulkan backend implementations (3,650 LOC)
- VMA (Vulkan Memory Allocator) integration
- Type-safe WebGPU-style API design

**Phase 3-4: Integration** ✅
- RHI Factory pattern for backend selection
- RendererBridge for gradual migration
- Full render loop via RHI
- 6/6 smoke tests passing

**Phase 5-6: Upper Layers** ✅
- ResourceManager, SceneManager migrated to RHI
- Mesh class using RHI buffers
- ImGui adapter pattern integration
- CommandManager removed

**Phase 7-8: Consolidation** ✅
- All legacy Vulkan wrappers removed (~890 LOC deleted)
- Directory refactored to modular structure
- Initialization order bugs fixed
- Zero validation errors achieved

---

## Phase Breakdown

### Phase 1: RHI Interface Design & Specification ✅

**Goal**: Define platform-independent abstract interfaces

**Deliverables**:
- 15 header files (RHITypes, RHIDevice, RHIBuffer, RHITexture, etc.)
- 2,125 lines of interface code
- Comprehensive Doxygen documentation
- WebGPU-style API design

**Key Insights**:
- Type safety via operator overloads improves API usability
- Interface dependencies require careful ordering
- Convenience headers enhance developer experience

**Reference**: [PHASE1_INTERFACE_DESIGN.md](PHASE1_INTERFACE_DESIGN.md)

---

### Phase 2: Vulkan Backend Implementation ✅

**Goal**: Convert existing Vulkan code to RHI implementations

**Deliverables**:
- 12 Vulkan RHI implementation classes (3,650 LOC)
- VMA integration for unified memory management
- Wrapper pattern applied to existing RAII Vulkan code

**Implementation Classes**:
| Class | Purpose | Lines |
|-------|---------|-------|
| VulkanRHIDevice | Device management | ~400 |
| VulkanRHIQueue | Queue submission | ~150 |
| VulkanRHIBuffer | Buffer abstraction | ~200 |
| VulkanRHITexture | Texture abstraction | ~250 |
| VulkanRHISampler | Sampler abstraction | ~100 |
| VulkanRHIShader | Shader management | ~150 |
| VulkanRHIBindGroup | Resource binding | ~300 |
| VulkanRHIPipeline | Pipeline creation | ~350 |
| VulkanRHICommandEncoder | Command recording | ~400 |
| VulkanRHISwapchain | Swapchain presentation | ~200 |
| VulkanRHISync | Synchronization | ~100 |
| VulkanRHICapabilities | Feature queries | ~150 |

**Key Patterns**:
- Wrapper pattern for code reuse (80-90% reuse rate)
- VMA for all buffer/texture memory management
- RAII preservation throughout

**Reference**: [PHASE2_VULKAN_BACKEND.md](PHASE2_VULKAN_BACKEND.md)

---

### Phase 3: Factory Pattern & Integration Bridge ✅

**Goal**: Create backend selection and gradual migration infrastructure

**Deliverables**:
- RHIFactory for backend instantiation
- RendererBridge for legacy/RHI coexistence
- Smoke tests for integration verification

**Architecture**:
```cpp
// Backend selection via factory
auto device = rhi::RHIFactory::createDevice(rhi::RHIBackendType::Vulkan, createInfo);

// Bridge for gradual migration
class RendererBridge {
    std::unique_ptr<rhi::RHIDevice> m_rhiDevice;
    std::unique_ptr<rhi::RHISwapchain> m_rhiSwapchain;
public:
    rhi::RHIDevice* getDevice();
    rhi::RHISwapchain* getSwapchain();
};
```

**Reference**: [PHASE3_FACTORY_BRIDGE.md](PHASE3_FACTORY_BRIDGE.md)

---

### Phase 4: Renderer Core Migration ✅

**Goal**: Incrementally migrate Renderer to RHI-based implementation

**Strategy**: Incremental migration with rollback points after each sub-phase

**Sub-Phases**:
1. Resource creation (buffers, textures, bind groups)
2. Command recording via RHI
3. Synchronization primitives (fences, semaphores)
4. Pipeline creation
5. Buffer upload via staging buffers

**Results**:
- Full render loop integration (drawFrameRHI)
- 6/6 smoke tests passing
- Application test: 742KB vertices, 369KB indices uploaded

**Reference**: [PHASE4_RENDERER_MIGRATION.md](PHASE4_RENDERER_MIGRATION.md)

---

### Phase 5: Scene & Resource Layer Migration ✅

**Goal**: Migrate scene layer (Mesh, ResourceManager, SceneManager) to RHI

**Key Changes**:
- Mesh: VulkanBuffer → rhi::RHIBuffer
- ResourceManager: VulkanImage → rhi::RHITexture cache
- SceneManager: Updated to use RHI types
- Removed duplicate buffers in Renderer

**Code Impact**: ~205 lines modified (89% of estimate)

**Reference**: [PHASE5_SCENE_MIGRATION.md](PHASE5_SCENE_MIGRATION.md)

---

### Phase 6: ImGui Adapter Integration ✅

**Goal**: Integrate ImGui with RHI abstraction layer

**Strategy**: Adapter pattern for backend-specific ImGui implementations

**Architecture**:
```cpp
// Abstract backend interface
class ImGuiBackend {
public:
    virtual void init(rhi::RHIDevice*, rhi::RHISwapchain*) = 0;
    virtual void render(rhi::RHICommandEncoder*) = 0;
};

// Vulkan-specific implementation
class ImGuiVulkanBackend : public ImGuiBackend {
    // Wraps imgui_impl_vulkan
};
```

**Code Impact**: ~600 lines (+450 new, ~250 modified, ~100 removed)

**Reference**: [PHASE6_IMGUI_INTEGRATION.md](PHASE6_IMGUI_INTEGRATION.md)

---

### Phase 7: Complete Renderer Migration & Cleanup ✅

**Goal**: Complete RHI migration and remove CommandManager

**Achievements**:
- Migrated drawFrame() to use drawFrameRHI()
- Deleted CommandManager completely
- Removed LegacyCommandBufferAdapter
- Achieved ~80% RHI-native codebase

**Code Impact**: +22 lines added, -440 lines deleted (net: -418 lines)

**Phase 7.5: RHI Runtime Fixes** ✅
- Fixed semaphore initialization
- Fixed format mismatch (SRGB vs UNORM)
- Implemented descriptor set binding
- Fixed command buffer synchronization
- Fixed image layout transitions
- Resolved semaphore reuse errors

**Reference**: [PHASE7_RENDERER_CLEANUP.md](PHASE7_RENDERER_CLEANUP.md)

---

### Phase 8: Legacy Cleanup & Modular Architecture ✅

**Goal**: Remove all legacy Vulkan wrappers and refactor to modular structure

**Part 1: Legacy Code Deletion**

| Component | Files | Lines Deleted | Replacement |
|-----------|-------|---------------|-------------|
| VulkanBuffer | .hpp/.cpp | ~250 | rhi::RHIBuffer |
| VulkanImage | .hpp/.cpp | ~200 | rhi::RHITexture |
| VulkanPipeline | .hpp/.cpp | ~75 | rhi::RHIRenderPipeline |
| VulkanSwapchain | .hpp/.cpp | ~86 | rhi::RHISwapchain |
| SyncManager | .hpp/.cpp | ~140 | RHI internal sync |
| CommandManager | .hpp/.cpp | ~140 | RHI command encoding |
| **Total** | **10 files** | **~890** | **100% RHI** |

**Part 2: Directory Refactoring**

```
src/
├── rhi/                           # RHI Abstract Interface Module
│   ├── include/rhi/               # 15 public headers
│   ├── src/RHIFactory.cpp         # Factory implementation
│   └── CMakeLists.txt             # rhi_interface + rhi_factory
│
├── rhi-vulkan/                    # Vulkan Backend Module
│   ├── include/rhi-vulkan/        # 12 Vulkan implementations
│   ├── src/                       # Implementation files
│   └── CMakeLists.txt             # rhi_vulkan library
│
└── rhi-webgpu/                    # WebGPU Backend (Phase 9+)
```

**Critical Fixes**:
1. Initialization order: Swapchain created before depth resources
2. Semaphore synchronization: Fence wait before image acquisition

**Benefits**:
- Industry-standard pattern (Unreal Engine, Unity)
- Independent module build/test
- Clear dependency inversion (DIP)
- Easy backend addition (OCP)

**Reference**: [PHASE8_MODULAR_ARCHITECTURE.md](PHASE8_MODULAR_ARCHITECTURE.md)

---

### Phase 9: WebGPU Backend Implementation 📋

**Goal**: Web deployment capability

**Tasks**:
- Research Dawn vs wgpu-native
- Create src/rhi-webgpu/ module structure
- Implement 12 WebGPU RHI classes
- SPIR-V to WGSL conversion (Naga/Tint)
- Handle async operations
- Emscripten build configuration
- Browser testing (Chrome, Firefox)

**Estimated Lines**: ~3,430 lines

**Acceptance Criteria**:
- WebGPU backend compiles and links
- Application runs in Chrome and Firefox
- Acceptable web performance (30+ FPS)
- WGSL shader conversion works

---

### Phase 10-11: Future Backends (Optional) 📋

**Phase 10**: Direct3D 12 (Windows)
**Phase 11**: Metal (macOS/iOS)
**Phase 12**: Ray Tracing Extensions

*Detailed planning deferred until Phase 9 complete*

---

## Dependencies

### External Libraries

| Library | Purpose | Version | License |
|---------|---------|---------|---------|
| **Vulkan SDK** | Vulkan backend | 1.3.x | Apache 2.0 |
| **VMA** | Vulkan memory management | 3.0.1+ | MIT |
| **SPIRV-Cross** | Shader cross-compilation | Latest | Apache 2.0 |
| **Dawn / wgpu-native** | WebGPU implementation | Latest | Apache 2.0 / MPL |
| **Naga / Tint** | SPIR-V to WGSL | Latest | Apache 2.0 |
| **Emscripten** | WebAssembly compilation | 3.1.x+ | MIT |
| **D3D12MA** | D3D12 memory management | 2.0.1+ | MIT |

### Build Requirements

- **CMake**: 3.20+
- **C++ Compiler**: C++17 or later
  - GCC 9+, Clang 10+, MSVC 2019+, AppleClang 12+

### Platform Requirements

- **Linux**: Ubuntu 22.04+, Fedora 36+
- **macOS**: macOS 12.0+ (Monterey)
- **Windows**: Windows 10/11

---

## Quality Metrics

### Code Quality

- **Documentation Coverage**: 100% of public APIs
- **Code Review**: 100% of changes reviewed
- **Static Analysis**: Zero critical warnings
- **Compilation**: Zero warnings with `-Wall -Wextra -Wpedantic`

### Testing

- **Unit Test Coverage**: >80% of RHI implementation code
- **Integration Tests**: All major workflows covered
- **Regression Tests**: Automated comparison with baseline
- **Platform Coverage**: Linux, macOS, Windows

### Performance

- **Frame Time**: < 5% increase vs. baseline
- **Memory Overhead**: < 1% increase (vtable pointers only)
- **Startup Time**: < 10% increase
- **Memory Leaks**: Zero detected by Valgrind/ASAN

### Security

- **Vulkan Validation**: Zero errors
- **Buffer Overflows**: Zero detected by ASAN
- **Use-After-Free**: Zero detected by ASAN
- **Memory Leaks**: Zero detected by Valgrind/ASAN

---

## Risk Management

### High-Priority Risks

| Risk | Probability | Impact | Mitigation Strategy |
|------|------------|--------|---------------------|
| **Performance degradation > 5%** | Medium | High | Profile early and often; optimize hot paths |
| **API semantic mismatches** | High | High | Study WebGPU/D3D12/Metal thoroughly; prototype early |
| **Shader cross-compilation issues** | Medium | High | Test SPIRV-Cross extensively; maintain SPIR-V as IR |
| **WebGPU async API complexity** | Medium | Medium | Study Dawn/wgpu examples; design async-friendly API |
| **Timeline overrun** | Medium | Medium | Phased approach with checkpoints |
| **Regression bugs** | Low | High | Comprehensive test suite; pixel comparisons |

### Risk Mitigation Checklist

- Profile performance after each phase
- Run regression tests after each phase
- Document all API changes and design decisions
- Create unit tests for each backend implementation
- Set up CI/CD pipeline for automated testing
- Maintain backward compatibility where possible
- Regular code reviews for architecture consistency

---

## Appendix A: Architecture Patterns

### RHI Interface Hierarchy

```
RHITypes.hpp (foundation)
    ↓
RHICapabilities, RHISync
    ↓
RHIBuffer, RHITexture, RHISampler, RHIShader
    ↓
RHIBindGroup ← RHIBindGroupLayout
    ↓
RHIPipeline ← RHIPipelineLayout
RHIRenderPass
    ↓
RHICommandBuffer ← RHICommandEncoder, RHIRenderPassEncoder
RHISwapchain
    ↓
RHIQueue
    ↓
RHIDevice (aggregates all)
    ↓
RHI.hpp (convenience header)
```

### Backend Dependency Graph

```
┌─────────────────────────────────────────────────────┐
│              Application Layer                      │
│         (Application, ImGuiManager)                 │
└────────────────────┬────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────┐
│           High-Level Rendering                      │
│   (Renderer, ResourceManager, SceneManager)         │
└────────────────────┬────────────────────────────────┘
                     │ depends on
                     ▼
            ┌────────────────┐
            │      rhi       │  ← Abstract Interface Only
            │ (Pure Virtual) │
            └────────┬───────┘
                     │ implemented by
      ┌──────────────┼──────────────┐
      ▼              ▼              ▼
┌──────────┐  ┌──────────┐  ┌──────────┐
│rhi-vulkan│  │rhi-webgpu│  │rhi-metal │
└────┬─────┘  └────┬─────┘  └────┬─────┘
     │             │             │
     ▼             ▼             ▼
┌──────────┐  ┌──────────┐  ┌──────────┐
│Vulkan SDK│  │WebGPUImpl│  │Metal SDK │
└──────────┘  └──────────┘  └──────────┘
```

---

## Appendix B: Migration Metrics

### Code Statistics

| Metric | Before Migration | After Phase 8 | Change |
|--------|-----------------|---------------|--------|
| Total Lines | ~12,900 | ~12,010 | -890 (-7%) |
| Legacy Classes | 6 wrappers | 0 | -100% |
| Duplicate Resources | 4 | 0 | -100% |
| RHI Coverage | 0% | 100% | +100% |
| Modules | 1 monolithic | 2 independent | +100% |
| Validation Errors | Variable | 0 | ✅ |

### Performance Impact

| Metric | Target | Actual | Status |
|--------|--------|--------|--------|
| Frame Time Overhead | < 5% | ~1-2% | ✅ Exceeded |
| Memory Overhead | < 1% | ~0.5% | ✅ Exceeded |
| GPU Memory Savings | N/A | ~8.5 MB | ✅ Bonus |
| Startup Time | < 10% | < 5% | ✅ Exceeded |

---

## Appendix C: Documentation References

### Phase Documentation
- [PHASE1_INTERFACE_DESIGN.md](PHASE1_INTERFACE_DESIGN.md) - RHI interface design
- [PHASE2_VULKAN_BACKEND.md](PHASE2_VULKAN_BACKEND.md) - Vulkan implementation
- [PHASE3_FACTORY_BRIDGE.md](PHASE3_FACTORY_BRIDGE.md) - Factory pattern
- [PHASE4_RENDERER_MIGRATION.md](PHASE4_RENDERER_MIGRATION.md) - Renderer migration
- [PHASE5_SCENE_MIGRATION.md](PHASE5_SCENE_MIGRATION.md) - Scene layer migration
- [PHASE6_IMGUI_INTEGRATION.md](PHASE6_IMGUI_INTEGRATION.md) - ImGui integration
- [PHASE7_RENDERER_CLEANUP.md](PHASE7_RENDERER_CLEANUP.md) - Final cleanup
- [PHASE8_MODULAR_ARCHITECTURE.md](PHASE8_MODULAR_ARCHITECTURE.md) - Architecture refactoring

### Technical Guides
- [RHI_TECHNICAL_GUIDE.md](RHI_TECHNICAL_GUIDE.md) - Technical implementation details
- [ARCHITECTURE.md](../../ARCHITECTURE.md) - System architecture overview
- [TROUBLESHOOTING.md](../../TROUBLESHOOTING.md) - Known issues and solutions

### Strategy Documents
- [RHI_MIGRATION_STRATEGY_EN.md](RHI_MIGRATION_STRATEGY_EN.md) - Migration strategy overview

---

**END OF DOCUMENT**
