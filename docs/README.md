# Mini-Engine Documentation

Welcome to the comprehensive documentation for the Mini-Engine project.

This directory contains detailed technical documentation covering the engine's architecture, design decisions, cross-platform support, and the complete refactoring journey from a monolithic Vulkan application to a clean, modular rendering engine.

---

## 📚 Documentation Index

### Getting Started

**New to the project?** Start here:

1. **[Main README](../README.md)** - Project overview and quick start
2. **[Build Guide](BUILD_GUIDE.md)** - Detailed build instructions for Linux, macOS, Windows
3. **[Troubleshooting](TROUBLESHOOTING.md)** - Common issues and solutions

### Technical Documentation

**Understanding the architecture:**

1. **[Architecture Overview](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md)** - High-level architecture evolution and design patterns
2. **[Cross-Platform Rendering](refactoring/monolith-to-layered/PHASE8_CROSS_PLATFORM_SUPPORT.md)** - Platform compatibility and Vulkan version support (Linux/macOS/Windows)
3. **[Refactoring Journey](refactoring/monolith-to-layered/)** - Complete 11-phase refactoring process
4. **[FdF Integration Changelog](CHANGELOG_FDF_INTEGRATION.md)** - Detailed FdF wireframe visualization changelog
5. **[ImGui Integration Guide](IMGUI_INTEGRATION.md)** - Dear ImGui troubleshooting and implementation guide

#### Refactoring Journey

The engine was built through a systematic 11-phase refactoring process, transforming a monolithic `main.cpp` into a clean, modular 4-layer architecture with an 18-line entry point, cross-platform support, and multiple rendering modes.

**[Refactoring Overview](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md)**
- Overall architecture evolution (11 phases)
- Design patterns used (RAII, Dependency Injection, Facade)
- Key benefits and impact metrics
- Before/after comparisons

**Phase-by-Phase Documentation:**

**Core Architecture (Phases 1-7)**

1. **[Phase 1: Utility Layer](refactoring/monolith-to-layered/PHASE1_UTILITY_LAYER.md)** - Common utilities separated
2. **[Phase 2: Device Management](refactoring/monolith-to-layered/PHASE2_DEVICE_MANAGEMENT.md)** - VulkanDevice class encapsulation
3. **[Phase 3: Resource Management](refactoring/monolith-to-layered/PHASE3_RESOURCE_MANAGEMENT.md)** - RAII resource wrappers (VulkanBuffer, VulkanImage)
4. **[Phase 4: Rendering Layer](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md)** - Rendering subsystems (Sync, Command, Swapchain, Pipeline)
5. **[Phase 5: Scene Layer](refactoring/monolith-to-layered/PHASE5_SCENE_LAYER.md)** - Mesh abstraction and OBJLoader
6. **[Phase 6: Renderer Integration](refactoring/monolith-to-layered/PHASE6_RENDERER_INTEGRATION.md)** - High-level Renderer unification
7. **[Phase 7: Application Layer](refactoring/monolith-to-layered/PHASE7_APPLICATION_LAYER.md)** - Application class (18-line main.cpp)

**Infrastructure & Features (Phases 8-11)**

8. **[Phase 8: Cross-Platform Support](refactoring/monolith-to-layered/PHASE8_CROSS_PLATFORM_SUPPORT.md)** - Vulkan 1.1/1.3 dual rendering paths (Linux/macOS/Windows)
9. **[Phase 9: Subsystem Separation](refactoring/monolith-to-layered/PHASE9_SUBSYSTEM_SEPARATION.md)** - 4-layer architecture (ResourceManager, SceneManager)
10. **[Phase 10: FdF Integration](refactoring/monolith-to-layered/PHASE10_FDF_INTEGRATION.md)** - Wireframe visualization + Camera system
11. **[Phase 11: ImGui Integration](refactoring/monolith-to-layered/PHASE11_IMGUI_INTEGRATION.md)** - Debugging UI overlay

---

#### RHI (Render Hardware Interface) Migration

✅ **COMPLETED (2025-12-21)** - Multi-backend graphics architecture with complete Vulkan implementation.

**[RHI Migration PRD](refactoring/layered-to-rhi/RHI_MIGRATION_PRD.md)**

- Complete migration plan (Phases 1-8 complete)
- WebGPU-style API design
- Multi-backend architecture (Vulkan ✅, WebGPU/Metal/D3D12 planned)

**RHI Phase Documentation:**

