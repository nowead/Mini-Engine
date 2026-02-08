# Mini-Engine

> PBR & GPU-Driven Rendering Engine with Multi-Backend RHI Architecture

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)
![RHI](https://img.shields.io/badge/RHI-Completed-brightgreen.svg)
![CMake](https://img.shields.io/badge/CMake-3.28+-green.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)

---

## Korean Summary

**프로젝트 목표**: PBR 렌더링과 GPU-Driven 최적화를 갖춘 프로덕션 수준의 렌더링 엔진 개발

**핵심 성과**:

- **Cook-Torrance PBR**: GGX Distribution, Smith Geometry, Fresnel-Schlick 기반 물리 기반 렌더링
- **Image Based Lighting (IBL)**: HDR 환경맵 기반 Irradiance/Prefiltered Specular/BRDF LUT
- **GPU-Driven Rendering**: SSBO 기반 오브젝트 데이터 + Compute Shader Frustum Culling + Indirect Draw
- **RHI 멀티 백엔드**: Vulkan 1.3 (Desktop) + WebGPU (Web/WASM)
- **GPU 프로파일링**: vkCmdWriteTimestamp 기반 per-pass GPU 타이밍
- 100,000+ 오브젝트 실시간 렌더링 지원

**현재 기능**: Cook-Torrance PBR, IBL, GPU Frustum Culling, Indirect Draw, Shadow Mapping, GPU Profiling, ImGui UI

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
| **Cook-Torrance PBR** | **COMPLETED** | Physically-based rendering with metallic/roughness workflow |
| **Image Based Lighting** | **COMPLETED** | HDR environment maps, irradiance convolution, prefiltered specular |
| **GPU-Driven Rendering** | **COMPLETED** | SSBO-based per-object data, compute shader culling, indirect draw |
| **GPU Frustum Culling** | **COMPLETED** | Compute shader AABB-plane test with atomic indirect draw |
| **Shadow Mapping** | **COMPLETED** | Directional light PCF shadows with configurable bias/strength |
| **GPU Profiling** | **COMPLETED** | Per-pass timestamp queries (Culling, Shadow, Main Pass) |
| **Memory Aliasing** | **COMPLETED** | Transient resources, lazily allocated memory |
| **Async Compute** | **COMPLETED** | Timeline semaphores, dedicated compute queue |
| **RHI Architecture** | **COMPLETED** | Graphics API abstraction layer |
| **Vulkan Backend** | **COMPLETED** | Full RHI implementation (Desktop) |
| **WebGPU Backend** | **COMPLETED** | Full RHI implementation (Web/WASM) |
| ImGui UI | Completed | Real-time parameter adjustment + stress testing |

**Latest Achievements**:
- **GPU Profiling & Stress Test**: Per-pass GPU timing display, 100K object stress test with ImGui controls
- **PBR & IBL Pipeline**: Cook-Torrance BRDF + HDR environment-based ambient lighting
- **GPU-Driven Rendering**: Single indirect draw call for 100K+ objects with compute shader frustum culling

---

## Features

### Cook-Torrance PBR Pipeline

- **Physically Based Rendering**: GGX Distribution, Smith Geometry, Fresnel-Schlick BRDF
- **Metallic/Roughness Workflow**: Per-object material parameters via SSBO
- **ACES Filmic Tone Mapping**: Configurable exposure with sRGB output
- **Image Based Lighting (IBL)**: HDR environment maps with irradiance convolution, prefiltered specular, and BRDF LUT via compute shaders

### GPU-Driven Rendering

- **SSBO Object Data**: 128-byte `ObjectData` struct (world matrix, AABB, material) replaces per-instance vertex attributes
- **Compute Shader Frustum Culling**: Per-object AABB vs 6 frustum plane test (workgroup size 64)
- **Indirect Draw**: Single `drawIndexedIndirect` call renders 100K+ objects
- **Visible Indices Buffer**: Atomic-based compaction for culled object indirection

### Shadow Mapping

- **Directional Light Shadows**: Orthographic projection from sun direction
- **PCF Filtering**: Configurable shadow bias and strength
- **Cross-Platform**: Y-coordinate flip for WebGPU texture coordinate system

### GPU Profiling (Vulkan)

- **Per-Pass Timing**: `vkCmdWriteTimestamp` for Frustum Cull, Shadow Pass, Main Pass
- **EMA Smoothing**: Exponential moving average for stable timing display
- **Stress Test UI**: ImGui logarithmic slider (16 → 100K objects) with preset buttons

### Multi-Backend RHI

**Vulkan Backend (Desktop)**:

- Vulkan 1.3-based graphics pipeline
- VMA integration with memory aliasing and transient resources
- Timeline semaphores and async compute queue
- Slang shader compilation to SPIR-V

**WebGPU Backend (Web)**:

- Browser WebGPU API integration via Emscripten
- Runtime SPIR-V to WGSL shader conversion
- Complete RHI parity with Vulkan backend

### Resource Management

- **RAII Pattern**: Automatic RHI resource management with zero memory leaks
- **Memory Aliasing**: Transient depth buffer with lazily allocated memory
- **Staging Buffers**: Efficient CPU-to-GPU data transfer
- **RHI Abstraction**: Platform-independent buffer/texture/pipeline creation

### Scene & UI

- OBJ model loading, camera system, skybox rendering
- ImGui integration with real-time PBR parameter adjustment
- GPU timing display and stress test controls

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
│  │ [COMPLETE] Vulkan Backend (rhi-vulkan/) - Desktop          │  │
│  │  - VulkanRHIDevice, VulkanRHISwapchain, etc.               │  │
│  │  - VMA (Vulkan Memory Allocator) integration               │  │
│  │  - Platform-specific rendering (Vulkan 1.1/1.3)            │  │
│  │  - Complete implementation (15 RHI classes)                │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ [COMPLETE] WebGPU Backend (rhi-webgpu/) - Web Only         │  │
│  │  - WebGPURHIDevice, WebGPURHISwapchain, etc.               │  │
│  │  - Emscripten + Browser WebGPU API                         │  │
│  │  - SPIR-V → WGSL shader conversion                         │  │
│  │  - Complete implementation (15 RHI classes)                │  │
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
├── Application.cpp/hpp     # Window management, main loop, stress test control
│
├── rendering/              # High-Level Rendering (Layer 2)
│   ├── Renderer.cpp/hpp        # Main renderer: PBR, GPU culling, indirect draw
│   ├── RendererBridge.cpp/hpp  # RHI device management
│   ├── ShadowRenderer.cpp/hpp # Directional shadow mapping with PCF
│   ├── SkyboxRenderer.cpp/hpp # HDR skybox rendering
│   ├── IBLManager.cpp/hpp     # IBL pipeline (irradiance, prefilter, BRDF LUT)
│   ├── BatchRenderer.cpp/hpp  # Batch rendering utilities
│   └── InstancedRenderData.hpp # ObjectData struct (128-byte SSBO layout)
│
├── game/                   # Game Logic (Layer 2)
│   ├── entities/               # Entity definitions
│   │   └── BuildingEntity.cpp/hpp
│   ├── managers/               # Entity management
│   │   ├── BuildingManager.cpp/hpp  # Building SSBO generation
│   │   └── WorldManager.cpp/hpp     # World state management
│   ├── world/                  # World data structures
│   │   └── Sector.hpp
│   ├── sync/                   # Data synchronization
│   │   ├── PriceUpdate.hpp
│   │   └── MockDataGenerator.hpp
│   └── utils/                  # Game utilities
│       ├── AnimationUtils.hpp
│       └── HeightCalculator.hpp
│
├── effects/                # Visual Effects (Layer 2)
│   ├── ParticleSystem.cpp/hpp    # CPU particle simulation
│   └── ParticleRenderer.cpp/hpp  # GPU particle rendering
│
├── rhi/                    # RHI Abstraction Layer (Layer 3)
│   ├── include/rhi/       # Pure abstract interfaces (15 abstractions)
│   └── src/
│       └── RHIFactory.cpp  # Backend factory
│
├── rhi-vulkan/             # Vulkan Backend (Layer 4)
│   └── src/VulkanRHI*.cpp  # 15 Vulkan RHI classes + VMA
│
├── rhi-webgpu/             # WebGPU Backend (Layer 4)
│   └── src/WebGPURHI*.cpp  # 15 WebGPU RHI classes
│
├── resources/              # Resource Management (Layer 2)
│   └── ResourceManager.cpp/hpp
│
├── scene/                  # Scene Management (Layer 2)
│   ├── SceneManager.cpp/hpp
│   ├── Mesh.cpp/hpp
│   └── Camera.cpp/hpp
│
├── loaders/                # Asset Loaders
│   ├── OBJLoader.cpp/hpp
│   └── FDFLoader.cpp/hpp
│
├── ui/                     # UI System (Layer 2)
│   ├── ImGuiManager.cpp/hpp        # PBR controls, GPU timing, stress test UI
│   └── ImGuiVulkanBackend.cpp/hpp
│
├── core/                   # Legacy Core (ImGui compatibility)
│   └── VulkanDevice.cpp/hpp
│
└── utils/                  # Utilities
    ├── GpuProfiler.cpp/hpp # Vulkan timestamp-based GPU profiler
    ├── Logger.hpp          # Logging utility
    ├── Vertex.hpp
    └── FileUtils.hpp

shaders/                    # GLSL + WGSL dual shaders
├── building.{vert,frag}.glsl / building.wgsl   # PBR + IBL + SSBO
├── shadow.{vert,frag}.glsl / shadow.wgsl       # Shadow pass
├── skybox.{vert,frag}.glsl / skybox.wgsl       # Skybox
├── frustum_cull.comp.glsl / .wgsl              # GPU frustum culling
├── equirect_to_cubemap.comp.glsl / .wgsl       # HDR → cubemap
├── irradiance_map.comp.glsl / .wgsl            # Irradiance convolution
├── prefilter_env.comp.glsl / .wgsl             # Specular prefilter
├── brdf_lut.comp.glsl / .wgsl                  # BRDF integration LUT
└── particle.{vert,frag}.glsl / particle.wgsl   # Particle effects

scripts/
└── setup_emscripten.sh     # Automatic Emscripten SDK installer
```

**Development History**:

- **Phase 1-8**: RHI architecture design, implementation, and legacy cleanup (100% RHI-native)
- **Phase 9**: WebGPU backend — 15 RHI classes, Emscripten WASM, full web deployment
- **Week 1**: PBR pipeline — Cook-Torrance BRDF, IBL (irradiance/prefilter/BRDF LUT), HDR environment maps
- **Week 2**: GPU-Driven rendering — SSBO, compute shader frustum culling, indirect draw (100K+ objects)
- **Week 3**: Memory aliasing (transient resources, lazy allocation), async compute (timeline semaphores)
- **Week 4**: GPU profiling (`vkCmdWriteTimestamp`), stress test UI, documentation

---

## Quick Start

### Prerequisites

| Component | Version | Required For |
|-----------|----------|--------------|
| Vulkan SDK | 1.3+ (with slangc) | Desktop builds |
| CMake | 3.28+ | All builds |
| C++ Compiler | C++20 support (GCC 12+, Clang 15+, MSVC 19.30+) | All builds |
| vcpkg | Latest | Dependency management |
| Emscripten SDK | 3.1.71+ | Web builds (optional) |

### Desktop Build (Vulkan)

```bash
# Set environment variables
export VCPKG_ROOT=/path/to/vcpkg
export VULKAN_SDK=/path/to/vulkansdk

# Clone and build
git clone https://github.com/nowead/Mini-Engine.git
cd Mini-Engine
make build

# Run
./build/vulkanGLFW
```

### Web Build (WebGPU + WebAssembly)

```bash
# One-time setup: Automatic Emscripten SDK installation
make setup-emscripten
# OR manually run: ./scripts/setup_emscripten.sh

# Build for web
make wasm

# Serve locally and test in browser
make serve-wasm
# Open http://localhost:8000 in Chrome 113+ or Edge 113+
```

**Important Notes**:
- **Desktop development doesn't need Emscripten** - Only install for web deployment
- **Browser Requirements**: Chrome 113+, Edge 113+, or other WebGPU-compatible browsers
- **Automatic Setup**: `setup_emscripten.sh` installs Emscripten SDK 3.1.71 to `~/emsdk`
- **Platform Separation**: Desktop uses Vulkan backend, Web uses WebGPU backend

### Controls

| Input | Action |
|-------|--------|
| Mouse Drag | Rotate camera |
| Scroll | Zoom in/out |
| WASD | Move camera |
| ESC | Exit |

---

## Dependencies

### Desktop Build Dependencies (vcpkg)

- **GLFW** - Window and input management
- **GLM** - Mathematics library (matrices, vectors)
- **stb** - Image loading
- **tinyobjloader** - OBJ file parsing
- **ImGui** - UI system
- **Vulkan SDK** - Vulkan headers and validation layers
- **VulkanMemoryAllocator** - GPU memory management

### Web Build Dependencies (Emscripten ports)

- **Emscripten SDK** - WebAssembly compiler toolchain
- **Browser WebGPU API** - Native browser GPU access (Chrome 113+, Edge 113+)
- All other dependencies provided by Emscripten ports

**Installation**:
```bash
# Desktop dependencies (via vcpkg)
cmake --preset linux-default  # Auto-installs vcpkg deps

# Web dependencies (one command)
make setup-emscripten  # Auto-installs Emscripten SDK
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/README.md](docs/README.md) | Documentation hub |
| [docs/SUMMARY.md](docs/SUMMARY.md) | **Project overview and backend status** |
| [docs/refactoring/webgpu-backend/](docs/refactoring/webgpu-backend/) | **WebGPU Backend Documentation** |
| [ARCHITECTURE_CLARIFICATION.md](docs/refactoring/webgpu-backend/ARCHITECTURE_CLARIFICATION.md) | Mini-Engine vs Dawn architecture |
| [SUMMARY.md](docs/refactoring/webgpu-backend/SUMMARY.md) | WebGPU implementation summary |
| [docs/refactoring/layered-to-rhi/](docs/refactoring/layered-to-rhi/) | **RHI Migration Documentation** |
| [RHI_MIGRATION_PRD.md](docs/refactoring/layered-to-rhi/RHI_MIGRATION_PRD.md) | Complete migration plan and progress |
| [PHASE8_SUMMARY.md](docs/refactoring/layered-to-rhi/PHASE8_SUMMARY.md) | Phase 8 completion report (legacy cleanup) |
| [ARCHITECTURE.md](docs/refactoring/layered-to-rhi/ARCHITECTURE.md) | Complete 4-layer architecture guide |
| [RHI_TECHNICAL_GUIDE.md](docs/refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md) | RHI API reference |
| [TROUBLESHOOTING.md](docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md) | RHI migration troubleshooting |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | General troubleshooting guide |
| [docs/TROUBLESHOOTING_KR.md](docs/TROUBLESHOOTING_KR.md) | Troubleshooting guide (Korean) |
| [docs/refactoring/](docs/refactoring/) | Legacy refactoring journey |

---

## Development

### Build System (Makefile)

The project includes a comprehensive Makefile for both desktop and web builds:

```bash
# Desktop builds
make build              # Build desktop version (Vulkan)
make clean              # Clean build artifacts

# Web builds
make setup-emscripten   # One-click Emscripten SDK setup
make wasm               # Build WebAssembly version
make serve-wasm         # Serve WASM locally (port 8000)
make clean-wasm         # Clean WASM build artifacts

# Utilities
make help               # Show all available targets
```

**Automatic Emscripten Setup**:
- `make setup-emscripten` runs `scripts/setup_emscripten.sh`
- Automatically installs Emscripten SDK 3.1.71 to `~/emsdk`
- Idempotent: Safe to run multiple times
- Enhanced error messages guide users through setup

### Shader Compilation

```bash
# Compile Slang shaders (Desktop - SPIR-V)
slangc shaders/shader.slang -o shaders/slang.spv -target spirv
slangc shaders/fdf.slang -o shaders/fdf.spv -target spirv

# Note: WebGPU uses runtime SPIR-V → WGSL conversion
```

### Code Style

- C++20 Modern C++ style
- RAII-based resource management
- Using `vk::raii::*` Vulkan C++ wrappers for Vulkan backend
- Platform-specific implementations isolated in backend folders

---

## License

This project is created for educational purposes.  
Free to use for learning - please provide attribution when using.

---

**PBR & GPU-Driven Rendering Engine — Vulkan 1.3 + WebGPU + Modern C++20**

[Back to Top](#mini-engine)
