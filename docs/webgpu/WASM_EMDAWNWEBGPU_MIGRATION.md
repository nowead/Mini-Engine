# WASM 빌드 수정 기록 — emdawnwebgpu 포팅

Emscripten 최신 SDK(emdawnwebgpu 포트)로 `make wasm` 빌드를 성공시키기까지 발생한 모든 오류와 수정 내용을 정리한 문서.

---

## 배경

기존 WASM 빌드는 Emscripten의 구형 WebGPU 포트(`-s USE_WEBGPU=1`)를 사용하고 있었다.
Emscripten 최신 SDK는 **emdawnwebgpu**라는 새로운 포트(`--use-port=emdawnwebgpu`)를 사용하며, API가 대폭 변경되었다. 이 변경을 따라가기 위해 WebGPU 백엔드 전반을 수정했다.

---

## 주요 API 변경 사항 (구형 → emdawnwebgpu)

| 항목 | 구형 | emdawnwebgpu |
|---|---|---|
| 스왑체인 | `WGPUSwapChain` API | `WGPUSurface` API |
| 문자열 타입 | `const char*` | `WGPUStringView` |
| 어댑터 요청 | `wgpuInstanceRequestAdapter(instance, opts, cb, userdata)` | `wgpuInstanceRequestAdapter(instance, opts, callbackInfo)` |
| 디바이스 요청 | `wgpuAdapterRequestDevice(adapter, desc, cb, userdata)` | `wgpuAdapterRequestDevice(adapter, desc, callbackInfo)` |
| 에러 콜백 등록 | `wgpuDeviceSetUncapturedErrorCallback()` | `WGPUDeviceDescriptor.uncapturedErrorCallbackInfo` |
| 비트필드 타입명 | `WGPUBufferUsageFlags` 등 (`Flags` 접미사) | `WGPUBufferUsage` 등 (접미사 제거) |
| 버퍼 맵 상태 | `WGPUBufferMapAsyncStatus` | `WGPUMapAsyncStatus` |
| WGSL 디스크립터 | `WGPUShaderModuleWGSLDescriptor` | `WGPUShaderSourceWGSL` |
| 이미지 복사 구조체 | `WGPUImageCopyBuffer/Texture` | `WGPUTexelCopyBufferInfo/TextureInfo` |
| 깊이쓰기 플래그 | `WGPUBool depthWriteEnabled` | `WGPUOptionalBool depthWriteEnabled` |
| Canvas 셀렉터 | `WGPUSurfaceDescriptorFromCanvasHTMLSelector` | `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` |
| Present | `wgpuSwapChainPresent()` | 자동 (requestAnimationFrame) — 명시적 호출 불가 |

---

## 빌드 오류 수정 내역

### 1. 비트필드 타입명 변경 (`Flags` 접미사 제거)

**오류:** `WGPUBufferUsageFlags`, `WGPUTextureUsageFlags` 등의 타입을 찾을 수 없음

**원인:** emdawnwebgpu에서 `Flags` 접미사가 제거됨

**수정:** `WebGPUCommon.hpp`에 typedef 호환 정의 추가
```cpp
typedef WGPUBufferUsage    WGPUBufferUsageFlags;
typedef WGPUTextureUsage   WGPUTextureUsageFlags;
typedef WGPUShaderStage    WGPUShaderStageFlags;
typedef WGPUColorWriteMask WGPUColorWriteMaskFlags;
typedef WGPUMapMode        WGPUMapModeFlags;
```

---

### 2. 문자열 타입 변경 (`const char*` → `WGPUStringView`)

**오류:** `descriptor.label = "some label"` 타입 불일치

**원인:** 모든 문자열 필드가 `WGPUStringView` 타입으로 변경됨

**수정:** `WebGPUCommon.hpp`에 헬퍼 함수 및 매크로 추가
```cpp
inline WGPUStringView wgpuStr(const char* s) {
    return s ? WGPUStringView{s, WGPU_STRLEN} : WGPUStringView{nullptr, 0};
}
#define WGPU_LABEL(s) wgpuStr(s)
```

---

### 3. `WGPUShaderModuleWGSLDescriptor` 이름 변경

**오류:** `WGPUShaderModuleWGSLDescriptor` 타입 미정의

