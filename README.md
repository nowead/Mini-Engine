# Mini-Engine

> PBR & GPU-Driven Rendering Engine with Multi-Backend RHI Architecture

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)
![WebGPU](https://img.shields.io/badge/WebGPU-WASM-orange.svg)
![CMake](https://img.shields.io/badge/CMake-3.28+-green.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Web-lightgrey.svg)

<p align="center">
  <img src="docs/images/PBR+IBL.png" width="48%" alt="PBR Material Showcase with IBL" />
  <img src="docs/images/mini-engine.png" width="48%" alt="GPU-Driven Rendering" />
</p>
<p align="center">
  <em>PBR Material Showcase with HDR IBL (left) &nbsp;|&nbsp; GPU-Driven Rendering with 10K+ Objects (right)</em>
</p>

---

## Korean Summary

**프로젝트 목표**: PBR 렌더링과 GPU-Driven 최적화를 갖춘 프로덕션 수준의 렌더링 엔진 개발

**핵심 성과**:

- **Cook-Torrance PBR**: GGX Distribution, Smith Geometry, Fresnel-Schlick 기반 물리 기반 렌더링
- **Image Based Lighting (IBL)**: HDR 환경맵 기반 Irradiance/Prefiltered Specular/BRDF LUT
- **GPU-Driven Rendering**: SSBO 기반 오브젝트 데이터 + Compute Shader Frustum Culling + Indirect Draw
- **RHI 멀티 백엔드**: Vulkan 1.3 (Desktop) + WebGPU (Web/WASM)
- **GPU 프로파일링**: vkCmdWriteTimestamp 기반 per-pass GPU 타이밍
- 10,000+ 오브젝트 실시간 렌더링 지원

**현재 기능**: Cook-Torrance PBR, IBL, GPU Frustum Culling, Indirect Draw, Shadow Mapping, GPU Profiling, ImGui UI

**상세 문서**: [docs/refactoring/layered-to-rhi/](docs/refactoring/layered-to-rhi/) 폴더 참고

---

## Highlights

- **Cook-Torrance PBR** — GGX/Smith/Fresnel-Schlick BRDF with metallic/roughness workflow
- **Image Based Lighting** — HDR environment maps, irradiance convolution, prefiltered specular, BRDF LUT
- **GPU-Driven Rendering** — SSBO + compute shader frustum culling + indirect draw (100K+ objects)
- **Multi-Backend RHI** — Vulkan 1.3 (Desktop) + WebGPU (Web/WASM), fully API-agnostic upper layers
- **Shadow Mapping** — Directional PCF shadows with configurable bias/strength
- **GPU Profiling** — Per-pass timestamp queries with EMA smoothing

---

## Architecture

```text
┌──────────────────────────────────────────────────────────┐
│  Application    │  Window (GLFW)  │  Input  │  Main Loop │
├──────────────────────────────────────────────────────────┤
│  High-Level (API-Agnostic)                               │
│  Renderer · ResourceManager · SceneManager · ImGui UI    │
├──────────────────────────────────────────────────────────┤
│  RHI (Render Hardware Interface)                         │
│  Device · Swapchain · Pipeline · Buffer · Texture · ...  │
│  Pure abstract interfaces — no API-specific code         │
├──────────────────────────────────────────────────────────┤
│  Backends                                                │
│  Vulkan 1.3 (VMA, Timeline Semaphores, Async Compute)    │
│  WebGPU (Emscripten, SPIR-V → WGSL conversion)           │
└──────────────────────────────────────────────────────────┘
```

**Principles**: API abstraction via RHI interfaces / RAII resource management / Dependency injection / Backend selection via `RHIFactory`

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

### Multi-Backend RHI

**Vulkan Backend (Desktop)**:
- Vulkan 1.3-based graphics pipeline with VMA integration
- Memory aliasing, transient resources, lazily allocated memory
- Timeline semaphores and async compute queue
- Slang shader compilation to SPIR-V

**WebGPU Backend (Web)**:
- Browser WebGPU API integration via Emscripten
- Runtime SPIR-V to WGSL shader conversion
- Complete RHI parity with Vulkan backend

### Shadow Mapping & GPU Profiling

- **Directional Light Shadows**: Orthographic projection, PCF filtering, configurable bias/strength
- **Per-Pass Timing**: `vkCmdWriteTimestamp` for Frustum Cull, Shadow Pass, Main Pass
- **Stress Test UI**: ImGui logarithmic slider (16 → 100K objects) with preset buttons

---

## Feature Status

| Feature | Status | Description |
|---------|--------|-------------|
| **Cook-Torrance PBR** | Completed | Physically-based rendering with metallic/roughness workflow |
| **Image Based Lighting** | Completed | HDR environment maps, irradiance convolution, prefiltered specular |
| **GPU-Driven Rendering** | Completed | SSBO-based per-object data, compute shader culling, indirect draw |
| **GPU Frustum Culling** | Completed | Compute shader AABB-plane test with atomic indirect draw |
| **Shadow Mapping** | Completed | Directional light PCF shadows with configurable bias/strength |
| **GPU Profiling** | Completed | Per-pass timestamp queries (Culling, Shadow, Main Pass) |
| **Memory Aliasing** | Completed | Transient resources, lazily allocated memory |
| **Async Compute** | Completed | Timeline semaphores, dedicated compute queue |
| **Vulkan Backend** | Completed | Full RHI implementation (Desktop) |
| **WebGPU Backend** | Completed | Full RHI implementation (Web/WASM) |
| **ImGui UI** | Completed | Real-time parameter adjustment + stress testing |

---

## Quick Start

### Prerequisites

| Component | Version | Required For |
|-----------|---------|--------------|
| Vulkan SDK | 1.3+ (with slangc) | Desktop builds |
| CMake | 3.28+ | All builds |
| C++ Compiler | C++20 (GCC 12+, Clang 15+, MSVC 19.30+) | All builds |
| vcpkg | Latest | Dependency management |
| Emscripten SDK | 3.1.71+ | Web builds (optional) |

### Build & Run

```bash
# Desktop (Vulkan)
export VCPKG_ROOT=/path/to/vcpkg
export VULKAN_SDK=/path/to/vulkansdk
git clone https://github.com/nowead/Mini-Engine.git
cd Mini-Engine
make build
./build/vulkanGLFW

# Web (WebGPU + WASM)
make setup-emscripten   # one-time Emscripten SDK install
make wasm
make serve-wasm         # http://localhost:8000 (Chrome/Edge 113+)

# Demos
make demo-pbr           # PBR Material Showcase
make demo-instancing    # GPU Instancing Demo
```

**Notes**:
- Desktop development doesn't need Emscripten — only install for web deployment
- Browser Requirements: Chrome 113+, Edge 113+, or other WebGPU-compatible browsers

### Controls

| Input | Action |
|-------|--------|
| Mouse Drag | Rotate camera |
| Scroll | Zoom in/out |
| WASD | Move camera |
| ESC | Exit |

---

## Project Structure

```text
src/
├── main.cpp                # Entry point
├── Application.cpp/hpp     # Window management, main loop, stress test control
│
├── rendering/              # High-Level Rendering (Layer 2)
│   ├── Renderer.cpp/hpp        # Main renderer: PBR, GPU culling, indirect draw
│   ├── RendererBridge.cpp/hpp  # RHI device management
│   ├── ShadowRenderer.cpp/hpp  # Directional shadow mapping with PCF
│   ├── SkyboxRenderer.cpp/hpp  # HDR skybox rendering
│   ├── IBLManager.cpp/hpp      # IBL pipeline (irradiance, prefilter, BRDF LUT)
│   └── InstancedRenderData.hpp # ObjectData struct (128-byte SSBO layout)
│
├── rhi/                    # RHI Abstraction Layer (Layer 3)
│   ├── include/rhi/            # Pure abstract interfaces (15 abstractions)
│   └── src/RHIFactory.cpp      # Backend factory
│
├── rhi-vulkan/             # Vulkan Backend (Layer 4)
│   └── src/VulkanRHI*.cpp      # 15 Vulkan RHI classes + VMA
│
├── rhi-webgpu/             # WebGPU Backend (Layer 4)
│   └── src/WebGPURHI*.cpp      # 15 WebGPU RHI classes
│
├── scene/                  # Scene Management (Layer 2)
│   ├── SceneManager.cpp/hpp
│   ├── Mesh.cpp/hpp
│   └── Camera.cpp/hpp
│
├── resources/              # Resource Management (Layer 2)
│   └── ResourceManager.cpp/hpp
│
├── effects/                # Visual Effects (Layer 2)
│   ├── ParticleSystem.cpp/hpp
│   └── ParticleRenderer.cpp/hpp
│
├── ui/                     # UI System (Layer 2)
│   ├── ImGuiManager.cpp/hpp
│   └── ImGuiVulkanBackend.cpp/hpp
│
└── utils/                  # Utilities
    ├── GpuProfiler.cpp/hpp     # Vulkan timestamp-based GPU profiler
    └── Logger.hpp

shaders/                    # GLSL + WGSL dual shaders
├── building.{vert,frag}.glsl   # PBR + IBL + SSBO
├── frustum_cull.comp.glsl      # GPU frustum culling
├── shadow.{vert,frag}.glsl     # Shadow pass
├── skybox.{vert,frag}.glsl     # Skybox
├── equirect_to_cubemap.comp.glsl / irradiance_map / prefilter_env / brdf_lut  # IBL compute
└── particle.{vert,frag}.glsl   # Particle effects
```

---

## Dependencies

### Desktop Build (vcpkg)

- **GLFW** — Window and input management
- **GLM** — Mathematics library (matrices, vectors)
- **stb** — Image loading
- **tinyobjloader** — OBJ file parsing
- **ImGui** — UI system
- **Vulkan SDK** — Vulkan headers and validation layers
- **VulkanMemoryAllocator** — GPU memory management

### Web Build (Emscripten)

- **Emscripten SDK** — WebAssembly compiler toolchain
- **Browser WebGPU API** — Native browser GPU access (Chrome 113+, Edge 113+)

```bash
cmake --preset linux-default  # auto-installs vcpkg deps
make setup-emscripten         # auto-installs Emscripten SDK
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/README.md](docs/README.md) | Documentation hub |
| [docs/SUMMARY.md](docs/SUMMARY.md) | Project overview and backend status |
| [RHI Architecture](docs/refactoring/layered-to-rhi/ARCHITECTURE.md) | Complete 4-layer architecture guide |
| [RHI API Reference](docs/refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md) | RHI technical guide |
| [WebGPU Backend](docs/refactoring/webgpu-backend/SUMMARY.md) | WebGPU implementation summary |
| [RHI Migration](docs/refactoring/layered-to-rhi/RHI_MIGRATION_PRD.md) | Complete migration plan and progress |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common issues & fixes |

---

## Development

### Build System (Makefile)

```bash
# Desktop builds
make build              # Build desktop version (Vulkan)
make clean              # Clean build artifacts

# Web builds
make setup-emscripten   # One-click Emscripten SDK setup
make wasm               # Build WebAssembly version
make serve-wasm         # Serve WASM locally (port 8000)
make clean-wasm         # Clean WASM build artifacts

# Demos
make demo-pbr           # PBR Material Showcase
make demo-instancing    # GPU Instancing Demo

# Utilities
make help               # Show all available targets
```

### Development History

- **Phase 1-8**: RHI architecture design, implementation, and legacy cleanup (100% RHI-native)
- **Phase 9**: WebGPU backend — 15 RHI classes, Emscripten WASM, full web deployment
- **phase 10**: PBR pipeline — Cook-Torrance BRDF, IBL (irradiance/prefilter/BRDF LUT), HDR environment maps
- **phase 11**: GPU-Driven rendering — SSBO, compute shader frustum culling, indirect draw (100K+ objects)
- **phase 12**: Memory aliasing (transient resources, lazy allocation), async compute (timeline semaphores)
- **phase 13**: GPU profiling (`vkCmdWriteTimestamp`), stress test UI, documentation

---

## License

This project is created for educational purposes.
Free to use for learning — please provide attribution when using.

---

**PBR & GPU-Driven Rendering Engine — Vulkan 1.3 + WebGPU + Modern C++20**

[Back to Top](#mini-engine)
