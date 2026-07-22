# Y1c-full — Progressive brick fill (intensity buffer 우회) [Stub]

**작성일**: 2026-07-22 (stub)
**축**: Y (브라우저 대용량 페이징) · **단계**: Y1c-full
**전제**: [Y1f (chunked staging)](Y1F_CHUNKED_STAGING_PLAN.md) 완료 + baseline
측정. Y1f 결과에 따라 이 계획의 세부는 재조정.
**상태**: **스텁 (Sketch only)** — 상세 설계는 Y1f 데이터 확보 후 확정.

**목표**: `Volume3D.intensityHalf` 를 완전 제거. DICOM slice 가 decode 되면
즉시 해당 브릭들에 partial write → 브릭이 채워지면 즉시 GPU upload → slice
폐기. Y1f 이후 peak ~485 MB 에서 **halfData 96 MB 도 소멸 → ~380-460 MB**.
**실 임상 CT 256+ MB mobile 가능화**.

---

## 1. 왜 필요한가

Y1f 이후 예상 peak (192 MB 볼륨):
- Slab staging 26 + halfData 96 + GPU 209 + runtime 250 = **581 MB** (peak)

Halfdata 96 MB 는 "전체 볼륨 크기에 비례" — 256 MB 볼륨이면 128 MB, 500 MB
볼륨이면 250 MB. 볼륨 크기 증가에 따라 여전히 병목. 이걸 제거하면 **peak 이
GPU + slab staging + runtime 만 남아 볼륨 크기와 (거의) 무관** 해진다.

## 2. 설계 스케치

### 개념

DICOM Pass 2 를 "voxel 을 다 모아서 넘김" 이 아니라 "slice 하나씩 push".
`BrickedVolume` 이 slice 단위로 받아, 그 slice 에 걸치는 브릭들에 partial
write. 브릭이 다 채워지면 즉시 staging 에 pack + GPU upload + slice 폐기.

### 새 API 개괄

```cpp
// BrickedVolume 신규 API
struct ProgressiveContext {
    // build 시 결정된 브릭 그리드 · staging · 상태
    // 각 브릭의 "몇 개 slice 남았나" 카운터
    // 완성된 브릭을 flush 하는 helper
};

ProgressiveContext BrickedVolume::beginProgressiveBuild(
    rhi::RHIDevice*, rhi::RHIQueue*,
    uint32_t w, uint32_t h, uint32_t d,
    uint16_t emptyValueHalf);

// slice 는 w*h uint16 half packed
void BrickedVolume::pushSlice(ProgressiveContext&,
                              uint32_t z,
                              const uint16_t* sliceHalf);

void BrickedVolume::endProgressiveBuild(ProgressiveContext&);
```

### DICOM 측 변화

`loadDicomSeriesHalf` (Y1c-lite) 의 Pass 2:
- 기존: `for each slice: decode → write into intensityHalf[dstIdx]`
- 신규: `for each slice: decode into local sliceBuf → pushSlice(ctx, z, sliceBuf) → memfs unlink`

Volume3D 는 metadata (w, h, d, spacing, dataMin/Max) 만 반환. Voxel 데이터
필드 없음. Callers 는 Volume3D 로 GPU 리소스 준비 → progressive 로 채움.

### 브릭 partial-fill 로직

각 브릭 (bx, by, bz) 은 Z 범위 `[bz*64, (bz+1)*64+halo)` 를 커버. Slice z 가
push 되면 그 z 에 걸치는 모든 (bx, by, bz) 브릭이 partial update. 브릭당 카운터
로 "받아야 할 slice 수" 를 추적, 0 이 되면:
- 브릭을 slab staging 에 pack (기존 packBrickToStaging 재사용 가능)
- 그 slab 이 다 채워지면 (인접 브릭들도 완료) `copyBufferToTexture` + free

각 브릭이 사용하는 partial buffer 는 `brickStored^3 * 2 = ~575 KB` — pending
브릭 몇 개만 살아있으므로 총 수십 MB.

### Volume3D 인터페이스 breakage

- `Volume3D::intensity` / `intensityHalf` 필드 삭제 (또는 optional)
- NIfTI 경로도 영향 — 두 옵션:
  - (a) NIfTI 는 legacy 유지, DICOM 만 progressive
  - (b) NIfTI 도 slice-by-slice reader 로 변환
