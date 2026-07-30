# Y1c-full — Progressive brick fill (intensity buffer 우회)

**작성일**: 2026-07-22 (초안, Y1f 이후 refined)
**축**: Y (브라우저 대용량 페이징) · **단계**: Y1c-full
**전제**: Y1f 완료 (`874a714`), ADR-2026-07-22 mobile target tiering
**타깃**: iPhone Safari **256 MB CT (512×512×512)** 로드 성공.
ADR 상 iPhone 티어의 마감 스텝. 그 이상 (전신 CT) 은 별도 프로젝트.

**엔진 스코프 원칙**: Static path 만 진행. Streaming path 는 Y2 (IndexedDB)
가 있어야 실효 → 이번 스코프 밖.

## 1. 정직한 peak 예측

### 현재 Y1f (256 MB 볼륨)

- halfData 128 + slab staging 44 + GPU 279 + runtime 250 = **~701 MB** → **crash 예상**

### Y1c-full (256 MB 볼륨)

- rolling slice buffer 34 + slab staging 44 + GPU 279 + runtime 250 = **~607 MB**
- iOS 예산 ~600-650 MB → **marginal** (실측으로 확정 필요)
- Steady state (post-load): GPU 279 + runtime 250 = **~529 MB** — 안정

즉 Y1c-full 은 load 순간을 아슬아슬하게 통과할 것으로 예측. 실측 결과 여전히
crash 하면 rolling buffer 더 작게 (32 slices → 17 MB) 시도.

### Y1c-full (192 MB 볼륨, regression 체크)

- rolling 34 + slab 44 + GPU 209 + runtime 250 = **~537 MB** (Y1f 599 대비 -62 MB)
- 여유 확장.

## 2. 설계 — Builder 패턴 progressive API

DICOM Pass 2 loop 이 slice 마다 push. `BrickedVolume` 이 partial-fill 상태를 관리하고
브릭 완성 시 자동 pack + upload.

### 새 API

```cpp
class BrickedVolume {
public:
    // Progressive build state. Returned by beginProgressiveBuild, kept alive
    // by the caller for the duration of the DICOM Pass 2 loop, finalised
    // (and released) by endProgressiveBuild.
    struct ProgressiveContext {
        // Rolling slice buffer (holds up to kRingSlices slices at once).
        std::vector<uint16_t> ring;         // size = w * h * kRingSlices, uint16 half
        uint32_t              w, h, d;
        uint16_t              emptyValueHalf;
        // Brick-row completion tracking: rowState[bz] tells us which slices
        // for row bz have arrived. Row is complete when we've seen source Z
        // = (bz+1) * 64 (or z == d-1 for the last row).
        std::vector<uint32_t> rowSliceCount; // pageGrid.z entries
        // Cached from build-time decisions (recomputed in begin)
        glm::uvec3 pageGrid, atlasGrid, atlasVoxelsL0;
        glm::uvec3 brickInteriorL0, brickHaloL0, brickInteriorLastL0;
        bool       uniformSlotLayout;
        // Cached RHI handles (staging reused across all brick-rows)
        std::unique_ptr<rhi::RHIBuffer> slabStaging;
        uint32_t   slabDepthVox;
        uint64_t   slabBytes;
        // GPU texture (created inside begin, filled row-by-row inside push)
        // Owned by BrickedVolume, ctx just holds refs.
    };

    // Begin a progressive build. Allocates the atlas texture (GPU alloc
    // upfront so brick-row uploads can target it), page table, slab staging,
    // and the ring buffer. atlasGrid = 0 for auto-size like build().
    // NOTE: Static mode only. If nonEmpty > atlasSlots, fails. Caller must
    // fall back to legacy build() with a compressed source in that case.
    // (Actually we can't know nonEmpty before slices arrive -- see design
    // note below.)
    ProgressiveContext beginProgressiveBuild(
        rhi::RHIDevice* device, rhi::RHIQueue* queue,
        uint32_t w, uint32_t h, uint32_t d,
        uint16_t emptyValueHalf,
        glm::uvec3 atlasGrid = glm::uvec3(0));

    // Push one source slice at Z. sliceHalf points to w*h uint16 half data.
    // Copied into the ring buffer. If this slice completes any brick row(s),
    // those rows are packed + uploaded synchronously before returning.
    // The caller can free / unlink the source slice immediately after this
    // call returns.
    void pushSlice(ProgressiveContext& ctx, uint32_t z, const uint16_t* sliceHalf);

    // Finalise. Uploads the last brick row (with clamped halo), transitions
    // atlas to ShaderReadOnly, writes page table, and destroys the context.
    // Returns false if the atlas ended up smaller than nonEmpty count.
    bool endProgressiveBuild(ProgressiveContext&& ctx);
};
```

