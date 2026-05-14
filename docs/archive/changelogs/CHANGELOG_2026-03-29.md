# Changelog: 2026-03-29

> WASM Post-Process Pipeline + Deferred Resize + WebGPU Runtime Fix + Visual Polish

---

## Summary

오늘의 작업은 `finance-city-engine` fork와의 비교 분석을 통해 도출된 두 가지 주요 개선 사항을 Mini-Engine에 역이식(back-port)한 것이다.

1. **Phase 14 — WASM 멀티패스 포스트프로세스 파이프라인**
   - HDR 렌더 타겟 (RGBA16Float) → ACES Filmic Tonemap → FXAA Anti-Aliasing → 스왑체인
2. **Deferred Resize (WASM 안정성 개선)**
   - JS 이벤트 핸들러에서의 직접 스왑체인 재생성 → Asyncify 코루틴 내 안전한 지연 적용
3. **Pre-existing 버그 수정**
   - `Renderer.hpp`에 누락된 멤버 선언들 추가
   - `Application.cpp`의 `ImGuiManager` API 불일치 수정

---

## 변경 파일 목록

### 신규 파일

| 파일 | 설명 |
|------|------|
| `shaders/tonemap.wgsl` | WASM용 ACES Filmic Tonemap 셰이더 (fullscreen triangle, gamma 포함) |
| `shaders/fxaa.wgsl` | WASM용 FXAA 3.11 Anti-Aliasing 셰이더 (Rec.601 luminance 기반) |
| `shaders/tonemap.vert.glsl` | Vulkan 토네맵 버텍스 셰이더 (gl_VertexIndex 기반 fullscreen triangle) |
| `shaders/tonemap.frag.glsl` | Vulkan 토네맵 프래그먼트 셰이더 (ACES, sRGB 스왑체인이 gamma 처리) |

### 수정 파일

#### `src/rendering/Renderer.hpp`
- `handleFramebufferResize(int width, int height)` 오버로드 추가 (WASM 명시적 치수 전달용)
- `#ifdef __EMSCRIPTEN__` 가드 하에 HDR/LDR 렌더 타겟 멤버 추가:
  - `hdrColorTexture`, `hdrColorView`, `hdrSampler` (RGBA16Float HDR 버퍼)
  - `ldrColorTexture`, `ldrColorView` (RGBA8Unorm LDR 중간 버퍼)
- `#ifdef __EMSCRIPTEN__` 가드 하에 Tonemap 파이프라인 멤버 추가:
  - `tonemapVertexShader`, `tonemapFragmentShader`
  - `tonemapBindGroupLayout`, `tonemapBindGroup`
  - `tonemapPipelineLayout`, `tonemapPipeline`
- `#ifdef __EMSCRIPTEN__` 가드 하에 FXAA 파이프라인 멤버 추가:
  - `fxaaVertexShader`, `fxaaFragmentShader`
  - `fxaaBindGroupLayout`, `fxaaBindGroup`
  - `fxaaPipelineLayout`, `fxaaPipeline`
- `#ifdef __EMSCRIPTEN__` 가드 하에 private 메서드 추가:
  - `createHDRRenderTarget()`, `createTonemapPipeline()`, `createFXAAPipeline()`
- 누락된 `setShadowSceneRadius(float)` / `getShadowSceneRadius()` public 메서드 추가
- 누락된 `float shadowSceneRadius = 200.0f` private 멤버 추가
- `#ifndef __EMSCRIPTEN__` 하에 누락된 `GpuProfiler* getGpuProfiler()` public 메서드 추가
- `#ifndef __EMSCRIPTEN__` 하에 누락된 `std::unique_ptr<class GpuProfiler> gpuProfiler` private 멤버 추가

