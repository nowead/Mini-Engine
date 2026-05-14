# WebGPU Backend Implementation Plan

**Version**: 1.0
**Date**: 2025-12-26
**Status**: Planning Phase
**Target**: Multi-platform deployment (Native + Web via WebAssembly)

---

## Table of Contents

- [Overview](#overview)
- [Goals and Motivation](#goals-and-motivation)
- [WebGPU Architecture](#webgpu-architecture)
- [Implementation Phases](#implementation-phases)
- [Technical Specifications](#technical-specifications)
- [API Mapping Strategy](#api-mapping-strategy)
- [Build System Integration](#build-system-integration)
- [Testing and Validation](#testing-and-validation)
- [References](#references)

---

## Overview

이 문서는 Mini-Engine RHI 아키텍처에 WebGPU Backend를 추가하는 구현 계획을 담고 있습니다. WebGPU는 최신 그래픽스 API로, 네이티브 환경과 웹 브라우저 모두에서 실행 가능한 크로스 플랫폼 렌더링을 제공합니다.

### Key Benefits

| Benefit | Description |
|---------|-------------|
| **Web Deployment** | WebAssembly를 통한 브라우저 실행 지원 |
| **Cross-Platform** | Windows, macOS, Linux 네이티브 실행 |
| **Modern API** | Vulkan/Metal/D3D12의 현대적 추상화 |
| **Safety** | Type-safe API 설계로 런타임 오류 감소 |
| **Future-Proof** | W3C 표준으로 장기 지원 보장 |

### Current Status

- [COMPLETED] Phase 1-8: Vulkan Backend RHI 구현 완료
- [COMPLETED] RHI 인터페이스 설계 완료 (15개 추상화)
- [PLANNED] WebGPU Backend 구현 (본 문서)

---

## Goals and Motivation

### Primary Goals

1. **Web Deployment**: 브라우저에서 Mini-Engine 실행
2. **API Parity**: Vulkan Backend와 동일한 기능 제공
3. **Performance**: 네이티브 수준의 성능 유지
4. **Code Reuse**: 기존 Layer 1-2 코드 100% 재사용

### Motivation

```text
WHY WebGPU?
├─ Web Platform Support
│  └─ React/Vue 앱에 3D 렌더러 임베딩 가능
├─ Simpler API Surface
│  └─ Vulkan보다 간단한 API로 빠른 프로토타이핑
├─ Cross-Platform Native
│  └─ Dawn/wgpu-native로 Windows/macOS/Linux 지원
└─ Educational Value
   └─ 멀티 백엔드 RHI의 진정한 테스트
```

---

## WebGPU Architecture

### WebGPU vs Vulkan Comparison

| Concept | Vulkan | WebGPU |
|---------|--------|--------|
| **Device** | `VkDevice` | `wgpu::Device` |
| **Queue** | `VkQueue` | `wgpu::Queue` |
| **Swapchain** | `VkSwapchainKHR` | `wgpu::Surface` + `wgpu::SwapChain` |
| **Pipeline** | `VkPipeline` | `wgpu::RenderPipeline` |
| **Command Buffer** | `VkCommandBuffer` | `wgpu::CommandEncoder` |
| **Descriptor Set** | `VkDescriptorSet` | `wgpu::BindGroup` |
| **Shader** | SPIR-V | WGSL or SPIR-V |
| **Sync** | Semaphore/Fence | `wgpu::Queue::onSubmittedWorkDone()` |
| **Memory** | VMA | Automatic (managed by implementation) |

### WebGPU API Layers

```text
┌─────────────────────────────────────────────────────┐
│         Mini-Engine Layer 2 (API-Agnostic)          │
│  Renderer | ResourceManager | SceneManager          │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│              RHI Layer 3 (Abstraction)              │
│  RHIDevice | RHIQueue | RHIPipeline | ...           │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│          WebGPU Backend Implementation              │
│  WebGPURHIDevice | WebGPURHIQueue | ...             │
└─────────────────────────────────────────────────────┘
                        ↓
         ┌──────────────┴──────────────┐
         ↓                             ↓
┌──────────────────┐        ┌──────────────────┐
│   Native (C++)   │        │   Web (WASM)     │
│   wgpu-native    │        │   WebGPU API     │
│   or Dawn        │        │   (Browser)      │
└──────────────────┘        └──────────────────┘
```

---

## Implementation Phases

### Phase 1: Environment Setup (Week 1)

**Objective**: WebGPU 개발 환경 구축 및 기본 테스트

#### Tasks

- [ ] **1.1 Dependency Integration**
  - WebGPU 라이브러리 선택: Dawn vs wgpu-native
    - Dawn (Google): Chrome backend, C++ API
    - wgpu-native (Rust): Firefox backend, C API
  - CMake에 WebGPU 라이브러리 통합
  - vcpkg 또는 서브모듈로 의존성 관리

- [ ] **1.2 Hello Triangle (WebGPU Native)**
  - 최소 WebGPU 코드로 삼각형 렌더링
  - Window 생성 (GLFW + WebGPU Surface)
  - 기본 파이프라인 설정

- [ ] **1.3 Emscripten Setup**
  - Emscripten SDK 설치
  - CMake Emscripten toolchain 설정
  - WASM 빌드 테스트

#### Deliverables

```text
examples/
└── webgpu-hello-triangle/
    ├── main.cpp              # Native WebGPU hello triangle
    ├── CMakeLists.txt
    └── index.html            # WASM test page
```

#### Success Criteria

- ✅ 네이티브 WebGPU 삼각형 렌더링
- ✅ Emscripten WASM 빌드 성공
- ✅ 브라우저에서 삼각형 렌더링 확인

---

### Phase 2: RHI Interface Implementation (Week 2-3)

**Objective**: 15개 RHI 인터페이스를 WebGPU로 구현

#### 2.1 Core Components

| RHI Class | WebGPU Equivalent | Priority |
|-----------|-------------------|----------|
| `WebGPURHIDevice` | `wgpu::Device` | P0 (Critical) |
| `WebGPURHIQueue` | `wgpu::Queue` | P0 |
| `WebGPURHISwapchain` | `wgpu::SwapChain` | P0 |
| `WebGPURHICommandBuffer` | `wgpu::CommandEncoder` | P0 |

#### 2.2 Resource Management

| RHI Class | WebGPU Equivalent | Priority |
|-----------|-------------------|----------|
| `WebGPURHIBuffer` | `wgpu::Buffer` | P0 |
| `WebGPURHITexture` | `wgpu::Texture` | P0 |
| `WebGPURHITextureView` | `wgpu::TextureView` | P1 |
| `WebGPURHISampler` | `wgpu::Sampler` | P1 |

#### 2.3 Pipeline and Binding

| RHI Class | WebGPU Equivalent | Priority |
|-----------|-------------------|----------|
| `WebGPURHIPipeline` | `wgpu::RenderPipeline` | P0 |
| `WebGPURHIShader` | `wgpu::ShaderModule` (WGSL) | P0 |
| `WebGPURHIBindGroup` | `wgpu::BindGroup` | P0 |
| `WebGPURHIBindGroupLayout` | `wgpu::BindGroupLayout` | P1 |

#### 2.4 Synchronization

| RHI Class | WebGPU Equivalent | Priority |
|-----------|-------------------|----------|
| `WebGPURHIFence` | `wgpu::Queue::onSubmittedWorkDone()` | P1 |
| `WebGPURHISemaphore` | N/A (WebGPU는 자동 동기화) | P2 |

#### Directory Structure

```text
src/
├── rhi-webgpu/
│   ├── include/rhi-webgpu/
│   │   ├── WebGPURHIDevice.hpp
│   │   ├── WebGPURHIQueue.hpp
│   │   ├── WebGPURHISwapchain.hpp
│   │   ├── WebGPURHICommandBuffer.hpp
│   │   ├── WebGPURHIBuffer.hpp
│   │   ├── WebGPURHITexture.hpp
│   │   ├── WebGPURHIPipeline.hpp
│   │   ├── WebGPURHIShader.hpp
│   │   ├── WebGPURHIBindGroup.hpp
│   │   ├── WebGPURHISync.hpp
│   │   ├── WebGPURHISampler.hpp
│   │   └── WebGPUCommon.hpp
│   └── src/
│       ├── WebGPURHIDevice.cpp
│       ├── WebGPURHIQueue.cpp
│       ├── WebGPURHISwapchain.cpp
│       ├── WebGPURHICommandBuffer.cpp
│       ├── WebGPURHIBuffer.cpp
│       ├── WebGPURHITexture.cpp
│       ├── WebGPURHIPipeline.cpp
│       ├── WebGPURHIShader.cpp
│       ├── WebGPURHIBindGroup.cpp
│       ├── WebGPURHISync.cpp
│       └── WebGPURHISampler.cpp
```

#### Success Criteria

- ✅ 모든 RHI 인터페이스 구현 완료
- ✅ 컴파일 오류 없음
- ✅ 기본 버퍼/텍스처 생성 테스트 통과

---

### Phase 3: Shader Pipeline (Week 4)

**Objective**: SPIR-V → WGSL 변환 및 셰이더 파이프라인 구축

#### 3.1 Shader Conversion Strategy

**Option A: SPIR-V Cross (Recommended)**

```text
Slang (.slang) → SPIR-V → SPIR-V Cross → WGSL
                         └─ (Khronos 공식 도구)
```

**Option B: Tint (Google)**

```text
SPIR-V → Tint → WGSL
```

**Option C: Native WGSL**

```text
WGSL (.wgsl) → WebGPU ShaderModule
└─ Vulkan GLSL과 별도 유지 (듀얼 셰이더)
```

#### 3.2 Implementation Tasks

- [ ] SPIR-V Cross 통합
- [ ] 기존 SPIR-V 셰이더 변환 파이프라인
- [ ] Shader Module 생성 (`WebGPURHIShader`)
- [ ] Pipeline Layout 설정

#### 3.3 Shader Examples

**Current Vulkan Shader (Slang → SPIR-V)**

```glsl
// shaders/shader.slang
struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD;
};
```

**Target WGSL Shader**

```wgsl
// shaders/shader.wgsl
struct VSInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
}
```

#### Success Criteria

- ✅ 기존 Slang 셰이더를 WGSL로 자동 변환
- ✅ WebGPU 파이프라인에서 셰이더 로딩 성공
- ✅ 단색 렌더링 테스트 통과

---

### Phase 4: Rendering Integration (Week 5-6)

**Objective**: 기존 Renderer와 WebGPU Backend 통합

#### 4.1 Factory Pattern Update

**RHIFactory 확장**

```cpp
// src/rhi/src/RHIFactory.cpp
std::unique_ptr<RHIDevice> RHIFactory::createDevice(RHIBackend backend) {
    switch (backend) {
    case RHIBackend::Vulkan:
        return std::make_unique<VulkanRHIDevice>(instance, surface);
    case RHIBackend::WebGPU:  // NEW
        return std::make_unique<WebGPURHIDevice>(instance, surface);
    default:
        throw std::runtime_error("Unsupported backend");
    }
}
```

#### 4.2 Renderer Compatibility

**현재 Renderer 코드 (RHI 기반)**

```cpp
// src/rendering/Renderer.cpp
void Renderer::drawFrame() {
    auto commandBuffer = device->createCommandBuffer();
    auto encoder = commandBuffer->beginRendering(...);

    encoder->setVertexBuffer(0, mesh->getVertexBuffer());
    encoder->setIndexBuffer(mesh->getIndexBuffer());
    encoder->drawIndexed(...);

    commandBuffer->end();
    queue->submit(commandBuffer);
}
```

**No Changes Needed!** → WebGPU Backend만 구현하면 자동 동작

#### 4.3 Integration Tasks

- [ ] `RHIFactory`에 WebGPU 백엔드 추가
- [ ] `RendererBridge`에서 백엔드 선택 로직 추가
- [ ] 런타임 백엔드 전환 테스트

#### Success Criteria

- ✅ FDF Wireframe을 WebGPU로 렌더링
- ✅ OBJ Model을 WebGPU로 렌더링
- ✅ Camera 컨트롤 정상 동작
- ✅ 프레임 동기화 (VSync) 동작

---

### Phase 5: Resource Management (Week 7)

**Objective**: 버퍼, 텍스처, 메모리 관리 구현

#### 5.1 Buffer Management

**Staging Buffer Pattern**

```cpp
// WebGPU Buffer Upload (Vulkan과 유사)
void WebGPURHIBuffer::uploadData(const void* data, size_t size) {
    wgpu::BufferDescriptor stagingDesc{
        .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite,
        .size = size
    };
    auto stagingBuffer = device->CreateBuffer(&stagingDesc);

    // Map and copy
    stagingBuffer.MapAsync(...);
    memcpy(mappedData, data, size);
    stagingBuffer.Unmap();

    // Copy to device buffer
    encoder.CopyBufferToBuffer(stagingBuffer, 0, buffer, 0, size);
}
```

#### 5.2 Texture Management

- [ ] 2D Texture 생성 (`wgpu::Texture`)
- [ ] Texture View 생성
- [ ] Sampler 설정 (Linear, Nearest, Mipmap)
- [ ] STB Image 통합 유지

#### 5.3 Memory Management

**WebGPU의 자동 메모리 관리**

- Vulkan VMA 불필요 (WebGPU가 자동 관리)
- RAII 패턴으로 리소스 해제 (`wgpu::Buffer` 소멸자)

#### Success Criteria

- ✅ Vertex/Index Buffer 업로드 성공
- ✅ Uniform Buffer 동적 업데이트
- ✅ Texture 로딩 및 샘플링
- ✅ 메모리 누수 없음 (Valgrind 검증)

---

### Phase 6: ImGui Integration (Week 8)

**Objective**: ImGui WebGPU Backend 통합

#### 6.1 ImGui Backend Options

**Option A: Official ImGui WebGPU Backend**

```cpp
// imgui/backends/imgui_impl_wgpu.cpp (공식 지원)
ImGui_ImplWGPU_Init(device, swapchainFormat);
ImGui_ImplWGPU_NewFrame();
ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
```

**Option B: Custom Integration**

- RHI 기반 ImGui 렌더러 작성
- WebGPU RHI를 사용하는 커스텀 백엔드

#### 6.2 Implementation Tasks

- [ ] ImGui WebGPU Backend 통합
- [ ] `ImGuiManager`를 WebGPU 지원으로 확장
- [ ] UI 렌더링 테스트

#### Success Criteria

- ✅ ImGui UI가 WebGPU로 렌더링
- ✅ 실시간 파라미터 조정 동작
- ✅ 마우스/키보드 입력 정상

---

### Phase 7: WebAssembly Build (Week 9-10)

**Objective**: Emscripten 빌드 및 웹 배포

#### 7.1 Emscripten CMake Setup

```cmake
# CMakeLists.txt (Emscripten 빌드)
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_WEBGPU=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1")

    target_link_options(vulkanGLFW PRIVATE
        "SHELL:-s USE_GLFW=3"
        "SHELL:-s WASM=1"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
    )
endif()
```

#### 7.2 Build Tasks

- [ ] Emscripten toolchain 파일 작성
- [ ] WASM 빌드 스크립트 (`make wasm`)
- [ ] Asset 번들링 (--preload-file)
- [ ] HTML 템플릿 작성

#### 7.3 Web Deployment

```bash
# Build for Web
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release
emmake make -C build-wasm

# Run local server
python3 -m http.server 8080 --directory build-wasm
# Open http://localhost:8080/vulkanGLFW.html
```

#### Success Criteria

- ✅ WASM 빌드 성공
- ✅ 브라우저에서 Mini-Engine 실행
- ✅ FDF/OBJ 렌더링 동작
- ✅ 60 FPS 유지 (성능 검증)

---

### Phase 8: Testing and Validation (Week 11)

**Objective**: 품질 보증 및 벤치마킹

#### 8.1 Functional Testing

| Test Case | Vulkan | WebGPU Native | WebGPU WASM |
|-----------|--------|---------------|-------------|
| Triangle Rendering | ✅ | 🔲 | 🔲 |
| FDF Wireframe | ✅ | 🔲 | 🔲 |
| OBJ Model + Texture | ✅ | 🔲 | 🔲 |
| Camera Controls | ✅ | 🔲 | 🔲 |
| ImGui UI | ✅ | 🔲 | 🔲 |
| Window Resize | ✅ | 🔲 | 🔲 |

#### 8.2 Performance Benchmarking

```cpp
// Benchmark metrics
- Frame Time (ms)
- Draw Calls per Frame
- Vertex Count
- Memory Usage (MB)
- Shader Compilation Time (ms)
```

**Target**: WebGPU 성능 >= Vulkan의 90%

#### 8.3 Cross-Platform Testing

| Platform | Native WebGPU | WASM |
|----------|---------------|------|
| Chrome (Linux) | 🔲 | 🔲 |
| Chrome (Windows) | 🔲 | 🔲 |
| Chrome (macOS) | 🔲 | 🔲 |
| Firefox | 🔲 | 🔲 |
| Safari | 🔲 | 🔲 |

#### Success Criteria

- ✅ 모든 기능 테스트 통과
- ✅ 성능 목표 달성
- ✅ 5개 브라우저 호환성 확인

---

### Phase 9: Documentation and Polish (Week 12)

**Objective**: 문서화 및 최종 정리

#### 9.1 Documentation

- [ ] WebGPU Backend API 문서
- [ ] 빌드 가이드 (Native + WASM)
- [ ] 셰이더 변환 가이드
- [ ] 트러블슈팅 가이드

#### 9.2 Code Quality

- [ ] 코드 리뷰 및 리팩토링
- [ ] 성능 프로파일링
- [ ] 메모리 누수 검증
- [ ] 주석 및 문서화

#### 9.3 Demo Page

```text
demo/
├── index.html          # 메인 데모 페이지
├── fdf-wireframe.html  # FDF 렌더링
├── obj-model.html      # OBJ 모델
└── assets/             # 텍스처, 모델
```

#### Success Criteria

- ✅ README에 WebGPU 섹션 추가
- ✅ 온라인 데모 배포 (GitHub Pages)
- ✅ 코드 품질 검증 완료

---

## Technical Specifications

### WebGPU Library Selection

**Recommended: Dawn (Google)**

| Criteria | Dawn | wgpu-native |
|----------|------|-------------|
| Language | C++ | Rust (C bindings) |
| Maturity | Production (Chrome) | Stable |
| API Style | C++ Objects | C API |
| Platform Support | All | All |
| Documentation | Excellent | Good |

**Decision**: Dawn (C++ 친화적, Mini-Engine과 일관성)

### Shader Strategy

**Dual Shader Approach**

```text
Backend      | Shader Language | Build Process
-------------|-----------------|---------------------------
Vulkan       | SPIR-V          | Slang → SPIR-V
WebGPU       | WGSL            | Slang → SPIR-V → WGSL
                               (or direct WGSL authoring)
```

### Synchronization Model

**Vulkan (Explicit)**

```cpp
// Vulkan: 명시적 Fence/Semaphore
fence->wait();
queue->submit(commandBuffer, fence);
```

**WebGPU (Simplified)**

```cpp
// WebGPU: 자동 동기화 + 콜백
queue.OnSubmittedWorkDone([](WGPUQueueWorkDoneStatus status) {
    // GPU 작업 완료
});
```

**Mapping to RHI**

```cpp
// RHISync::waitForFence() 구현
void WebGPURHIFence::wait() {
    // WebGPU는 자동 동기화
    // 필요 시 Queue::OnSubmittedWorkDone() 사용
}
```

---

## API Mapping Strategy

### Device Initialization

#### Vulkan

```cpp
VkInstance instance;
vkCreateInstance(&instanceInfo, nullptr, &instance);

VkPhysicalDevice physicalDevice;
vkEnumeratePhysicalDevices(instance, &count, &physicalDevice);

VkDevice device;
vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
```

#### WebGPU

```cpp
wgpu::Instance instance = wgpu::CreateInstance();

wgpu::Adapter adapter;
instance.RequestAdapter(&adapterOptions, [&](wgpu::Adapter a) {
    adapter = a;
});

wgpu::Device device;
adapter.RequestDevice(&deviceDescriptor, [&](wgpu::Device d) {
    device = d;
});
```

### Buffer Creation

#### Vulkan (via RHI)

```cpp
auto bufferDesc = RHIBufferDesc{
    .size = 1024,
    .usage = RHIBufferUsage::Vertex | RHIBufferUsage::TransferDst
};
auto buffer = device->createBuffer(bufferDesc);
```

#### WebGPU (Implementation)

```cpp
// WebGPURHIDevice::createBuffer()
wgpu::BufferDescriptor desc{
    .size = bufferDesc.size,
    .usage = convertUsage(bufferDesc.usage),  // RHI → WebGPU
    .mappedAtCreation = false
};
wgpu::Buffer buffer = device.CreateBuffer(&desc);
return std::make_unique<WebGPURHIBuffer>(buffer);
```

### Command Recording

#### Vulkan

```cpp
vkBeginCommandBuffer(cmdBuffer, &beginInfo);
vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
vkEndCommandBuffer(cmdBuffer);
```

#### WebGPU

```cpp
wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDescriptor);
pass.SetPipeline(pipeline);
pass.Draw(3, 1, 0, 0);
pass.End();
wgpu::CommandBuffer commands = encoder.Finish();
```

---

## Build System Integration

### CMake Structure

```cmake
# CMakeLists.txt (Root)
option(BUILD_WEBGPU_BACKEND "Build WebGPU backend" ON)
option(BUILD_WASM "Build for WebAssembly" OFF)

if(BUILD_WEBGPU_BACKEND)
    add_subdirectory(src/rhi-webgpu)

    if(BUILD_WASM)
        # Emscripten-specific settings
        set(CMAKE_EXECUTABLE_SUFFIX ".html")
    else()
        # Native WebGPU (Dawn/wgpu-native)
        find_package(Dawn REQUIRED)
    endif()
endif()
```

### Build Targets

```bash
# Native Vulkan (default)
make

# Native WebGPU
cmake -B build-webgpu -DBUILD_WEBGPU_BACKEND=ON
make -C build-webgpu

# WebAssembly
emcmake cmake -B build-wasm -DBUILD_WASM=ON
emmake make -C build-wasm
```

---

## Testing and Validation

### Unit Tests

```cpp
// tests/webgpu/test_buffer.cpp
TEST_CASE("WebGPU Buffer Creation") {
    auto device = createWebGPUDevice();
    auto buffer = device->createBuffer({
        .size = 1024,
        .usage = RHIBufferUsage::Vertex
    });

    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->getSize() == 1024);
}
```

### Integration Tests

```cpp
// tests/webgpu/test_rendering.cpp
TEST_CASE("WebGPU Triangle Rendering") {
    auto renderer = createRenderer(RHIBackend::WebGPU);
    renderer->drawTriangle();

    auto framebuffer = renderer->captureFrame();
    REQUIRE(framebuffer.hasPixels());
}
```

### Visual Regression Testing

```bash
# Capture screenshots
./build/vulkanGLFW --backend vulkan --screenshot vulkan.png
./build/vulkanGLFW --backend webgpu --screenshot webgpu.png

# Compare images (ImageMagick)
compare vulkan.png webgpu.png diff.png
```

---

## References

### Official Documentation

- [WebGPU Specification](https://www.w3.org/TR/webgpu/)
- [WebGPU Shading Language (WGSL)](https://www.w3.org/TR/WGSL/)
- [Dawn Documentation](https://dawn.googlesource.com/dawn)
- [Emscripten WebGPU Guide](https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU.html)

### Learning Resources

- [Learn WebGPU (C++)](https://eliemichel.github.io/LearnWebGPU/)
- [WebGPU Fundamentals](https://webgpufundamentals.org/)
- [SPIR-V Cross](https://github.com/KhronosGroup/SPIRV-Cross)

### Example Projects

- [Dawn Samples](https://dawn.googlesource.com/dawn/+/refs/heads/main/examples/)
- [WebGPU Samples](https://webgpu.github.io/webgpu-samples/)
- [wgpu-native Examples](https://github.com/gfx-rs/wgpu-native/tree/master/examples)

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| WASM 성능 저하 | High | 프로파일링, 최적화, SIMD 활용 |
| 브라우저 호환성 | Medium | Feature detection, Fallback |
| 셰이더 변환 오류 | Medium | SPIR-V Cross 검증, 수동 WGSL |
| Dawn/wgpu-native 버그 | Low | 최신 버전 사용, 버그 리포트 |

---

## Timeline Summary

| Phase | Duration | Milestone |
|-------|----------|-----------|
| 1. Environment Setup | 1 week | Hello Triangle (Native + WASM) |
| 2. RHI Implementation | 2 weeks | All 15 RHI classes |
| 3. Shader Pipeline | 1 week | SPIR-V → WGSL conversion |
| 4. Rendering Integration | 2 weeks | Renderer + WebGPU |
| 5. Resource Management | 1 week | Buffers, Textures |
| 6. ImGui Integration | 1 week | UI rendering |
| 7. WebAssembly Build | 2 weeks | WASM deployment |
| 8. Testing & Validation | 1 week | QA, Benchmarking |
| 9. Documentation | 1 week | Docs, Demo |
| **Total** | **12 weeks** | **WebGPU Backend Complete** |

---

## Success Metrics

### Functional Completeness

- ✅ 15개 RHI 인터페이스 100% 구현
- ✅ FDF + OBJ 렌더링 동작
- ✅ ImGui UI 동작
- ✅ 5개 브라우저 지원

### Performance

- Native WebGPU: Vulkan의 95% 이상
- WASM: Vulkan의 70% 이상
- 60 FPS 유지 (1080p)

### Code Quality

- Zero Vulkan validation errors (유사 검증)
- Zero memory leaks
- 80% 이상 코드 커버리지 (테스트)

---

## Next Steps

1. **Immediate**: Phase 1 시작 (Dawn 설치)
2. **Week 1**: Hello Triangle 완료
3. **Week 2-3**: RHI 구현 시작
4. **월간 리뷰**: 진행 상황 점검 및 문서 업데이트

---

**Author**: Mini-Engine Team
**Last Updated**: 2025-12-26
**Version**: 1.0
