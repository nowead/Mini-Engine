# 변경 이력 — 2026-05-27

> 작업 범위: ENGINE_ROADMAP §D — **멀티스레드 커맨드 레코딩**(Vulkan 전용)의
> **D2 — 워커 스레드 풀**. D1(per-thread `VkCommandPool`, 커밋 `a4b933f`)에
> 이어, 의존성 없는 패스를 병렬 기록(D3)하기 위한 스레드 풀 인프라를 얹는다.
> 이 문서는 풀 자체의 설계 + 검증 중 잡은 동시성 버그 2종의 디버깅 여정을 다룬다.

---

## 1. 구성 — 독립 인프라

| 단계 | 내용 | 상태 |
| --- | --- | --- |
| D2-a | `utils::ThreadPool` (`std::jthread` 워커 + 작업 제출/대기) | ✅ 이 커밋 |
| D2-b | `threadpool_test` 독립 검증 (정확성·병렬성) | ✅ 이 커밋 |

D2 시점에는 **렌더 루프가 풀을 호출하지 않는다** — 순수 인프라이므로 단일 스레드
경로는 불변. 배선(RenderGraph 레벨 dispatch)은 D3. Vulkan 전용 작업이라 네이티브
`MiniEngine` 타깃에만 등록(WebGPU/WASM은 멀티스레드 명령 기록 미지원).

---

## 2. 설계 — 풀

- **워커**: `std::jthread` 벡터. 기본 개수 `max(1, hardware_concurrency() - 1)`
  (제출하는 메인 스레드에 1코어 양보).
- **작업 큐**: mutex로 보호되는 `std::queue<std::function<void()>>` + **순수
  `std::condition_variable` + 명시적 `m_stopping` 플래그**. (`condition_variable_any`
  + `stop_token` 대기는 의도적으로 회피 — §3.1 참조. `jthread`는 auto-join
  용도로만 사용.)
- **제출**: `submit(F, Args...) → std::future<R>`. `std::packaged_task`로 감싸
  예외가 호출자의 `future.get()`으로 전파됨(렌더 핫루프 밖 인프라라 예외 허용).
- **일괄 대기**: `waitForAll()` — 진행 중 + 대기 중 작업이 모두 끝날 때까지 블록
  (atomic 대신 mutex 하 `m_outstanding` 카운터 + cv). D3의 **의존성 레벨 배리어**가
  될 인터페이스.
- **종료 시 drain**: `m_stopping`이 큐를 버리지 않음. 워커는 큐가 빌 때까지 남은
  작업을 모두 처리한 뒤 종료 — 프레임 스코프 dispatch에 안전.

---

## 3. 디버깅 여정 — 통과한 줄 알았던 풀

D2-b 테스트(future 결과 / 동시 실행 / 예외 전파 / `waitForAll` / 종료 drain)를
작성해 돌리자 두 개의 동시성 버그가 순서대로 드러났다. 둘 다 단일 스레드에선
절대 안 보이는 종류다.

### 3.1 종료 데드락 — `condition_variable_any` + `stop_token`

첫 구현은 워커 대기에 C++20의 `std::condition_variable_any::wait(lock, stop_token,
pred)` (stop-aware 오버로드)를 썼다. 깔끔해 보였지만 — **첫 테스트(`testFutureResults`)
가 끝나고 풀 소멸자에서 멈췄다.** 출력은 `[ok] sum of squares matches`까지만
찍히고 프로세스가 영구 대기(kill 필요).

- 증상 진단이 까다로웠던 이유: 출력이 파일로 리다이렉트되면 stdout이 **full-buffered**
  라, 데드락 시점까지의 출력이 flush되지 않아 화면이 비어 보였다. `setvbuf(stdout,
  nullptr, _IONBF, 0)`로 무버퍼링하고서야 "어디까지 갔는지"가 보였다.
- 원인: MSVC STL에서 `condition_variable_any` + `stop_token` 대기는 종료 경로에서
  데드락한 전례가 있다. stop_callback이 내부 CV를 깨우는 메커니즘과 소멸 타이밍이
  얽힌다.
- 수정: 고전 패턴으로 교체 — 순수 `std::condition_variable` + mutex 하 `bool
  m_stopping`. 워커는 `wait(lock, []{ return m_stopping || !m_tasks.empty(); })`.
  소멸자가 `m_stopping=true` + `notify_all()`. `stop_token`을 대기에서 완전히
  제거(`jthread`는 join 편의 때문에 유지). 데드락 소멸.

### 3.2 멤버 소멸 순서 레이스 — drain이 큐를 버린다

데드락이 풀린 뒤 4/5 통과, 마지막 `testShutdownDrains`만 실패했다. 200개를
제출하고 `waitForAll` 없이 풀을 파괴하면 소멸자가 큐를 drain해야 하는데 —
**매번 정확히 "200개 중 2개"만 처리됐다.** 2 = 워커 수. 이 "워커 수만큼만"이라는
숫자가 결정적 단서였다.

소멸자에 진단을 넣자 소멸 시점에 **큐에 199개가 분명히 남아 있었다**(`queue size
= 199, outstanding = 200`). 큐는 비어 있지 않은데 워커는 1개씩만 처리하고 조기
종료한 것.

원인은 **멤버 소멸 순서**였다. 헤더 선언 순서가:

```
std::vector<std::jthread> m_workers;   // 가장 먼저 선언
std::queue<...>           m_tasks;
std::mutex                m_mutex;
std::condition_variable   m_taskAvailable;
...
```

