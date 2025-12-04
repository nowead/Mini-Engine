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

1. **[Architecture Overview](refactoring/REFACTORING_OVERVIEW.md)** - High-level architecture evolution and design patterns
2. **[Cross-Platform Rendering](CROSS_PLATFORM_RENDERING.md)** - Platform compatibility and Vulkan version support
3. **[Refactoring Journey](refactoring/)** - Complete 7-phase refactoring process

#### Refactoring Journey

The engine was built through a systematic 8-phase refactoring process, transforming a monolithic `main.cpp` into a clean, modular 4-layer architecture with an 18-line entry point.

**[Refactoring Overview](refactoring/REFACTORING_OVERVIEW.md)**
- Overall architecture evolution
- Design patterns used (RAII, Dependency Injection, Facade)
- Key benefits and impact metrics
- Before/after comparisons

**Phase-by-Phase Documentation:**

1. **[Phase 1: Utility Layer](refactoring/PHASE1_UTILITY_LAYER.md)**
   - Extracted common utilities and types
   - Created VulkanCommon.hpp, Vertex.hpp, FileUtils.hpp
   - Common utilities separated

2. **[Phase 2: Device Management](refactoring/PHASE2_DEVICE_MANAGEMENT.md)**
   - Encapsulated Vulkan device initialization
   - Created VulkanDevice class
   - Instance, physical device, logical device, queue management
   - Device management encapsulated

3. **[Phase 3: Resource Management](refactoring/PHASE3_RESOURCE_MANAGEMENT.md)**
   - Implemented RAII resource wrappers
   - Created VulkanBuffer class (vertex, index, uniform, staging)
   - Created VulkanImage class (textures, depth, automatic views)
   - RAII resource wrappers implemented

4. **[Phase 4: Rendering Layer](refactoring/PHASE4_RENDERING_LAYER.md)**
   - Modularized rendering subsystems
   - Created SyncManager (synchronization primitives)
   - Created CommandManager (command pool/buffers)
   - Created VulkanSwapchain (swapchain lifecycle)
   - Created VulkanPipeline (graphics pipeline)
   - Rendering subsystems modularized

5. **[Phase 5: Scene Layer](refactoring/PHASE5_SCENE_LAYER.md)**
   - Abstracted mesh and geometry handling
   - Created Mesh class (geometry + GPU buffers)
   - Created OBJLoader (file loading with deduplication)
   - Mesh and geometry handling abstracted

6. **[Phase 6: Renderer Integration](refactoring/PHASE6_RENDERER_INTEGRATION.md)**
   - Unified all subsystems into high-level Renderer
   - Created Renderer class (owns all rendering components)
   - Simple 5-method public interface
   - All rendering subsystems unified

7. **[Phase 7: Application Layer](refactoring/PHASE7_APPLICATION_LAYER.md)**
   - Finalized application entry point
   - Created Application class (window + main loop)
   - RAII initialization and cleanup
   - main.cpp simplified to pure entry point (18 lines)

---

## 🗂️ Documentation Structure

```
docs/
├── README.md (this file)               # Documentation hub and navigation
├── BUILD_GUIDE.md                      # Detailed build instructions (all platforms)
├── TROUBLESHOOTING.md                  # Common issues and solutions
├── CROSS_PLATFORM_RENDERING.md         # Platform compatibility guide
└── refactoring/                        # Refactoring journey documentation
    ├── REFACTORING_OVERVIEW.md         # High-level architecture overview
    ├── REFACTORING_PLAN.md             # Original refactoring plan
    ├── PHASE1_UTILITY_LAYER.md         # Phase 1: Utilities
    ├── PHASE2_DEVICE_MANAGEMENT.md     # Phase 2: Device
    ├── PHASE3_RESOURCE_MANAGEMENT.md   # Phase 3: Resources (RAII)
    ├── PHASE4_RENDERING_LAYER.md       # Phase 4: Rendering subsystems
    ├── PHASE5_SCENE_LAYER.md           # Phase 5: Scene and meshes
    ├── PHASE6_RENDERER_INTEGRATION.md  # Phase 6: Renderer
    └── PHASE7_APPLICATION_LAYER.md     # Phase 7: Application
```

---

## 🎯 Reading Guide

### For Different Audiences

#### **Game Developers / Engineers**
If you're evaluating this project for a technical interview or portfolio review:

1. **Architecture understanding:**
   - Start with [Refactoring Overview](refactoring/REFACTORING_OVERVIEW.md)
   - Review key design decisions in main README

2. **Technical depth:**
   - [Phase 3: Resource Management](refactoring/PHASE3_RESOURCE_MANAGEMENT.md) - RAII implementation
   - [Phase 4: Rendering Layer](refactoring/PHASE4_RENDERING_LAYER.md) - Synchronization and command management
   - [Cross-Platform Rendering](CROSS_PLATFORM_RENDERING.md) - Platform abstraction

3. **Code quality:**
   - Check code metrics in each phase document
   - Review "Benefits" sections for architectural decisions
   - Examine "Testing" sections for validation approach

#### **Students / Learners**
If you're learning Vulkan or modern C++ game engine architecture:

1. **Sequential learning:**
   - Read [Phase 1](refactoring/PHASE1_UTILITY_LAYER.md) through [Phase 7](refactoring/PHASE7_APPLICATION_LAYER.md) in order
   - Each phase builds on previous concepts
   - Code examples demonstrate incremental improvements

