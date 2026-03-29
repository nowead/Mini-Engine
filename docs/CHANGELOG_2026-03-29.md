# Changelog: 2026-03-29

> WASM Post-Process Pipeline + Deferred Resize + Bug Fixes

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