C++ 멤버는 **선언 역순으로 소멸**한다. `m_workers`가 맨 먼저 선언됐으니 **가장
나중에 소멸**(= jthread join이 가장 늦게 일어남). 그래서 소멸 순서가:

1. `~ThreadPool` 본문 (`m_stopping=true`, `notify_all`)
2. ... `m_mutex` 소멸 ← **mutex 파괴**
3. `m_tasks` 소멸 ← **큐 파괴**
4. `m_workers` 소멸 ← jthread join (워커가 **아직 돌면서** 죽은 mutex/큐를 사용!)

즉 워커가 큐를 drain하는 도중에 큐와 뮤텍스가 발밑에서 파괴됐다. 워커는 파괴된
(빈) 큐를 보고 `if (m_tasks.empty()) return;`으로 조기 종료 → 197개 유실. UB.

- 수정: **소멸자 본문에서 멤버 파괴 전에 워커를 명시적 join**.

  ```cpp
  ThreadPool::~ThreadPool() {
      { std::lock_guard lock(m_mutex); m_stopping = true; }
      m_taskAvailable.notify_all();
      for (std::jthread& w : m_workers)
          if (w.joinable()) w.join();   // mutex/큐가 살아 있는 동안 join
  }
  ```

  이러면 워커가 큐를 끝까지 drain하고 종료한 **뒤**에 멤버가 파괴된다.

> **D3로의 함의**: 3.2는 정확히 §D의 D3 장애물 중 하나(패스별 CB 수명 — 프레임
> 풀 reset)와 같은 부류의 "**동시 실행 중인 자원의 수명**" 문제다. D3에서 워커가
> primary CB에 기록하는 동안 메인이 풀을 reset/제출하는 순서를 동일하게 조심해야
> 한다.

---

## 4. 검증

`threadpool_test` — 순수 std 라이브러리 테스트(Vulkan/GLFW 의존 없음):

1. `submit` 결과가 `std::future`로 회수됨 (제곱합 100개).
2. 작업이 실제 병렬 실행(워커 수만큼의 블로킹 작업이 모두 배리어 도달 + 별개
   스레드 id).
3. 작업 내 예외가 `future.get()`으로 전파.
4. `waitForAll`이 배리어로 동작(모든 작업 완료 관측) + 배리어 후 풀 재사용 가능.
5. 종료 시 큐 drain(명시적 `waitForAll` 없이도 소멸자가 전부 처리).

5회 연속 실행 모두 5/5 결정적 통과(24코어 → 워커 23). `MiniEngine` 네이티브
빌드도 통과(풀은 아직 미호출이라 렌더 무회귀).

---

## 5. 수정 파일 요약

| 파일 | 변경 |
| --- | --- |
| `src/utils/ThreadPool.hpp` | **신규** — 풀 인터페이스 + `submit` 템플릿(packaged_task) |
| `src/utils/ThreadPool.cpp` | **신규** — 워커 루프(plain CV + `m_stopping`), drain, `waitForAll`, 소멸자 명시적 join |
| `tests/threadpool_test.cpp` | **신규** — 5종 정확성·병렬성 테스트 |
| `CMakeLists.txt` | 네이티브 `MiniEngine`에 ThreadPool 소스 + `threadpool_test` 실행 타깃(native-only) |

---

## 6. 다음 — D3

D3: RenderGraph 병렬 스케줄러. 정렬된 패스를 의존성 레벨로 그룹화 → 같은 레벨
독립 패스를 워커가 각자 primary CB에 기록 → 메인이 레벨 순서대로 제출. 착수 전
§D에 적힌 장애물 둘을 먼저 해소: (1) 전역 `s_imageLayouts` 레이스, (2)
`~VulkanRHICommandBuffer`의 `waitIdle()`(패스별 CB 수명 모델 재설계). 3.2에서
본 "동시 실행 중 자원 수명" 주의가 (2)에 그대로 적용된다.

---

## 변경 이력 (2부) — D3 장애물 해소 (D3-0a/0b)

> 승인받은 D3 시퀀스: "멀티-CB를 먼저 단일 스레드로 정확히 동작시킨 뒤 병렬을
> 켠다." 그 첫 단계로 §D가 명시한 D3 착수 전 장애물 둘을 해소했다. 둘 다
> **렌더링 동작은 불변**이지만, 병렬 패스 기록(D3-2)이 가능하도록 RHI의 공유
> 상태/스톨 의존성을 걷어낸다.

### 7. D3-0a — 전역 `s_imageLayouts` 우회

`beginRenderPass`(동적 렌더링 경로)는 정적 맵 `s_imageLayouts`를 읽어 진입
레이아웃 배리어를 emit한다. 워커가 동시에 `beginRenderPass`하면 이 맵이 레이스.
하지만 **그래프 경로에선 이미 redundant**다: `RenderGraph::execute`의
`transitionTex`가 `BarrierBatch`로 이미지를 `ColorAttachmentOptimal`로 명시
전이하고 `notifyImageLayoutChange`로 맵을 동기화 → `beginRenderPass`는
`tracker == target`이라 조기 반환(아무 배리어도 안 emit).

- 인코더에 `setGraphManagedLayouts(bool)` + `m_graphManagedLayouts` 추가.
- render pass 인코더 ctor에 `graphManagedLayouts` 파라미터 — 켜지면
  `emitBarrierToColor/Depth`가 조기 반환(맵 read/write·배리어 emit 전부 생략).
