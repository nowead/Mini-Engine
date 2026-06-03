# M3-3 v1-α — Brick Streaming 계획서

**작성일**: 2026-06-03
**상태**: 미진입 (이 문서는 코드 작업 전 설계 합의용)
**기준 baseline**: [BASELINE_2026-06-03.md](BASELINE_2026-06-03.md)
**전제 구조**: M3-3 v0 (brick atlas + page table) + M4 v1 (progressive 누적)
**결정 묶음**: B / A / B / A — 아래 §2 참조

---

## 1. 목표와 범위

### 들어가는 것 (v1-α)

- **Camera-frustum 기반 brick 가시성**: 매 프레임 카메라가 볼 brick만 atlas에 올린다.
- **LRU eviction**: atlas가 가득 차면 가장 오래 안 쓴 brick을 비운다.
- **Incremental upload**: 매 프레임 atlas 전체 재업로드가 아니라, **변경된 slot
  만** `copyBufferToTexture`로 갱신.
- **자동 모드 전환**: 작은 볼륨(v0가 더 효율적인 경우)에서는 v0 행동 유지. 큰
  볼륨에서만 streaming 켜짐.

### 들어가지 않는 것 (v1-β로 연기)

- **디스크 페이징**: 원본 brick 데이터를 디스크에서 stream-in. v1-α는 CPU RAM에
  원본 그대로 보관.
- **예측적 prefetch**: 카메라 속도 기반 brick 미리 가져오기. v1-α는 best-effort
  (보고 있는 brick만 + 페이지인 지연 발생 시 다음 프레임에 채워짐).
- **셰이더 측 hit/miss 로깅**: 어떤 brick read가 sentinel을 만났는지 추적. v1-α
  는 CPU 측 통계만.
- **다중 카메라 / 시야 분할**: 단일 카메라 가정.

### 베이스라인에서 노리는 개선

| 지표 | 현재 (v0) | v1-α 목표 |
| --- | --- | --- |
| 1024³ atlas alloc | 280 MB | ~140 MB (절반) |
| 1024³ atlas live | 167 MB | ~50 MB (frustum 가시 brick만) |
| 4096³+ 볼륨 지원 | 불가 (atlas cap 초과) | **지원** (streaming만이 길) |
| 작은 볼륨(96³) 회귀 | -160% 손실 | 자동 v0로 회피 → **무회귀** |

---

## 2. 확정 결정 사항

직전 합의: **B/A/B/A**.

| 결정 | 선택 | 영향 |
| --- | --- | --- |
| 1. Atlas 크기 | **B 자동 산정** | 720p × 60° fov 기준 ~256 slots = ~140MB. 호출자 override 가능 |
| 2. Frustum culling | **A CPU** | 16×16×8 = 2048 brick → <0.1ms. compute 셰이더 작성 회피 |
| 3. 모드 전환 | **B 자동** | non-empty 수 + atlas cap 비교로 v0/v1 자동 선택. 작은 볼륨 회귀 방지 |
| 4. 원본 데이터 위치 | **A CPU RAM** | `m_originalHalfData`를 BrickedVolume이 보관. 4GB+ 임상 데이터는 v1-β |

---

## 3. 새로 도입할 데이터 구조

### BrickedVolume 내부