1. **[Phase 1: RHI Interface Design](refactoring/layered-to-rhi/PHASE1_SUMMARY.md)** ✅
2. **[Phase 2: Vulkan Backend](refactoring/layered-to-rhi/PHASE2_SUMMARY.md)** ✅
3. **[Phase 3: Factory & Bridge](refactoring/layered-to-rhi/PHASE3_SUMMARY.md)** ✅
4. **[Phase 4: Renderer Migration](refactoring/layered-to-rhi/PHASE4_SUMMARY.md)** ✅
5. **[Phase 5: Scene Layer Migration](refactoring/layered-to-rhi/PHASE5_SUMMARY.md)** ✅
6. **[Phase 6: ImGui Migration](refactoring/layered-to-rhi/PHASE6_SUMMARY.md)** ✅
7. **[Phase 7: Renderer RHI Migration](refactoring/layered-to-rhi/PHASE7_SUMMARY.md)** ✅
8. **[Phase 8: Legacy Code Cleanup](refactoring/layered-to-rhi/PHASE8_SUMMARY.md)** ✅ **LATEST**
   - **[Directory Refactoring](refactoring/layered-to-rhi/PHASE8_DIRECTORY_REFACTORING.md)** - Modular structure

**Additional RHI Documentation:**

- **[Architecture Overview](refactoring/layered-to-rhi/ARCHITECTURE.md)** ✨ **NEW** - Complete 4-layer architecture guide
- **[RHI Technical Guide](refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md)** - API usage and patterns
- **[Troubleshooting](refactoring/layered-to-rhi/TROUBLESHOOTING.md)** - Migration issues and solutions
- **[Legacy Code Reference](refactoring/layered-to-rhi/LEGACY_CODE_REFERENCE.md)** - Pre-RHI code archive

---

## 🗂️ Documentation Structure

```
docs/
├── README.md (this file)                    # Documentation hub and navigation
├── BUILD_GUIDE.md                           # Detailed build instructions (all platforms)
├── TROUBLESHOOTING.md                       # Common issues and solutions
├── CHANGELOG_FDF_INTEGRATION.md             # FdF integration changelog (detailed)
├── IMGUI_INTEGRATION.md                     # ImGui integration guide (troubleshooting)
└── refactoring/
    └── monolith-to-layered/                 # Refactoring journey documentation
        ├── REFACTORING_OVERVIEW.md          # High-level architecture overview (11 phases)
        ├── REFACTORING_PLAN.md              # Original refactoring plan
        ├── PHASE1_UTILITY_LAYER.md          # Phase 1: Utilities
        ├── PHASE2_DEVICE_MANAGEMENT.md      # Phase 2: Device
        ├── PHASE3_RESOURCE_MANAGEMENT.md    # Phase 3: Resources (RAII)
        ├── PHASE4_RENDERING_LAYER.md        # Phase 4: Rendering subsystems
        ├── PHASE5_SCENE_LAYER.md            # Phase 5: Scene and meshes
        ├── PHASE6_RENDERER_INTEGRATION.md   # Phase 6: Renderer
        ├── PHASE7_APPLICATION_LAYER.md      # Phase 7: Application
        ├── PHASE8_CROSS_PLATFORM_SUPPORT.md # Phase 8: Cross-platform (Vulkan 1.1/1.3)
        ├── PHASE9_SUBSYSTEM_SEPARATION.md   # Phase 9: 4-layer architecture
        ├── PHASE10_FDF_INTEGRATION.md       # Phase 10: FdF wireframe visualization
        └── PHASE11_IMGUI_INTEGRATION.md     # Phase 11: Dear ImGui debugging UI
```

---

## 🎯 Reading Guide

### For Different Audiences

#### **Game Developers / Engineers**
If you're evaluating this project's architecture:

1. **Architecture understanding:**
   - Start with [Refactoring Overview](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md)
   - Review [RHI Migration PRD](refactoring/layered-to-rhi/RHI_MIGRATION_PRD.md) for abstraction layer design

2. **Technical depth:**
   - [Phase 3: Resource Management](refactoring/monolith-to-layered/PHASE3_RESOURCE_MANAGEMENT.md) - RAII implementation
   - [Phase 4: Rendering Layer](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md) - Synchronization and command management
   - [RHI Technical Guide](refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md) - Hardware abstraction design

3. **Code quality:**
   - Check code metrics in each phase document
   - Review "Benefits" sections for architectural decisions
   - Examine "Testing" sections for validation approach

