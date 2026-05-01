# 변경 이력 — 2026-05-01

> 작업 범위: Windows WASM 빌드 인프라 구축 · emdawnwebgpu remote port 전환 · WebGPU 백엔드 API 호환성 레이어 정리 · Slope-scaled Shadow Bias · OBJLoader C++17 호환성 수정

---

## 1. 개요

두 가지 핵심 축으로 진행된 업데이트:

1. **Windows 환경에서 WASM 빌드 가능** — emsdk + Ninja 기반의 WASM 빌드 체인을 Windows에서 동작하도록 완성. Emscripten toolchain의 `.bat` wrapper 처리, `cmake/Platform/` 파일 추가, `wasm-windows` CMake preset, PowerShell 빌드 스크립트, 그리고 bundled emdawnwebgpu remote port 도입.

2. **WebGPU 백엔드 API 정리** — emdawnwebgpu의 업데이트된 callback 시그니처·타입 별칭·구조체명을 반영하여 `WebGPUCommon.hpp` 호환성 레이어를 재정리. `WGPU_BOOL()` 매크로 통합으로 `#ifdef __EMSCRIPTEN__` 분기 제거.

추가로 그림자 peter-panning 버그를 slope-scaled bias로 수정하고, C++17 미지원 `.contains()` 호출을 수정.

---

## 2. Windows WASM 빌드 인프라

### 2.1 신규 파일

| 파일 | 설명 |
|---|---|
| `cmake/Platform/Emscripten.cmake` | CMake Platform 파일 — `target_compile_features`가 컴파일러 기능 목록 조회에 실패하지 않도록 Emscripten 컴파일러 식별 정보 제공 |
| `scripts/wasm.ps1` | Windows PowerShell WASM 빌드 스크립트 (`emcmake cmake` → `ninja`) |
| `third_party/emdawnwebgpu/` | Bundled emdawnwebgpu remote port (`emdawnwebgpu.remoteport.py`) — 네트워크 없이 재현 가능한 빌드 보장 |

### 2.2 CMakePresets.json — `wasm-windows` preset 추가

```json
{
  "name": "wasm-windows",
  "displayName": "WebAssembly (Windows + Emscripten)",
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/build_wasm",
  "toolchainFile": "${sourceDir}/cmake/EmscriptenToolchain.cmake",
  "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
}
```

기존 `linux-default`와 동일한 구조로 Windows에서 `cmake --preset wasm-windows` 호출 가능.

### 2.3 EmscriptenToolchain.cmake 개선

**파일:** `cmake/EmscriptenToolchain.cmake`

| 항목 | 이전 | 수정 후 |
|---|---|---|
| `CMAKE_AR` / `CMAKE_RANLIB` | 플랫폼 무관 `emar` / `emranlib` | Windows: `$EMSDK/upstream/emscripten/emar.bat` 절대 경로 |
| `EMSCRIPTEN` 설정 | `set(EMSCRIPTEN 1)` | `set(EMSCRIPTEN ON)` (CMake bool 타입 일치) |
| `TRY_COMPILE` | 없음 | `set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)` — 크로스 컴파일 환경에서 실행 불가 바이너리 생성 방지 |
| Module path | 없음 | `list(APPEND CMAKE_MODULE_PATH ...)` — `Platform/Emscripten.cmake` 탐색 경로 등록 |

Windows에서 CMake는 `emar.bat` 같은 `.bat` wrapper를 `CreateProcess`로 직접 실행할 수 없으나, 절대 경로로 지정하면 `cmd /c`를 자동으로 삽입하여 처리함.

### 2.4 emdawnwebgpu Remote Port 경로 통일

**파일:** `CMakeLists.txt`, `src/rhi/CMakeLists.txt`, `src/rhi/backends/webgpu/CMakeLists.txt`

```cmake
# Before: 하드코딩된 포트명
--use-port=emdawnwebgpu

# After: bundled 파일 경로로 통일
set(EMDAWNWEBGPU_PORT "${CMAKE_SOURCE_DIR}/third_party/emdawnwebgpu/emdawnwebgpu.remoteport.py")
--use-port=${EMDAWNWEBGPU_PORT}
```

모든 타깃(`MiniEngine`, `rhi_factory`, `rhi_webgpu`, test targets)에 동일하게 적용.

### 2.5 showcase_demo WASM 빌드 제외

```cmake
# Before: EMSCRIPTEN 분기로 일부 동작
# After: WASM 빌드에서 showcase_demo 타겟 전체 제외
if(NOT EMSCRIPTEN)
    add_executable(showcase_demo ...)
    ...
endif() # NOT EMSCRIPTEN (showcase_demo)
```

showcase_demo는 Vulkan/ImGui 의존성이 있어 WASM에서 빌드 불가. 불필요한 `if(NOT EMSCRIPTEN)` 내부 분기 제거.

### 2.6 CMakeLists — `target_compile_features` → `set_property` (Emscripten)

**파일:** `src/rhi/CMakeLists.txt`, `src/rhi/backends/webgpu/CMakeLists.txt`