- `RenderGraph::execute`가 패스 루프 전 `setGraphManagedLayouts(true)`.
- **패스 함수(GBufferPass 등) 무변경** — 인코더 단위 플래그라 워커마다 독립.
  비-그래프 경로(데모/RendererBridge)는 그대로 맵 사용.

자동 배리어가 원래 no-op이었으므로 동작 동일. 검증: 검증 레이어 켠 채 7초 실행,
VUID 0. 이로써 **그래프의 명시적 배리어만으로 레이아웃 전이가 완결**됨이 확인 —
패스별 독립 CB 기록의 전제.

### 8. D3-0b — per-CB `waitIdle` 제거 + 프레임-펜스 CB 수명

`~VulkanRHICommandBuffer`가 CB 소멸마다 `device->waitIdle()`. drawFrame의 로컬
CB가 매 프레임 소멸 → **매 프레임 전체 GPU 스톨**(프레임을 사실상 단일 버퍼링).
패스당 CB를 만들면 프레임당 N회 스톨로 병렬화 무력화.

설계 — 전역 제거 대신 **opt-out**(소비자 ~13곳의 blast radius 회피):

- 기반 `RHICommandBuffer`에 `setExternallyManaged(bool)` 훅(기본 no-op). Vulkan
  CB는 켜지면 소멸자 `waitIdle` 생략. **one-shot 셋업 경로(텍스처 업로드·IBL
  bake·Mesh·ImGui·swapchain)는 기본값 유지 → 무변경·무위험** (그들은 이미
  `fence->wait`/`queue->waitIdle`로 명시 동기화).
- `RendererBridge`에 **프레임-펜스 retirement ring**
  (`m_retiredCommandBuffers`, 슬롯당 버킷). 제출된 프레임/컴퓨트 CB를
  `retireCommandBuffer`로 ring에 넘기고, `beginFrame`이 그 슬롯의 in-flight
  펜스를 대기한 직후 비움 → GPU 완료 보장 하에서만 해제. 슬롯이 다중 CB를
  보유(graphics + async compute; D3에서 패스별 CB도 같은 ring이 흡수).
- drawFrame 메인 CB + 비동기 컬링 컴퓨트 CB가 ring 경유로 전환.

검증 결과 per-frame 리소스(`rhiUniformBuffers[i]`, cull/indirect/visible[i],
per-frame 디스크립터 풀)가 **이미 frameIndex로 분리**돼 있어, 스톨 제거로
2프레임이 실제 오버랩해도 sync 해저드 없음 — 엔진이 애초에 2-in-flight로
설계됐고 스톨만 직렬화하고 있었음.

### 9. 스톨이 가리고 있던 latent 버그 — present 세마포어 재사용

스톨을 걷어내자 프레임이 실제로 겹치면서 검증 에러가 떴다
(VUID-vkQueueSubmit-pSignalSemaphores-00067): `renderFinished` 세마포어가
**frame-in-flight 단위**(`m_currentFrame`)라, presentation 엔진이 아직 그
세마포어를 쓰는 중에 다음 프레임이 재사용. 매 프레임 `waitIdle`이 GPU를 완전히
비워 가려주던 버그.

- 수정: 검증 레이어 권고대로 **renderFinished를 스왑체인 이미지 수만큼**
  (`getBufferCount()`) 만들어 **acquired image index로 인덱싱**. `createSwapchain`
  에서 생성(리사이즈 시 재생성, 선행 `waitIdle`이 안전 보장). fence·imageAvailable은
  frame-in-flight 단위 유지.
- 제출 signal(`getRenderFinishedSemaphore`)과 present wait가 모두
  `m_currentImageIndex`를 사용 — 일치.

검증: 검증 레이어 켠 채 10초 실행, VUID/sync 해저드/"command buffer in use"
**0건**.

### 10. 수정 파일 요약 (2부)

| 파일 | 변경 |
| --- | --- |
| `src/rhi/include/rhi/RHICommandBuffer.hpp` | `setExternallyManaged()` 가상 훅(기본 no-op) |
| `src/rhi/backends/vulkan/.../VulkanRHICommandEncoder.hpp` | CB `m_externallyManaged` + override; render pass 인코더 `graphManagedLayouts` 파라미터; 인코더 `setGraphManagedLayouts` |
| `src/rhi/backends/vulkan/src/VulkanRHICommandEncoder.cpp` | 소멸자 waitIdle을 `!m_externallyManaged`로 가드 + move 시 플래그 이전; `emitBarrierToColor/Depth` graph-managed 조기 반환; `beginRenderPass` 플래그 전달 |
| `src/rendering/graph/RenderGraph.cpp` | `execute()`가 패스 루프 전 `setGraphManagedLayouts(true)` |
| `src/rendering/RendererBridge.{hpp,cpp}` | 프레임-펜스 retirement ring(`m_retiredCommandBuffers`, `retireCommandBuffer`, beginFrame clear); renderFinished 세마포어를 이미지별로(createSwapchain 생성, image-index 인덱싱) |
| `src/rendering/Renderer.cpp` | drawFrame 메인 CB + 비동기 컴퓨트 CB를 `retireCommandBuffer` 경유 |

### 11. 다음 — D3-1

장애물 2개 해소 완료. 다음은 멀티-CB 스케줄러 + 병렬 기록.

---

## 변경 이력 (3부) — D3-1/D3-2 셰도우 캐스케이드 병렬 기록