#### **Students / Learners**
If you're learning Vulkan or modern C++ game engine architecture:

1. **Sequential learning:**
   - Read [Phase 1](refactoring/monolith-to-layered/PHASE1_UTILITY_LAYER.md) through [Phase 7](refactoring/monolith-to-layered/PHASE7_APPLICATION_LAYER.md) in order
   - Each phase builds on previous concepts
   - Code examples demonstrate incremental improvements

2. **Concept deep-dive:**
   - **RAII pattern:** [Phase 3: Resource Management](refactoring/monolith-to-layered/PHASE3_RESOURCE_MANAGEMENT.md)
   - **Vulkan synchronization:** [Phase 4: Rendering Layer](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md)
   - **Scene graph design:** [Phase 5: Scene Layer](refactoring/monolith-to-layered/PHASE5_SCENE_LAYER.md)

3. **Best practices:**
   - Each phase has "Key Design Decisions" section
   - "Problems Solved" sections explain architectural choices
   - Code metrics demonstrate impact of good design

#### **Developers Contributing to Mini-Engine**
If you're extending or modifying the engine:

1. **Entry point:** [Phase 7: Application Layer](refactoring/monolith-to-layered/PHASE7_APPLICATION_LAYER.md)
2. **High-level rendering:** [Phase 6: Renderer Integration](refactoring/monolith-to-layered/PHASE6_RENDERER_INTEGRATION.md)
3. **Specific subsystems:**
   - Adding meshes: [Phase 5: Scene Layer](refactoring/monolith-to-layered/PHASE5_SCENE_LAYER.md)
   - Buffer/texture management: [Phase 3: Resource Management](refactoring/monolith-to-layered/PHASE3_RESOURCE_MANAGEMENT.md)
   - Pipeline changes: [Phase 4: Rendering Layer](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md)
   - Device queries: [Phase 2: Device Management](refactoring/monolith-to-layered/PHASE2_DEVICE_MANAGEMENT.md)

#### **Code Reviewers**
Quick navigation to key sections:

- **Code metrics:** Each phase document has "Code Metrics" section
- **Testing approach:** Check "Testing" sections
- **Design patterns:** [Refactoring Overview](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md#design-patterns-used)
- **Cross-platform support:** [Cross-Platform Rendering](CROSS_PLATFORM_RENDERING.md)

---

## 📊 Summary Statistics

### Overall Impact

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| main.cpp | Monolithic | Pure entry point (18 lines) | Layered architecture |
| Files | 1 | 50+ | Modular architecture |
| Classes | 0 | 20+ | Reusable components |
| Helper functions | 20+ | 0 | Encapsulated in classes |
| RAII coverage | None | Complete | Zero leaks |
| Platforms | 1 | 3 (Linux/macOS/Windows) | Cross-platform |
| Rendering modes | 1 | 2 (OBJ/FdF) | Multi-mode support |
| Debugging UI | None | Dear ImGui | Real-time profiling |

### Phase Breakdown

| Phase | Work Completed | Classes | Key Achievement |
|-------|----------------|---------|-----------------|
| 1 | Utility separation | 0 (utils) | Foundation |
| 2 | Device encapsulation | 1 | Device management |
| 3 | Resource abstraction | 2 | RAII resources |
| 4 | Rendering separation | 4 | Rendering modularization |
| 5 | Scene layer extraction | 2 | Scene abstraction |
| 6 | Subsystem integration | 1 | Renderer integration |
| 7 | Entry point finalization | 1 | Application finalization |
| 8 | Cross-platform support | 0 | Vulkan 1.1/1.3 dual paths |
| 9 | Manager separation | 2 | 4-layer architecture |
| 10 | FdF integration | 3 | Wireframe + Camera |
| 11 | ImGui integration | 1 | Debugging UI |
| **Total** | **Complete engine** | **20+** | **Production-ready architecture** |

---

## 🔍 Quick Reference

### By Topic

**Architecture & Design:**
- [Overall architecture evolution](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md#architecture-evolution)
- [Design patterns used](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md#design-patterns-used)
- [Key benefits](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md#key-benefits)

**Implementation Details:**
- [VulkanDevice](refactoring/monolith-to-layered/PHASE2_DEVICE_MANAGEMENT.md#implementation-details)
- [VulkanBuffer (RAII)](refactoring/monolith-to-layered/PHASE3_RESOURCE_MANAGEMENT.md#implementation-highlights)
- [VulkanImage (RAII)](refactoring/monolith-to-layered/PHASE3_RESOURCE_MANAGEMENT.md#implementation-highlights-1)
- [SyncManager](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md#phase-41-syncmanager-implementation)
- [CommandManager](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md#phase-42-commandmanager-implementation)
- [VulkanSwapchain](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md#phase-43-vulkanswapchain-implementation)
- [VulkanPipeline](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md#phase-44-vulkanpipeline-implementation)
- [Mesh](refactoring/monolith-to-layered/PHASE5_SCENE_LAYER.md#implementation-highlights)
- [Renderer](refactoring/monolith-to-layered/PHASE6_RENDERER_INTEGRATION.md#implementation-highlights)
- [Application](refactoring/monolith-to-layered/PHASE7_APPLICATION_LAYER.md#implementation-highlights)

**Cross-Platform:**
- [Platform requirements](refactoring/monolith-to-layered/PHASE8_CROSS_PLATFORM_SUPPORT.md#platform-specific-requirements)
- [Conditional compilation](refactoring/monolith-to-layered/PHASE8_CROSS_PLATFORM_SUPPORT.md#conditional-compilation)
- [Testing](refactoring/monolith-to-layered/PHASE8_CROSS_PLATFORM_SUPPORT.md#testing)

**Code Metrics:**
- [Phase 1 metrics](refactoring/monolith-to-layered/PHASE1_UTILITY_LAYER.md#code-metrics)
- [Phase 2 metrics](refactoring/monolith-to-layered/PHASE2_DEVICE_MANAGEMENT.md#code-metrics)
- [Phase 3 metrics](refactoring/monolith-to-layered/PHASE3_RESOURCE_MANAGEMENT.md#code-metrics)
- [Phase 4 metrics](refactoring/monolith-to-layered/PHASE4_RENDERING_LAYER.md#phase-4-complete)
- [Phase 5 metrics](refactoring/monolith-to-layered/PHASE5_SCENE_LAYER.md#code-metrics)
- [Phase 6 metrics](refactoring/monolith-to-layered/PHASE6_RENDERER_INTEGRATION.md#code-metrics)
- [Phase 7 metrics](refactoring/monolith-to-layered/PHASE7_APPLICATION_LAYER.md#code-metrics)
- [Phase 8 metrics](refactoring/monolith-to-layered/PHASE8_CROSS_PLATFORM_SUPPORT.md#code-metrics)
- [Phase 10 metrics](refactoring/monolith-to-layered/PHASE10_FDF_INTEGRATION.md#code-metrics)
- [Phase 11 metrics](refactoring/monolith-to-layered/PHASE11_IMGUI_INTEGRATION.md#code-metrics)

---

## 🚀 Next Steps

After reading the documentation:

1. **Build the project:** Follow instructions in [main README](../README.md#how-to-build-and-run)
2. **Explore the code:** Start with [Application.cpp](../src/Application.cpp) (18 lines)
3. **Understand the architecture:** Trace through [Renderer.cpp](../src/rendering/Renderer.cpp)
4. **Examine RAII patterns:** Check [VulkanBuffer.cpp](../src/resources/VulkanBuffer.cpp)
5. **Review synchronization:** Study [SyncManager.cpp](../src/rendering/SyncManager.cpp)

---

## 📝 Documentation Conventions

Throughout the documentation:

- **Code snippets** demonstrate key implementation details
- **Metrics sections** quantify improvements
- **Benefits sections** explain architectural decisions
- **Testing sections** describe validation approach
- **File references** link to actual source code (where applicable)

---

## 🔄 Keeping Documentation Updated

This documentation reflects the current state of the project. As the engine evolves:

- New features will be documented in appropriate sections
- Phase documentation remains as historical record
- Cross-platform guide will be updated with new platforms/features
- Main README roadmap tracks future work

---

## 💬 Feedback

Found an error in the documentation? Want to suggest improvements?

- Open an issue in the project repository
- Clearly describe the documentation page and section
- Suggest specific improvements

---

<div align="center">

**📖 Happy Reading! 📖**

[⬆ Back to Top](#mini-engine-documentation) | [📂 Project Home](../README.md)

</div>

---

*Last Updated: 2025-12-21*
*Project: Mini-Engine*
*Latest: Phase 8 Complete - RHI Migration & Legacy Cleanup*
