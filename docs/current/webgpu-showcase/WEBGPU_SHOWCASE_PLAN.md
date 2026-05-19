# WebGPU Showcase 격상 — 계획 및 현황

**작성일**: 2026-05-14
**최종 수정일**: 2026-05-19
**상태**:

- Task 1 (Emscripten bindings) — ✅ 계획대로 완료
- Task 2 (HTML overlay UI) — ✅ 계획대로 완료
- Task 3 (CSM cascade 색상) — ⚠️ 그림자 재설계로 **대체/사실상 폐기** (§3 참조)
- Task 4 (동적 점 광원) — ✅ 완료 (단, 9개 / 계획 20–32와 다름)
- Task 5 (패스 타이밍) — ◐ **CPU 근사로 부분 완료**, 진짜 GPU timestamp-query 미완

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
| 동적 점 광원 (가로등) | ✅ | ✅ 9개 | 슬라이더 0–9 (`setPointLightCount`) |
| CSM cascade 색상 | ✅ | ⚠️ 무의미 | 단일 맵 → 전 프래그먼트 cascade 0, 화면 단색 틴트 (§3) |
| 패스 타이밍 패널 | GPU (`vkCmdWriteTimestamp`) | ◐ **CPU 근사** | `(CPU ms)` 라벨 명시, 6행 폴링 표시 |

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

1. **프레이밍 레이어** — 첫 진입 시 "브라우저에서 WebGPU로 실행되는 디퍼드
   렌더러: G-Buffer / PBR+IBL / PCSS / SSAO / Bloom, 모두 실시간" 한 문단 +
   닫기. 5초 안에 "무엇이 어려운지" 전달.
2. **가이드 투어** — `CHANGELOG_2026-05-15`의 *시연 시나리오*를 "▶ Tour"
   버튼으로 코드화: Normals → Final → SSAO off/on → 광원 0→max(타이밍 불변).
3. **진짜 GPU timestamp-query** (= 미완 Task 5) — `(CPU ms)`는 시니어가 즉시
   간파. RHI `timestampWrites` 지원 추가 → 멀티 백엔드 동일 프로파일링 메시지.
4. **디퍼드 결정적 증명을 극적으로** — 광원 9 → 수십~수백, 광원 0→다수
   전환 시 G-Buffer 패스 타이밍 평탄 유지를 핵심 장면으로.

### P1 — 격 상승

1. **A/B 분할 화면** — SSAO·점광원 유/무를 좌/우 반반으로(토글보다 압도적).
2. **비-WebGPU 폴백 영상** — Safari·모바일·구형 브라우저 대비 30초 캡처,
   README/링크드인 임베드.
3. **깊이 레이어 링크** — `CHANGELOG_2026-05-19` 디버깅 여정 + RHI
   아키텍처 문서를 데모 페이지에서 링크 (엔지니어 청중 전환).
4. **cascade 시각화 정리** — 단일 맵에서 오해 유발(§3). WebGPU 패널에서
   제거하거나 "Shadow Map / PCSS penumbra" 류 시각화로 재설계.

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
| `tests/wasm_shell.html` | `#controlPanel` 오버레이 + pending-queue/rAF + 타이밍 폴러 | 2, 5 |
| `shaders/shadow.wgsl` | `ObjectData` 128B 일치, 기하학적 지면 컬링 | (재작성) |
| `shaders/deferred_lighting.wgsl` | 단일 맵 + PCSS 재작성 | (재작성) |
| `src/rendering/ShadowRenderer.{hpp,cpp}` | CSM 폐기 → 단일 씬-고정 행렬 | (재작성) |
| `CMakeLists.txt` | `--bind`, WASMBindings 소스, `--shell-file`, WGSL/HTML `LINK_DEPENDS` | 1, 2, 환경 |
| `cmake/EmscriptenToolchain.cmake` | `WIN32`→`CMAKE_HOST_WIN32` | 환경 |
| `scripts/wasm.ps1`, `scripts/serve_nocache.py` | emcmake PATH 선행, no-cache dev 서버 | 환경 |

> 미반영(향후): 진짜 GPU `timestamp-query` — `src/rhi/.../webgpu/...`,
> `Renderer.cpp` 패스별 `timestampWrites`, `getGPUTimings()` 바인딩.