```cpp
// 모드 분기.
enum class Mode { StaticFullyLoaded, Streaming };
Mode m_mode = Mode::StaticFullyLoaded;

// Streaming 시: atlas의 각 slot이 현재 어떤 virtual brick을 담고 있나.
struct AtlasSlotState {
    uint32_t residentPageIdx = kInvalidPageIdx;  // 비어있으면 sentinel
    uint64_t lastFrameUsed   = 0;                // LRU 비교용 (단조 증가)
};
std::vector<AtlasSlotState> m_slotStates;     // [totalSlots]

// Streaming 시: page index → atlas slot 빠른 역방향 lookup.
// (page table은 GPU buffer; 이건 CPU 측 사본/색인)
std::unordered_map<uint32_t, uint32_t> m_pageToSlot;

// Streaming 시: 원본 half-float 볼륨 (per-frame brick 추출용).
// Static 모드에서는 비어 있음 (build 종료 후 버림).
std::vector<uint16_t> m_originalHalfData;
uint32_t m_srcW = 0, m_srcH = 0, m_srcD = 0;
uint16_t m_emptyValueHalf = 0;

// Streaming 시: CPU 측 page table mirror (변경분만 GPU buffer로 push).
std::vector<uint32_t> m_pageTableHost;

// Streaming staging: brick 한 개 분량 (66^3 * 2B = ~574KB) ring 버퍼.
// 한 프레임에 K개 brick upload 가능하도록 K개 슬롯 미리 할당.
static constexpr uint32_t kStreamUploadsPerFrame = 8;
std::array<std::unique_ptr<rhi::RHIBuffer>, kStreamUploadsPerFrame> m_stageBuffers;
```

### 신규 API (BrickedVolume에 추가)

```cpp
// Streaming 모드: 매 프레임 호출. 가시 brick page-index 목록을 받아
// (1) LRU 결정, (2) 변경된 slot upload, (3) page table dirty 부분만 push.
// frameIdx는 lastFrameUsed 갱신과 (호스트 측) 단조 증가 시간축.
// 반환값: 이 프레임에 evict/upload된 slot 수 (stats panel용).
struct StreamUpdateStats {
    uint32_t bricksUploaded   = 0;
    uint32_t bricksEvicted    = 0;
    uint32_t visibleResident  = 0;
    uint32_t visibleMissing   = 0;  // 다음 프레임으로 미뤄진 수 (best-effort)
};
StreamUpdateStats updateStreaming(
    const std::vector<uint32_t>& visibleBrickPageIndices,
    uint64_t frameIdx,
    rhi::RHIQueue* queue);

bool isStreaming() const { return m_mode == Mode::Streaming; }
```

---

## 4. 라이프사이클 변화

### Build time (load) — 모드 결정 시점

`BrickedVolume::build`에 추가 로직:

```
1. pageGrid 계산
2. atlasGrid 자동 산정 (B 결정)
3. non-empty brick 수 스캔 → non_empty_count
4. 모드 결정:
     fits_static = (non_empty_count <= atlasGrid 총 slot 수)
     m_mode = fits_static ? StaticFullyLoaded : Streaming

5a. Static 모드 (현 v0 동작 그대로):
     - atlas 전체 packing
     - page table 전체 업로드
     - m_originalHalfData를 비워둠

5b. Streaming 모드 (신규):
     - atlas 텍스처는 할당만 (slot 내용 모두 emptyValueHalf로 초기화)
     - page table 전부 kEmptySlot으로 초기화하여 업로드
     - m_originalHalfData에 halfData 복사 (CPU 보관)
     - m_slotStates를 모두 빈 상태로
     - 첫 프레임 updateStreaming 호출까지 atlas는 비어 있음 (모든 sampleVolume → 0)
```

### Per frame (Streaming 모드만)

호출자 흐름 (viewer):

```
1. 카메라 view/proj → frustum 6 평면
2. CPU frustum cull:
     visible = []
     for bz, by, bx in pageGrid:
         brickAABB = (aabbMin + brickIdx*brickWorldSize, ...)
         if frustum.intersects(brickAABB) AND !isInteriorEmpty(brickIdx):
             visible.push_back(pageIdx(bx, by, bz))
3. m_volume->updateBrickStreaming(visible, m_frameIdx)
4. updateUBO, render normally
```

BrickedVolume::updateStreaming 내부:

```
1. 가시 brick 분류:
     stillResident  = visible ∩ m_pageToSlot.keys()
     newlyNeeded    = visible − m_pageToSlot.keys()
     stillResident.forEach(p): m_slotStates[m_pageToSlot[p]].lastFrameUsed = frameIdx

2. eviction 후보 선정:
     candidatesForEviction = m_slotStates where (residentPageIdx not in visible)
     LRU 정렬: lastFrameUsed 오름차순

3. 업로드 횟수 제한:
     uploadCount = min(newlyNeeded.size(), kStreamUploadsPerFrame,
                       candidatesForEviction.size() + emptySlots.size())

4. 각 (pageIdx in newlyNeeded[0..uploadCount-1]) 에 대해:
     slot = pop empty slot OR pop LRU candidate
     if (slot evicted): m_pageToSlot.erase(slot.residentPageIdx)
     packBrickToStaging(m_originalHalfData, pageIdx, m_stageBuffers[i])
     copyBufferToTexture(staging → atlas, slot 위치)
     m_slotStates[slot] = {pageIdx, frameIdx}
     m_pageToSlot[pageIdx] = slot
     m_pageTableHost[pageIdx] = slot

5. m_pageTableHost의 변경된 영역만 page table buffer로 push
     (대안: 매 프레임 전체 push - 16K~1MB 수준이므로 일단 전체 push)

6. 남은 newlyNeeded.size() - uploadCount 만큼은 다음 프레임으로 (visibleMissing)
   → 셰이더에선 그동안 sampleVolume이 0 반환 (잠시 검은 영역)
```

---

## 5. API 변경

### BrickedVolume.hpp

```cpp
// 새 enum / 새 메서드 추가. 기존 build 시그니처는 유지(자동 모드 분기는 내부).
+ enum class Mode { StaticFullyLoaded, Streaming };
+ Mode mode() const;
+ bool isStreaming() const;
+ StreamUpdateStats updateStreaming(const std::vector<uint32_t>& visible,
+                                    uint64_t frameIdx, rhi::RHIQueue* queue);
+ // 호출자가 frustum cull 구현 시 필요한 brick의 월드 AABB.
+ glm::vec3 brickWorldMin(uint32_t bx, uint32_t by, uint32_t bz,
+                          const glm::vec3& aabbMin, const glm::vec3& aabbMax) const;
+ glm::vec3 brickWorldMax(...);  // 동일
```

### VolumeRenderer.hpp/.cpp

```cpp
// 뷰어가 매 프레임 호출. 내부적으로 frustum 평면 계산 + brick AABB 테스트 +
// BrickedVolume::updateStreaming 호출. m_brick.isStreaming() == false면 no-op.
+ StreamUpdateStats updateBrickStreaming(const glm::mat4& view,
+                                         const glm::mat4& proj,
+                                         uint64_t frameIdx);
```

### tests/volume_viewer{,_wasm}.cpp

```cpp
// render() 진입부, updateUBO 직후:
+ const auto stats = m_volume->updateBrickStreaming(view, proj, m_frameIdx);
+ m_lastStreamStats = stats;  // ImGui/HTML 패널 표시
```

### Stats 패널 (D3 Phase 1 위에 4줄 추가)

```
Streaming: ON   (또는 OFF static 모드)
  visible resident: 96
  uploaded this frame: 3
  evicted: 2
  pending (missing): 0
```

### 셰이더 - **무변경**

`sampleVolume(uvw)` 헬퍼는 이미 page table 간접 참조 후 sentinel 처리 포함.
Streaming은 page table 내용을 동적으로 갱신할 뿐, 구조는 동일.

---

## 6. 구조적 영향

### 메모리 모델 변화

| 데이터 | v0 (static) | v1-α Streaming |
| --- | --- | --- |
| 원본 halfData | build 후 폐기 | **build 후 보관** (CPU RAM) |
| Atlas 텍스처 | 한 번 채워짐 | **빈 상태로 시작**, 매 프레임 일부 채움 |
| Page table buffer | 한 번 채워짐 | **매 프레임 일부 갱신** |
| CPU staging | build 시 일회성 큰 버퍼 | **per-frame K=8개 작은 버퍼** ring |
| Atlas slot 매핑 | 항상 동일 (slot N = brick X 고정) | **동적** (slot N의 brick이 시간에 따라 바뀜) |

### 라이프사이클 복잡도

