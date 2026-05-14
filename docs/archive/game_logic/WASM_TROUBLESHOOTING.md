# WebGPU/WASM Troubleshooting Guide

이 문서는 Mini-Engine의 WebGPU/WASM 빌드 과정에서 발생한 문제들과 해결 방법을 기록합니다.

## 목차
- [1. 셰이더 파일 로딩 실패](#1-셰이더-파일-로딩-실패)
- [2. wgpuSwapChainPresent 지원 안됨](#2-wgpuswapchainpresent-지원-안됨)
- [3. depthSlice 검증 오류](#3-depthslice-검증-오류)
- [4. 텍스처 포맷 불일치](#4-텍스처-포맷-불일치)
- [5. 낮은 해상도 렌더링](#5-낮은-해상도-렌더링)
- [6. WebGPU 디바이스 초기화 문제](#6-webgpu-디바이스-초기화-문제)
- [7. wasm-ld "section too large" 링커 오류](#7-wasm-ld-section-too-large-링커-오류)
- [8. asyncifyStubs is not defined 런타임 오류](#8-asyncifystubs-is-not-defined-런타임-오류)
- [9. undefined symbol: RHIFactory 링커 경고 (macOS)](#9-undefined-symbol-rhifactory-링커-경고-macos)
- [10. Emscripten 버전별 WebGPU API 호환성](#10-emscripten-버전별-webgpu-api-호환성)

---

## 1. 셰이더 파일 로딩 실패

### 증상
```
[stderr] Aborted(Assertion failed: Exception thrown, but exception catching is not enabled...)
at instancing_test.wasm.examples::loadSPIRV(char const*)
```

### 원인
WGSL 셰이더 파일이 WASM 가상 파일시스템에 임베드되지 않음

### 해결 방법
CMakeLists.txt에 `--preload-file` 옵션 추가:

```cmake
target_link_options(instancing_test PRIVATE
    "SHELL:--preload-file ${CMAKE_CURRENT_BINARY_DIR}/shaders@/shaders"
)
```

빌드 전 셰이더 파일 복사:
```bash
mkdir -p build_wasm/shaders
cp shaders/instancing_test.*.wgsl build_wasm/shaders/
```

**관련 파일**:
- [CMakeLists.txt:406](../CMakeLists.txt)

---

## 2. wgpuSwapChainPresent 지원 안됨

### 증상
```
Aborted(wgpuSwapChainPresent is unsupported (use requestAnimationFrame via html5.h instead))
```

### 원인
Emscripten의 WebGPU는 명시적인 `wgpuSwapChainPresent()` 호출을 지원하지 않음. 브라우저의 `requestAnimationFrame`을 통해 자동으로 프레젠트됨.

### 해결 방법

#### 1) 메인 루프를 `emscripten_set_main_loop`으로 변경

**tests/instancing_test_main.cpp**:
```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

void run() {
#ifdef __EMSCRIPTEN__
    // WebGPU: Use emscripten_set_main_loop for browser's requestAnimationFrame
    emscripten_set_main_loop_arg(
        [](void* arg) {
            auto* demo = static_cast<InstancingDemo*>(arg);
            demo->mainLoop();
        },
        this,
        0,  // Use browser's requestAnimationFrame (typically 60 FPS)
        1   // Simulate infinite loop
    );
#else
    // Native: Traditional game loop
    while (!glfwWindowShouldClose(m_window)) {
        mainLoop();
    }
#endif
}
```

#### 2) `swapchain->present()` 호출 조건부 컴파일

```cpp
#ifndef __EMSCRIPTEN__
    // Native Vulkan: Explicit present call
    // WebGPU: Present is automatic via emscripten_set_main_loop / requestAnimationFrame
    swapchain->present(renderFinishedSemaphore);
#endif
```

**관련 파일**:
- [tests/instancing_test_main.cpp:199-225](../tests/instancing_test_main.cpp)
- [tests/instancing_test_main.cpp:295-299](../tests/instancing_test_main.cpp)

---

## 3. depthSlice 검증 오류

### 증상
```
[WebGPU Error] Validation: depthSlice (0) is defined for a non-3D attachment
```

### 원인
WebGPU의 `WGPURenderPassColorAttachment` 구조체에서 `depthSlice` 필드가 초기화되지 않아 잘못된 값이 설정됨. 2D 텍스처(스왑체인)에는 `depthSlice`를 `WGPU_DEPTH_SLICE_UNDEFINED`로 설정해야 함.

### 해결 방법

**src/rhi-webgpu/src/WebGPURHICommandEncoder.cpp**:
```cpp
WGPURenderPassColorAttachment wgpuAttachment{};
wgpuAttachment.view = webgpuView->getWGPUTextureView();
// ... other fields ...

// Depth slice - must be WGPU_DEPTH_SLICE_UNDEFINED for non-3D textures
wgpuAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
```

**관련 파일**:
- [src/rhi-webgpu/src/WebGPURHICommandEncoder.cpp:239-240](../src/rhi-webgpu/src/WebGPURHICommandEncoder.cpp)

---

## 4. 텍스처 포맷 불일치

### 증상
```
[WebGPU Error] Validation: Attachment state of [RenderPipeline] is not compatible with [RenderPassEncoder].
[RenderPassEncoder] expects: BGRA8Unorm
[RenderPipeline] has: BGRA8UnormSrgb
```

그리고:
```
Uncaught TypeError: Failed to execute 'configure' on 'GPUCanvasContext':
Unsupported canvas context format 'bgra8unorm-srgb'.
```

### 원인
WebGPU는 스왑체인 포맷으로 `BGRA8UnormSrgb`를 지원하지 않음. `BGRA8Unorm` 또는 `RGBA8Unorm`만 지원.

### 해결 방법

#### 1) 스왑체인 포맷을 백엔드별로 분기

**src/rendering/RendererBridge.cpp**:
```cpp
void RendererBridge::createSwapchain(uint32_t width, uint32_t height, bool vsync) {
    rhi::SwapchainDesc desc;
    desc.width = width;
    desc.height = height;

    // WebGPU only supports BGRA8Unorm (not SRGB variant)
    // Vulkan can use either, but we'll use SRGB for better color accuracy on Vulkan
    auto backendType = m_device->getBackendType();
    if (backendType == rhi::RHIBackendType::WebGPU) {
        desc.format = rhi::TextureFormat::BGRA8Unorm;
    } else {
        desc.format = rhi::TextureFormat::BGRA8UnormSrgb;
    }

    // ... rest of the code
}
```

#### 2) 파이프라인 포맷도 백엔드별로 분기

**src/examples/InstancingTest.cpp**:
```cpp
// Color target - must match swapchain format
// WebGPU only supports BGRA8Unorm, Vulkan uses BGRA8UnormSrgb
auto backendType = m_device->getBackendType();
rhi::TextureFormat colorFormat = (backendType == rhi::RHIBackendType::WebGPU)
    ? rhi::TextureFormat::BGRA8Unorm
    : rhi::TextureFormat::BGRA8UnormSrgb;

pipelineDesc.colorTargets = {
    rhi::ColorTargetState{colorFormat}
};
```

**관련 파일**:
- [src/rendering/RendererBridge.cpp:94-101](../src/rendering/RendererBridge.cpp)
- [src/examples/InstancingTest.cpp:292-301](../src/examples/InstancingTest.cpp)

---

## 5. 낮은 해상도 렌더링

### 증상
WebGPU 버전이 네이티브에 비해 해상도가 낮고 흐릿하게 보임 (특히 Retina 디스플레이)

### 원인
캔버스의 실제 픽셀 크기가 CSS 표시 크기와 동일하게 설정되어 `devicePixelRatio`가 고려되지 않음

### 해결 방법

**tests/wasm_shell.html**:
```html
<canvas id="canvas"></canvas>

<script>
var canvas = document.getElementById('canvas');

// Set canvas size with device pixel ratio for high DPI displays
var displayWidth = 1280;
var displayHeight = 720;
var dpr = window.devicePixelRatio || 1;

// Set CSS size (what the user sees)
canvas.style.width = displayWidth + 'px';
canvas.style.height = displayHeight + 'px';

// Set actual canvas buffer size (accounting for device pixel ratio)
canvas.width = displayWidth * dpr;
canvas.height = displayHeight * dpr;
</script>
```

**효과**:
- Retina 디스플레이 (dpr=2): 실제 렌더링 2560x1440, 표시 1280x720
- 일반 디스플레이 (dpr=1): 실제 렌더링 1280x720, 표시 1280x720

**관련 파일**:
- [tests/wasm_shell.html:121-132](../tests/wasm_shell.html)

---

## 6. WebGPU 디바이스 초기화 문제

### 6.1. Instance 생성 실패

#### 증상
```
Aborted(Assertion failed: descriptor == nullptr,
at: ../../../system/lib/webgpu/webgpu.cpp,24,wgpuCreateInstance)
```

#### 원인
Emscripten WebGPU는 `wgpuCreateInstance`에 `nullptr`를 전달해야 함 (네이티브 WebGPU와 다름)

#### 해결 방법
**src/rhi-webgpu/src/WebGPURHIDevice.cpp**:
```cpp
#ifdef __EMSCRIPTEN__
    // Emscripten WebGPU requires nullptr descriptor
    m_instance = wgpuCreateInstance(nullptr);
#else
    // Native WebGPU (Dawn) can use descriptor
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;
    m_instance = wgpuCreateInstance(&desc);
#endif
```

### 6.2. Device Limits 쿼리 실패

#### 증상
```
Aborted(Assertion failed: attempt to write non-integer (undefined)
into integer heap) at _wgpuDeviceGetLimits
```

#### 원인
`wgpuDeviceGetLimits`가 Emscripten에서 undefined 값을 반환함

#### 해결 방법
**src/rhi-webgpu/src/WebGPURHICapabilities.cpp**:
```cpp
void WebGPURHICapabilities::queryLimits(WGPUDevice device) {
#ifdef __EMSCRIPTEN__
    // Emscripten: Use WebGPU guaranteed minimum limits
    m_limits.maxTextureDimension1D = 8192;
    m_limits.maxTextureDimension2D = 8192;
    m_limits.maxTextureDimension3D = 2048;
    // ... (all guaranteed WebGPU limits)
#else
    // Native: Query actual device limits
    WGPUSupportedLimits supportedLimits{};
    wgpuDeviceGetLimits(device, &supportedLimits);
    const WGPULimits& limits = supportedLimits.limits;
    m_limits.maxTextureDimension1D = limits.maxTextureDimension1D;
    // ... (copy all limits from device)
#endif
}
```

**관련 파일**:
- [src/rhi-webgpu/src/WebGPURHIDevice.cpp:137-145](../src/rhi-webgpu/src/WebGPURHIDevice.cpp)
- [src/rhi-webgpu/src/WebGPURHICapabilities.cpp:17-87](../src/rhi-webgpu/src/WebGPURHICapabilities.cpp)

---

## 빌드 및 테스트 절차

### WASM 빌드
```bash
cd /Users/mindaewon/projects/Mini-Engine
make build-wasm
```

### 셰이더 복사 (자동화 전)
```bash
mkdir -p build_wasm/shaders
cp shaders/instancing_test.*.wgsl build_wasm/shaders/
```

### 서버 실행
```bash
make serve-instancing
```

### 브라우저 테스트
http://localhost:8000/instancing_test.html

---

## 주요 차이점: Native vs WebGPU

| 항목 | Native (Vulkan) | WebGPU (Emscripten) |
|------|----------------|---------------------|
| **메인 루프** | `while` 동기식 루프 | `emscripten_set_main_loop` (requestAnimationFrame) |
| **Present** | `swapchain->present()` 명시 호출 | 자동 (호출 불필요) |
| **스왑체인 포맷** | `BGRA8UnormSrgb` 지원 | `BGRA8Unorm`만 지원 |
| **Instance 생성** | Descriptor 사용 | `nullptr` 전달 |
| **Device Limits** | `wgpuDeviceGetLimits` 쿼리 | 하드코딩된 최소 보장값 사용 |
| **DepthSlice** | 자동 처리 | `WGPU_DEPTH_SLICE_UNDEFINED` 명시 필요 |
| **셰이더 파일** | 파일시스템 직접 접근 | `--preload-file`로 임베드 필요 |

---

## 참고 자료

- [WebGPU Specification](https://www.w3.org/TR/webgpu/)
- [Emscripten Documentation](https://emscripten.org/docs/)
- [WebGPU Guaranteed Limits](https://www.w3.org/TR/webgpu/#limits)
- [Emscripten Main Loop](https://emscripten.org/docs/porting/emscripten-runtime-environment.html#browser-main-loop)

---

## 성공 로그 예시

```
[stdout] === GPU Instancing Test ===
[stdout] Window created: 1280x720
[stdout] [WebGPU] Initializing WebGPU RHI Device
[stdout] [WebGPU] Device initialized successfully
[stdout] Swapchain created: 2560x1440  (Retina 디스플레이)
[stdout] [InstancingTest] Pipeline created successfully!
[stdout] [InstancingTest] Initialization complete! Ready to render 1000 cubes.
[stdout] FPS: 60.0 (1000 instances, 1 draw call)
```

---

---

## 7. wasm-ld "section too large" 링커 오류

### 증상
```
wasm-ld: error: tiny_obj_loader.cc.obj: section too large
wasm-ld: error: RHIFactory.cpp.obj: section too large
```

### 원인
Emscripten 3.1.60 이상의 `wasm-ld` 링커에 macOS arm64(Apple Silicon) 환경에서 발생하는 버그.
Linux x86_64에서는 같은 버전에서도 문제가 없으며, **동일한 코드와 커밋이 Linux에서는 빌드되고 macOS에서는 실패하는** 플랫폼 고유 문제.

### 해결 방법

**Emscripten 3.1.50으로 다운그레이드:**
```bash
cd ~/emsdk
./emsdk install 3.1.50
./emsdk activate 3.1.50
source ./emsdk_env.sh
```

**추가 최적화 (CMakeLists.txt에 이미 적용됨):**
```cmake
# 전역 WASM 최적화 플래그 (section too large 방지)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_RELEASE "-Oz -g0 -DNDEBUG -ffunction-sections -fdata-sections")

# rhi_factory 타겟별 최적화
if(EMSCRIPTEN)
    target_compile_options(rhi_factory PRIVATE -Oz -g0 -ffunction-sections -fdata-sections)
endif()
```

### 버전 호환성 표

| Emscripten 버전 | macOS arm64 | Linux x86_64 | 비고 |
|----------------|-------------|--------------|------|
| 3.1.50 | ✅ 정상 | ✅ 정상 | **권장 버전** |
| 3.1.60 | ❌ section too large | ✅ 정상 | wasm-ld 버그 |
| 3.1.74 | ❌ section too large | ✅ 정상 | wasm-ld 버그 |
| 5.0.0+ | ❌ USE_WEBGPU deprecated | ❌ USE_WEBGPU deprecated | API 변경 |

**관련 파일**:
- [CMakeLists.txt:26-31](../CMakeLists.txt)
- [src/rhi/CMakeLists.txt:65-67](../src/rhi/CMakeLists.txt)

---

## 8. asyncifyStubs is not defined 런타임 오류

### 증상
브라우저 콘솔에서:
```
ReferenceError: asyncifyStubs is not defined
```

### 원인
ASYNCIFY가 올바르게 설정되지 않았거나 `ASYNCIFY_STACK_SIZE`가 누락됨.
Emscripten의 WebGPU 비동기 작업(어댑터 요청, 디바이스 요청 등)에 ASYNCIFY가 필요.

### 해결 방법

**CMakeLists.txt 링크 옵션에 추가:**
```cmake
target_link_options(${TARGET} PRIVATE
    "SHELL:-s ASYNCIFY=1"
    "SHELL:-s ASYNCIFY_STACK_SIZE=16384"
)
```

> **주의**: `-sASYNCIFY`는 **링커 전용** 플래그. `target_compile_options`에 추가하면 안 됨.

**관련 파일**:
- [CMakeLists.txt:430-431](../CMakeLists.txt) (MiniEngine)
- [CMakeLists.txt:558-559](../CMakeLists.txt) (instancing_test)

---

## 9. undefined symbol: RHIFactory 링커 경고 (macOS)

### 증상
```
wasm-ld: warning: undefined symbol: rhi::RHIFactory::createDevice(...)
wasm-ld: warning: undefined symbol: rhi::RHIFactory::getDefaultBackend()
```

### 원인
macOS에서 기본 시스템 `ar`/`ranlib`이 WASM 아카이브(.a) 파일의 인덱스를 올바르게 생성하지 못함.
이로 인해 `wasm-ld`가 아카이브 내 심볼을 찾지 못함.

### 해결 방법

**cmake/EmscriptenToolchain.cmake에 emar/emranlib 설정:**
```cmake
set(CMAKE_AR emar CACHE FILEPATH "Emscripten ar")
set(CMAKE_RANLIB emranlib CACHE FILEPATH "Emscripten ranlib")
```

이 설정으로 Emscripten 전용 아카이브 도구가 사용되어 WASM 호환 아카이브 인덱스가 생성됨.

**관련 파일**:
- [cmake/EmscriptenToolchain.cmake:6-7](../cmake/EmscriptenToolchain.cmake)

---

## 10. Emscripten 버전별 WebGPU API 호환성

### 증상
Emscripten 3.1.50으로 다운그레이드 후 다양한 컴파일 오류 발생:
```
error: no member named 'requiredFeatureCount' in 'WGPUDeviceDescriptor'
error: no matching function for call to 'wgpuQueueOnSubmittedWorkDone'
error: no member named 'depthSlice' in 'WGPURenderPassColorAttachment'
error: use of undeclared identifier 'WGPUStorageTextureAccess_ReadOnly'
```

### 원인
WebGPU API가 Emscripten 3.1.50과 3.1.60 사이에서 변경됨:

| API 변경 사항 | 3.1.50 (이전) | 3.1.60+ (이후) |
|-------------|-------------|--------------|
| Device features count | `requiredFeaturesCount` | `requiredFeatureCount` |
| Queue work done 콜백 | 4인자 (signalValue 포함) | 3인자 |
| Render pass depthSlice | 필드 없음 | `depthSlice` 필드 추가 |
| Storage texture access | `WriteOnly`만 지원 | `ReadOnly` 추가 |

### 해결 방법

`WebGPUCommon.hpp`에 버전 비교 매크로를 정의하고 조건부 컴파일 사용:

```cpp
// WebGPUCommon.hpp에 정의
#ifdef __EMSCRIPTEN__
#define EMSCRIPTEN_VERSION_LESS_THAN(major, minor, tiny) \
    ((__EMSCRIPTEN_major__ < (major)) || \
     ((__EMSCRIPTEN_major__ == (major)) && (__EMSCRIPTEN_minor__ < (minor))) || \
     ((__EMSCRIPTEN_major__ == (major)) && (__EMSCRIPTEN_minor__ == (minor)) && (__EMSCRIPTEN_tiny__ < (tiny))))
#define EMSCRIPTEN_VERSION_AT_LEAST(major, minor, tiny) \
    (!EMSCRIPTEN_VERSION_LESS_THAN(major, minor, tiny))
#endif
```

사용 예:
```cpp
#if defined(__EMSCRIPTEN__) && EMSCRIPTEN_VERSION_LESS_THAN(3, 1, 60)
    deviceDesc.requiredFeaturesCount = 0;  // 이전 API
#else
    deviceDesc.requiredFeatureCount = 0;   // 새 API
#endif
```

**관련 파일**:
- [src/rhi/backends/webgpu/include/rhi/webgpu/WebGPUCommon.hpp](../src/rhi/backends/webgpu/include/rhi/webgpu/WebGPUCommon.hpp)
- [src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp:232](../src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp)
- [src/rhi/backends/webgpu/src/WebGPURHISync.cpp:53](../src/rhi/backends/webgpu/src/WebGPURHISync.cpp)
- [src/rhi/backends/webgpu/src/WebGPURHICommandEncoder.cpp:241](../src/rhi/backends/webgpu/src/WebGPURHICommandEncoder.cpp)
- [src/rhi/backends/webgpu/src/WebGPURHIBindGroup.cpp:68](../src/rhi/backends/webgpu/src/WebGPURHIBindGroup.cpp)

---

## 주요 차이점: Native vs WebGPU (업데이트)

| 항목 | Native (Vulkan) | WebGPU (Emscripten 3.1.50) |
|------|----------------|---------------------------|
| **메인 루프** | `while` 동기식 루프 | `emscripten_set_main_loop` (requestAnimationFrame) |
| **Present** | `swapchain->present()` 명시 호출 | 자동 (호출 불필요) |
| **스왑체인 포맷** | `BGRA8UnormSrgb` 지원 | `BGRA8Unorm`만 지원 |
| **Instance 생성** | Descriptor 사용 | `nullptr` 전달 |
| **Device Limits** | `wgpuDeviceGetLimits` 쿼리 | 하드코딩된 최소 보장값 사용 |
| **DepthSlice** | 자동 처리 | 필드 없음 (3.1.50), `WGPU_DEPTH_SLICE_UNDEFINED` (3.1.60+) |
| **셰이더 파일** | 파일시스템 직접 접근 | `--preload-file`로 임베드 필요 |
| **비동기 처리** | 동기식 또는 Dawn 폴링 | ASYNCIFY 필수 (`-s ASYNCIFY=1`) |
| **아카이브 도구** | 시스템 `ar` | `emar`/`emranlib` 필수 (macOS) |

**최종 업데이트**: 2026-02-03
**테스트 환경**: macOS (Apple Silicon), Chrome 131+, Emscripten 3.1.50