#### `src/rendering/Renderer.cpp`
- **생성자**: `createHDRRenderTarget()` 호출 (`createBuildingPipeline()` 이전), `createTonemapPipeline()` + `createFXAAPipeline()` 호출 추가
- **`handleFramebufferResize(int, int)` 구현**: `waitIdle()` → `createSwapchain(w,h,true)` → `createRHIDepthResources()` → (WASM) HDR 타겟 재생성 + 바인드 그룹 재생성
- **`recreateSwapchain()`**: WASM에서 HDR 타겟 재생성 + tonemap/FXAA 바인드 그룹 재생성
- **`drawFrame()` geometry pass**: WASM에서 color attachment를 HDR 텍스처로 변경
  ```cpp
  #ifdef __EMSCRIPTEN__
      colorAttachment.view = (hdrColorView && tonemapPipeline) ? hdrColorView.get() : swapchainView;
  #else
      colorAttachment.view = swapchainView;
  #endif
  ```
- **`drawFrame()` Tonemap pass** (WASM): geometry pass 이후, LDR 타겟으로 fullscreen `draw(3)`
- **`drawFrame()` FXAA pass** (WASM): tonemap pass 이후, 스왑체인으로 fullscreen `draw(3)`
- **`createHDRRenderTarget()`**: RGBA16Float HDR 텍스처 + shared linear/clamp 샘플러 + RGBA8Unorm LDR 텍스처 생성
- **`createTonemapPipeline()`**: `tonemap.wgsl` 로드, RGBA8Unorm 컬러 타겟으로 파이프라인 생성
- **`createFXAAPipeline()`**: `fxaa.wgsl` 로드, 스왑체인 포맷 컬러 타겟으로 파이프라인 생성

#### `src/Application.hpp`
- `#ifdef __EMSCRIPTEN__` 가드 하에 deferred resize 상태 추가:
  ```cpp
  bool m_pendingResize = false;
  int  m_pendingWidth  = 0;
  int  m_pendingHeight = 0;
  ```

#### `src/Application.cpp`
- `#include <emscripten/html5.h>` 추가
- **`initWindow()` WASM 경로**: `window.innerWidth/innerHeight`를 `EM_ASM_INT`로 읽어 초기 윈도우 크기 결정
- **`initWindow()` WASM 경로**: `emscripten_set_resize_callback`으로 pending dims 저장
- **`framebufferResizeCallback()`**: WASM에서 no-op (스왑체인 재생성은 Asyncify 코루틴 밖에서 불안전)
- **`mainLoopFrame()` WASM deferred resize**:
  ```cpp
  if (m_pendingResize && m_pendingWidth > 0 && m_pendingHeight > 0) {
      glfwSetWindowSize(window, m_pendingWidth, m_pendingHeight);
      renderer->handleFramebufferResize(m_pendingWidth, m_pendingHeight);
      camera->setAspectRatio(...);
      m_pendingResize = false;
  }
  ```
- **버그 수정**: `ImGuiManager::GpuTimingData` → `ImGuiManager::GPUTiming`
- **버그 수정**: `imgui->setGpuTimingData()` → `imgui->setGPUTiming()`
- **버그 수정**: `imgui->getAndClearScaleRequest()` → `imgui->getBuildingCountChange(int& outCount)`

#### `CMakeLists.txt`
- `tonemap.vert.spv`, `tonemap.frag.spv` 컴파일 커스텀 커맨드 추가
- `building_shaders` DEPENDS 목록에 tonemap SPV 파일 추가

---

## 기술적 세부 사항

### 멀티패스 렌더링 파이프라인 (WASM 전용)

```
[Geometry Pass]
  → 입력: 씬 오브젝트, SSBO, 텍스처
  → 출력: HDR Color Texture (RGBA16Float, 씬 해상도)
         Depth Texture

[Tonemap Pass]
  → 입력: HDR Color Texture + Sampler (set 0, binding 0/1)
  → 출력: LDR Color Texture (RGBA8Unorm, 씬 해상도)
  → 알고리즘: ACES Filmic (x*(2.51x+0.03))/(x*(2.43x+0.59)+0.14) + gamma 2.2

[FXAA Pass]
  → 입력: LDR Color Texture + Sampler + 해상도 uniform (set 0, binding 0/1/2)
  → 출력: Swapchain (최종 표시)
  → 알고리즘: FXAA 3.11 Simplified (12-step edge walk, Rec.601 luminance)
```

