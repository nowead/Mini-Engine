# 변경 이력 — 2026-05-20

> 작업 범위: 쇼케이스 격상 P1 마무리(깊이 레이어 링크 P1.3, A/B 분할 비교
> P1.1), 폴백 영상 P1.2 의도적 de-scope, 그리고 그 사이에 표면화된 **WebGPU
> Timer mapAsync 콜백의 간헐 ASYNCIFY 충돌** 진단 및 수정.

---

## 1. 개요

쇼케이스 격상의 P1 잔여 작업을 마치고, 그 과정에서 발견된 한 가지 진짜 버그를
잡았다. 이 문서는 결론보다 **진단 여정** 쪽에 분량을 더 둔다. 증상이 "간헐적
멈춤"이라 손에 잡히지 않고, 원인이 새 코드(`WebGPUTimer`)에 있지만 폭발 지점은
기존 코드(`WebGPURHIFence::wait`)였기 때문이다. 같은 함정에서 다시 미끄러지지
않도록 추론 경로를 그대로 남긴다.

오늘 끝낸 항목:

- **P1.3 깊이 레이어 링크** — 데모 페이지에서 디버깅 여정 / RHI 아키텍처 / 진화
  과정 문서로 직접 진입.
- **간헐 ASYNCIFY 충돌 수정** — `WebGPUTimer` mapAsync 콜백 본문에서 wgpu 함수
  호출 제거, Get/Unmap을 메인 스레드 안전 컨텍스트(`endFrame` 진입부)로 이동.
- **P1.1 A/B 분할 비교** — `uv.x` 기반 좌(베이스라인) · 우(전체 파이프라인) 동시
  비교. SSAO + 점광원만 토글, 분할선 + 플로팅 라벨.
- **P1.2 폴백 영상** — De-scope. 미디어 제작은 본 코드 작업 범위에서 분리.

---

## 2. 간헐 ASYNCIFY 충돌 — 진단 여정

### 2.1 증상

WebGPU 빌드가 페이지 로드 직후 **간헐적으로** 멈췄다. 콘솔에는 다음 순서로
로그가 찍힌다:

```text
[INFO][WorldManager] Initialization complete - 3 sectors created
Aborted(Assertion failed: Cannot have multiple async operations in flight at once)
user callback triggered after runtime exited or application aborted.  Ignoring.
21× Aborted(Assertion failed: Cannot have multiple async operations in flight at once)
18× Aborted(...)
```

새로고침하면 정상 동작. 다음 새로고침에서 다시 멈출 수도, 안 멈출 수도 있음.
즉 **타이밍 의존 버그**.

### 2.2 1차 가설들 — 모두 폐기

- "초기화 순서 문제" → 로그상 초기화는 정상 완료. 첫 프레임 진입 시점에서 폭발.
- "셰이더 컴파일 실패" → 모든 `WGSL OK` 로그가 찍힘. 셰이더 문제 아님.
- "swapchain 생성 실패" → swapchain 관련 메시지 정상.

이 단계에서 핵심 단서는 **`Cannot have multiple async operations in flight at
once`** 문자열 자체였다. 이건 Emscripten의 **ASYNCIFY** 런타임이 던지는 특정
어설션이다. ASYNCIFY는 "wasm 한 스택에 동시 진행 중인 비동기 작업은 하나"라는
불변식을 강제한다. 누군가 그 불변식을 깨고 있다.

### 2.3 의심 후보 좁히기

ASYNCIFY를 트리거할 수 있는 코드 패턴은 사실상 두 가지다:

1. `emscripten_sleep(...)` 호출 — 스택을 명시적으로 suspend
2. emdawnwebgpu 일부 함수가 내부적으로 같은 매커니즘 사용 (특히 `wgpuBufferMapAsync`
   결과를 기다리는 경로)

저장소에서 `emscripten_sleep` 사용처는 두 곳:

- `src/rhi/backends/webgpu/src/WebGPURHIDevice.cpp` — adapter/device 요청 직후의
  스핀 루프(초기화에서만)