> **방향 전환 (조사 결과)**: §D는 원래 "RenderGraph 패스를 병렬 기록"을 전제했으나,
> 코드 조사 결과 **RenderGraph는 지오메트리 이후의 가벼운 패스(SSAO/deferred/TAA/
> bloom/postprocess)만** 보유하고, **CPU 기록 비용이 큰 무거운 패스는 그래프 밖**에서
> 메인 인코더에 직접 기록됨을 발견: 셰도우 4 캐스케이드(각 전 인스턴스) + GBuffer
> (전 인스턴스). 따라서 병렬 대상을 **셰도우 4 캐스케이드**로 재설정 — 교과서적
> 독립 패스(서로 다른 레이어), §D의 "패스당 primary CB" 모델에 정확히 부합. (사용자
> 승인.)
>
> 승인된 시퀀스: **D3-1 단일 스레드 멀티-CB → D3-2 병렬**. 정확성과 동시성을 분리
> 검증.

### 12. D3-1 — 셰도우 캐스케이드를 분리 CB로, 단일 스레드 멀티-CB 제출

핵심 메커니즘: 한 번의 `vkQueueSubmit`에 **CB 배열을 순서대로** 넘기면 큐가 그
순서로 실행하고, 배리어는 CB 경계를 넘어 동작한다. 동기화(wait imageAvailable /
signal renderFinished+fence)는 단일 제출로 그대로 유지.

drawFrame Vulkan 경로를 분리 — 셰도우 캐스케이드가 culling과 GBuffer 사이에서
실행돼야 하므로 단일 CB를 쪼갠다:

- **pre 버퍼**(기존 `encoder`): 프로파일러 beginFrame, IBL/스카이박스 init 배리어,
  프러스텀 컬링(inline), 셰도우 before-barrier(전 레이어 undef→depth), ShadowPass
  타이머 begin → `finish()`.
- **캐스케이드 i (0..3)**: 각자 인코더 + `setGraphManagedLayouts(true)` →
  `beginShadowPass` + 전 인스턴스 draw + end → `finish()`. (D3-1은 메인 스레드 순차)
- **post 버퍼**: `encoder`를 새 인코더로 **재할당**(이후 GBuffer/graph 코드 무변경)
  → 셰도우 after-barrier(전 레이어 depth→shaderRead) + GBuffer + 그래프 + 프로파일러
  endFrame → `finish()`.
- 프레임 CB를 순서 리스트 `frameCBs = [pre, cas0..3, post]`로 모아 **단일
  `vkQueueSubmit`**(commandBuffers 배열). 셰도우가 없으면(인스턴스 ≤1) `frameCBs =
  [single]`. 전부 retirement ring으로.

캐스케이드 인코더의 `setGraphManagedLayouts(true)`(D3-0a)로 per-cascade
beginRenderPass 자동 배리어를 우회 — pre의 before-barrier가 이미 전 레이어를 depth로
전이했으므로. WebGPU는 단일 CB 경로 유지(`#ifndef` 분기).

검증: 검증 레이어 10초 무에러(sync 해저드·레이아웃·"command buffer in use" 0).
여전히 단일 스레드라 동작 동일.

### 13. D3-2 — 4 캐스케이드를 ThreadPool에 dispatch

- **D3-2a (전제)**: `ShadowRenderer::beginShadowPass`를 stateless화 —
  공유 멤버 `m_currentRenderPass`(+ `endShadowPass`)를 제거하고
  `unique_ptr<RHIRenderPassEncoder>`를 **반환**. 메서드가 per-cascade 상태
  (`m_uniformBuffers[fi][c]`, `m_cascadeViews[c]`, `m_bindGroups[fi][c]`,
  `m_lightSpaceMatrices[c]`)와 read-only 핸들(`m_pipeline`)만 만지므로 서로 다른
  캐스케이드의 동시 호출이 안전. 단일 스레드 무회귀 검증.
- **D3-2b**: 캐스케이드 루프를 `utils::ThreadPool`(NUM_CASCADES 워커, lazy 생성,
  Vulkan 전용) dispatch로 교체. 각 태스크 = { 워커 thread-local 풀(D1)에서 인코더
  생성 + `setGraphManagedLayouts(true)` + beginShadowPass→draw→end + `finish()`
  반환 }. `waitForAll` 후 4 CB를 캐스케이드 순서로 수집. CB는 기존 retirement ring.

#### 핵심 위험과 그 해소 — 커맨드 풀 수명 불변식 (반드시 숙지)

D1은 "`VkCommandPool`은 소유 스레드가 lock-free 사용"이 불변식. 그런데 D3-0b
retirement ring은 **메인 스레드가 워커 CB를 free**(`beginFrame`의 ring clear)한다.
Vulkan은 풀의 alloc/record/free를 외부 동기화해야 하므로, 워커 W의 alloc/record와
메인의 free가 겹치면 풀 레이스.

**해소 근거 — 시간적 분리**: 매 프레임 `waitForAll()`이 캐스케이드 태스크를 모두
끝낸 뒤에야 프레임이 반환된다. 따라서 다음 프레임 `beginFrame`의 ring clear(메인,
W의 풀 CB free)가 도는 시점에 W는 풀의 condition_variable에 park돼 있어(태스크 없음)
자신의 `VkCommandPool`을 만지지 않는다. alloc/record는 dispatch~waitForAll 창
안에서만 발생. 두 구간이 절대 겹치지 않아 외부 동기화가 충족된다(프레임 구조 +
waitForAll 배리어가 직렬화). 검증 레이어 thread-safety 체커로 실증 — 13초 무에러
(이 레이어는 앞서 D3-0b의 세마포어 VUID를 실제로 잡았으므로 활성·유효함이 확인됨).

