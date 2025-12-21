# Mini-Engine

> Modern C++20 Multi-Backend 3D Rendering Engine with RHI Architecture

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)
![RHI](https://img.shields.io/badge/RHI-Completed-brightgreen.svg)
![CMake](https://img.shields.io/badge/CMake-3.28+-green.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)

---

## Korean Summary

**프로젝트 목표**: Vulkan Tutorial을 학습하며 만든 렌더러를 멀티 백엔드 엔진 아키텍처로 발전

**핵심 성과**:
- ✅ **RHI (Render Hardware Interface) 아키텍처 마이그레이션 완료** (2025-12-21)
- 그래픽스 API 추상화 계층으로 멀티 백엔드 지원 (Vulkan, WebGPU 준비 완료)
- 4계층 객체지향 아키텍처 + RAII 패턴 적용
- 체계적인 리팩토링 과정 문서화

**현재 기능**: FDF Wireframe, OBJ Model Loading, ImGui UI, Camera Controls, 100% RHI-Native Rendering

**상세 문서**: [docs/refactoring/layered-to-rhi/](docs/refactoring/layered-to-rhi/) 폴더 참고

---

## Table of Contents

- [Project Overview](#project-overview)
- [Features](#features)
- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Dependencies](#dependencies)
- [Documentation](#documentation)
- [Development](#development)
- [License](#license)

---

## Project Overview

Mini-Engine is a modern multi-backend rendering engine built from scratch, evolved from learning materials at [vulkan-tutorial.com](https://vulkan-tutorial.com/) into an extensible RHI-based engine architecture.

### Goals

- **Multi-Backend Support**: Graphics API abstraction enabling Vulkan, WebGPU, D3D12, and Metal backends
- **API Independence**: Upper layers (Renderer, ResourceManager) are completely API-agnostic
- **Architecture**: RHI (Render Hardware Interface) layer with RAII pattern for safe resource management
- **Cross-Platform**: Support for Linux, macOS (MoltenVK), Windows, and Web (WebGPU/WebAssembly)

### Current Status

| Feature | Status | Description |
|---------|--------|-------------|
| **RHI Architecture** | ✅ **Completed** | Graphics API abstraction layer |
| **Vulkan Backend** | ✅ **Completed** | Full RHI implementation with validation |
| FDF Wireframe | ✅ Completed | Heightmap-based wireframe rendering |
| OBJ Model Loading | ✅ Completed | 3D model loading with texture mapping |
| ImGui UI | ✅ Completed | Real-time parameter adjustment UI |
| Camera Controls | ✅ Completed | Mouse/keyboard camera manipulation |
| WebGPU Backend | 🔲 Planned | For web deployment (Phase 8) |
| Ray Tracing | 🔲 Planned | Using VK_KHR_ray_tracing_pipeline |

**Latest Achievement (2025-12-21)**: ✅ Core RHI migration complete with zero Vulkan validation errors!

---

## Features

### Rendering Pipeline

- Vulkan 1.3-based graphics pipeline
- Swapchain management and frame synchronization (Semaphore, Fence)
- Slang shader compilation support

### Resource Management

- **RAII Pattern**: Automatic memory management with `VulkanBuffer`, `VulkanImage`
- **Zero Memory Leak**: All Vulkan resources automatically cleaned up
- Efficient GPU memory transfer via staging buffers

### 3D Rendering

- OBJ model loading (tinyobjloader)
- FDF heightmap parsing and wireframe generation
- MVP matrix transformations and camera system
- Texture loading (STB Image)

### UI System

- ImGui integration (GLFW + Vulkan backend)
- Real-time rendering parameter adjustment

---

## Architecture

### RHI-Based Multi-Backend Architecture

```text
┌─────────────────────────────────────────────────────────┐
│ Layer 1: Application                                    │
│  - Window management, main loop, input handling         │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 2: Renderer (Orchestration)                       │
│  - API-agnostic rendering orchestration                 │
│  - Uses RHI abstractions only                           │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 3: RHI (Render Hardware Interface)                │
│  - Graphics API abstraction layer                       │
│  - RHIDevice, RHISwapchain, RHIPipeline, etc.          │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 4: Backend Implementations                        │
│  ├─ Vulkan Backend (VulkanRHI*)    [✅ Completed]      │
│  ├─ WebGPU Backend (WebGPURHI*)    [🔲 Planned]        │
│  ├─ D3D12 Backend (D3D12RHI*)      [🔲 Future]         │
│  └─ Metal Backend (MetalRHI*)      [🔲 Future]         │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ Subsystems (API-Agnostic)                               │
│  - ResourceManager, SceneManager, ImGuiManager          │
│  - All use RHI abstractions                             │
└─────────────────────────────────────────────────────────┘
```

### Design Principles

| Principle | Description |
|-----------|-------------|
| **API Abstraction** | Graphics API isolated to backend implementations |
| **Dependency Rule** | Upper layers depend only on RHI abstractions, not backends |
| **Single Responsibility** | Each class has one clear responsibility |
| **RAII** | Automatic resource management via `vk::raii::*` wrappers |
| **Dependency Injection** | Dependencies injected through constructors |
| **Zero-Cost Abstraction** | Virtual function overhead < 5% |

### Project Structure

```text
src/
├── Application.cpp/hpp     # Window management, main loop
├── main.cpp
├── rhi/                    # ✨ RHI Abstraction Layer (NEW)
│   ├── RHI*.hpp           # Graphics API abstractions (15 interfaces)
│   └── vulkan/            # Vulkan backend implementation
│       ├── VulkanRHI*.cpp # 12 Vulkan RHI implementations
│       └── VulkanRHI*.hpp
├── core/                   # Core Vulkan components
│   └── VulkanDevice        # Instance, device, queue management
├── rendering/              # API-Agnostic Rendering
│   ├── Renderer            # Uses RHI abstractions only
│   └── RendererBridge      # RHI factory and device management
├── resources/              # Resource management
│   ├── ResourceManager     # Buffer/image creation (RHI-based)
│   ├── VulkanBuffer        # Legacy GPU buffer (RAII)
│   └── VulkanImage         # Legacy GPU image (RAII)
├── scene/                  # Scene management
│   ├── SceneManager        # Scene object management (RHI-based)
│   ├── Mesh                # Mesh data (RHI buffers)
│   └── Camera              # Camera system
├── loaders/                # Asset loaders
│   ├── OBJLoader           # OBJ model loader
│   ├── FDFLoader           # FDF heightmap loader
│   └── TextureLoader       # Texture loader
├── ui/
│   ├── ImGuiManager        # ImGui integration (RHI-based)
│   └── ImGuiVulkanBackend  # Vulkan-specific ImGui backend
└── utils/                  # Utilities (Header-only)
    ├── Vertex.hpp
    ├── VulkanCommon.hpp
    └── FileUtils.hpp
```

**Migration Status**:
- ✅ **100% RHI-native rendering** (Phases 1-7.5 complete)
- ✅ All subsystems (Renderer, ResourceManager, SceneManager, ImGuiManager) use RHI
- ✅ Zero Vulkan validation errors
- 🔲 WebGPU backend implementation (Phase 8 - planned)

---

## Quick Start

### Prerequisites

| Component | Version |
|-----------|----------|
| Vulkan SDK | 1.3+ (with slangc) |
| CMake | 3.28+ |
| C++ Compiler | C++20 support (GCC 12+, Clang 15+, MSVC 19.30+) |
| vcpkg | Latest |

### Build

```bash
# Set environment variables
export VCPKG_ROOT=/path/to/vcpkg
export VULKAN_SDK=/path/to/vulkansdk

# Clone and build
git clone https://github.com/nowead/Mini-Engine.git
cd Mini-Engine
make  # or: cmake --preset=default && cmake --build build
```

### Run

```bash
./build/vulkanGLFW
```

### Controls

| Input | Action |
|-------|--------|
| Mouse Drag | Rotate camera |
| Scroll | Zoom in/out |
| WASD | Move camera |
| ESC | Exit |

---

## Dependencies

Managed via vcpkg:

- **GLFW** - Window and input management
- **GLM** - Mathematics library (matrices, vectors)
- **stb** - Image loading
- **tinyobjloader** - OBJ file parsing
- **ImGui** - UI system

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/README.md](docs/README.md) | Documentation hub |
| [docs/refactoring/layered-to-rhi/](docs/refactoring/layered-to-rhi/) | **RHI Migration Documentation** |
| [RHI_MIGRATION_PRD.md](docs/refactoring/layered-to-rhi/RHI_MIGRATION_PRD.md) | Complete migration plan and progress |
| [PHASE7_SUMMARY.md](docs/refactoring/layered-to-rhi/PHASE7_SUMMARY.md) | Phase 7 & 7.5 completion report |
| [RHI_TECHNICAL_GUIDE.md](docs/refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md) | RHI API reference |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Troubleshooting guide |
| [docs/TROUBLESHOOTING_KR.md](docs/TROUBLESHOOTING_KR.md) | Troubleshooting guide (Korean) |
| [docs/refactoring/](docs/refactoring/) | Legacy refactoring journey |

---

## Development

### Shader Compilation

```bash
# Compile Slang shaders
slangc shaders/shader.slang -o shaders/slang.spv -target spirv
slangc shaders/fdf.slang -o shaders/fdf.spv -target spirv
```

### Code Style

- C++20 Modern C++ style
- RAII-based resource management
- Using `vk::raii::*` Vulkan C++ wrappers

---

## License

This project is created for educational and portfolio purposes.  
Free to use for learning - please provide attribution when using.

---

**Built with Vulkan API and Modern C++**

[Back to Top](#mini-engine)