- **`src/rhi/backends/webgpu/src/WebGPURHISync.cpp`** — `WebGPURHIFence::wait()`
  안에서 `emscripten_sleep(1)`. 이건 **매 프레임** 호출됨.

`fence->wait()`는 `RendererBridge.cpp:157`에서 매 프레임 첫머리에 불린다. 즉
WebGPU 빌드는 매 프레임 시작 시점에 ASYNCIFY suspend 한 번을 거친다. 이 구조는
이전부터 있었고 그동안 잘 돌아갔다.

그렇다면 **2026-05-19에 추가된 `WebGPUTimer`** 가 무엇을 새로 도입했는지 본다.
헤더의 알림 한 줄이 단서였다: `[WebGPUTimer] Initialized (2 frames, 10 queries/frame,
5 physical timers)`. 새 readback ring + `wgpuBufferMapAsync` + 비동기 콜백.
콜백 모드는 `WGPUCallbackMode_AllowSpontaneous`.

### 2.4 충돌 시퀀스 재구성

`AllowSpontaneous`는 JS 측 약속: "콜백은 다른 wgpu 호출 안에서도, 외부 이벤트
루프 진행 중에도, 어디서든 트리거될 수 있다." 이게 ASYNCIFY와 만났을 때 어떤
경로가 위험한지 그려본다.

steady state에서 한 프레임:

1. `fence->wait()` → `emscripten_sleep(1)` → wasm 메인 스택 **suspend**.
2. JS 이벤트 루프 진행. 이전 프레임의 `wgpuBufferMapAsync` 콜백이 펜딩 상태였다면
   브라우저가 지금 콜백을 깨움.
3. 콜백이 wasm으로 재진입 — `WebGPUTimer::onMapped` 진입.
4. `onMapped` 본문이 `wgpuBufferGetConstMappedRange(...)`, `wgpuBufferUnmap(...)`
   호출.
5. emdawnwebgpu의 이 두 함수가 내부적으로 추가 ASYNCIFY suspend를 요구하는 경로를
   탈 수 있다. **이미 메인 스택이 suspend 상태인데** 또 suspend → 어설션.

**왜 간헐적인가**: 콜백이 정확히 sleep 윈도우(1ms)와 겹쳐서 깨어나야 충돌. 콜백
타이밍은 브라우저·GPU 큐 상태·JS 이벤트 루프 일정에 따라 들쭉날쭉. 첫 프레임
직후처럼 콜백이 펜딩될 가능성이 높은 구간에서 자주 트립.

### 2.5 수정 원칙 — "콜백은 plain memory만 만진다"

JS spontaneous 콜백 안에서 wgpu 함수 호출 자체가 위험하다. **콜백 본문을
완전히 결정적(deterministic)이고 부작용이 없는 코드로 환원**한다. 실제 wgpu
호출(Get/Unmap)은 **메인 스레드의 안전 컨텍스트**로 미룬다. 안전 컨텍스트는
"다른 async op이 진행 중이지 않은 지점" — 우리 코드에선 매 프레임 `endFrame`
진입부가 가장 자연스러움(`drawFrame` 안, fence wait 이후, 새 encoder 구성 직전).

구체 변경:

- `SlotState::Mapped` 추가 — 콜백이 떨어졌고 Get/Unmap이 아직 수행되지 않은
  상태를 표현.
- `FrameSlot`에 `mapSucceeded: bool` 추가 — 콜백이 status를 단순한 plain memory로
  기록만 함.
- `WebGPUTimer::onMapped` — 콜백 본문에서 wgpu 호출 전부 제거. 두 줄로 축소:
  - `s.mapSucceeded = (status == WGPUMapAsyncStatus_Success);`
  - `s.state = SlotState::Mapped;`
- 새 `WebGPUTimer::consumeMappedSlots()` 추가 — `Mapped` 슬롯들에 대해
  `wgpuBufferGetConstMappedRange` + `wgpuBufferUnmap`을 수행하고 결과 파싱·
  `m_results` 기록 후 상태를 `Idle`로 복귀.