> ⚠️ **이 불변식을 깨는 변경 금지**: 워커를 프레임 경계 너머로 돌리거나(이
> `waitForAll` 제거), fire-and-forget 워커 태스크를 추가하면 free↔alloc 직렬화가
> 깨져 풀이 조용히 레이스한다. 견고한 대안은 **(thread, frame-slot)별 풀 + 소유
> 워커가 reset**(§D "프레임 풀 reset 기반") — 그땐 풀을 단일 스레드만 만져 시간적
> 분리에 의존하지 않는다. drawFrame 셰도우 구간의 `COMMAND-POOL LIFETIME INVARIANT`
> 블록 주석에도 동일 경고를 박아 두었다.

검증: 빌드 통과, 검증 레이어(thread-safety 포함) 13초 무에러(경쟁/깜빡임/레이아웃/
sync 해저드 0). 사용자 시각 확인 완료(그림자 깜빡임·아티팩트 없음). 여전히 단일
스레드 대비 픽셀 동일.

### 14. 수정 파일 요약 (3부)

| 파일 | 변경 |
| --- | --- |
| `src/rendering/ShadowRenderer.{hpp,cpp}` | D3-2a: `beginShadowPass` → `unique_ptr` 반환(stateless), `m_currentRenderPass`/`endShadowPass` 제거 |
| `src/rendering/Renderer.hpp` | `m_shadowThreadPool` 멤버(Vulkan 전용) + `ThreadPool.hpp` include |
| `src/rendering/Renderer.cpp` | D3-1: drawFrame 셰도우~제출을 pre/cascade/post 멀티-CB + 단일 `vkQueueSubmit`로 재구성. D3-2: 캐스케이드를 ThreadPool dispatch + waitForAll + 풀 수명 불변식 주석 |

### 15. 다음 — D4

D4: CPU 프레임 시간 측정(단일 스레드 D3-1 vs 병렬 D3-2)으로 병렬 기록 이득 정량화.

---

## 변경 이력 (4부) — D4 측정과 정직한 결론

> D3-2의 정성 검증(경쟁/깜빡임 없음)은 끝났다. D4는 **정량 측정**으로 "병렬 기록이
> 실제로 빨라지는가"를 따진다. 결과는 예상과 달랐고, 그 다름 자체가 D4의 산출물이다.

### 16. 측정 인프라

- Renderer에 셰도우 캐스케이드 **기록 구간**(첫 캐스케이드 인코더 생성 ~ 4개 CB 수집)
  의 CPU 벽시계 시간을 `std::chrono::steady_clock`로 재고 EMA 평활(`m_shadowRecordCpuMs`).
- **A/B 런타임 토글** `m_parallelShadowCascades`: true=ThreadPool dispatch(D3-2),
  false=메인 스레드 순차 기록(D3-1, 레퍼런스 경로로 보존). 두 경로 모두 동일한
  `frameCBs`를 만들고 기록 방식만 다름.
- ImGui Statistics 패널에 **"Shadow Rec (CPU)" 수치 + "Parallel shadow recording"
  체크박스** 추가(Application이 매 프레임 토글을 Renderer에 적용, 측정값을
  ImGuiManager로 push). GPU "Shadow Pass" 바와는 **별개의 CPU 측정**임을 명시.

측정 방법: 임시 콘솔 로그 + 시작 시 `regenerateBuildings(1000)` 임시 주입으로 헤드리스
자동 측정(측정 후 임시 코드 제거).

### 17. 측정 결과

| 인스턴스 | serial (D3-1) | parallel (D3-2) |
| --- | --- | --- |
| 17 (기본 씬) | ~0.135 ms | ~0.145 ms |
| 1001 (스트레스) | ~0.11 ms | ~0.14 ms |

두 가지가 드러났다:

1. **기록 비용이 인스턴스 수와 무관(O(1))** — 1001 인스턴스가 17과 거의 같다.
   이유: 셰도우 캐스케이드가 **단일 인스턴스드 드로우**
   (`drawIndexed(idx, instanceCount)`)다. 캐스케이드당 기록은 beginRenderPass +
   setBindGroup + setVertexBuffer + setIndexBuffer + drawIndexed + end ≈ **6개 명령**
   으로, 인스턴스가 17이든 1001이든 동일. GPU가 인스턴싱을 처리하지 CPU 기록량은
   안 늘어난다.
2. **parallel이 serial보다 항상 ~0.02–0.03ms 느림** — 캐스케이드당 분할할 CPU
   기록량(6 명령)이 thread dispatch + waitForAll 오버헤드보다 작아, 병렬화가 이길
   crossover에 **영원히 도달 못 함**.

### 18. 정직한 결론

이 엔진의 셰도우 패스는 **기록-바운드(record-bound)가 아니다.** 인스턴스드 드로우라
캐스케이드당 명령이 6개뿐이고, 그걸 4스레드로 쪼개면 스레드 오버헤드만 추가된다.
**병렬 커맨드 기록이 이기려면 각 병렬 단위가 상당한 CPU 기록(수백 개의 개별·비인스턴스
드로우)을 해야 한다** — 예: 다양한 메시·머티리얼의 비인스턴스 지오메트리, 또는 한
패스가 수천 드로우콜을 내는 경우.

이것이 D4의 가치다: **능력을 만들고(측정 가능한 토글까지) → 측정하고 → "이 워크로드엔
이득 없음 + 어떤 워크로드라야 이득"을 정량적으로 규명**했다. D3 인프라(스레드 풀,
멀티-CB, 풀 수명 불변식)는 정확·검증됐고 재사용 가능하다. 단지 이 특정 패스가
기록-바운드가 아닐 뿐.