### Ring buffer sizing

브릭 row bz 가 필요로 하는 source Z 범위: `[bz*64 - 1, bz*64 + 65)` (halo 포함).
즉 66 개 슬라이스. 인접 row 는 1 슬라이스 겹침 (halo).

Ring buffer 는 **kRingSlices = 66** 슬라이스 (일반 케이스) 또는 65 (첫 row 는
halo 없음, 마지막 row 는 halo 없음). 안전하게 66 통일.

Ring 은 순환 사용. `ring[(z % 66) * w * h]` 위치에 슬라이스 저장. 브릭 pack 시
`ring[((srcZ + 66) % 66) * w * h + y * w + x]` 로 sample.

메모리: 66 × 512 × 512 × 2 = 34 MB. 볼륨 크기와 무관 (W, H 에만 비례).

### 브릭 row 완성 트리거

`pushSlice(z=Z)` 시:

- Z 를 pageGrid.z 좌표로 매핑: `bz_covered = { bz : bz*64 <= Z <= bz*64 + 65 }`
  일반적으로 최대 2개 bz (경계 슬라이스에서만)
- 각 bz 에 대해 `rowSliceCount[bz]++`
- `rowSliceCount[bz] == expected_slices_for_row(bz)` 이면 row 완성 → pack + upload

`expected_slices_for_row(bz)`:

- 일반: 66 (halo 양쪽 + interior 64)
- 첫 row (bz=0): 65 (하단 halo 없음, clamp)
- 마지막 row (bz==pageGrid.z-1): min(65, d - bz*64 + 1)

브릭 row pack: Y1f slab loop 와 동일하지만 rolling buffer 에서 읽음.

### 슬롯 지정 문제

Static mode 는 non-empty brick 만 slot 지정. 하지만 non-empty 판단은
"interior 가 모두 emptyValueHalf" 인지 검사 필요 → **모든 슬라이스가 도착한 뒤에만
확정 가능**.

두 옵션:

- **Option Bright**: 모든 브릭에 slot 예약 (nonEmpty=totalPages 로 가정). Sparse
  볼륨에서 slot 낭비. Atlas 도 pageGrid 크기.
- **Option Delay**: brick row 완성 시 그 row 의 non-empty 판별 후 slot 지정.
  Slot 순번 monotonic 하지 않음 → uniform layout counter 못 씀. Shrink layout
  (slot == pageIdx) 로 강제.

**추천: Option Delay + shrink layout 강제**. 자연스러움. Shrink layout 은
atlasGrid == pageGrid 필수 → 큰 볼륨에서 atlas 도 크지만 그 대가로 slot
예약 낭비 없음.

## 3. Atomic 단계 (총 ~3-4 세션)

### Y1c-full-1 — API 스켈레톤 + Volume3D 최소화 (~1 세션)

- `BrickedVolume::beginProgressiveBuild` / `pushSlice` / `endProgressiveBuild`
  헤더 선언 + 스텁 (구현은 임시로 기존 `build()` 로 위임)
- `assets::Volume3D` 에 "progressive" 지원 flag / callback 추가 (
  intensityHalf 미사용 모드 표시)
- 시그니처 컴파일 pass. 기능 회귀 무.

### Y1c-full-2 — BrickedVolume progressive 구현 (~1-2 세션)

- Ring buffer 관리 (allocate, write, cyclic read)
- Row completion 추적 (`rowSliceCount`)
- Row 완성 시 pack + upload (Y1f slab 인프라 재사용)
- Slot 지정 (shrink layout 강제)
- Empty-brick 검사 (row completion 시점에)
- endProgressiveBuild 에서 page table 확정 + upload