**수정:** `WebGPUCommon.hpp`에 typedef 추가
```cpp
typedef WGPUShaderSourceWGSL WGPUShaderModuleWGSLDescriptor;
#define WGPUSType_ShaderModuleWGSLDescriptor WGPUSType_ShaderSourceWGSL
```

---

### 4. `WGPUErrorType_DeviceLost` / `WGPUDeviceLostReason_Undefined` 제거

**오류:** 열거형 값 미정의

**수정:** `WebGPUCommon.hpp`에 매크로 정의
```cpp
#define WGPUErrorType_DeviceLost        WGPUErrorType_Unknown
#define WGPUDeviceLostReason_Undefined  WGPUDeviceLostReason_Unknown
```

---

### 5. 어댑터/디바이스 요청 콜백 시그니처 변경

**오류:** `wgpuInstanceRequestAdapter`, `wgpuAdapterRequestDevice` 인자 수 불일치

**원인:** 콜백+userdata 패턴 → `CallbackInfo` 구조체 패턴으로 변경

**수정:** `WebGPURHIDevice.cpp`에서 `#ifdef __EMSCRIPTEN__` 분기 처리
```cpp
#ifdef __EMSCRIPTEN__
static void onAdapterRequestEnded(WGPURequestAdapterStatus status,
                                   WGPUAdapter adapter, WGPUStringView message,
                                   void* userdata1, void*) { ... }
// ...
WGPURequestAdapterCallbackInfo adapterCallbackInfo{};
adapterCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
adapterCallbackInfo.callback = onAdapterRequestEnded;
adapterCallbackInfo.userdata1 = &callbackData;
wgpuInstanceRequestAdapter(m_instance, &options, adapterCallbackInfo);
#else
wgpuInstanceRequestAdapter(m_instance, &options, onAdapterRequestEnded, &callbackData);
#endif
```

---

### 6. `wgpuDeviceSetUncapturedErrorCallback` 제거

**원인:** 에러 콜백을 `WGPUDeviceDescriptor.uncapturedErrorCallbackInfo`로 설정해야 함

**수정:** 디바이스 생성 전 디스크립터에 콜백 주입, 구형 호출은 `#ifndef __EMSCRIPTEN__`으로 감싸기

---

### 7. `rhi_factory` 라이브러리가 `webgpu/webgpu.h` 를 찾지 못함

**오류:** `fatal error: 'webgpu/webgpu.h' file not found`

**원인:** `--use-port=emdawnwebgpu`는 해당 타겟에 직접 지정해야 함. 상위 실행파일에만 지정하면 전파되지 않음

**수정:** `src/rhi/CMakeLists.txt`에 `rhi_factory`용 옵션 추가
```cmake
if(EMSCRIPTEN)
    target_compile_options(rhi_factory PRIVATE
        -Oz -g0 -ffunction-sections -fdata-sections --use-port=emdawnwebgpu)
endif()
```

---

## 런타임 오류 수정 내역

### 8. 예외 캐치 비활성화 오류 (빌드 직후 즉시 크래시)

**오류:** `Aborted(Assertion failed: Exception thrown, but exception catching is not enabled.)`

**원인:** Emscripten 기본 설정에서 C++ 예외 처리가 비활성화됨

**수정:** `CMakeLists.txt` 링커 옵션에 추가 (디버깅용)
```cmake
-s DISABLE_EXCEPTION_CATCHING=0
```
> **주의:** 이 옵션은 바이너리 크기를 늘린다(538K → 758K). 릴리즈 빌드에서는 제거 검토 필요.

---

### 9. 셰이더 파일을 찾을 수 없음

**오류:** `std::runtime_error: failed to open file: shaders/brdf_lut.wgsl`

**원인 1:** `IBLManager::loadComputeShader`에서 확장자를 `.wgsl`로 읽고 있었으나 실제 파일명은 `.comp.wgsl`

**수정 1:** `IBLManager.cpp`
```cpp
std::string path = "shaders/" + name + ".comp.wgsl";  // 기존: ".wgsl"
```

**원인 2:** 컴퓨트 셰이더 파일이 WASM `--preload-file` 목록에 포함되지 않음

**수정 2:** `CMakeLists.txt`의 preload 복사 커맨드에 5개 컴퓨트 셰이더 추가
```
brdf_lut.comp.wgsl
equirect_to_cubemap.comp.wgsl
irradiance_map.comp.wgsl
prefilter_env.comp.wgsl
frustum_cull.comp.wgsl
```