> 면접 스토리: "멀티스레드 커맨드 레코딩을 구현하고 측정했더니 이 패스에선 이득이
> 없었다. 왜냐면 인스턴스드 드로우라 기록이 O(1)이기 때문. 병렬 기록은 패스당
> 드로우콜이 많을 때만 의미가 있고, 그 임계점을 측정으로 보일 수 있다." — 측정 기반
> 판단을 보여주는 강한 답변.

### 19. 기본값 결정 — 병렬 ON 유지

측정상 병렬이 미세하게 느리지만(0.02ms, 16ms 프레임 예산 대비 무의미), 이 엔진은
시연 중심이라 **핵심 역량(멀티스레드 기록)을 기본 노출**하기로 함(사용자 결정).
패널 체크박스로 즉시 A/B 토글 가능. `m_parallelShadowCascades = true` 기본.

### 20. 수정 파일 요약 (4부)

| 파일 | 변경 |
| --- | --- |
| `src/rendering/Renderer.{hpp,cpp}` | `m_parallelShadowCascades` 토글 + `m_shadowRecordCpuMs` EMA 측정; drawFrame 캐스케이드 구간을 timing + parallel/serial 분기(`recordCascade` 람다 공유); getter/setter |
| `src/ui/ImGuiManager.{hpp,cpp}` | GPUTiming에 `shadowRecordCpuMs`; "Parallel shadow recording" 체크박스 + CPU 수치 표시; `getParallelShadowRecording()` |
| `src/Application.cpp` | 매 프레임 UI 토글을 Renderer에 적용 + 측정값 push |

### 21. 로드맵 D 종결

D1~D4 완료. 멀티스레드 커맨드 레코딩 인프라(per-thread 풀 → 스레드 풀 → 장애물
해소 → 멀티-CB → 병렬 → 측정)가 정확성·동시성 검증과 정직한 성능 규명까지 끝났다.
후속 후보(이득이 실제로 날 워크로드): GBuffer 등 **비인스턴스 다중 드로우 패스의
병렬/secondary-CB 분할** — §D "secondary 안 씀" 결정과 충돌하므로 별도 재논의.

---

## 변경 이력 (5부) — 볼륨 렌더링 (Vulkan 우선)

> 로드맵 D 종결 후 다음 방향으로 **볼륨 렌더링**(CAREER_ROADMAP의 해당 단계 —
> 3D 텍스처 + Ray Marching, 의료/디지털 트윈 도메인 진입)을 착수. 백엔드 범위는
> **Vulkan 전용 먼저**로 합의(WebGPU 포팅은 후속).

### 22. 착수 조사 — 문서 vs 실제 코드

CAREER_ROADMAP Task 7.1은 "RHI 3D 텍스처 미구현, 추가 필요"라 했으나 **현재 코드엔
이미 구현돼 있었다**: `VulkanRHITexture.cpp`의 `VK_IMAGE_TYPE_3D`/`extent.depth`/
`View3D → VK_IMAGE_VIEW_TYPE_3D` 경로와 `copyBufferToTexture`의 `copySize.depth`
처리. 단 **아무도 안 써봐서 미검증**이었다. → 7-1은 "구현"이 아니라 **"검증"**으로 축소.

엔진-로드맵 렌즈가 제기한 실제 난제(ENGINE_ROADMAP §1 "투명 패스 없음"): 이 엔진은
**디퍼드 단독(불투명만)**이라, 레이마칭 볼륨을 **씬 위에 알파 합성 + 씬 뎁스로
가림(occlusion)**하는 단계가 없었다. 볼륨 렌더링의 핵심은 "3D 텍스처"가 아니라 이
**뎁스-인식 투명 합성**이었다.

### 23. 단계별 구현

- **7-1/7-2 — 3D 텍스처 + 절차적 볼륨** (`VolumeRenderer`): 128³ R8Unorm 밀도
  텍스처를 만들고, CPU에서 **소프트 스피어 + 3옥타브 value noise**로 구름형 밀도장을
  생성해 스테이징 → `copyBufferToTexture`(depth=128, tightly-packed) 업로드 →
  `View3D` + linear/clamp 샘플러. 미검증이던 RHI 3D 경로가 검증 에러 0으로 동작 확인.
- **7-3 — 레이마칭 + 뎁스-인식 합성**: 신규 `volume_march.{vert,frag}.glsl`.
  풀스크린 프래그가 픽셀별 카메라 레이 복원(`deferred_lighting.frag`와 동일하게
  invProj→/w→invView) → **ray-AABB 교차**[tNear,tFar] → **씬 뎁스로 tFar 클램프**
  (불투명 지오메트리가 볼륨을 가림) → Beer-Lambert front-to-back 누적 →
  **premultiplied** 출력. `VolumeRenderer`가 파이프라인(premult-alpha 블렌드
  `One`/`OneMinusSrcAlpha`, depth write 없음) + per-frame UBO + 바인드그룹
  (depth + 3D 볼륨 + 샘플러) 보유. drawFrame이 **RenderGraph 패스 "VolumeMarch"**를
  TAA 뒤·bloom 앞에 삽입(reads depth, writes HDR with blend) — 볼륨은 모션벡터가
  없으니 TAA 뒤에 둬 고스팅 회피, bloom이 밝은 볼륨 픽업. 두 네이티브 리사이즈
  경로에서 볼륨 바인드그룹을 새 depth view로 재생성(dangling 방지).