- v0: build → 정적. 동시성 우려 0.
- v1-α: per-frame update가 GPU 큐에 작업 추가. **현재와 다른 점**: 매 프레임의
  copyBufferToTexture가 같은 atlas 텍스처에 접근.
  - Vulkan/WebGPU: copy ↔ sample 동기화는 큐 내 순서로 자동 보장 (같은 큐).
  - 다중 frame-in-flight 환경: per-frame staging buffer가 다른 ring slot이면
    GPU에서 frame N copy와 frame N+1 sample이 겹쳐도 안전.

### Atlas alloc vs live의 의미가 바뀜

- v0: alloc = live + 영구 빈 slot 패딩. 한 번 결정.
- v1-α: alloc은 그대로, **live는 매 프레임 변함**. stats panel 표시 의미가 미세
  하게 다름 — "현재 resident bricks의 메모리"로 재정의.

### 호환성

- Static 모드 path는 코드상 v0 그대로. 회귀 0.
- Streaming 모드는 별도 path. 둘이 mutually exclusive (런타임 모드 변경 없음).
- 셰이더 무변경 → 양 백엔드 동시 적용.

---

## 7. 영향 받는 파일

| 파일 | 변경 종류 |
| --- | --- |
| `src/rendering/BrickedVolume.hpp` | 멤버 + API 추가. enum 신규. |
| `src/rendering/BrickedVolume.cpp` | build에 모드 분기, updateStreaming 구현, helper들 추가 |
| `src/rendering/VolumeRenderer.hpp` | updateBrickStreaming 메서드 추가 |
| `src/rendering/VolumeRenderer.cpp` | 위 메서드 구현 (frustum 평면 계산 + brick AABB cull) |
| `tests/volume_viewer.cpp` | render() 진입부에 updateBrickStreaming 호출 + ImGui stats |
| `tests/volume_viewer_wasm.cpp` | 동일 + emscripten 바인딩 추가 |
| `tests/volume_viewer_shell.html` | streaming stats 4줄 readout |
| `docs/current/medical-volume/VIEWERS.md` | 스트리밍 동작 1 섹션 추가 |
| `docs/current/README.md` | 진행 상태 갱신 |
| `learning/MEDICAL_VOLUME_GRAPHICS.md` | §6 brick atlas 섹션에 v1 streaming 단락 추가 |

새 파일 없음. 셰이더 변경 없음.

---

## 8. 구현 마일스톤 (atomic)

각 단계마다 빌드 + 검증 + 커밋. 사이에 사용자 확인 단계 있음.

### v1-1: CPU Frustum Culling (진단 모드)

- BrickedVolume / VolumeRenderer에 frustum cull 헬퍼 + `updateBrickStreaming`
  메서드 추가. **단, 실제 streaming 동작은 안 함**. 출력은 stats 로깅만.
- 뷰어에 `visible brick count` 표시. 카메라 돌릴 때 숫자가 자연스럽게 변하는지 확인.
- 산출물: frustum 산수 검증 + 통합 진입점 마련.
- 작업량: 3-4시간.
- 검증: stats가 카메라 fov 안에 들어오는 bricks 수와 시각적으로 일치 (1024³에서
  ~50-200 정도 예상).

### v1-2: Streaming 모드 자동 진입 + 빈 atlas

- BrickedVolume::build에 모드 분기 추가. Streaming일 때 atlas는 빈 상태 + 원본
  halfData 보관 + slot states 초기화.
- 셰이더는 그대로. 첫 프레임 atlas 비어 있어서 화면이 다 검정. **이 시점은
  의도된 임시 상태**.
- 작업량: 2-3시간.
- 검증: 모드 분기 로그 (`"streaming ON: %d non-empty bricks > %d slots"`)
  + 빈 atlas 시각화 (검정 화면 정상).

### v1-3: LRU + Incremental Upload

- updateStreaming의 실제 구현. 한 프레임에 최대 K=8 brick upload + LRU
  evict + page table 부분 push.
- 첫 가시화: 처음 보는 brick들이 frame 0..N에 걸쳐 채워짐. 카메라 돌리면 새
  brick 채워지고 안 보이는 brick은 evict.
