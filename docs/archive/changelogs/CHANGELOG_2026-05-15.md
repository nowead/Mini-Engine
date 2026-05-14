# 변경 이력 — 2026-05-15

> 작업 범위: WebGPU 쇼케이스 격상 — Emscripten 바인딩 · HTML 컨트롤 패널 · 동적 점 광원 · 렌더 패스 타이밍

---

## 1. 개요

WebGPU/WASM 빌드의 렌더링 품질은 이미 네이티브(Vulkan)와 동등한 수준에 도달해 있었으나,
네이티브가 ImGui 패널로 제공하는 **인터랙티브 제어 기능**이 WASM 빌드에는 없었음.

이번 작업에서 다섯 가지 변경을 통해 WebGPU 쇼케이스를 네이티브와 동등한 수준으로 격상:

1. **Emscripten 바인딩** — `Renderer` 설정 함수를 JavaScript에 직접 노출
2. **HTML 컨트롤 패널** — G-Buffer 뷰 전환, 포스트 프로세스 토글/슬라이더, 조명 제어, CSM 디버그
3. **CSM cascade 색상 시각화** — 이미 `deferred_lighting.wgsl`에 구현됨. UBO 업데이트 경로 확인으로 완료
4. **동적 점 광원 배치** — 건물 격자 교차점에 가로등 9개 자동 배치
5. **렌더 패스 타이밍** — 패스별 CPU 소요 시간을 HTML 패널에 실시간 표시

---

## 2. Emscripten 바인딩

### 2.1 배경

`Renderer.hpp`에는 이미 `setDebugView()`, `setBloomStrength()`, `setExposure()` 등
렌더러 상태를 제어하는 setter가 모두 구현되어 있었음.
JavaScript에서 이 함수들을 호출할 수 있는 **연결 경로**만 없었음.

### 2.2 신규 파일

**`src/wasm/WASMBindings.hpp`**

전역 `Application*` 포인터 setter 선언:

```cpp
namespace wasm {
    void setApp(Application* app);
}
```

**`src/wasm/WASMBindings.cpp`**

정적 포인터 `g_app`을 통해 `Application` 브리지 메서드를 호출하는 자유 함수 정의,
`EMSCRIPTEN_BINDINGS`으로 JavaScript에 노출:

```cpp
EMSCRIPTEN_BINDINGS(mini_engine) {
    emscripten::function("setDebugView",      &wasm::js_setDebugView);
    emscripten::function("setBloomStrength",   &wasm::js_setBloomStrength);
    emscripten::function("setAOStrength",      &wasm::js_setAOStrength);
    emscripten::function("setTonemapEnabled",  &wasm::js_setTonemapEnabled);
    emscripten::function("setFXAAEnabled",     &wasm::js_setFXAAEnabled);
    emscripten::function("setDebugCascades",   &wasm::js_setDebugCascades);
    emscripten::function("setSunIntensity",    &wasm::js_setSunIntensity);
    emscripten::function("setExposure",        &wasm::js_setExposure);
    emscripten::function("setPointLightCount", &wasm::js_setPointLightCount);
    // 타이밍 조회 (섹션 6 참조)
    emscripten::function("getPassTimeGBuffer",     &wasm::js_getPassTimeGBuffer);
    emscripten::function("getPassTimeDeferred",    &wasm::js_getPassTimeDeferred);
    emscripten::function("getPassTimeSSAO",        &wasm::js_getPassTimeSSAO);
    emscripten::function("getPassTimeBloom",       &wasm::js_getPassTimeBloom);
    emscripten::function("getPassTimePostProcess", &wasm::js_getPassTimePostProcess);
    emscripten::function("getPassTimeTotal",       &wasm::js_getPassTimeTotal);
}
```

### 2.3 Application.hpp 변경

`#ifdef __EMSCRIPTEN__` 공개 섹션에 인라인 브리지 메서드 추가.
각 메서드는 `renderer` 포인터 null 체크 후 대응 `Renderer` setter를 위임:

```cpp
#ifdef __EMSCRIPTEN__
    void wasm_setDebugView(int v)        { if (renderer) renderer->setDebugView(v); }
    void wasm_setBloomStrength(float s)   { if (renderer) renderer->setBloomStrength(s); }
    void wasm_setAOStrength(float s)      { if (renderer) renderer->setAOStrength(s); }
    void wasm_setTonemapEnabled(bool on)  { if (renderer) renderer->setTonemapEnabled(on); }
    void wasm_setFXAAEnabled(bool on)     { if (renderer) renderer->setFXAAEnabled(on); }
    void wasm_setDebugCascades(bool on)   { if (renderer) renderer->setDebugCascades(on); }
    void wasm_setSunIntensity(float i)    { if (renderer) renderer->setSunIntensity(i); }
    void wasm_setExposure(float e)        { if (renderer) renderer->setExposure(e); }
    void wasm_setPointLightCount(int n);  // Application.cpp 구현
#endif
```

### 2.4 main.cpp 변경

WASM 경로에서 Application 인스턴스를 바인딩 모듈에 등록:

```cpp
Application app;
#ifdef __EMSCRIPTEN__
    wasm::setApp(&app);
#endif
app.run();
```

`run()` 내부의 `emscripten_set_main_loop_arg(sim_infinite_loop=1)` 호출 전에 `setApp`이 실행되므로,
렌더러가 첫 프레임을 그리기 시작할 시점에 `g_app`이 유효한 상태로 보장됨.

### 2.5 CMakeLists.txt 변경

| 항목 | 변경 내용 |
| --- | --- |
| `target_link_options` | `"SHELL:--bind"` 추가 |
| WASM 소스 목록 | `src/wasm/WASMBindings.hpp`, `src/wasm/WASMBindings.cpp` 추가 |
| WGSL 셰이더 복사 목록 | `tonemap.wgsl` + `fxaa.wgsl` → `postprocess.wgsl` (이미 통합된 셰이더가 복사 대상에 누락되어 있던 버그 수정) |

---

## 3. HTML 컨트롤 패널

### 3.1 설계

네이티브 ImGui 패널과 동등한 제어 항목을 HTML로 구성.
WebGPU canvas 위에 `position: fixed` 오버레이로 배치.
각 컨트롤의 `oninput`/`onchange`에서 `Module.setXxx()` 직접 호출.

### 3.2 패널 구성

`tests/wasm_shell.html`에 `#controlPanel` 오버레이 추가:

| 섹션 | 컨트롤 | 호출 함수 |
| --- | --- | --- |
| G-Buffer View | 라디오 9개 (Normal / Normals / Albedo / Metallic / Roughness / AO / Depth / SSAO / Bloom) | `Module.setDebugView(n)` |
| Post-Process | 체크박스 2개 (Tonemap, FXAA) | `Module.setTonemapEnabled()`, `Module.setFXAAEnabled()` |
| Post-Process | 슬라이더 3개 (Exposure, Bloom, SSAO) | `Module.setExposure()`, `Module.setBloomStrength()`, `Module.setAOStrength()` |
| Lighting | 슬라이더 1개 (Sun) | `Module.setSunIntensity()` |
| Lighting | 슬라이더 1개 (Street Lights 0–9) | `Module.setPointLightCount()` |
| Debug | 체크박스 1개 (CSM Cascades) | `Module.setDebugCascades()` |
| Pass Timings | 표시 전용 6행 | `setInterval` 폴링 |

### 3.3 슬라이더 초기값

슬라이더 초기값은 `Renderer.hpp` 기본값과 일치시킴:

| 슬라이더 | 초기값 | `Renderer` 기본값 |
| --- | --- | --- |
| Exposure | 1.0 | `float exposure = 1.0f` |
| Bloom | 0.04 | `float bloomStrength = 0.04f` |
| SSAO | 0.6 | `float aoStrength = 0.6f` |
| Sun | 1.5 | `float sunIntensity = 1.5f` |

### 3.4 Module.onRuntimeInitialized

바인딩 등록은 C++ 정적 초기화 시점에 완료되므로 `onRuntimeInitialized` 콜백 시점에 이미 사용 가능.
이벤트 핸들러를 이 콜백 내부에서 연결하여 WASM 로드 이전 접근을 방지:

```javascript
var Module = {
    onRuntimeInitialized: function() {
        wireControls();  // 모든 이벤트 핸들러 + 타이밍 폴러 등록
    },
    ...
};
```

---

## 4. 동적 점 광원 배치

### 4.1 가로등 초기화

`Application::initGameLogic()` 내 건물 격자 생성 루프가 끝난 직후,
`#ifdef __EMSCRIPTEN__` 블록에서 가로등 9개를 초기화:

```cpp
// gridSize=4, spacing=30.0f → 교차점 3×3 = 9개
int intervals = gridSize - 1;  // 3
for (int ix = 0; ix < intervals; ++ix) {
    for (int iz = 0; iz < intervals; ++iz) {
        PointLight pl;
        pl.position  = glm::vec3(startX + (ix + 0.5f) * spacing, 10.0f,
                                  startZ + (iz + 0.5f) * spacing);
        pl.radius    = 25.0f;
        pl.color     = glm::vec3(1.0f, 0.75f, 0.35f);  // 따뜻한 주황
        pl.intensity = 3.0f;
        streetLights.push_back(pl);
    }
}
renderer->setPointLights(streetLights);
```

`startX`, `startZ`는 건물 격자와 동일한 기준점 사용. 교차점 좌표는 건물 사이 중간:
- x축: `startX + 0.5·spacing`, `startX + 1.5·spacing`, `startX + 2.5·spacing`
- z축: 동일

### 4.2 JavaScript 제어

`wasm_setPointLightCount(int n)` 구현:

```cpp
void Application::wasm_setPointLightCount(int n) {
    if (!renderer) return;
    int total = static_cast<int>(streetLights.size());
    int count = n < 0 ? 0 : (n > total ? total : n);
    renderer->setPointLights(std::vector<PointLight>(
        streetLights.begin(), streetLights.begin() + count));
}
```

HTML 패널 슬라이더(0–9)에서 `Module.setPointLightCount(n)` 호출 시 즉시 반영됨.

### 4.3 Deferred Rendering의 이점 시연

점 광원 수가 늘어나도 G-Buffer 기록 비용은 불변 — 조명 계산만 Deferred Lighting 패스에서 추가됨.
슬라이더로 0 → 9개를 전환하면서 Deferred 파이프라인의 다중 광원 처리 성능을 직접 확인 가능.

---

## 5. CSM Cascade 색상 시각화

`shaders/deferred_lighting.wgsl`의 조명 계산 이후에 cascade 색상 오버레이가 이미 구현되어 있었음:

```wgsl
if (ubo.debugCascades > 0.5) {
    let cascadeColors = array<vec3<f32>, 4>(
        vec3<f32>(1.0, 0.2, 0.2),  // cascade 0: 빨강
        vec3<f32>(0.2, 1.0, 0.2),  // cascade 1: 초록
        vec3<f32>(0.2, 0.4, 1.0),  // cascade 2: 파랑
        vec3<f32>(1.0, 1.0, 0.2),  // cascade 3: 노랑
    );
    color = mix(color, cascadeColors[ci], 0.5);
}
```

`Renderer.cpp`의 `updateRHIUniformBuffer()` 역시 이미 `ubo.debugCascades = debugCascades ? 1.0f : 0.0f;`를 올바르게 기록하고 있었음.

이번 작업에서는 HTML 패널의 "CSM Cascades" 체크박스에서 `Module.setDebugCascades(bool)`를 호출하는 연결만 추가. 셰이더·UBO 양 방향 모두 추가 코드 없음.

---

## 6. 렌더 패스 타이밍

### 6.1 설계 배경

WebGPU `timestamp-query`는 선택적(optional) 기능으로 브라우저 지원이 제한적이며,
RHI 레이어에서 `WGPURenderPassDescriptor.timestampWrites`를 지원하려면 인터페이스 변경이 필요함.

대신 `std::chrono::high_resolution_clock`으로 **CPU 명령 기록(recording) 시간**을 측정.
WebGPU 패스는 GPU와 순차 실행되므로 CPU 기록 시간은 실제 렌더 비용의 합리적인 근사값임.
UI 패널에 "(CPU ms)" 레이블로 측정 방식을 명시.

### 6.2 Renderer.cpp 변경

`drawFrame()` 내 WASM 경로 각 패스 전후에 `_Clock::now()` 삽입:

```cpp
#ifdef __EMSCRIPTEN__
    using _Clock = std::chrono::high_resolution_clock;
    auto _tFrame = _Clock::now();
    auto _tPass  = _tFrame;
#endif

// ... G-Buffer pass ...
#ifdef __EMSCRIPTEN__
    m_passTimeGBuffer = duration_ms(_Clock::now() - _tPass);
    _tPass = _Clock::now();

    // ... Deferred Lighting pass ...
    m_passTimeDeferred = duration_ms(_Clock::now() - _tPass);
    _tPass = _Clock::now();

    // ... SSAO + blur passes ...
    m_passTimeSSAO = duration_ms(_Clock::now() - _tPass);
    _tPass = _Clock::now();

    // ... Bloom passes ...
    m_passTimeBloom = duration_ms(_Clock::now() - _tPass);
    _tPass = _Clock::now();

    // ... PostProcess pass ...
    m_passTimePostProcess = duration_ms(_Clock::now() - _tPass);
#endif

auto commandBuffer = encoder->finish();
#ifdef __EMSCRIPTEN__
    m_passTimeTotal = duration_ms(_Clock::now() - _tFrame);  // 명령 인코딩 총합
#endif
```

### 6.3 Renderer.hpp 변경

`#ifdef __EMSCRIPTEN__` private 멤버 및 public getter 추가:

```cpp
// private
float m_passTimeGBuffer     = 0.0f;
float m_passTimeDeferred    = 0.0f;
float m_passTimeSSAO        = 0.0f;
float m_passTimeBloom       = 0.0f;
float m_passTimePostProcess = 0.0f;
float m_passTimeTotal       = 0.0f;

// public
float getPassTimeGBuffer()     const { return m_passTimeGBuffer; }
float getPassTimeDeferred()    const { return m_passTimeDeferred; }
float getPassTimeSSAO()        const { return m_passTimeSSAO; }
float getPassTimeBloom()       const { return m_passTimeBloom; }
float getPassTimePostProcess() const { return m_passTimePostProcess; }
float getPassTimeTotal()       const { return m_passTimeTotal; }
```

### 6.4 HTML 패널 + JavaScript 폴러

패널 하단 "Pass Timings" 섹션에 6행 표시 추가.
`onRuntimeInitialized`에서 `setInterval(updateTimings, 500)`으로 0.5초마다 갱신:

```javascript
function updateTimings() {
    document.getElementById('tGBuffer').textContent     = Module.getPassTimeGBuffer().toFixed(2) + ' ms';
    document.getElementById('tDeferred').textContent    = Module.getPassTimeDeferred().toFixed(2) + ' ms';
    document.getElementById('tSSAO').textContent        = Module.getPassTimeSSAO().toFixed(2) + ' ms';
    document.getElementById('tBloom').textContent       = Module.getPassTimeBloom().toFixed(2) + ' ms';
    document.getElementById('tPostProcess').textContent = Module.getPassTimePostProcess().toFixed(2) + ' ms';
    document.getElementById('tTotal').textContent       = Module.getPassTimeTotal().toFixed(2) + ' ms';
}
setInterval(updateTimings, 500);
```

---

## 7. 수정된 파일 목록

| 파일 | 변경 내용 |
| --- | --- |
| `src/wasm/WASMBindings.hpp` | **신규** — `wasm::setApp()` 선언 |
| `src/wasm/WASMBindings.cpp` | **신규** — `EMSCRIPTEN_BINDINGS` (9개 setter + 6개 타이밍 getter) |
| `src/main.cpp` | WASM 경로에서 `wasm::setApp(&app)` 호출 추가 |
| `src/Application.hpp` | `wasm_setXxx()` 인라인 브리지 메서드 8개 + `wasm_setPointLightCount()` 선언 + 6개 타이밍 getter + `streetLights` 멤버 |
| `src/Application.cpp` | 가로등 9개 초기화 + `wasm_setPointLightCount()` 구현 |
| `src/rendering/Renderer.hpp` | WASM 전용 타이밍 private 멤버 6개 + public getter 6개 |
| `src/rendering/Renderer.cpp` | `drawFrame()` WASM 경로 패스별 `std::chrono` 타이밍 삽입 |
| `CMakeLists.txt` | `--bind` 링크 플래그, WASMBindings 소스 추가, `postprocess.wgsl` 셰이더 복사 수정 |
| `tests/wasm_shell.html` | 디버그 컨트롤 패널 + Pass Timings 섹션 + `wireControls()` JS |
| `docs/current/README.md` | 모든 쇼케이스 작업 ✅ 완료로 업데이트 |
| `docs/current/webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md` | 상태 "Task 1–5 완료"로 업데이트 |