`emcmake`는 컴파일러 ABI 감지를 건너뛰기 때문에 `target_compile_features(cxx_std_20)`가 실패. Emscripten 빌드에서는 `set_property`로 직접 설정:

```cmake
if(EMSCRIPTEN)
    set_property(TARGET rhi_factory PROPERTY CXX_STANDARD 20)
    set_property(TARGET rhi_factory PROPERTY CXX_STANDARD_REQUIRED ON)
else()
    target_compile_features(rhi_factory PUBLIC cxx_std_20)
endif()
```

---

## 3. emdawnwebgpu API 호환성 레이어 정리

### 3.1 WebGPUCommon.hpp 재구성

**파일:** `src/rhi/backends/webgpu/include/rhi/webgpu/WebGPUCommon.hpp`

기존에 순서 없이 나열되어 있던 typedef/define 목록을 기능별로 그룹화하고 각 항목에 이름 변경 이유를 주석으로 명시:

```
[변경 내용 요약]
Bitfield typedefs   — WGPUBufferUsageFlags 등 'Flags' 접미사 복원
MapAsyncStatus      — WGPUBufferMapAsyncStatus → WGPUMapAsyncStatus alias
ShaderSourceWGSL    — WGPUShaderModuleWGSLDescriptor → WGPUShaderSourceWGSL alias
ImageCopy types     — WGPUTexelCopyBufferInfo/TextureInfo → 구 이름 alias
QueueWorkDone       — WGPUQueueWorkDoneStatus_Unknown → _Error 매핑
Canvas surface      — WGPUEmscriptenSurfaceSourceCanvasHTMLSelector alias
ErrorType/DeviceLost— WGPUErrorType_DeviceLost, WGPUDeviceLostReason_Undefined 매핑
```

`EMSCRIPTEN_VERSION_LESS_THAN` / `EMSCRIPTEN_VERSION_AT_LEAST` 매크로를 별도 섹션으로 분리.

### 3.2 WebGPURHIDevice.cpp — Callback 시그니처 정리

**파일:** `src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp`

emdawnwebgpu callback에서 `(message.data && message.length) ? std::string(...) : "Unknown error"` 패턴을
`message.data ? message.data : "Unknown error"` 로 단순화 (WGPU_STRLEN 방식은 null-termination 보장).

`requestAdapter()` / `requestDevice()` 함수의 동기 대기 루프를 `#ifdef`로 분리하여 가독성 향상:

```cpp
// Synchronous wait for callback
#ifdef __EMSCRIPTEN__
    while (!callbackData.requestEnded) { emscripten_sleep(10); }
#else
    while (!callbackData.requestEnded) { wgpuInstanceProcessEvents(m_instance); }
#endif
```

`requestDevice()`에서 에러 콜백을 분리된 `wgpuDeviceSetUncapturedErrorCallback` 호출 대신
`deviceDesc.uncapturedErrorCallbackInfo.callback`으로 직접 등록 (emdawnwebgpu 권장 방식).

### 3.3 WebGPURHICommandEncoder.cpp — setPushConstants() 스텁 구현

**파일:** `src/rhi/backends/webgpu/src/WebGPURHICommandEncoder.cpp`

WebGPU는 push constants를 지원하지 않음. RHI 인터페이스 구현 완성을 위한 no-op 스텁 추가:

```cpp
void WebGPURHIRenderPassEncoder::setPushConstants(
    rhi::RHIPipelineLayout*, rhi::ShaderStage,
    uint32_t, uint32_t, const void*) {
    // WebGPU has no push constants; callers must use uniform bind groups instead.
}
```

`WebGPURHIComputePassEncoder`에도 동일하게 추가.

### 3.4 WebGPURHIPipeline.cpp — WGPU_BOOL() 매크로 통합

**파일:** `src/rhi/backends/webgpu/src/WebGPURHIPipeline.cpp`

```cpp
// Before: ifdef 분기
#ifdef __EMSCRIPTEN__
    depthStencilState.depthWriteEnabled = desc.depthStencil->depthWriteEnabled
        ? WGPUOptionalBool_True : WGPUOptionalBool_False;
#else
    depthStencilState.depthWriteEnabled = desc.depthStencil->depthWriteEnabled;
#endif

// After: 매크로 통합
depthStencilState.depthWriteEnabled = WGPU_BOOL(desc.depthStencil->depthWriteEnabled);
```

### 3.5 WebGPURHISync.cpp — WGPUStringView message 파라미터 추가

**파일:** `src/rhi/backends/webgpu/src/WebGPURHISync.cpp`

```cpp
// emdawnwebgpu: onQueueWorkDone 시그니처 변경
static void onQueueWorkDone(WGPUQueueWorkDoneStatus status,
                            WGPUStringView /*message*/,   // 신규 파라미터
                            void* userdata1, void*) { ... }
```

초기 status를 `WGPUQueueWorkDoneStatus_Success` → `WGPUQueueWorkDoneStatus_Unknown`으로 변경 (미초기화 상태 구분).

