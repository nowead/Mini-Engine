# Mini-Engine

> A Vulkan-based rendering engine built from scratch with Modern C++

<!-- [GIF or Video] -->

---

## 🇰🇷 한국어 요약

**프로젝트 목표**: Vulkan Tutorial을 학습하며 만든 렌더러를 **확장 가능한 엔진 아키텍처**로 발전시키기

**핵심 성과**:
- 4계층 객체지향 아키텍처 + Foundation (RAII, Dependency Injection, Facade 패턴 적용)
- 단일 렌더링 기법이 아닌 **다중 렌더링 기법을 지원하는 플랫폼** 설계
- 체계적인 리팩토링 과정 문서화

**상세 문서**: [docs/](docs/) 폴더에 전체 개발 과정과 설계 결정 기록

---

## 💡 Project Overview

A Vulkan rendering engine designed to support **multiple rendering techniques** through a well-architected, extensible platform.

**Current**: FdF (Rasterization pipeline)
**Planned**: MiniRT (Ray tracing using `VK_KHR_ray_tracing_pipeline`)
**Goal**: Both projects share the same engine foundation with different rendering backends

### Development Journey

**Starting point** → Learned Vulkan from [vulkan-tutorial.com](https://vulkan-tutorial.com/)
**Challenge** → Initial code was monolithic and hard to extend
**Approach** → Systematic refactoring with object-oriented principles
**Result** → Layered architecture with clear abstractions and reusable components

Each refactoring phase is documented in [docs/refactoring/](docs/refactoring/)

---

## ✨ Features

**Vulkan Rendering Pipeline**
- Complete Vulkan initialization and management
- Swapchain, graphics pipeline, command buffers
- Frame synchronization (semaphores, fences)

**RAII Resource Management**
- Automatic memory management (VulkanBuffer, VulkanImage)
- Zero memory leaks guaranteed

**Cross-Platform**
- Linux (Vulkan 1.1), macOS (MoltenVK), Windows (Vulkan 1.3)
- Single codebase, platform-specific optimizations

**3D Rendering**
- OBJ model loading, texture mapping
- Camera transformations (MVP matrices)

**Coming Soon**: Heightmap visualization, camera controls, ray tracing pipeline

---

## 🏗️ Architecture

### Object-Oriented Layered Design

The engine uses a **pragmatic 4-layer architecture** + Foundation components with strict separation of concerns:

```
Layer 1: Application (Window management, main loop)
    ↓
Layer 2: Rendering Orchestration (Renderer - coordinates all subsystems)
    ↓
Layer 3: Rendering Components & Layer 4: Subsystems
    (VulkanSwapchain, VulkanPipeline,    (ResourceManager, SceneManager)
     CommandManager, SyncManager)
    ↓
Foundation: Core & Resources
    (VulkanDevice, VulkanBuffer, VulkanImage, Mesh)
    ↓
Utilities: Header-only (Vertex, VulkanCommon, PlatformConfig)
```

**Design Principles**:
- **Dependency Rule**: Each layer depends only on layers below (never above)
- **Single Responsibility**: Each class has one clear purpose
- **RAII**: All Vulkan resources automatically managed with `vk::raii::*` wrappers
- **Pragmatic Design**: Avoid over-engineering, only 2 true "managers" (Resource, Scene)

**Key Patterns**:
- **RAII** (VulkanBuffer, VulkanImage): Automatic resource management, zero memory leaks guaranteed
- **Dependency Injection**: Components receive dependencies via constructor
- **Facade** (Renderer): Simple interface to complex subsystems (5 public methods)

**Extensibility**:
- New rendering techniques (e.g., FDF wireframe, ray tracing) can be added without modifying core layers
- Platform abstraction (Linux: Vulkan 1.1, macOS/Windows: Vulkan 1.3 with dynamic rendering)
- Clear interfaces enable unit testing and mocking

See [docs/refactoring/](docs/refactoring/) for the evolution from monolithic code to this architecture.

---

## 🚀 Quick Start

### Prerequisites
- **Vulkan SDK** 1.3+ (with `slangc`)
- **CMake** 3.28+
- **C++20** compiler
- **vcpkg** package manager

### Build
```bash
# Set environment variables
export VCPKG_ROOT=/path/to/vcpkg
export VULKAN_SDK=/path/to/vulkan/sdk

# Clone and build
git clone https://github.com/your-username/vulkan-fdf.git
cd vulkan-fdf
make  # or: cmake --preset=default && cmake --build build
```

### Run
```bash
./build/vulkanGLFW
```

For detailed build instructions and troubleshooting, see [docs/BUILD_GUIDE.md](docs/BUILD_GUIDE.md).

---

## 📚 Documentation

- **[Documentation Hub](docs/README.md)** - Start here for navigation
- **[Build Guide](docs/BUILD_GUIDE.md)** - Detailed build instructions for all platforms
- **[Troubleshooting](docs/TROUBLESHOOTING.md)** - Common issues and solutions
- **[Refactoring Journey](docs/refactoring/)** - 8-phase architecture evolution (Phase 1-8)
- **[Cross-Platform Support](docs/CROSS_PLATFORM_RENDERING.md)** - Platform compatibility guide

### Architecture Highlights
- **4 layers** + Foundation components with strict dependency hierarchy
- **11 reusable components** with clear responsibilities
- **Design patterns**: RAII, Dependency Injection, Facade
- **Full documentation**: Design decisions and evolution process recorded

---

## 📄 License

Educational and portfolio purposes. Free to use for learning - please provide attribution.

---

<div align="center">

**Built with Vulkan API and Modern C++**

[⬆ Back to Top](#mini-engine)

</div>
