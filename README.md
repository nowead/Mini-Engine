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

- **RHI (Render Hardware Interface) 아키텍처 마이그레이션 완료** (Phase 1-8, 2025-12-21)
- **레거시 코드 완전 제거** - 100% RHI 네이티브 렌더링 파이프라인
- 그래픽스 API 추상화 계층으로 멀티 백엔드 지원 (Vulkan, WebGPU 준비 완료)
- 4계층 객체지향 아키텍처 + RAII 패턴 적용
- 체계적인 리팩토링 과정 문서화 (~890 LOC 레거시 제거)

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
| **RHI Architecture** | **COMPLETED** | Graphics API abstraction layer |
| **Vulkan Backend** | **COMPLETED** | Full RHI implementation with validation |
| **Legacy Code Cleanup** | **COMPLETED** | 100% RHI-native (Phase 8) |
| FDF Wireframe | Completed | Heightmap-based wireframe rendering |
| OBJ Model Loading | Completed | 3D model loading with texture mapping |
| ImGui UI | Completed | Real-time parameter adjustment UI |
| Camera Controls | Completed | Mouse/keyboard camera manipulation |
| WebGPU Backend | Planned | For web deployment (Phase 9+) |
| Ray Tracing | Planned | Using VK_KHR_ray_tracing_pipeline |

**Latest Achievement (2025-12-21)**: **Phase 8 Complete** - Legacy code removed, 100% RHI-native rendering with zero validation errors!

---

## Features

### Rendering Pipeline

- Vulkan 1.3-based graphics pipeline
- Swapchain management and frame synchronization (Semaphore, Fence)
- Slang shader compilation support

### Resource Management