### 3.6 WebGPURHITexture.cpp — TextureAspect 단순화

**파일:** `src/rhi/backends/webgpu/src/WebGPURHITexture.cpp`

깊이 포맷별 `WGPUTextureAspect_DepthOnly` / `WGPUTextureAspect_All` 분기 제거. WebGPU는 포맷으로부터 aspect를 자동 결정하므로 `WGPUTextureAspect_All`로 통일.

---

## 4. 렌더링 버그 수정

### 4.1 Shadow Peter-Panning 수정 — Slope-Scaled Bias

**파일:** `shaders/building.wgsl`, `src/rendering/Renderer.hpp`

**버그:** 기존 상수 bias(`0.008`)는 경사면에서 peter-panning(그림자 부유 현상) 유발.

**수정:** Slope-scaled bias 적용 — 빛과 표면 노멀의 각도(`cosTheta`)에 따라 bias를 동적 조절:

```wgsl
// Before
let bias = ubo.shadowBias * 0.01;

// After
let cosTheta = clamp(dot(normalize(normal), normalize(lightDir)), 0.0, 1.0);
let bias = ubo.shadowBias * mix(2.0, 1.0, cosTheta);
// 경사각 클수록 bias 증가 (최대 2×), 직교면은 1×
```

기본 `shadowBias` 값도 `0.008` → `0.0015`로 조정:

```cpp
// Before
float shadowBias = 0.008f;

// After
float shadowBias = 0.0015f; // Slope-scaled bias (building.wgsl: mix 1×–2×)
```

### 4.2 Shadow Shader ObjectData 구조체 크기 일치

**파일:** `shaders/shadow.wgsl`

C++ `ObjectData` 구조체가 144바이트인데 WGSL 정의에 `texParams` 필드가 누락되어 있어 메모리 레이아웃 불일치 발생. 필드 추가:

```wgsl
struct ObjectData {
    // ... 기존 필드 ...
    roughnessAOPad: vec4<f32>,
    texParams: vec4<f32>,   // C++ ObjectData 144 bytes 일치 (신규)
}
```

### 4.3 OBJLoader C++17 호환성 수정

**파일:** `src/loaders/OBJLoader.cpp`

`std::unordered_map::contains()`는 C++20에서 도입된 메서드로 Emscripten(em++) 빌드에서 컴파일 오류 발생:

```cpp
// Before (C++20)
if (!uniqueVertices.contains(vertex)) { ... }

// After (C++17 호환)
if (uniqueVertices.find(vertex) == uniqueVertices.end()) { ... }
```

---

## 5. 수정된 파일 목록

| 파일 | 변경 내용 |
|---|---|
| `CMakeLists.txt` | EMDAWNWEBGPU_PORT 변수, showcase_demo WASM 제외, 빌드 정리 |
| `CMakePresets.json` | `wasm-windows` preset 추가 |
| `cmake/EmscriptenToolchain.cmake` | Windows emar.bat 경로, TRY_COMPILE STATIC_LIBRARY, Module path |
| `cmake/Platform/Emscripten.cmake` | 신규: CMake Platform 파일 |
| `scripts/wasm.ps1` | 신규: Windows WASM 빌드 스크립트 |
| `third_party/emdawnwebgpu/` | 신규: bundled emdawnwebgpu remote port |
| `src/rhi/CMakeLists.txt` | EMDAWNWEBGPU_PORT, compile_features → set_property |
| `src/rhi/backends/webgpu/CMakeLists.txt` | EMDAWNWEBGPU_PORT, compile_features → set_property |
| `src/rhi/backends/webgpu/include/rhi/webgpu/WebGPUCommon.hpp` | 호환성 레이어 재구성 및 주석 정리 |
| `src/rhi/backends/webgpu/src/WebGPURHICommandEncoder.cpp` | setPushConstants() no-op 스텁 추가 |
| `src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp` | Callback 시그니처 정리, 대기 루프 분리 |
| `src/rhi/backends/webgpu/src/WebGPURHIBuffer.cpp` | 콜백 변수명 통일, status 초기값 수정 |
| `src/rhi/backends/webgpu/src/WebGPURHIPipeline.cpp` | WGPU_BOOL() 매크로로 ifdef 제거 |
| `src/rhi/backends/webgpu/src/WebGPURHISwapchain.cpp` | 코드 정렬, 주석 정리 |
| `src/rhi/backends/webgpu/src/WebGPURHISync.cpp` | WGPUStringView 파라미터, status 초기값 수정 |
| `src/rhi/backends/webgpu/src/WebGPURHITexture.cpp` | TextureAspect_All 단순화 |
| `shaders/building.wgsl` | Slope-scaled shadow bias |
| `shaders/shadow.wgsl` | ObjectData `texParams` 필드 추가 |
| `src/rendering/Renderer.hpp` | shadowBias 0.008 → 0.0015 |
| `src/loaders/OBJLoader.cpp` | `.contains()` → `.find()` (C++17 호환) |