- `WebGPUTimer::endFrame` 진입부에서 `consumeMappedSlots()` 한 줄 호출 → 메인
  스레드 컨텍스트에서만 wgpu 호출이 일어남.
- 소멸자에서도 마지막 매핑이 남아 있을 경우를 대비해 `consumeMappedSlots()`
  호출(JS 측 ArrayBuffer 해제 보장).

빌드·실행 후 어설션 재현 안 됨. P0.3 (진짜 GPU 타이밍) 기능은 그대로 동작
— 타이밍 라벨이 `(GPU ms)`로 떠 있고 값이 갱신됨.

### 2.6 같은 함정 회피 규칙

향후 `WGPUCallbackMode_AllowSpontaneous` 콜백을 추가할 때 다음 규칙을 따른다:

1. 콜백 본문에서는 **wgpu 함수를 호출하지 않는다**. emdawnwebgpu의 어떤 함수가
   ASYNCIFY를 트리거하는지 명시되지 않으므로, 전부 위험하다고 가정.
2. 콜백은 plain memory write(필드 갱신·플래그 세팅)만 수행.
3. 실제 처리(매핑된 메모리 읽기·Unmap·dispatch 등)는 메인 루프의 알려진
   안전 지점에서 수행.
4. `AllowSpontaneous`를 굳이 쓸 이유가 없다면 `AllowProcessEvents` + 명시적
   `wgpuInstanceProcessEvents()` 폴링이 더 안전한 대안. 단 우리 경우엔 콜백
   본문을 환원하는 방식이 코드 변경이 적었고 충분.

---

## 3. P1.1 — A/B 분할 비교 구현

### 3.1 목표와 디자인 결정

`SHOWCASE_PLAN.md` P1.1: "SSAO·점광원 유/무를 좌/우 반반으로(토글보다 압도적)".
포트폴리오 메시지를 한 장 스크린샷에 박는 게 목표. 디자인 결정 두 가지를 미리
못박았다:

- **무엇을 비교?** 좌측 = baseline(SSAO off + 점광원 off) · 우측 = 전체 파이프라인.
  그림자·태양·IBL은 양쪽 동일 — "deferred 기본만 켠 상태"가 기준선. 브룸은
  명시적으로 끄지 않음(점광원이 없으면 휘도 임계 초과 픽셀이 거의 없어 자동으로
  좌측에서 약해짐 — 의도된 부수 효과).
- **UBO 확장 최소화** — 새 필드를 무자비하게 추가하면 셰이더 다섯 곳 동기·
  바인딩 사이즈·alignas 검토 등 변경 면적이 커진다. 대신 **단일 float `abSplitX`**
  로 활성 여부와 위치를 한 번에 인코딩: `0.0 = off`, `(0, 1) = 분할 위치`. 기존
  `UniformBufferObject._pad2`(이미 16B 클러스터 끝의 패드 슬롯)를 그대로 용도
  변경 → 레이아웃 비파괴.

### 3.2 변경 면적

코드 측:

- `src/utils/Vertex.hpp` — `_pad2: f32` → `abSplitX: f32`.
- `src/rendering/Renderer.{hpp,cpp}` — `abSplitX` 멤버 + `setABSplitX(float)`/
  `getABSplitX()`. 매 프레임 두 UBO(deferred lighting의 UBO, PostProcessParams)
  양쪽에 값을 전달.
- `src/rendering/Renderer.cpp` PostProcessParams — 기존 32B → **48B** 확장
  (`abSplitX: f32` + 3 pad 슬롯). BindGroup 바인딩 사이즈와 버퍼 생성 사이즈도
  같이 32 → 48로 갱신. 세 곳을 동시에 맞추지 않으면 WebGPU validation error.

셰이더 측:

- `shaders/deferred_lighting.wgsl` — UBO의 `_pad2: f32` → `abSplitX: f32`로 이름·
  의미 일치화. 프래그먼트에서 `let onBaselineSide = abActive && uv.x < ubo.abSplitX;`
  계산 후 **점광원 루프 전체를 if 가드로 감쌈**(baseline 측은 점광원 0).