- **RAII Pattern**: Automatic RHI resource management
- **Zero Memory Leak**: All GPU resources automatically cleaned up
- Efficient GPU memory transfer via staging buffers
- **RHI Abstraction**: Platform-independent buffer/texture creation

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
┌──────────────────────────────────────────────────────────────────┐
│                    Layer 1: Application                          │
│  ┌────────────┐  ┌──────────────┐  ┌─────────────────┐           │
│  │ Window     │  │ Input        │  │ Main Loop       │           │
│  │ (GLFW)     │  │ Handling     │  │ & Event System  │           │
│  └────────────┘  └──────────────┘  └─────────────────┘           │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│          Layer 2: High-Level Subsystems (API-Agnostic)           │
│  ┌────────────────┐  ┌────────────────┐  ┌──────────────────┐    │
│  │   Renderer     │  │ ResourceManager│  │  SceneManager    │    │
│  │ - Orchestrates │  │ - GPU Buffers  │  │  - Meshes        │    │
│  │   rendering    │  │ - Textures     │  │  - Camera        │    │
│  │ - Frame loop   │  │ - Staging ops  │  │  - Transforms    │    │
│  └────────────────┘  └────────────────┘  └──────────────────┘    │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │              ImGuiManager (UI System)                    │    │
│  │  - Real-time parameter adjustment                        │    │
│  │  - Debug visualization                                   │    │
│  └──────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│           Layer 3: RHI (Render Hardware Interface)               │
│  ┌────────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────┐      │
│  │ RHIDevice  │ │Swapchain │ │ Pipeline │ │ CommandBuffer │      │
│  └────────────┘ └──────────┘ └──────────┘ └───────────────┘      │
│  ┌────────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────┐      │
│  │ RHIBuffer  │ │ Texture  │ │ Sampler  │ │  BindGroup    │      │
│  └────────────┘ └──────────┘ └──────────┘ └───────────────┘      │
│  ┌────────────┐ ┌──────────┐ ┌──────────┐                        │
│  │  Shader    │ │  Queue   │ │   Sync   │                        │
│  └────────────┘ └──────────┘ └──────────┘                        │
│                                                                  │
│  Pure abstract interfaces (no API-specific code)                 │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│              Layer 4: Backend Implementations                    │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ [COMPLETE] Vulkan Backend (rhi-vulkan/)                    │  │
│  │  - VulkanRHIDevice, VulkanRHISwapchain, etc.               │  │
│  │  - VMA (Vulkan Memory Allocator) integration               │  │
│  │  - Platform-specific rendering (Vulkan 1.1/1.3)            │  │
│  │  - Complete implementation (12 RHI classes)                │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ [PLANNED] WebGPU Backend (rhi-webgpu/)                     │  │
│  │  - WebGPURHIDevice, WebGPURHISwapchain, etc.               │  │
│  │  - Web and native deployment                               │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ [PLANNED] Future Backends (D3D12, Metal)                   │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│                   Native Graphics APIs                           │
│          Vulkan | WebGPU | D3D12 | Metal                         │
└──────────────────────────────────────────────────────────────────┘
```

**Key Principles**:

- **Abstraction**: Layers 1-2 are 100% API-agnostic
- **Dependency Rule**: Upper layers only depend on RHI interfaces
- **Backend Selection**: Runtime selection via `RHIFactory`
- **Zero Legacy**: All deprecated wrapper classes removed (Phase 8)

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
├── main.cpp                # Entry point
├── Application.cpp/hpp     # Window management, main loop
│
├── rhi/                    # RHI Abstraction Layer (Layer 3)
│   ├── include/rhi/       # Pure abstract interfaces
│   │   ├── RHIDevice.hpp          # Device abstraction
│   │   ├── RHISwapchain.hpp       # Swapchain abstraction
│   │   ├── RHIPipeline.hpp        # Pipeline abstraction
│   │   ├── RHIBuffer.hpp          # Buffer abstraction
│   │   ├── RHITexture.hpp         # Texture abstraction
│   │   ├── RHICommandEncoder.hpp  # Command recording
│   │   ├── RHIBindGroup.hpp       # Resource binding
│   │   ├── RHIShader.hpp          # Shader abstraction
│   │   ├── RHISync.hpp            # Synchronization
│   │   ├── RHIQueue.hpp           # Queue abstraction
│   │   └── ... (15 abstractions total)
│   └── src/
│       └── RHIFactory.cpp  # Backend factory
│
├── rhi-vulkan/             # Vulkan Backend (Layer 4)
│   ├── include/rhi-vulkan/
│   │   └── VulkanRHI*.hpp  # Vulkan implementations
│   └── src/
│       └── VulkanRHI*.cpp  # 12 Vulkan RHI classes
│
├── rendering/              # High-Level Rendering (Layer 2)
│   ├── Renderer.cpp/hpp        # Orchestrates rendering (API-agnostic)
│   └── RendererBridge.cpp/hpp  # RHI device management
│
├── resources/              # Resource Management (Layer 2)
│   └── ResourceManager.cpp/hpp # GPU buffer/texture creation (RHI-based)
│
├── scene/                  # Scene Management (Layer 2)
│   ├── SceneManager.cpp/hpp    # Scene object management (RHI-based)
│   ├── Mesh.cpp/hpp            # Mesh data (RHI buffers)
│   └── Camera.cpp/hpp          # Camera system
│
├── loaders/                # Asset Loaders
│   ├── OBJLoader.cpp/hpp       # OBJ model loader
│   └── FDFLoader.cpp/hpp       # FDF heightmap loader
│
├── ui/                     # UI System (Layer 2)
│   ├── ImGuiManager.cpp/hpp        # ImGui integration (RHI-based)
│   └── ImGuiVulkanBackend.cpp/hpp  # Vulkan-specific backend
│
├── core/                   # Legacy Core (to be removed)
│   └── VulkanDevice.cpp/hpp    # Direct Vulkan device (for ImGui compat)
│
└── utils/                  # Utilities (Header-only)
    ├── Vertex.hpp
    ├── VulkanCommon.hpp
    └── FileUtils.hpp
```

**Migration Status**:

- [COMPLETE] **Phase 1-7**: RHI architecture implementation complete
- [COMPLETE] **Phase 8**: Legacy code cleanup complete (~890 LOC removed)
  - Deleted: VulkanBuffer, VulkanImage, VulkanPipeline, VulkanSwapchain, SyncManager
  - 100% RHI-native rendering
  - Zero Vulkan validation errors
- [PLANNED] **Phase 9+**: WebGPU backend, VulkanDevice removal

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
| [PHASE8_SUMMARY.md](docs/refactoring/layered-to-rhi/PHASE8_SUMMARY.md) | Phase 8 completion report (legacy cleanup) |
| [ARCHITECTURE.md](docs/refactoring/layered-to-rhi/ARCHITECTURE.md) | Complete 4-layer architecture guide |
| [PHASE8_DIRECTORY_REFACTORING.md](docs/refactoring/layered-to-rhi/PHASE8_DIRECTORY_REFACTORING.md) | Directory restructuring guide |
| [PHASE7_SUMMARY.md](docs/refactoring/layered-to-rhi/PHASE7_SUMMARY.md) | Phase 7 & 7.5 completion report |
| [RHI_TECHNICAL_GUIDE.md](docs/refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md) | RHI API reference |
| [TROUBLESHOOTING.md](docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md) | RHI migration troubleshooting |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | General troubleshooting guide |
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

This project is created for educational purposes.  
Free to use for learning - please provide attribution when using.

---

**Built with Vulkan API and Modern C++**

[Back to Top](#mini-engine)