2. **Concept deep-dive:**
   - **RAII pattern:** [Phase 3: Resource Management](refactoring/PHASE3_RESOURCE_MANAGEMENT.md)
   - **Vulkan synchronization:** [Phase 4: Rendering Layer](refactoring/PHASE4_RENDERING_LAYER.md)
   - **Scene graph design:** [Phase 5: Scene Layer](refactoring/PHASE5_SCENE_LAYER.md)

3. **Best practices:**
   - Each phase has "Key Design Decisions" section
   - "Problems Solved" sections explain architectural choices
   - Code metrics demonstrate impact of good design

#### **Developers Contributing to Mini-Engine**
If you're extending or modifying the engine:

1. **Entry point:** [Phase 7: Application Layer](refactoring/PHASE7_APPLICATION_LAYER.md)
2. **High-level rendering:** [Phase 6: Renderer Integration](refactoring/PHASE6_RENDERER_INTEGRATION.md)
3. **Specific subsystems:**
   - Adding meshes: [Phase 5: Scene Layer](refactoring/PHASE5_SCENE_LAYER.md)
   - Buffer/texture management: [Phase 3: Resource Management](refactoring/PHASE3_RESOURCE_MANAGEMENT.md)
   - Pipeline changes: [Phase 4: Rendering Layer](refactoring/PHASE4_RENDERING_LAYER.md)
   - Device queries: [Phase 2: Device Management](refactoring/PHASE2_DEVICE_MANAGEMENT.md)

#### **Code Reviewers**
Quick navigation to key sections:

- **Code metrics:** Each phase document has "Code Metrics" section
- **Testing approach:** Check "Testing" sections
- **Design patterns:** [Refactoring Overview](refactoring/REFACTORING_OVERVIEW.md#design-patterns-used)
- **Cross-platform support:** [Cross-Platform Rendering](CROSS_PLATFORM_RENDERING.md)

---

## 📊 Summary Statistics

### Overall Impact

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| main.cpp | Monolithic | Pure entry point (18 lines) | Layered architecture |
| Files | 1 | 34+ | Modular architecture |
| Classes | 0 | 14 | Reusable components |
| Helper functions | 20+ | 0 | Encapsulated in classes |
| RAII coverage | None | Complete | Zero leaks |

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
| 8 | Manager separation | 2 | EP01 architecture |
| **Total** | **Layered structure** | **14** | **4-layer architecture** |

---

## 🔍 Quick Reference

### By Topic

**Architecture & Design:**
- [Overall architecture evolution](refactoring/REFACTORING_OVERVIEW.md#architecture-evolution)
- [Design patterns used](refactoring/REFACTORING_OVERVIEW.md#design-patterns-used)
- [Key benefits](refactoring/REFACTORING_OVERVIEW.md#key-benefits)

**Implementation Details:**
- [VulkanDevice](refactoring/PHASE2_DEVICE_MANAGEMENT.md#implementation-details)
- [VulkanBuffer (RAII)](refactoring/PHASE3_RESOURCE_MANAGEMENT.md#implementation-highlights)
- [VulkanImage (RAII)](refactoring/PHASE3_RESOURCE_MANAGEMENT.md#implementation-highlights-1)
- [SyncManager](refactoring/PHASE4_RENDERING_LAYER.md#phase-41-syncmanager-implementation)
- [CommandManager](refactoring/PHASE4_RENDERING_LAYER.md#phase-42-commandmanager-implementation)
- [VulkanSwapchain](refactoring/PHASE4_RENDERING_LAYER.md#phase-43-vulkanswapchain-implementation)
- [VulkanPipeline](refactoring/PHASE4_RENDERING_LAYER.md#phase-44-vulkanpipeline-implementation)
- [Mesh](refactoring/PHASE5_SCENE_LAYER.md#implementation-highlights)
- [Renderer](refactoring/PHASE6_RENDERER_INTEGRATION.md#implementation-highlights)
- [Application](refactoring/PHASE7_APPLICATION_LAYER.md#implementation-highlights)

**Cross-Platform:**
- [Platform requirements](CROSS_PLATFORM_RENDERING.md#platform-specific-requirements)
- [Configuration system](CROSS_PLATFORM_RENDERING.md#platform-configuration-system)
- [Testing matrix](CROSS_PLATFORM_RENDERING.md#testing-matrix)

**Code Metrics:**
- [Phase 1 metrics](refactoring/PHASE1_UTILITY_LAYER.md#code-metrics)
- [Phase 2 metrics](refactoring/PHASE2_DEVICE_MANAGEMENT.md#code-metrics)
- [Phase 3 metrics](refactoring/PHASE3_RESOURCE_MANAGEMENT.md#code-metrics)
- [Phase 4 metrics](refactoring/PHASE4_RENDERING_LAYER.md#phase-4-complete)
- [Phase 5 metrics](refactoring/PHASE5_SCENE_LAYER.md#code-metrics)
- [Phase 6 metrics](refactoring/PHASE6_RENDERER_INTEGRATION.md#code-metrics)
- [Phase 7 metrics](refactoring/PHASE7_APPLICATION_LAYER.md#code-metrics)

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

*Last Updated: 2025-11-17*
*Project: Mini-Engine (vulkan-fdf)*