- **7-4 — 실시간 컨트롤** (ImGui "Volume Rendering" 패널): Enable + Density·
  Extinction·Threshold·Color mix·Step size 슬라이더 + **Low/High color 픽커**
  (전이함수가 두 색을 density로 보간 — 사용자가 색 직접 지정). 기존 라이팅/D4 토글과
  동일 패턴(ImGuiManager 설정 → Application 매 프레임 적용 → Renderer 포워드).

### 24. 통합 설계 결정

- **합성 위치**: 디퍼드 라이팅(+TAA) 결과 HDR 위에 **알파 블렌드 합성**, 씬 뎁스는
  **샘플(read-only)**로 occlusion에만 사용. 엔진에 없던 "투명/반투명 합성" 단계를
  RenderGraph 패스로 깔끔히 추가(자동 배리어가 depth→read, HDR→ColorWrite 처리).
- **Vulkan 전용**: 헤더/소스/Renderer 멤버 모두 `#ifndef __EMSCRIPTEN__` 가드,
  CMake도 네이티브 타깃에만. WebGPU/WGSL 포팅은 후속(WASM 데모엔 아직 볼륨 없음).

### 25. 수정 파일 요약 (5부)

| 파일 | 변경 |
| --- | --- |
| `src/rendering/VolumeRenderer.{hpp,cpp}` | **신규** — 3D 밀도 텍스처 생성·절차 생성·업로드 + 레이마칭 파이프라인/UBO/바인드그룹 + 런타임 튜닝(파라미터·색) |
| `shaders/volume_march.{vert,frag}.glsl` | **신규** — 풀스크린 레이마칭(ray-AABB, 뎁스 occlusion, Beer-Lambert, 사용자 색 전이함수) |
| `src/rendering/Renderer.{hpp,cpp}` | VolumeRenderer 멤버 + init/pipeline 생성 + drawFrame VolumeMarch 그래프 패스 + 리사이즈 시 바인드그룹 재생성 + 볼륨 setter 포워드 |
| `src/ui/ImGuiManager.{hpp,cpp}` | "Volume Rendering" 패널 — enable + 파라미터 슬라이더 + Low/High 색 픽커; `VolumeSettings` + getter |
| `src/Application.cpp` | 매 프레임 볼륨 설정/색을 Renderer에 적용(`#ifndef __EMSCRIPTEN__`) |
| `CMakeLists.txt` | VolumeRenderer 소스(네이티브) + volume_march SPV 컴파일/목록 |

### 26. 후속

- **WASM 볼륨 포팅** (WebGPU/WGSL 레이마칭 + 3D 텍스처) — 듀얼 백엔드 데모 복원.
- ✅ **WASM TAA 미연결 수정** — 27부에서 처리(별도 커밋).
- Transfer Function을 1D LUT 텍스처로 확장(의료 CT Hounsfield 프리셋 등).

---

## 변경 이력 (6부) — WASM에서 TAA가 켜지지 않던 버그

> 브라우저(WebGPU) 데모에서 건물/헬멧 외곽선이 톱니처럼 거칠다는 보고. TAA는
> 2026-05-26에 WebGPU로 포팅됐는데 왜 안 듣나 조사.

### 27. 진단 — 포팅은 됐지만 한 번도 켜진 적이 없음

TAA 리졸브·velocity 타깃은 WebGPU에 구현돼 있었으나, **활성화 배선이 빠져** 있었다:

- `m_taaEnabled` 기본값이 `false`.
- 네이티브는 ImGui `LightingSettings.enableTAA=true`가 매 프레임 `setTAAEnabled(true)`
  를 호출해 켜짐 → 매끄러움.
- **WASM은 ImGui가 없고**(브라우저는 HTML 패널), `wasm_shell.html`엔 FXAA 토글만
  있고 TAA 토글이 없으며 `setTAAEnabled` 바인딩도 없었음 → TAA가 영영 off →
  **FXAA만 적용돼 톱니**. (TAA WebGPU 포팅 때 남은 미연결부. 네이티브만 봐서 놓침.)

### 28. 수정

- `m_taaEnabled` 기본값 `true`(양 백엔드 — 네이티브는 ImGui가 어차피 구동).
- `setTAAEnabled`를 WASM에 노출: `WASMBindings.cpp` 바인딩 +
  `Application::wasm_setTAAEnabled`(FXAA 패턴 그대로).
- `wasm_shell.html` Post-Process 섹션에 **"TAA (temporal AA)" 토글**(기본 체크)
  추가 — FXAA와 동일하게 onchange로 `setTAAEnabled` 큐잉.

검증: WASM 빌드 통과(`MiniEngine.wasm` 재생성), 네이티브 무회귀. 브라우저에서
기본 매끄러움 + 토글로 A/B 비교 가능.

> 교훈: **Vulkan 전용 멤버를 가드 없는 공개 메서드가 참조하면 안 된다.** 또한
> 기능을 "포팅 완료"로 간주하기 전에 **양 백엔드에서 실제로 켜져 동작하는지** 확인할
> 것 — 구현과 배선(기본값·UI·바인딩)은 별개다.

---

## 변경 이력 (7부) — 볼륨 렌더링 WebGPU 포팅 (듀얼 백엔드 복원)

> 볼륨 렌더링은 Vulkan 전용으로 먼저 만들었다(5부). 라이브 데모가 WebGPU이므로
> 듀얼 백엔드 명제 복원을 위해 WebGPU로 포팅. `VolumeRenderer`를 dual-backend로
> 전환(전면 `#ifndef __EMSCRIPTEN__` 가드 제거).