- `shaders/building.wgsl` — 같은 UBO를 공유하므로 `_pad2: f32` → `abSplitX: f32`
  이름만 동기.
- `shaders/postprocess.wgsl` — `PostProcessParams`에 `abSplitX` + 3 pad 추가.
  베이스라인 측에서 `aoStrengthEff = 0.0`으로 강제(SSAO off). 추가로
  `applyDivider(uv, color)` 헬퍼를 만들어 모든 return 경로(debug view ssao, debug
  view bloom, FXAA disabled early-out, FXAA no-edge early-out, FXAA final) 다섯
  곳을 감쌈 → `abSplitX` 위치에 1.5px 흰 분할선 오버레이.

JS 브릿지·UI 측:

- `src/Application.hpp` — `wasm_setABSplitX(float)` 인라인 브릿지.
- `src/wasm/WASMBindings.cpp` — `js_setABSplitX` + Emscripten binding 등록.
- `tests/wasm_shell.html` — "A/B Compare" 섹션: 토글 + 분할 위치 슬라이더
  (0.05~0.95, 기본 0.5). 토글 off → 슬라이더 disabled + `setABSplitX(0.0)` 전송.
  토글 on → `setABSplitX(현재 슬라이더값)` 전송 + **"Baseline" / "Full pipeline"
  플로팅 라벨**을 캔버스 상단에 표시(`top: 16px`, 분할 위치에 따라 `left: 25%`
  / `left: 75%` 자동 계산). `.ab-label*` CSS는 backdrop-blur + 반투명 카드 스타일.

### 3.3 한계·트레이드오프

- 분할 좌측의 점광원 가시화 비용은 **drop된다**(루프 자체가 건너뛰어짐). 즉
  G-Buffer 패스에는 부담을 거의 안 주지만 deferred lighting 단계의 비용은 좌측
  픽셀에서 줄어든다. "동일 GPU 비용 하에 시각 차이" 시연이 아니라 "같은 씬에서
  보는 시각적 기여도 차이" 시연이라는 점은 명확.
- 셰이더 분기가 추가됨. 좌·우 픽셀이 한 워프 안에 같이 있으면 wave divergence
  발생. 분할선 근방 ~1 워프 폭 정도는 양쪽 경로를 다 도는 효과가 있을 텐데,
  실제 프레임 시간 영향은 측정 범위에서 무시 가능(분할선이 그어진 한 칼럼
  근방만 영향). 측정값은 P0.3 GPU 타이머가 그대로 보여준다.

---

## 4. P1.3 — 깊이 레이어 링크

목적: 데모는 "엔진 개발자용 디버그 패널"에 머무를 위험이 있는데, 이미 만들어 둔
기술 자료(EVOLUTION, RHI 기술 가이드, 2026-05-19 셰도우 디버깅 여정)가 데모
페이지에서 직접 진입 불가했다. 외부 청중(특히 시니어 그래픽스 엔지니어)의 깊이
검증 경로를 짧게 만든다.

변경:

- `tests/wasm_index.html`(랜딩) — 컴포넌트 데모 그리드 아래에 **"How It Was
  Built"** 섹션 신설. 좌측 강조선 + hover 시 `translateX(2px)` 마이크로
  인터랙션이 들어간 가로형 카드 3개:
  - Architecture Evolution → `docs/EVOLUTION.md`
  - RHI Architecture → `docs/archive/refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md`
  - Debugging the Shadow Rewrite → `docs/archive/changelogs/CHANGELOG_2026-05-19.md`
  
  미디어 쿼리 `≤560px`에서 카드가 세로 스택으로 전환. `.section-label.spaced`
  CSS 유틸로 섹션 간 여백을 인라인 style 없이 처리. footer GitHub 사용자명
  오타(`mindaewon` → `nowead`)도 같이 정정.

- `tests/wasm_shell.html`(데모 셸의 인트로 모달) — 버튼 아래 구분선 + 동일 3개
  링크를 작은 인라인 텍스트로 표시. 토스트/투어 등 메인 UX 흐름은 손대지 않음.
  `.intro-links` 스타일 추가.

