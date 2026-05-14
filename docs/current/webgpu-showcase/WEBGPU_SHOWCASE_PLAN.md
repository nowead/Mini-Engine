# WebGPU Showcase 격상 계획

**작성일**: 2026-05-14
**상태**: Task 1–5 완료
**선행 작업**: WebGPU Deferred Rendering 포팅 (Phase 0–6) 완료

---

## 목표

WebGPU/WASM 빌드의 렌더링 품질은 네이티브(Vulkan)와 동등한 수준에 도달했다.
이제 네이티브 쇼케이스가 ImGui 패널로 제공하는 **인터랙티브 제어 기능**을 WebGPU에서도 동등하게 제공한다.

---

## 현황 분석

### 렌더링 품질 — 이미 동등

| 기능 | 네이티브 | WebGPU |
| --- | --- | --- |
| G-Buffer Deferred Rendering | ✅ | ✅ |
| PBR + 4-Cascade CSM + IBL | ✅ | ✅ |
| SSAO + bilateral blur | ✅ | ✅ |
| Bloom (prefilter + Gaussian) | ✅ | ✅ |
| ACES tonemap + FXAA → swapchain | ✅ | ✅ |
| GPU frustum culling | ✅ | ✅ |

### 쇼케이스/인터랙션 — 공백

| 기능 | 네이티브 | WebGPU | 비고 |
| --- | --- | --- | --- |
| ImGui 전체 UI 패널 | ✅ | ❌ | `Application.cpp` 259–329줄 전체 `#ifndef __EMSCRIPTEN__` |
| Debug view (GBuffer/SSAO/Bloom) | ✅ | ❌ | `debugView` UBO는 `postprocess.wgsl`에 이미 존재 |
| Post-process On/Off 토글 | ✅ | ❌ | `tonemapOn`, `fxaaOn`, `aoStrength`, `bloomStrength` UBO 이미 존재 |
| 조명 설정 (sun, shadow, exposure) | ✅ | ❌ | `Renderer` setter API 이미 존재 |
| CSM cascade 색상 시각화 | ✅ | ❌ | `deferred_lighting.wgsl` 셰이더 추가 필요 |
| 동적 점 광원 (가로등) | ✅ | ❌ | `setPointLights()` API 존재, 씬에 미배치 |
| GPU 타이밍 패널 | ✅ | ❌ | WebGPU `timestamp-query` optional feature |
| 건물 수 조절 | ✅ | ❌ | |

### 핵심 관찰

렌더러 제어 API(`setDebugView`, `setBloomStrength`, `setAOStrength`, `setTonemapEnabled`,
`setFXAAEnabled`, `setDebugCascades`, `setSunDirection`, `setPointLights` 등)는
**이미 `Renderer.hpp`에 구현되어 있다.**
JavaScript에서 이 함수들을 호출할 수 있는 **연결 경로**만 만들면 된다.

---

## 전략

```
네이티브:  Renderer ←→ ImGuiManager (C++)
WebGPU:   Renderer ←→ Emscripten bindings ←→ HTML/CSS overlay (JavaScript)
```

브라우저 DOM을 UI 레이어로 사용한다.
Emscripten `EMSCRIPTEN_BINDINGS`로 C++ 함수를 JavaScript에 노출하고,
HTML 패널에서 직접 호출한다.

---

## 작업 목록

### Task 1 — Emscripten Bindings

**선행 조건**: 없음 (모든 후속 작업의 기반)
**난이도**: 소

노출할 `Renderer` 함수 목록:

```cpp
// 이미 존재하는 setter — JS에 그대로 노출
setDebugView(int)
setBloomStrength(float)
setAOStrength(float)
setTonemapEnabled(bool)
setFXAAEnabled(bool)
setDebugCascades(bool)
setSunDirection(float x, float y, float z)
setSunIntensity(float)
setExposure(float)
setShadowBias(float)
setShadowStrength(float)
setPointLights(...)     // PointLight 벡터 → JS Array로 노출
```

구현 위치: `src/main.cpp` 또는 신규 `src/wasm/WASMBindings.cpp`

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>