### Y1c-full-3 — DICOM progressive path (~1 세션)

- `loadDicomSeriesProgressive(dir, VolumeRenderer&, ProgressiveHooks)` 신규
- Pass 1: 기존 Y1b metadata + sort 그대로
- Pass 2: slice decode → `BrickedVolume::pushSlice` → memfs unlink
- 로컬 slice buffer 한 번만 alloc + 재사용

### Y1c-full-4 — Wire-up + viewer + baseline (~1 세션)

- `VolumeRenderer::loadFromDicomProgressive(dir)` 신규 entry
- `volume_viewer_wasm.cpp`: DICOM 경로 → progressive; NIfTI 경로 → legacy build
  유지
- `volume_viewer.cpp`: 동일 wire-up (native 회귀 체크)
- iPhone 256 MB + 192 MB regression 실측
- `BASELINE_2026-07-23_Y1C_FULL.md` 작성

### 합계: ~3-4 세션

## 4. 영향 받는 파일

| 단계 | 파일 |
| --- | --- |
| Y1c-full-1 | `src/rendering/BrickedVolume.{hpp,cpp}`, `src/assets/NiftiFile.hpp` (Volume3D) |
| Y1c-full-2 | `src/rendering/BrickedVolume.cpp` (progressive impl) |
| Y1c-full-3 | `src/assets/DicomFile.{hpp,cpp}` (loadDicomSeriesProgressive) |
| Y1c-full-4 | `src/rendering/VolumeRenderer.{hpp,cpp}`, `tests/volume_viewer_wasm.cpp`, `tests/volume_viewer.cpp`, `docs/current/medical-volume/baselines/BASELINE_2026-07-23_Y1C_FULL.md` |

## 5. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| iPhone 256 MB 도 여전히 crash | Rolling buffer 축소 (66 → 32 slices, -17 MB). 또는 slab 크기 조정. 최악 ADR 재검토. |
| Streaming path 재설계 필요성 | Y1c-full 은 Static-only. Streaming 은 Y2 (IndexedDB) 대기. Volume3D 는 progressive 와 legacy 둘 다 지원 유지. |
| Volume3D 인터페이스 breakage 파급 | Volume3D 는 legacy field 유지 (intensity, intensityHalf 남김). progressive 는 옵션. 상위 코드 최소 수정. |
| Slot 지정 순서 문제 | Shrink layout (slot == pageIdx) 강제. atlasGrid == pageGrid 는 이미 Y1f 코드에서 흔한 케이스. |
| Ring buffer 경계 처리 (첫/마지막 row halo) | `expected_slices_for_row(bz)` 함수로 명시적 분기. Test 커버. |
| NIfTI 도 progressive 로 옮겨야 하나 | 이번 스코프 밖. NIfTI 는 mmap 기반이라 다른 병목. legacy build 유지. |

## 6. 성공 기준

| 지표 | 목표 |
| --- | --- |
| iPhone Safari 256 MB (count 512) 로드 | 성공 |
| Peak MB (256 MB 볼륨, WASM 추정) | ~607 MB (iOS 예산 안) |
| iPhone Safari 192 MB regression | Y1f baseline 성능 유지 |
| Native (RTX 4070) 128/192/256 MB | 무회귀 |
| Load time 128 MB (native) | Y1f 대비 ±10% |
| Steady state CPU | 무회귀 |

## 7. 후속 (본 스코프 밖)

- **Y2 (IndexedDB brick cache)** — iPhone 이 그 이상 (전신 CT) 렌더링 필요
  시. ADR-2026-07-22 재검토 시점.
- **NIfTI progressive** — mmap 페이징이 이미 있어 시급성 낮음.
- **Predictive prefetch** — Streaming path 개선. Y2 이후.
- **v1-γ LOD seam** — Y2 이후.

## 8. 다음 진입점

**Y1c-full-1 부터**. API 스켈레톤 + Volume3D flag. 컴파일 회귀 무 목표.
