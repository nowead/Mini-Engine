# WebGPU Showcase 격상 — 계획 및 현황

**작성일**: 2026-05-14
**최종 수정일**: 2026-05-19
**상태**:

- Task 1 (Emscripten bindings) — ✅ 계획대로 완료
- Task 2 (HTML overlay UI) — ✅ 계획대로 완료
- Task 3 (CSM cascade 색상) — ⚠️ 그림자 재설계로 **대체/사실상 폐기** (§3 참조)
- Task 4 (동적 점 광원) — ✅ 완료 (단, 9개 / 계획 20–32와 다름)
- Task 5 (패스 타이밍) — ◐ **CPU 근사로 부분 완료**, 진짜 GPU timestamp-query 미완
- §5 P0.1 (프레이밍 레이어) — ✅ 완료 (2026-05-19)
- §5 P0.2 (가이드 투어) — ✅ 완료 (2026-05-19)
- §5 P0.4 (광원 9 → 100) — ✅ 완료 (2026-05-19)
- §5 P1.4 (cascade 시각화 정리) — ✅ 완료 (2026-05-19)
- §5 P0.3 (진짜 GPU timestamp-query) — ✅ 완료 (2026-05-19)
- §5 P1.1–1.3 — ⬜ 미착수

> 이 문서는 원래 "구현 계획"이었으나, 구현 과정에서 그림자 파이프라인이 전면
> 재작성되는 등 계획과 어긋난 부분이 많아 **현황 기준으로 최신화**했다.
> 상세 변경 이력: [CHANGELOG_2026-05-15](../../archive/changelogs/CHANGELOG_2026-05-15.md)
> (쇼케이스 격상) · [CHANGELOG_2026-05-19](../../archive/changelogs/CHANGELOG_2026-05-19.md)
> (그림자 재작성 + 빌드 환경 수정).

---

## 1. 목표

WebGPU/WASM 빌드의 렌더링 품질은 네이티브(Vulkan)와 동등하다. 네이티브가
ImGui 패널로 제공하는 **인터랙티브 제어 기능**을 브라우저에서도 동등하게
제공하고, 나아가 이 라이브 데모를 **포트폴리오 수준**으로 격상한다.

전략:

```
네이티브:  Renderer ←→ ImGuiManager (C++)
WebGPU:   Renderer ←→ Application::wasm_* 브리지 ←→ Emscripten bindings ←→ HTML/CSS overlay (JS)
```

브라우저 DOM을 UI 레이어로 사용한다. `EMSCRIPTEN_BINDINGS`로 C++ 함수를
JavaScript에 노출하고, `tests/wasm_shell.html`의 오버레이 패널에서 호출한다.

---

## 2. 현재 구현 상태 (사실 기준)

### 렌더링 품질 — 네이티브와 동등

| 기능 | 네이티브 | WebGPU |
| --- | --- | --- |
| G-Buffer Deferred Rendering | ✅ | ✅ |
| PBR + IBL | ✅ | ✅ |
| 그림자 | 4-Cascade CSM | **단일 씬-고정 맵 + PCSS** (§3) |
| SSAO + bilateral blur | ✅ | ✅ |
| Bloom (prefilter + Gaussian) | ✅ | ✅ |
| ACES tonemap + FXAA (unified postprocess) | ✅ | ✅ |
| GPU frustum culling | ✅ | ✅ |

### 쇼케이스/인터랙션 — 구현됨