---

### 10. `RG16Float` 포맷이 Storage Texture로 사용 불가

**오류:** `[WebGPU Error] Validation: Texture format RG16Float doesn't support StorageBinding`

**원인:** WebGPU 스펙상 `RG16Float`은 쓰기 가능한 Storage Texture로 사용할 수 없음 (RGBA16Float은 가능)

**수정:** BRDF LUT 텍스처 포맷을 Emscripten에서만 `RGBA16Float`으로 변경
- `IBLManager.cpp`: `#ifdef __EMSCRIPTEN__`으로 포맷 분기
- `shaders/brdf_lut.comp.wgsl`: `rg16float` → `rgba16float`

---

### 11. Storage Buffer를 Vertex 스테이지에 Read-Write로 바인딩 불가

**오류:** `Read-write storage buffer binding with Vertex visibility is not allowed`

**원인:** WebGPU는 Vertex 셰이더에서 read-write SSBO 바인딩을 금지함

**수정:** `RHIBindGroup.hpp`에 `ReadOnlyStorageBuffer` 열거형 추가 후, Renderer의 SSBO 레이아웃에 적용
```cpp
enum class BindingType {
    ...
    ReadOnlyStorageBuffer,  // Vertex 스테이지에서 사용 가능
    ...
};
```

---

### 12. 바인드 그룹 엔트리 수 불일치

**오류:** `Bind group has 1 entries but bind group layout expects 2`

**원인:** `rhiBindGroupLayout`에 사용하지 않는 SampledTexture 엔트리(binding=1)가 정의되어 있었지만 실제 바인드 그룹에는 제공되지 않음

**수정:** `Renderer.cpp`에서 사용하지 않는 레이아웃 엔트리 제거

---

### 13. `wgpuSurfacePresent` 미지원으로 크래시

**오류:** `wgpuSurfacePresent is unsupported` → Abort

**원인:** Emscripten에서는 `requestAnimationFrame` 루프가 자동으로 Present를 처리함. 명시적으로 `wgpuSurfacePresent`를 호출하면 크래시

**수정:** `WebGPURHISwapchain.cpp`의 `present()`를 Emscripten에서 no-op으로 처리
```cpp
#ifdef __EMSCRIPTEN__
    // 자동 처리됨 — 명시적 present 불필요
#else
    wgpuSwapChainPresent(m_swapchain);
#endif
```

---

### 14. `depthSlice (0) is defined for a non-3D attachment` (매 프레임)

**오류 메시지:**
```
[WebGPU Error] Validation: depthSlice (0) is defined for a non-3D attachment
 - While validating colorAttachments[0].
 - While encoding [CommandEncoder].BeginRenderPass([RenderPassDescriptor "RHI Main Render Pass"]).
```

#### 원인 분석

WebGPU 스펙은 2D/Cube 텍스처에 연결된 Color Attachment에 대해 `depthSlice` 필드를 **반드시 `WGPU_DEPTH_SLICE_UNDEFINED`** (`= UINT32_MAX = 0xFFFFFFFF`)로 설정하도록 요구한다. 값이 0이면 "3D 텍스처의 0번 슬라이스를 지정한다"는 의미로 해석되어 검증 오류가 발생한다.

`WebGPURHICommandEncoder.cpp`에는 다음과 같은 코드가 있었다:

```cpp
WGPURenderPassColorAttachment wgpuAttachment{};  // 모든 필드 0으로 초기화
// ... view, loadOp, storeOp 등 설정 ...

#if !defined(__EMSCRIPTEN__) || EMSCRIPTEN_VERSION_AT_LEAST(3, 1, 60)
    wgpuAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
```

`EMSCRIPTEN_VERSION_AT_LEAST`는 `WebGPUCommon.hpp`에 `__EMSCRIPTEN__`이 정의된 경우에만 매크로로 정의된다:

```cpp
#ifdef __EMSCRIPTEN__
    #define EMSCRIPTEN_VERSION_AT_LEAST(major, minor, tiny) \
        (!EMSCRIPTEN_VERSION_LESS_THAN(major, minor, tiny))
#endif
```

여기서 핵심 문제가 발생한다.

#### 왜 `depthSlice`가 0이었나 — 스테일 빌드 캐시