- Native viewer / WASM viewer 시그니처 조정
- Streaming mode 와의 관계 — `m_originalHalfData` 대체 방안 (또는 Streaming
  진입 시 legacy 폴백)

## 3. 세션 분할 초안 (총 3-4 세션)

### Y1c-full-1: 인터페이스 설계 + 유틸 (~1 세션)

- Volume3D 최소화 (metadata only) — 파급 파일 목록화
- Slice↔brick 매핑 유틸 (`bricksIntersectingZ(z)`)
- `ProgressiveContext` 구조체 + 브릭 카운터
- 기존 build() 는 그대로 유지 (NIfTI 폴백용)

### Y1c-full-2: BrickedVolume progressive API 구현 (~1 세션)

- `beginProgressiveBuild` / `pushSlice` / `endProgressiveBuild`
- Partial-fill state machine
- 브릭 완료 시 자동 slab pack + GPU upload
- Empty-brick detection 은 slice 를 다 모은 뒤 (endProgressiveBuild) 처리

### Y1c-full-3: DICOM path 재작성 (~1 세션)

- `loadDicomSeriesProgressive(dir, ProgressiveContext&)` 신규
- Pass 1 은 Y1b 그대로 (metadata + sort)
- Pass 2 는 slice 마다 pushSlice + unlink
- Local slice buffer 재사용 (한 번 alloc)

### Y1c-full-4: Wire-up + 양 viewer + 측정 (~1 세션)

- VolumeRenderer 신규 entry: `loadFromDicomProgressive(dir)`
- `volume_viewer_wasm.cpp` 진입: applyVolumeData 대체 flow
- `volume_viewer.cpp` (native) 동일 wire-up
- iPhone 256 MB 시도 + baseline 문서

## 4. 열린 질문 (Y1f 뒤 결정)

- **NIfTI path** — legacy vs progressive?
- **Streaming mode** — `m_originalHalfData` 재설계? 아니면 대용량 진입 시
  Streaming 은 Disk-paging 경로 강제?
- **에러 처리** — slice 중간에 실패하면 partial GPU 상태를 어떻게 롤백?
  아마 empty-brick 폴백으로 안전하게 처리 가능
- **Wide brick edge case** — Option C (per-axis brick shape) 과의 상호작용

## 5. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| Volume3D 인터페이스 breakage 파급 범위 큼 | Y1c-full-1 세션에서 파급 파일 사전 목록화, 스텁으로 컴파일 유지 |
| 브릭 완성 순서 비선형 (halo 때문) | `bz*64 + halo` 범위 마지막 slice 도착이 트리거. 카운터 정확 계산 |
| GPU upload 를 slice 마다 waitIdle 하면 총 시간 폭발 | Slab 단위 배치 upload (여러 브릭 완료 시 slab 통째로 flush) — Y1f 인프라 재사용 |
| Legacy path (NIfTI) 유지 부담 | 옵션 (a) 채택 시 최소 유지 비용. Volume3D 는 union / variant |
| 상위 앱 (VolumeRenderer entry) 시그니처 변경 | Public API 는 `loadFromDicomProgressive` 만 추가, 기존 `loadFromHalfData` 유지 |

## 6. 성공 기준 (재조정 예정)

| 지표 | 잠정 목표 |
| --- | --- |
| iPhone Safari 256 MB 로드 | 성공 |
| Peak MB (192 MB 볼륨) | Y1f ~485 → **~380-460 MB** |
| Peak MB (256 MB 볼륨) | ~450-500 MB |
| Peak MB 볼륨 크기 의존성 | (거의) 무관 (slab + GPU + runtime 만) |
| Native regression | wall-time ±20% 이내 |

## 7. 다음 진입점

**Y1f 완료 후 baseline 데이터 (특히 실제 peak MB 분포) 확인 → 이 계획 §2-3
확정 → 세션 개시**. Y1f 로 192 MB 가 뚫리면 Y1c-full 은 "다음 확장" 이 되고,
Y1f 로도 안 뚫리면 Y1c-full 은 "다음 필수" 가 됨.