| 기능 | 네이티브 | WebGPU | 비고 |
| --- | --- | --- | --- |
| 전체 UI 패널 | ImGui | ✅ HTML overlay | `tests/wasm_shell.html` `#controlPanel` |
| Debug view | ✅ | ✅ 라디오 9개 | Normal/Normals/Albedo/Metallic/Roughness/AO/Depth/SSAO/Bloom |
| Post-process 토글/슬라이더 | ✅ | ✅ | Tonemap·FXAA 체크 + Exposure/Bloom/SSAO 슬라이더 |
| 조명 제어 | ✅ | ✅ | Sun Intensity 슬라이더 |
| 동적 점 광원 (가로등) | ✅ | ✅ 100개 | 슬라이더 0–100 (`setPointLightCount`), Tier1 iconic 9 + Tier2 colored 91 |
| CSM cascade 색상 | ✅ | ❌ 제거됨 | 단일 맵에서 오해 유발하여 UI에서 제거 (2026-05-19, §5 P1.4) |
| 패스 타이밍 패널 | GPU (`vkCmdWriteTimestamp`) | ✅ **진짜 GPU** | `timestamp-query` feature, 단일 패스 phase는 정확 · multi-pass(SSAO/Bloom)는 첫 sub-pass만 · Frame = PostProcess_end − GBuffer_begin · 라벨 자동 `(GPU ms)` / fallback `(CPU ms)` |
| 인트로 프레이밍 | — | ✅ 모달 | 로딩 후 자동 표시, 기술 설명 + 6개 기능 태그 + Tour/Explore 버튼 |
| 가이드 투어 | — | ✅ 9단계 | 자동 시퀀스 + 토스트 UI, 패널 헤더 ▶ Tour 버튼으로 재실행 |
| 랜딩 페이지 | — | ✅ | `localhost:8000/` → 데모 인덱스 (메인 + 4개 컴포넌트 데모) |

### 핵심 관찰

렌더러 제어 setter는 이미 `Renderer.hpp`에 구현돼 있었고, 이번 작업은
JS→C++ **연결 경로**(바인딩 + 브리지 + HTML)를 만든 것이다.
바인딩은 계획의 자유함수 `g_app_*`가 아니라 `wasm::js_*` →
`Application::wasm_*` → `Renderer::setXxx` 의 3단 브리지로 구현됐다.

---

## 3. 계획 이후 주요 변경 (2026-05-19) — 그림자 전면 재작성

Task 3은 **4-cascade CSM 전제**로 작성됐으나, WebGPU 그림자가 지속적으로
깨져 근본 원인을 추적한 결과 다음이 확정·교체됐다 (상세:
[CHANGELOG_2026-05-19](../../archive/changelogs/CHANGELOG_2026-05-19.md)):

- **진짜 버그**: `shadow.wgsl`의 `ObjectData`만 144B(타 셰이더·C++는 128B)로
  SSBO 스트라이드가 어긋나 셰도우 패스가 쓰레기 지오메트리를 기록.
  → `texParams` 제거, 128B로 일치.
- **CSM 폐기 → 단일 씬-고정 맵 + PCSS**: 작은 고정 쇼케이스 씬에 4-cascade
  CSM은 과설계. 카메라 무관 단일 행렬을 4 UBO 슬롯에 복제, `cascadeSplits`를
  큰 값으로 둬 셰이더 cascade 선택이 항상 슬롯 0으로 귀결. 소프트 섀도우는
  PCSS(blocker search → penumbra → 가변 Poisson PCF).
- **빌드 환경 결함 4종 수정** (재발 방지): WGSL/셸 HTML `LINK_DEPENDS` 등록,
  `EmscriptenToolchain.cmake` `WIN32`→`CMAKE_HOST_WIN32`, `wasm.ps1` emcmake
  PATH 선행, `serve_nocache.py` 추가.

**Task 3에 대한 함의**: cascade 색상 오버레이 코드는 `deferred_lighting.wgsl`에
남아 있으나, 단일 맵에서는 전 프래그먼트가 cascade 0으로 귀결돼 "CSM Cascades"
체크 시 거리 밴드가 아니라 **화면 전체가 빨강 단색으로 틴트**된다. 현재
오해를 부르는 상태이므로 §5 P1에서 정리 대상.

---

## 4. Task별 계획 대비 실제 결과

### Task 1 — Emscripten Bindings — ✅ 완료

`src/wasm/WASMBindings.{hpp,cpp}` 신규. `EMSCRIPTEN_BINDINGS(mini_engine)`에
9개 setter + 6개 타이밍 getter 노출. `src/main.cpp`가 `wasm::setApp(&app)` 호출,
`Application::wasm_*` 인라인 브리지가 `renderer` null 체크 후 위임.
계획과 차이: 자유함수 `g_app_*` 대신 `Application` 경유 브리지. `setPointLights`는
`setPointLightCount(int)` 형태로 노출.

### Task 2 — HTML Overlay UI — ✅ 완료