`WebGPUCommon.hpp`는 헤더 파일이지만, `.cpp` 파일의 변경이 있더라도 **Make/CMake 빌드 시스템의 캐시**로 인해 이전 `.o` 오브젝트 파일이 재사용될 수 있다. `WebGPURHICommandEncoder.cpp`에 `depthSlice` 대입 코드가 추가된 이후, 해당 소스 파일이 실제로 다시 컴파일되지 않은 채로 기존 오브젝트가 링크에 사용되었다. 결과적으로:

- 소스 파일: `depthSlice = WGPU_DEPTH_SLICE_UNDEFINED` 코드 존재
- 바이너리: 구형 코드 — `depthSlice = 0` (zero-initialized 상태 유지)

#### 최종 수정

`#if` 버전 가드를 완전히 제거하여 항상 설정되도록 단순화했다:

```cpp
// 수정 전
#if !defined(__EMSCRIPTEN__) || EMSCRIPTEN_VERSION_AT_LEAST(3, 1, 60)
    wgpuAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

// 수정 후
wgpuAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
```

이 변경으로 CMake가 소스 파일 변경을 감지하여 강제 재컴파일을 수행, 올바른 코드가 바이너리에 반영되었다.

> **교훈:** 빌드 캐시 문제를 의심해야 할 때는 해당 소스 파일에 의미 있는 변경을 가하거나, `make clean-wasm && make wasm`으로 전체 재빌드를 수행한다.

---

## 스왑체인 교체: `WGPUSwapChain` → `WGPUSurface`

emdawnwebgpu는 `WGPUSwapChain` API를 완전히 제거했다. `WebGPURHISwapchain.cpp`에서 모든 스왑체인 관련 함수를 `#ifdef __EMSCRIPTEN__`으로 분기 처리했다.

| 함수 | 기존 (native) | Emscripten |
|---|---|---|
| `initSwapchain()` | `wgpuDeviceCreateSwapChain()` | `wgpuSurfaceConfigure()` |
| `destroySwapchain()` | `wgpuSwapChainRelease()` | `wgpuSurfaceUnconfigure()` |
| `acquireNextImage()` | `wgpuSwapChainGetCurrentTextureView()` | `wgpuSurfaceGetCurrentTexture()` + `wgpuTextureCreateView()` |
| `present()` | `wgpuSwapChainPresent()` | no-op |

---

## 파일별 수정 요약

| 파일 | 수정 내용 |
|---|---|
| `src/rhi/backends/webgpu/include/rhi/webgpu/WebGPUCommon.hpp` | emdawnwebgpu 호환 typedef/매크로 추가, 버전 비교 매크로 정의 |
| `src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp` | 어댑터/디바이스 요청 콜백 시그니처 분기, 에러 콜백 등록 방식 변경 |
| `src/rhi/backends/webgpu/src/WebGPURHISwapchain.cpp` | WGPUSurface API로 전환 (initSwapchain, destroySwapchain, acquireNextImage, present) |
| `src/rhi/backends/webgpu/src/WebGPURHICommandEncoder.cpp` | `depthSlice = WGPU_DEPTH_SLICE_UNDEFINED` 버전 가드 제거 |
| `src/rhi/backends/webgpu/src/WebGPURHIBindGroup.cpp` | `ReadOnlyStorageBuffer` 바인딩 타입 추가 |
| `src/rhi/backends/vulkan/src/VulkanRHIBindGroup.cpp` | `ReadOnlyStorageBuffer` → Vulkan Storage 매핑 추가 |
| `src/rhi/include/rhi/RHIBindGroup.hpp` | `ReadOnlyStorageBuffer` 열거형 추가 |
| `src/rhi/CMakeLists.txt` | `rhi_factory`에 `--use-port=emdawnwebgpu` 컴파일 옵션 추가 |
| `CMakeLists.txt` | 컴퓨트 셰이더 preload 목록 추가, `-s DISABLE_EXCEPTION_CATCHING=0` 추가 |
| `src/rendering/IBLManager.cpp` | 셰이더 확장자 수정 (`.wgsl` → `.comp.wgsl`), RGBA16Float 포맷 분기 |
| `src/rendering/Renderer.cpp` | SSBO 레이아웃을 `ReadOnlyStorageBuffer`로 변경, 미사용 레이아웃 엔트리 제거 |
| `shaders/brdf_lut.comp.wgsl` | Storage 포맷을 `rgba16float`으로 변경 |