**Fullscreen Triangle 기법**: 버텍스 버퍼 없이 `@builtin(vertex_index)` (WGSL) / `gl_VertexIndex` (GLSL)로 3개 버텍스 위치를 직접 계산:
```wgsl
var pos = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
```

### Deferred Resize 메커니즘

**문제**: `emscripten_set_resize_callback`은 JS 이벤트 핸들러에서 실행된다. Emscripten의 Asyncify 코루틴 외부에서 `waitIdle()` + 스왑체인 재생성을 호출하면 deadlock/corruption이 발생한다.

**해결**: 리사이즈 이벤트가 발생하면 pending 치수만 저장하고, 다음 프레임 시작 시(`mainLoopFrame()` 진입부) 안전하게 적용한다. `glfwSetWindowSize()`를 먼저 호출하여 GLFW 내부 상태와 캔버스 버퍼 크기를 동기화한 후, 명시적 치수를 전달하는 `handleFramebufferResize(w, h)` 오버로드로 스왑체인을 재생성한다.

### Vulkan vs WASM 렌더링 파이프라인 차이

| 특성 | Vulkan (Desktop) | WebGPU (WASM) |
|------|-----------------|---------------|
| 포스트프로세스 | 스왑체인 직접 렌더 | HDR→Tonemap→FXAA→스왑체인 3-pass |
| Tonemapping | 빌딩 셰이더 내 인라인 | 별도 Tonemap pass |
| Anti-Aliasing | 없음 | FXAA pass |
| Gamma | sRGB 스왑체인 자동 처리 | 토네맵 셰이더에서 명시적 pow(x, 1/2.2) |
| 셰이더 언어 | GLSL → SPIR-V | WGSL |

---

## 빌드 검증

```
$ make
...
[100%] Built target vulkanGLFW
Build complete!
```

---

## 참고 사항

- Phase 3 (WebSocket 실시간 데이터 레이어)는 이번 작업 범위에 포함되지 않았다.
- `finance-city-engine`과의 비교에서 도출된 Phase 1 (WASM 렌더링 품질) + Phase 2 (WASM 안정성)만 역이식했다.
- FXAA는 현재 WASM 빌드에서만 활성화된다. 네이티브 빌드에서의 FXAA 도입은 별도 작업이 필요하다.

---

## 추가 작업: WebGPU 런타임 오류 수정 + 시각적 개선

> 브라우저 콘솔에서 발견된 파이프라인 validation 오류들을 연쇄적으로 수정하고, finance-city-engine 비교를 통해 시각적 품질을 개선한다.

---

## 배경: WebGPU 파이프라인 validation 오류 패턴

WebGPU는 잘못된 파이프라인 생성 시 null 대신 **poisoned 객체**를 반환한다. 이 객체를 사용하면 `[Invalid CommandBuffer]` → `Queue.Submit failed` 오류가 매 프레임 연쇄 발생한다. 오류 메시지 패턴:

```
[Invalid ComputePipeline "..."] is invalid due to a previous error.
[Invalid RenderPipeline "..."] is invalid due to a previous error.
[WebGPU Error] Validation: [Invalid CommandBuffer] is invalid...
[Queue].Submit([[Invalid CommandBuffer]])
```

근본 원인: WGSL 셰이더의 `var<storage, read>` 선언과 바인드 그룹 레이아웃의 `StorageBuffer`(read-write) 타입 불일치.

---

## 수정된 WebGPU 오류

### 오류 1: `[Invalid ComputePipeline "Frustum_Cull_Pipeline"]`

**원인**: `frustum_cull.comp.wgsl`의 binding 1 (`objectBuffer`)이 `var<storage, read>`인데, C++ 레이아웃에서 `StorageBuffer`(read-write)로 선언됨.

**수정** — `src/rendering/Renderer.cpp` (`createCullingPipeline()`):

```cpp
// Before
objEntry.type = rhi::BindingType::StorageBuffer;
// After
objEntry.type = rhi::BindingType::ReadOnlyStorageBuffer;  // shader: var<storage, read>
```

---

### 오류 2: `[Invalid RenderPipeline "ShadowPipeline"]`