`tests/wasm_shell.html`(계획의 `src/wasm/shell.html` 아님)에 `#controlPanel`
오버레이. CMake `--shell-file`로 지정. ASYNCIFY 충돌(`multiple async operations`)
회피를 위해 **pending 값 큐 + `requestAnimationFrame` flush** 패턴 추가
(계획에 없던 사항). 슬라이더 초기값은 `Renderer.hpp` 기본값과 일치.

### Task 3 — CSM Cascade 색상 — ⚠️ 대체/폐기

§3 참조. 셰이더·UBO 경로는 기구현 상태였으나 그림자 재설계로 단일 맵이 되어
cascade 시각화가 무의미해짐. WebGPU 패널에서 제거 또는 재설계 필요(§5 P1).

### Task 4 — 동적 점 광원 — ✅ 완료 (수량 차이)

`Application::initGameLogic()` `#ifdef __EMSCRIPTEN__` 블록에서 건물 격자
교차점에 가로등 배치. **계획 20–32 → 실제 9개**(gridSize 4 → 3×3 교차점,
intensity 3.0, radius 25, 따뜻한 주황). 슬라이더 0–9로 `setPointLightCount`
실시간 제어. 디퍼드의 다중 광원 이점 시연용.

### Task 5 — 패스 타이밍 — ◐ CPU 근사로 부분 완료

`timestamp-query`는 WebGPU optional + RHI 인터페이스 변경 필요로 보류.
대신 `drawFrame()` WASM 경로 각 패스 전후에 `std::chrono::high_resolution_clock`
삽입 → **CPU 명령 기록 시간** 측정, 패널에 `(CPU ms)` 라벨로 측정 방식 명시.
G-Buffer/Deferred/SSAO/Bloom/PostProcess/Total 6행, JS `setInterval` 500ms 폴링.

> **미완**: 진짜 GPU per-pass `timestamp-query`. 신뢰성 격상의 최대 레버 — §5 P0.

---

## 5. 다음 단계: 포트폴리오 수준 격상 (권장)

현재 데모는 "엔진 개발자용 디버그 패널"이다. 라이브 WebGPU 데모라는 형태
자체는 그래픽스 포트폴리오로 최적(클릭 한 번 실행 + 디버그 뷰로 구현 검증
가능). 남은 작업은 **노브 추가가 아니라 내러티브와 신뢰성**이다.

청중을 둘로 나눠 같은 데모에 다른 레이어를 입힌다:

| 청중 | 필요 | 대응 |
| --- | --- | --- |
| 리크루터·채용 매니저 | 즉각 임팩트 + 평문 설명 | 인트로 + 가이드 투어 |
| 시니어 그래픽스 엔지니어 | 깊이 검증 + 숫자 신뢰 | 디버그 뷰(완) + **GPU 타이밍** + 아키텍처/디버깅 글 |

### P0 — 없으면 포트폴리오가 아님

1. ✅ **프레이밍 레이어** (2026-05-19) — 로딩 완료 후 인트로 모달이 자동 표시.
   "Mini-Engine / WebGPU · WebAssembly · Real-time" 헤더 + 기술 설명 1단락 +
   6개 기능 태그(G-Buffer Deferred, PBR + IBL, PCSS, SSAO, Bloom + FXAA,
   GPU Frustum Culling) + **▶ Guided Tour** / **Explore** 두 버튼. ESC로 닫기.
2. ✅ **가이드 투어** (2026-05-19) — 9단계 자동 시퀀스. 단계마다 하단 토스트에
   번호/제목/설명 표시, 진행바 포함. 인트로 모달의 **▶ Guided Tour** 또는
   패널 헤더의 **▶ Tour** 버튼으로 시작. 단계: G-Buffer Normals → Albedo →
   SSAO Mask → SSAO off → SSAO on → 0 lights → 9 lights → Bloom Mask →
   Full Pipeline. 각 단계에서 디버그 뷰·슬라이더가 실제로 변경되며 패널 UI도
   동기화. **✕ Skip** 또는 ESC로 중단.