EMSCRIPTEN_BINDINGS(renderer_api) {
    emscripten::function("setDebugView",      &g_app_setDebugView);
    emscripten::function("setBloomStrength",  &g_app_setBloomStrength);
    emscripten::function("setAOStrength",     &g_app_setAOStrength);
    emscripten::function("setTonemapEnabled", &g_app_setTonemapEnabled);
    emscripten::function("setFXAAEnabled",    &g_app_setFXAAEnabled);
    emscripten::function("setDebugCascades",  &g_app_setDebugCascades);
    emscripten::function("setSunIntensity",   &g_app_setSunIntensity);
    emscripten::function("setExposure",       &g_app_setExposure);
    emscripten::function("addPointLight",     &g_app_addPointLight);
    emscripten::function("clearPointLights",  &g_app_clearPointLights);
}
#endif
```

각 `g_app_*` 함수는 전역 `Application*` 포인터를 통해 `renderer->setXxx()`를 호출한다.

**수정 파일**:

| 파일 | 변경 내용 |
| --- | --- |
| `src/wasm/WASMBindings.cpp` | 신규 — Emscripten binding 정의 |
| `src/wasm/WASMBindings.hpp` | 신규 — 전역 앱 포인터 setter 선언 |
| `src/main.cpp` | WASM 경로에서 `WASMBindings::setApp(app.get())` 호출 |
| `CMakeLists.txt` | `--bind` 링크 플래그 추가, `WASMBindings.cpp` 소스 추가 |

---

### Task 2 — HTML Overlay UI

**선행 조건**: Task 1
**난이도**: 소

네이티브 ImGui 패널과 동등한 컨트롤을 HTML로 구성한다.
WebGPU canvas 위에 CSS `position: fixed` 패널로 오버레이한다.

**패널 구성**:

```
┌─────────────────────────────────┐
│ Mini-Engine WebGPU              │
├─────────────────────────────────┤
│ [Debug View]                    │
│  ○ Final  ○ Normals  ○ Albedo   │
│  ○ Depth  ○ SSAO    ○ Bloom    │
├─────────────────────────────────┤
│ [Post-Process]                  │
│  ☑ Tonemap (ACES)               │
│  ☑ FXAA                         │
│  ☑ SSAO     Strength: ──●── 0.6 │
│  ☑ Bloom    Strength: ──●── 0.04│
│    Exposure:          ──●── 1.0 │
├─────────────────────────────────┤
│ [Lighting]                      │
│  Sun Intensity: ──●── 1.5       │
│  Shadow Bias:   ──●── 0.0015    │
│  ☐ Show Cascade Regions         │
├─────────────────────────────────┤
│ [Scene]                         │
│  Point Lights: ──●── 20         │
└─────────────────────────────────┘
```

구현 방식: `shell.html` (Emscripten 템플릿) 내부에 패널 HTML/CSS 삽입,
각 컨트롤의 `oninput`/`onchange`에서 `Module.setXxx()` 직접 호출.

```html
<!-- Debug View 라디오 버튼 예시 -->
<input type="radio" name="dbg" value="0" checked
       onchange="Module.setDebugView(0)"> Final
<input type="radio" name="dbg" value="1"
       onchange="Module.setDebugView(1)"> Normals

<!-- SSAO 슬라이더 예시 -->
<input type="range" min="0" max="1" step="0.01" value="0.6"
       oninput="Module.setAOStrength(parseFloat(this.value))">
```

**수정 파일**:

| 파일 | 변경 내용 |
| --- | --- |
| `src/wasm/shell.html` | 신규 또는 기존 파일 수정 — 패널 HTML/CSS 추가 |
| `CMakeLists.txt` | `--shell-file` 옵션으로 커스텀 shell.html 지정 |

---

### Task 3 — CSM Cascade 색상 시각화 (WGSL)

**선행 조건**: Task 1
**난이도**: 소
**독립 실행 가능**: Task 2와 병렬 진행 가능

`deferred_lighting.wgsl`에 cascade 색상 오버레이 추가.
선택한 cascade index에 따라 빨강/초록/파랑/노랑 오버레이를 반투명하게 합성한다.

```wgsl
// PostProcessParams UBO에 debugCascades 플래그 추가
// DeferredLighting UBO에 debugCascades 추가 (이미 ubo.debugCascades 없으면 신규)

let cascadeColors = array<vec3<f32>, 4>(
    vec3<f32>(1.0, 0.2, 0.2),  // cascade 0: 빨강 (0–10m)
    vec3<f32>(0.2, 1.0, 0.2),  // cascade 1: 초록 (10–50m)
    vec3<f32>(0.2, 0.4, 1.0),  // cascade 2: 파랑 (50–200m)
    vec3<f32>(1.0, 1.0, 0.2),  // cascade 3: 노랑 (200m+)
);

if ubo.debugCascades != 0 {
    finalColor = mix(finalColor, cascadeColors[selectedCascade], 0.35);
}
```

`Renderer`의 `setDebugCascades(bool)` → UBO 업데이트 경로는 이미 존재.
`UniformBufferObject` 구조체에 `debugCascades` 필드 추가 필요.

**수정 파일**:

| 파일 | 변경 내용 |
| --- | --- |
| `shaders/deferred_lighting.wgsl` | `debugCascades` 분기 추가 |
| `src/rendering/Renderer.cpp` | `updateRHIUniformBuffer()`에서 `debugCascades` 필드 업데이트 |
| `src/utils/Vertex.hpp` (또는 UBO 정의 위치) | `UniformBufferObject`에 `debugCascades: u32` 추가 |

---

### Task 4 — 동적 점 광원 씬 배치

**선행 조건**: Task 1
**난이도**: 소
**독립 실행 가능**: Task 2, 3과 병렬 진행 가능

도로 교차점마다 가로등 광원을 배치하여 Deferred Rendering의 핵심 장점
(다수 동적 광원에서도 G-Buffer 비용 불변)을 시각적으로 증명한다.

`Application.cpp` WebGPU 경로에서 `setPointLights()` 호출:

```cpp
#ifdef __EMSCRIPTEN__
// 도로 교차점 좌표에 가로등 배치 (gridSize × gridSize 격자)
std::vector<PointLight> streetLights;
for (int x = 0; x < gridSize - 1; ++x) {
    for (int z = 0; z < gridSize - 1; ++z) {
        PointLight light;
        light.position  = glm::vec3(startX + (x + 0.5f) * spacing, 8.0f,
                                    startZ + (z + 0.5f) * spacing);
        light.color     = glm::vec3(1.0f, 0.9f, 0.7f);  // 따뜻한 가로등 색
        light.intensity = 50.0f;
        light.radius    = 25.0f;
        streetLights.push_back(light);
    }
}
renderer->setPointLights(streetLights);
#endif
```

Task 2의 UI 패널에서 광원 수를 0–32 범위 슬라이더로 제어.

**수정 파일**:

| 파일 | 변경 내용 |
| --- | --- |
| `src/Application.cpp` | `#ifdef __EMSCRIPTEN__` 블록에 점 광원 초기화 추가 |
| `src/wasm/WASMBindings.cpp` | `setPointLightCount(int)` 바인딩 추가 |