**원인**: 렌더 셰이더(`shadow.wgsl`, `building.wgsl`) set 1의 `objectBuffer`와 `visibleIndices` 두 바인딩 모두 `var<storage, read>`인데, SSBO 바인드 그룹 레이아웃이 둘 다 `StorageBuffer`로 선언됨.

**수정** — `src/rendering/Renderer.cpp` (`createBuildingPipeline()` 내 SSBO layout):

```cpp
// Before (두 바인딩 모두)
ssboEntry.type = rhi::BindingType::StorageBuffer;
visibleIndicesEntry.type = rhi::BindingType::StorageBuffer;
// After
ssboEntry.type = rhi::BindingType::ReadOnlyStorageBuffer;         // var<storage, read>
visibleIndicesEntry.type = rhi::BindingType::ReadOnlyStorageBuffer; // var<storage, read>
```

> **원칙**: `var<storage, read>` → `ReadOnlyStorageBuffer`, `var<storage, read_write>` → `StorageBuffer`. Frustum cull compute 레이아웃의 `indirect`(binding 2)와 `visibleIndices`(binding 3)는 write가 필요하므로 `StorageBuffer` 유지.

---

### 파이프라인 색상 포맷 불일치 수정

WASM에서 geometry pass는 HDR 오프스크린 타겟(RGBA16Float)으로 렌더링하는데, 빌딩/파티클/스카이박스 파이프라인이 모두 스왑체인 포맷(`BGRA8Unorm`)으로 선언되어 있었다. 런타임에 render pass 색상 첨부 포맷과 불일치로 draw call validation 오류 발생.

**수정** — `src/rendering/Renderer.cpp`:

| 함수                       | 수정 내용                                    |
|----------------------------|----------------------------------------------|
| `createBuildingPipeline()` | `#ifdef __EMSCRIPTEN__` → `RGBA16Float`      |
| `createParticleRenderer()` | `#ifdef __EMSCRIPTEN__` → `RGBA16Float`      |
| `createSkyboxRenderer()`   | `#ifdef __EMSCRIPTEN__` → `RGBA16Float`      |

```cpp
// Before
rhi::TextureFormat colorFormat = swapchain->getFormat();  // BGRA8Unorm on WASM

// After
#ifdef __EMSCRIPTEN__
    rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA16Float;
#else
    rhi::TextureFormat colorFormat = swapchain->getFormat();
#endif
```

---

## WebGPU surface/swapchain 연쇄 수정 (이전 세션 누적)

초기 "context is not configured" 오류를 해결하기 위해 finance-city-engine 패턴을 역이식한 내용:

| 파일                      | 수정 내용                                                                           |
|---------------------------|-------------------------------------------------------------------------------------|
| `WebGPURHIDevice.cpp`     | EMSCRIPTEN에서도 device-level surface 생성 유지 (two-surface 패턴)                  |
| `WebGPURHISwapchain.cpp`  | `WGPUCompositeAlphaMode_Auto` 사용; `wgpuTextureRelease()` 호출 제거                |
| `RendererBridge.cpp`      | 신규 스왑체인 생성 전 `m_swapchain.reset()` 명시 호출 (RAII 순서 버그)              |
| `WebGPUCommon.hpp`        | emdawnwebgpu 호환 shim 추가 (`WGPUSurfaceDescriptorFromCanvasHTMLSelector` typedef) |
| `tests/wasm_shell.html`   | JS `canvas.width/height` 조작 제거; CSS `100vw/100vh` 풀스크린; 로딩 오버레이 추가  |

---

## 시각적 개선 (finance-city-engine 비교 분석)

### 셰이더: building.wgsl — HDR 파이프라인 정합성

**문제**: HDR 렌더 타겟(RGBA16Float) 도입 후에도 빌딩 셰이더가 내부적으로 ACES 톤매핑 + 감마 보정을 수행하고 있었다. 이는 이미 별도의 `tonemap.wgsl` pass가 동일한 작업을 수행하므로 이중 처리에 해당한다.