3. ✅ **진짜 GPU timestamp-query** (2026-05-19) — `timestamp-query` feature를
   어댑터 조건부로 요청, `src/utils/WebGPUTimer.{hpp,cpp}` 신설(`WGPUQuerySet` +
   resolve buffer + 3-frame readback ring + async map state machine).
   `WebGPURHICommandEncoder::setPendingTimestamps()` state setter 추가 →
   `beginRenderPass`/`beginComputePass`가 다음 pass에 `timestampWrites` 주입.
   Renderer WASM 경로의 `chrono` 측정 옆에 `m_webgpuTimer->beginPhase()` 호출
   삽입(각 phase 시작 직전). WASM 바인딩 `isGPUTimingAvailable()` 추가,
   HTML이 첫 폴링 시 자동으로 `(CPU ms)` → `(GPU ms)` 라벨 전환.

   **알려진 한계** (P1 후속 작업으로 분리):
   - SSAO(2 패스) / Bloom(5 패스)는 첫 sub-pass만 측정 — blur 시간 누락
   - Frame total은 GBuffer_begin → PostProcess_end 으로 계산 (정확)
   - 향후 multi-pass timing 정확성 격상: phase별 4-슬롯 + "마지막 sub-pass" 표식 API
4. ✅ **디퍼드 결정적 증명을 극적으로** (2026-05-19) — `MAX_POINT_LIGHTS` 32 → 128
   상향, 가로등 9 → 100으로 확장 (Tier1: 기존 iconic 교차로 9개 / Tier2: 10×10
   격자에 5색 팔레트 순환하는 colored accent 91개). 슬라이더 max 9 → 100, 기본값
   9 유지 (초기 화면 보존). 투어 step 7을 "9 lights" → "100 lights"로 재작성 —
   광원 0 → 100 전환에서 G-Buffer 패스 타이밍 평탄 유지가 핵심 시연.

### P1 — 격 상승

1. ⬜ **A/B 분할 화면** — SSAO·점광원 유/무를 좌/우 반반으로(토글보다 압도적).
2. ⬜ **비-WebGPU 폴백 영상** — Safari·모바일·구형 브라우저 대비 30초 캡처,
   README/링크드인 임베드.
3. ⬜ **깊이 레이어 링크** — `CHANGELOG_2026-05-19` 디버깅 여정 + RHI
   아키텍처 문서를 데모 페이지에서 링크 (엔지니어 청중 전환).
4. ✅ **cascade 시각화 정리** (2026-05-19) — `wasm_shell.html`의 "Debug" 섹션
   및 "CSM Cascades" 체크박스 제거(`setDebugCascades` 호출 경로 미사용). 단일
   맵 환경에서 전 화면 빨간 틴트 → 오해 유발 해소. C++ 바인딩은 그대로 유지
   (향후 "Shadow Map / PCSS penumbra" 류로 재설계 시 재사용 가능).

### 보너스 (2026-05-19, 계획에 없던 작업)

- ✅ **랜딩 페이지** — `localhost:8000/`이 데모 인덱스로 자동 연결.
  `tests/wasm_index.html` 신규 + CMake `POST_BUILD`로 `build_wasm/index.html`
  복사. 메인 카드(WebGPU Deferred Renderer) + 4개 컴포넌트 데모(PBR /
  Dual Light / Instancing / RHI Smoke Test) 그리드. `make serve-wasm`이
  `serve_nocache.py` 사용하도록 변경(캐싱 이슈 동시 해소).

### Non-goal

디버그 토글 추가. 패널은 이미 충분히 깊다. 가치는 내러티브·신뢰성에 있다.

---

## 6. 수정 파일 요약 (현행)