---

### Task 5 — WebGPU Timestamp-Query (선택)

**선행 조건**: Task 1, 2
**난이도**: 중
**주의**: `timestamp-query`는 WebGPU optional feature — 브라우저 지원 여부 런타임 확인 필요

WebGPU `GPUQuerySet` + `timestamp-query` feature를 사용하여
각 렌더 패스의 GPU 소요 시간을 측정하고 HTML 패널에 표시한다.

```wgsl
// WebGPU timestamp-query 흐름
GPUQuerySet querySet = device.createQuerySet({ type: 'timestamp', count: 16 });
// 각 renderPassDescriptor에 timestampWrites 추가
// resolveQuerySet → readback buffer → JavaScript로 전달
```

RHI 레이어에서 이미 `RHICapabilities`에 optional feature 쿼리 구조가 존재.
`timestamp-query` 지원 여부를 확인하고 미지원 시 패널에 "N/A" 표시로 대체.

**수정 파일**:

| 파일 | 변경 내용 |
| --- | --- |
| `src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp` | `timestamp-query` feature 요청 추가 |
| `src/rendering/Renderer.cpp` | WebGPU 경로 각 패스에 timestamp 삽입 |
| `src/wasm/WASMBindings.cpp` | `getGPUTimings()` 바인딩 — JS에 타이밍 배열 노출 |
| `src/wasm/shell.html` | GPU 타이밍 표시 섹션 추가 |

---

## 구현 순서 및 의존 관계

```
Task 1 (bindings)
    ├── Task 2 (HTML UI)          ← Task 1 완료 후 진행
    ├── Task 3 (CSM WGSL)         ← Task 1과 병렬 가능, Task 2 없이도 동작 확인 가능
    ├── Task 4 (point lights)     ← Task 1과 병렬 가능
    └── Task 5 (timestamp)        ← Task 2 완료 후 진행 (UI 표시 필요)
```

권장 진행 순서:

1. Task 1 → Task 3 → Task 4 (렌더링 기능 완성 우선)
2. Task 2 (UI 패널로 모든 기능을 한 번에 연결)
3. Task 5 (선택적 마무리)

---

## 완성 시 시연 시나리오

```
1. 브라우저에서 WebGPU 빌드 실행
2. [Debug View → Normals] 선택
   → G-Buffer normal 채널이 fullscreen으로 출력됨
3. [Debug View → Final] 복귀
4. [Show Cascade Regions] 체크
   → 거리별 cascade 경계가 빨강/초록/파랑/노랑으로 구분됨
5. [SSAO 체크박스 해제]
   → 건물 기저부 암부가 사라지며 효과 차이 즉시 확인
6. [Point Lights 슬라이더 → 20]
   → 가로등 20개 활성화 — Deferred 파이프라인에서 성능 변화 미미
```

---

## 수정 파일 요약

| 파일 | 작업 | Task |
| --- | --- | --- |
| `src/wasm/WASMBindings.hpp` | 신규 — 전역 앱 포인터 선언 | 1 |
| `src/wasm/WASMBindings.cpp` | 신규 — Emscripten binding 정의 | 1, 4, 5 |
| `src/wasm/shell.html` | 신규/수정 — HTML overlay 패널 | 2, 5 |
| `src/main.cpp` | WASM 경로 바인딩 초기화 | 1 |
| `src/Application.cpp` | WebGPU 경로 점 광원 초기화 | 4 |
| `shaders/deferred_lighting.wgsl` | `debugCascades` 색상 오버레이 | 3 |
| `src/rendering/Renderer.cpp` | UBO에 `debugCascades` 업데이트 | 3 |
| `src/utils/Vertex.hpp` (UBO 정의) | `UniformBufferObject`에 `debugCascades` 추가 | 3 |
| `src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp` | `timestamp-query` feature 요청 | 5 |
| `CMakeLists.txt` | `--bind` 플래그, `WASMBindings.cpp`, `--shell-file` 추가 | 1, 2 |