청중 차별 의도가 명확해진다: 처음 진입한 리크루터는 인트로 모달의 "Guided Tour"
버튼으로, 깊이 보려는 엔지니어는 같은 모달 하단의 링크나 랜딩의 카드로 즉시
들어감.

---

## 5. P1.2 — 폴백 영상 De-scope

이전 계획서: "Safari·모바일·구형 브라우저 대비 30초 캡처, README/링크드인
임베드." 본질적으로 **미디어 제작**이라 본 코드/문서 작업 범위에서 분리.
필요 시 차후 별도 작업으로 재검토.

(스캐폴드 안건 — WebGPU 검출 + 폴백 패널 UI를 미리 만들고 영상만 드롭하면
즉시 작동하는 형태 — 도 후보였으나, 영상 없이 폴백 UI만 두면 빈 컨테이너로
오히려 인상 손해. 영상이 준비됐을 때 한 번에 구현하는 게 깔끔.)

---

## 6. 수정 파일 요약

| 파일 | 변경 |
| --- | --- |
| `src/utils/WebGPUTimer.{hpp,cpp}` | ASYNCIFY 충돌 수정: `SlotState::Mapped` + `mapSucceeded` 추가, 콜백을 plain memory write로 환원, Get/Unmap을 `consumeMappedSlots()`로 분리해 `endFrame` 진입부와 소멸자에서 호출 |
| `src/utils/Vertex.hpp` | `UniformBufferObject._pad2` → `abSplitX`(의미 변경, 레이아웃 동일) |
| `src/rendering/Renderer.{hpp,cpp}` | `abSplitX` 멤버 + `setABSplitX`/`getABSplitX`; 매 프레임 deferred UBO·PostProcessParams 양쪽에 전달; PostProcessParams 32B→48B 확장(필드 추가 + BindGroup binding size + 버퍼 생성 size 동기) |
| `shaders/deferred_lighting.wgsl` | `_pad2`→`abSplitX`; 좌측 분할 영역에서 점광원 루프 가드 |
| `shaders/building.wgsl` | UBO 동기를 위해 `_pad2`→`abSplitX` |
| `shaders/postprocess.wgsl` | `PostProcessParams`에 `abSplitX` + 3 pad 추가; 좌측에서 `aoStrength=0` 강제; `applyDivider()` 헬퍼 + 5개 return 경로에서 1.5px 흰 분할선 |
| `src/Application.hpp`, `src/wasm/WASMBindings.cpp` | `wasm_setABSplitX` / `setABSplitX` 노출 |
| `tests/wasm_shell.html` | "A/B Compare" 섹션(토글 + 슬라이더) · "Baseline"/"Full pipeline" 플로팅 라벨 · `.ab-label*` 스타일; 인트로 모달 하단에 "How it was built:" 3개 링크 + `.intro-links` 스타일 |
| `tests/wasm_index.html` | "How It Was Built" 섹션 + 3개 writeup 카드 + `.writeup*` / `.section-label.spaced` CSS; footer GitHub 사용자명 오타 정정 |
| `docs/current/webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md` | P1.3/P1.1 완료 표시, P1.2 De-scope 명시, 파일 표 갱신; 라인 36 코드 블록에 `text` 언어 태그 |

---

## 7. 후속

쇼케이스 격상 작업은 사실상 종결. 남은 미반영 항목:

- **P0.3 후속(multi-pass phase 정확한 timing)** — SSAO 2 패스 / Bloom 5 패스의
  첫 sub-pass만 측정되는 현재 동작은 시니어 그래픽스 면접에서 명시적으로
  지적될 때 잡는다는 방침(우선순위 낮음). 설계 메모는 `WEBGPU_SHOWCASE_PLAN.md`
  §5 P0.3 "알려진 한계" 블록에 남아 있음.

본 계획서는 같은 세션 내에서 `docs/current/webgpu-showcase/` →
`docs/archive/refactoring/webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md`로 이동
완료. `docs/current/README.md`와 `docs/README.md`도 같이 갱신(활성 작업 없음
상태로 정리, archive 트리 항목 추가).