- 작업량: 6-8시간.
- 검증:
  - 시각: 1024³에서 streaming 모드로 정상 렌더 (카메라 회전 시 brief 검정 영역
    가능 = 기대된 best-effort 동작).
  - stats: bricksUploaded / bricksEvicted / visibleResident / visibleMissing 표
    시. 카메라 정지 시 upload/evict가 0으로 수렴해야 함.
  - 메모리: 양 백엔드 stats에서 atlas live MB가 v0 280MB → 50~100MB로 감소
    확인.

### v1-4: 뷰어 통합 마무리 + 문서

- D3 stats 패널에 streaming 4줄 추가 (양 백엔드).
- VIEWERS.md / learning/MEDICAL_VOLUME_GRAPHICS.md 갱신.
- 베이스라인 재측정 → `BASELINE_2026-06-03.md`에 "v1-α 측정" 섹션 추가.
- 작업량: 2-3시간.

총 작업량 추정: **13-18시간**.

---

## 9. 위험과 대응

### 9.1 Streaming Latency (검정 영역 깜빡임)

- 증상: 빠른 카메라 회전 시 새로 보이는 brick 영역이 한두 프레임 검정.
- 원인: per-frame upload 한도 K=8이 visible newly-needed 보다 작을 때.
- 완화: K를 동적으로 늘림 (한 프레임 budget 안에서). 또는 v1-β의 prefetch.
- 수용: v1-α 범위에서는 best-effort, 사용자 인지 가능한 짧은 artifact 허용.

### 9.2 Atlas Thrashing

- 증상: 매 프레임 brick 업로드/eviction이 한도 K=8을 꽉 채우며 멈추지 않음.
- 원인: visible brick 수 > atlas slot 수. 정지 상태에서도 churn.
- 완화: atlas 자동 산정에서 "visible upper bound" 보수적으로 잡음. 정지 상태
  thrashing 발견 시 atlas 크기 늘리는 권장 로그 (recommend-on-thrash).
- v1-β에서: 압축 사용 빈도 모니터링 + dynamic atlas resize.

### 9.3 Page Table Race

- 증상: GPU가 page table N을 읽는 중에 CPU가 N+1로 덮어씀 → 셰이더가 잘못된
  slot 가리킴 → 잘못된 brick data 샘플.
- 완화: per-frame double buffering 또는 큐 내 순서 보장 (같은 큐의 buffer write
  → render는 자동 동기화).
- v1-α: page table buffer 단일. 큐 동기화에 의존 (Vulkan/WebGPU 모두 명시적
  barrier 없이 큐 내 순서로 안전).

### 9.4 Vulkan vs WebGPU 차이

- copy command + render command 같은 큐 자동 hazard 해소는 양쪽 모두 보장.
- Dawn은 staging buffer mapWrite + queue.submit 사이 동기화에 더 strict.
  ASYNCIFY rule(한 번에 하나만) 준수 확인 필요.
- v1-3 작업 시 양 백엔드 동시 빌드 + 검증.

---

## 10. v1-β로 연기된 항목

- **디스크 페이징**: `m_originalHalfData`를 CPU RAM 대신 mmap'd 파일로. 4GB+
  볼륨 지원.
- **예측 Prefetch**: 카메라 속도/방향으로 다음 프레임 needed brick 미리 page-in.
- **Compute frustum culling**: pageGrid 64³+ 에서 CPU 비용 발산 시.
- **Brick LOD**: 멀리 있는 brick은 1/8 다운샘플 brick으로 대체.
- **Compression**: brick 단위 BC4/ASTC. CPU staging 대역폭과 GPU read 양쪽 절감.

이 항목들은 baseline 측정 결과와 v1-α의 실 동작을 본 뒤 우선순위 재산정.

---

## 11. 시작 신호

이 계획서를 사용자가 승인하면 **v1-1부터 atomic 단계로 진입**. 각 단계 종료
시점에 빌드 + 검증 + 커밋 + 다음 단계 진입 확인. 중간 어디서든 일시 정지하고
방향 재조정 가능.