| 파일 | 작업 | Task |
| --- | --- | --- |
| `src/wasm/WASMBindings.hpp` | 신규 — `wasm::setApp()` 선언 | 1 |
| `src/wasm/WASMBindings.cpp` | 신규 — `EMSCRIPTEN_BINDINGS` 9 setter + 6 타이밍 getter | 1, 4, 5 |
| `src/main.cpp` | WASM 경로 `wasm::setApp(&app)` | 1 |
| `src/Application.{hpp,cpp}` | `wasm_*` 브리지 + 가로등 9개 초기화 + `wasm_setPointLightCount` | 1, 4 |
| `src/rendering/Renderer.{hpp,cpp}` | WASM 패스별 `std::chrono` 타이밍 + getter | 5 |
| `tests/wasm_shell.html` | `#controlPanel` 오버레이 + pending-queue/rAF + 타이밍 폴러; **인트로 모달 + 가이드 투어 (9단계) + cascade 디버그 섹션 제거 + 슬라이더 max 9 → 100 + 투어 step 7 "100 lights"** (2026-05-19) | 2, 5, P0.1, P0.2, P0.4, P1.4 |
| `src/utils/Vertex.hpp` | **`MAX_POINT_LIGHTS` 32 → 128** (2026-05-19) | P0.4 |
| `src/Application.cpp` | **광원 생성 Tier1(9 iconic) + Tier2(91 colored accent) = 100개로 확장**; `wasm_isGPUTimingAvailable` 브리지 추가 (2026-05-19) | P0.4, P0.3 |
| `shaders/deferred_lighting.wgsl`, `shaders/deferred_lighting.frag.glsl`, `shaders/building.wgsl` | **`array<PointLight, 32>` → `array<PointLight, 128>` (UBO offset 동기화)** (2026-05-19) | P0.4 |
| `src/utils/WebGPUTimer.{hpp,cpp}` | **신규** — QuerySet + resolve + 3-frame readback ring + state machine + virtual Frame timer (2026-05-19) | P0.3 |
| `src/rhi/backends/webgpu/include/rhi/webgpu/WebGPURHICommandEncoder.hpp` | `getWGPUEncoder()` + `setPendingTimestamps()` state setter (2026-05-19) | P0.3 |
| `src/rhi/backends/webgpu/src/WebGPURHICommandEncoder.cpp` | `beginRenderPass`/`beginComputePass`에서 pending state 소비 → `timestampWrites` 주입 (2026-05-19) | P0.3 |
| `src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp` | 어댑터 지원 시 `WGPUFeatureName_TimestampQuery` 요청 (2026-05-19) | P0.3 |
| `src/rhi/backends/webgpu/src/WebGPURHICapabilities.cpp` | `m_features.timestampQuery = wgpuDeviceHasFeature(...)` 동적 검출 (2026-05-19) | P0.3 |
| `src/rendering/Renderer.{hpp,cpp}` | `m_webgpuTimer` 멤버; getter들이 GPU/CPU 중 적절한 값 반환; WASM 경로에 `beginPhase()` 5회 + `endFrame()` 호출 (2026-05-19) | P0.3 |
| `src/wasm/WASMBindings.cpp` | `isGPUTimingAvailable` 노출 (2026-05-19) | P0.3 |
| `tests/wasm_shell.html` | 패스 타이밍 라벨 ID 추가 + 폴러가 `Module.isGPUTimingAvailable()` 첫 호출로 `(GPU ms)` / `(CPU ms)` 자동 전환 (2026-05-19) | P0.3 |
| `CMakeLists.txt` | WASM 타겟 소스에 `WebGPUTimer.{hpp,cpp}` 추가 (2026-05-19) | P0.3 |
| `tests/wasm_index.html` | **신규** — 랜딩 페이지 (메인 카드 + 4개 컴포넌트 데모 그리드) (2026-05-19) | 보너스 |
| `shaders/shadow.wgsl` | `ObjectData` 128B 일치, 기하학적 지면 컬링 | (재작성) |
| `shaders/deferred_lighting.wgsl` | 단일 맵 + PCSS 재작성 | (재작성) |
| `src/rendering/ShadowRenderer.{hpp,cpp}` | CSM 폐기 → 단일 씬-고정 행렬 | (재작성) |
| `CMakeLists.txt` | `--bind`, WASMBindings 소스, `--shell-file`, WGSL/HTML `LINK_DEPENDS`; **`POST_BUILD`로 `wasm_index.html` → `build_wasm/index.html` 복사** (2026-05-19) | 1, 2, 환경, 보너스 |
| `Makefile` | `serve-wasm` 대상이 `serve_nocache.py` 사용 (2026-05-19) | 보너스 |
| `cmake/EmscriptenToolchain.cmake` | `WIN32`→`CMAKE_HOST_WIN32` | 환경 |
| `scripts/wasm.ps1`, `scripts/serve_nocache.py` | emcmake PATH 선행, no-cache dev 서버 | 환경 |

> 미반영(향후 우선순위 순): A/B 분할 화면 (P1.1) · 폴백 영상 (P1.2) ·
> 깊이 레이어 링크 (P1.3) · multi-pass phase 정확한 timing (P0.3 후속).
