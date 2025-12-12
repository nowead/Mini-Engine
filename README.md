# Mini-Engine

> Modern C++20 Vulkan-based 3D Rendering Engine

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)
![CMake](https://img.shields.io/badge/CMake-3.28+-green.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)

---

## Korean Summary

**프로젝트 목표**: Vulkan Tutorial을 학습하며 만든 렌더러를 확장 가능한 엔진 아키텍처로 발전

**핵심 성과**:
- 4계층 객체지향 아키텍처 + RAII 패턴 적용
- 다중 렌더링 기법을 지원하는 확장 가능한 플랫폼
- 체계적인 리팩토링 과정 문서화

**현재 기능**: FDF Wireframe, OBJ Model Loading, ImGui UI, Camera Controls  

**상세 문서**: [docs/](docs/) 폴더 참고

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

Mini-Engine is a Vulkan rendering engine built from scratch, evolved from learning materials at [vulkan-tutorial.com](https://vulkan-tutorial.com/) into an extensible engine architecture.

### Goals

- **Extensibility**: Platform designed to support multiple rendering techniques (Rasterization, Ray Tracing)
- **Architecture**: 4-layer object-oriented design with RAII pattern for safe resource management
- **Cross-Platform**: Support for Linux, macOS (MoltenVK), and Windows

### Current Status

| Feature | Status | Description |
|---------|--------|-------------|
| FDF Wireframe | Completed | Heightmap-based wireframe rendering |
| OBJ Model Loading | Completed | 3D model loading with texture mapping |
| ImGui UI | Completed | Real-time parameter adjustment UI |
| Camera Controls | Completed | Mouse/keyboard camera manipulation |
| Ray Tracing | Planned | Using VK_KHR_ray_tracing_pipeline |

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

4-layer architecture with clear separation of concerns.

```text
Layer 1: Application
  - Window management, main loop, input handling

Layer 2: Renderer (Orchestration)
  - Coordinates all subsystems, frame rendering

Layer 3: Rendering Components
  - VulkanSwapchain, VulkanPipeline, CommandManager, SyncManager

Layer 4: Subsystems
  - ResourceManager, SceneManager

Foundation: Core & Resources
  - VulkanDevice, VulkanBuffer, VulkanImage, Mesh, Camera
```

### Design Principles

| Principle | Description |
|-----------|-------------|
| **Dependency Rule** | Upper layers depend only on lower layers |
| **Single Responsibility** | Each class has one clear responsibility |
| **RAII** | Automatic resource management via `vk::raii::*` wrappers |
| **Dependency Injection** | Dependencies injected through constructors |

### Project Structure

```text
src/
├── Application.cpp/hpp     # Window management, main loop
├── main.cpp
├── core/                   # Vulkan core components
│   ├── VulkanDevice        # Instance, device, queue management
│   └── CommandManager      # Command buffer management
├── rendering/              # Rendering pipeline
│   ├── Renderer            # Rendering orchestration
│   ├── VulkanSwapchain     # Swapchain management
│   ├── VulkanPipeline      # Graphics pipeline
│   └── SyncManager         # Synchronization primitives
├── resources/              # Resource management
│   ├── ResourceManager     # Buffer/image creation factory
│   ├── VulkanBuffer        # GPU buffer (RAII)
│   └── VulkanImage         # GPU image (RAII)
├── scene/                  # Scene management
│   ├── SceneManager        # Scene object management
│   ├── Mesh                # Mesh data
│   └── Camera              # Camera system
├── loaders/                # Asset loaders
│   ├── OBJLoader           # OBJ model loader
│   ├── FDFLoader           # FDF heightmap loader
│   └── TextureLoader       # Texture loader
├── ui/
│   └── ImGuiManager        # ImGui integration
└── utils/                  # Utilities (Header-only)
    ├── Vertex.hpp
    ├── VulkanCommon.hpp
    └── FileUtils.hpp
```

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
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Troubleshooting guide |
| [docs/TROUBLESHOOTING_KR.md](docs/TROUBLESHOOTING_KR.md) | Troubleshooting guide (Korean) |
| [docs/refactoring/](docs/refactoring/) | Refactoring journey |

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