**수정** — `shaders/building.wgsl` (WASM 소스):
- `ACESFilm()` 함수 제거
- `fs_main()` 말미의 `ACESFilm(color * exp)` + `pow(color, 1/2.2)` 제거
- exposure 스케일링만 유지하여 HDR 값 그대로 출력

```wgsl
// Before
color = ACESFilm(color * exp);
color = pow(color, vec3<f32>(1.0 / 2.2));
return vec4<f32>(color, 1.0);

// After — HDR output; tonemap pass handles ACES + gamma
color = color * exp;
return vec4<f32>(color, 1.0);
```

> `shaders/building.frag.glsl` (Vulkan): Vulkan 경로에는 별도 tonemap pass가 없으므로 ACES 유지.

---

### 셰이더: skybox.wgsl — 따뜻한 일몰 팔레트

finance-city-engine과의 비교를 통해 차가운 낮 하늘을 따뜻한 일몰 색상으로 교체:

| 요소 | Before | After |
|------|--------|-------|
| 천정(zenith) | `(0.15, 0.35, 0.65)` 낮 파랑 | `(0.05, 0.15, 0.40)` 심청색 |
| 지평선(horizon) | `(0.60, 0.75, 0.90)` 연한 파랑 | `(0.60, 0.40, 0.25)` 오렌지 |
| 지면(ground) | `(0.20, 0.20, 0.22)` 회색 | `(0.10, 0.08, 0.05)` 어두운 갈색 |
| 수평선 헤이즈 | `(0.70, 0.75, 0.85)` 차가운 청회색 | `(0.65, 0.50, 0.35)` 황갈색 |
| 태양 광채(glow) | `pow(s,8) * 0.3` | `pow(s,8) * 0.5` (더 밝게) |
| 함수명 | `getSkyColor()` | `proceduralSky()` |

`shaders/skybox.frag.glsl` (Vulkan)은 이미 `proceduralSky()` 패턴과 같은 warm 색상을 사용하므로 수정 불필요.

---

### BuildingManager: 지면 무한대 확장

**수정** — `src/game/managers/BuildingManager.cpp`:

```cpp
// Before: 빌딩 그리드 크기에 맞춰 동적 계산 (최소 300m)
float gridExtent = 300.0f;
// ...maxDist 계산...
glm::vec3 scale(gridExtent, 0.1f, gridExtent);

// After: 사실상 무한대 (100km)
glm::vec3 scale(100000.0f, 0.1f, 100000.0f);
```

재질도 회녹색(`0.55, 0.58, 0.52`) → 아스팔트(`0.35, 0.35, 0.38`, roughness 0.92)로 변경.

---

### Camera: 도시 조망 시점 개선

finance-city-engine의 aerial city overview 스타일을 mini-engine 씬 스케일에 맞게 적용:

| 파라미터 | Before | After | 이유 |
|---------|--------|-------|------|
| FOV | 70° | 55° | 더 현실적인 도시 투시 |
| pitch | 20° | 35° | 위에서 내려다보는 조망 |
| distance | 150 | 250 | 4×4 그리드 전체 조망 |
| target | `(0, 15, 0)` | `(0, 0, 0)` | 그리드 정중앙 |
| nearPlane | 0.1 | 0.5 | depth precision 개선 |

---

## 렌더링 파이프라인 최종 구조 (WASM)

```
[Frustum Cull — Compute]
  objectBuffer (binding 1): ReadOnlyStorageBuffer ✓

[Shadow Pass]
  set 0: LightSpaceUBO (uniform)
  set 1: ssboBindGroupLayout
    binding 0: objectBuffer → ReadOnlyStorageBuffer ✓
    binding 1: visibleIndices → ReadOnlyStorageBuffer ✓

[Geometry Pass → HDR RGBA16Float]
  building pipeline: colorTarget = RGBA16Float ✓
  particle pipeline: colorTarget = RGBA16Float ✓
  skybox pipeline:   colorTarget = RGBA16Float ✓
  building shader: HDR 출력 (ACES 제거) ✓

[Tonemap Pass → LDR RGBA8Unorm]
  ACES Filmic + gamma 2.2

[FXAA Pass → Swapchain BGRA8Unorm]
  FXAA 3.11 Simplified
```