### 29. 구현

- `VolumeRenderer`가 양 백엔드 컴파일. 셰이더 로딩 분기(Vulkan SPIR-V / WebGPU
  `volume_march.wgsl`), 바인드그룹 레이아웃·바인딩 분기(아래 함정 참조). Renderer의
  `volumeRenderer` 멤버·생성·`createPipeline`을 무조건 경로로 이동, CMake는 WASM
  타깃에도 `VolumeRenderer.cpp` + WGSL preload 추가.
- WebGPU는 RenderGraph가 없으므로 drawFrame의 순차 패스 목록에 **볼륨 패스를 직접
  삽입**(TAA copy 뒤·bloom 앞, `hdrColorView`에 Load + premult 블렌드).

### 30. WebGPU 함정 3종 (미리 회피)

1. **3D 업로드의 256-row 정렬** (CLAUDE.md §9). 128³ R8은 한 행 128B인데 WebGPU는
   `bytesPerRow`가 256의 배수여야 함. `uploadVolume`을 분기: WebGPU는 256B 패딩
   스테이징 + depth 슬라이스마다 per-row memcpy + `bytesPerRow=256, rowsPerImage=128`;
   Vulkan은 tightly-packed(0/0).
2. **뎁스 샘플링.** WebGPU 뎁스는 `texture_depth_2d` + `textureLoad`(샘플러 불필요).
   바인드그룹 레이아웃을 분기: WebGPU는 `DepthTexture`(샘플러 없음, 바인딩 0/1/2/3),
   Vulkan은 `SampledTexture`+`Sampler`(GLSL texelFetch용, 바인딩 0/1/2/3/4).
3. **NDC y-flip.** WebGPU는 projection y-flip이 없어 `deferred_lighting.wgsl`처럼
   `ndc.y = 1 - uv.y*2`(Vulkan은 `uv.y*2 - 1`). `volume_march.wgsl`이 deferred의
   재구성(invProj→/w→invView, y-flip)을 그대로 미러해 트랩 회피.

추가: WGSL 마칭 루프의 동적 break 안에서 `textureSample`은 비균일 제어흐름 위반이라
**`textureSampleLevel(..., 0.0)`** 사용(파생값 불필요).

### 31. 수정 파일 요약 (7부)

| 파일 | 변경 |
| --- | --- |
| `src/rendering/VolumeRenderer.{hpp,cpp}` | dual-backend화(가드 제거); WebGPU 256-row 3D 업로드; WGSL 셰이더 로딩 + 뎁스/바인딩 레이아웃 백엔드 분기 |
| `shaders/volume_march.wgsl` | **신규** — WGSL 레이마칭(y-flip 재구성, `texture_depth_2d` textureLoad, `textureSampleLevel`) |
| `src/rendering/Renderer.{hpp,cpp}` | `volumeRenderer` 멤버·init·createPipeline 무조건화; WebGPU drawFrame에 볼륨 패스 삽입(bloom 앞) |
| `CMakeLists.txt` | WASM 타깃에 `VolumeRenderer.cpp` + WGSL preload에 `volume_march.wgsl` |

검증: WASM 빌드 통과(`MiniEngine.wasm` 재생성), 네이티브 무회귀, 브라우저에서 구름
합성 + 건물 occlusion 정상(사용자 확인). 듀얼 백엔드 볼륨 렌더링 복원. (브라우저
파라미터 컨트롤은 후속 — 현재 기본값 고정.)

---

## 변경 이력 (8부) — WASM 볼륨 HTML 컨트롤

> 7부에서 WASM 볼륨은 기본값 고정이었다. 브라우저에서도 네이티브 ImGui 패널처럼
> 조작 가능하게 wasm_shell HTML 패널에 Volume 섹션을 추가.

### 32. 구현

- 볼륨 setter를 무조건 경로로 노출(`Renderer::setVolume*`) + JS 친화적 개별 setter:
  density/extinction/threshold/colorMix(스칼라) + enable(bool) + low/high color.
- WASM 바인딩: `WASMBindings.cpp`의 `setVolume*` + `Application::wasm_setVolume*`
  (기존 FXAA/TAA 패턴 동일).
- `wasm_shell.html` "Volume" 섹션: 체크박스 + 슬라이더 4종 + 컬러 픽커 2종.
- **색상은 `#rrggbb` → `0xRRGGBB` int로 패킹**해 단일 인자로 전달 — `_queue`가
  함수당 단일 값만 coalesce하므로(rAF 플러시로 ASYNCIFY 충돌 회피) 다중 인자 불가.
  C++ `Renderer::unpackRGB`가 int를 vec3로 복원.

### 33. 수정 파일 요약 (8부)

| 파일 | 변경 |
| --- | --- |
| `src/rendering/Renderer.hpp` | 볼륨 setter 무조건화 + 개별 setter(스칼라/packed-color) + `unpackRGB` |
| `src/rendering/VolumeRenderer.hpp` | 개별 setter(densityScale/extinction/threshold/colorMix/low·highColor) |
| `src/Application.hpp` | `wasm_setVolume*` 포워더 |
| `src/wasm/WASMBindings.cpp` | `setVolume*` 바인딩 |
| `tests/wasm_shell.html` | "Volume" 섹션 — enable + 슬라이더 4 + 컬러 픽커 2, packed-color 큐잉 |

검증: WASM 빌드 통과, 네이티브 무회귀, 브라우저에서 슬라이더/색 실시간 반영(사용자 확인).
