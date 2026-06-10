# Disk Paging Step 5 — mmap'd int16 source → on-demand brick pack

**작성일**: 2026-06-07
**목표**: BrickedVolume의 `m_originalHalfData` (2-4 GB) + Volume3D float
intermediate (4-8 GB) 두 개의 RAM 비대 원인을 모두 제거. NIfTI int16 / uint16
파일을 직접 mmap → brick pack 시 int16→float→half 변환. 4 GB+ 임상 데이터를
16 GB RAM 워크스테이션에서 로드 가능하게 한다.

**로드맵 정렬**: §0 "대용량 실데이터" 차별화 + DISK_PAGING_PLAN의 미해결 항목
"Step 5+ (가칭): mmap'd int16 → brick-pack 시점에 직접 변환".

---

## 1. 현재 정직한 메모리 회계 (BASELINE_2026-06-07_DISK_PAGING 측정)

1024³ dense int16 NIfTI (2.1 GB on disk):

| 항목 | 크기 | 단계 |
| --- | --- | --- |
| mmap 파일 본체 | 0 GB (OS 페이지 캐시) | Step 2 ✓ |
| Volume3D::intensity (float) | 4.2 GB | **남은 한계** |
| halfData 변환 임시 | 2.1 GB | **남은 한계** |
| m_originalHalfData (engine 상주) | 2.1 GB | **남은 한계** |
| atlas (4 LOD) | ~580 MB | streaming 진입 |

피크: ~6.57 GB. Step 5가 위 세 항목을 모두 제거.

---

## 2. 설계 — VoxelSource 추상화

```cpp
struct VoxelSource {
    enum class Format { HalfFloat, Int16, Uint16 };
    const void* data;
    Format      format;
    float       slope    = 1.0f;
    float       intercept = 0.0f;

    // 물리 단위(HU 등) float로 변환. 호출 위치는 brick pack 내부 루프.
    inline float read(size_t idx) const noexcept;
};
```

BrickedVolume 멤버 교체:

- 기존: `std::vector<uint16_t> m_originalHalfData`
- 새로: `VoxelSource m_source` + 선택적 `std::vector<uint16_t> m_ownedHalf` (포맷 HalfFloat 일 때만) + 선택적 `utils::MmappedFile m_mmap` (Int16/Uint16 일 때 lifetime 보호)

`packBrickToStaging`: 첫 인자가 `const VoxelSource&`. 내부 sampling 헬퍼가 포맷별 분기. LOD>0 박스 필터는 voxel을 float로 읽고 평균 → pack.

`emptyValueHalf` → `emptyValuePhysical` (예: -1000 HU). 빈-brick 검사는 voxel의 physical 값을 비교.

---

## 3. Atomic 단계

### Step 5.1 — VoxelSource 추상화 (HalfFloat 전용, 무회귀) (~2-3h)

- `VoxelSource` 정의 + `packBrickToStaging` 시그니처 변경 (raw `uint16_t*` →
  `const VoxelSource&`)
- BrickedVolume 내부 read path만 변경, 외부 인터페이스 그대로
- 시각 결과 비트 동일 (HalfFloat 경로는 기존 `glm::unpackHalf1x16` 그대로)

### Step 5.2 — Int16/Uint16 포맷 추가 (~3-4h)

- VoxelSource::read 가 포맷별 분기
- BrickedVolume::buildFromSource 신규 (raw 데이터 + slope/intercept + spacing + dims)
- m_ownedHalf / m_mmap 분기 lifetime 관리
- 빈-brick 검사가 physical 값 기준
- 시각 결과: HalfFloat 경로 무회귀 + Int16 신규 경로는 합성 데이터로 비트 동일성 검증

### Step 5.3 — NIfTI 로더 int16 직접 경로 + 뷰어 진입 (~2-3h)

- `assets::loadNiftiAsInt16Source` (또는 Volume3D 변형 struct): int16/uint16 +
  trivial slope/intercept 자동 감지 → mmap + offset + slope/intercept 반환
- 비-int16 fallback: 기존 Volume3D float 경로 (회귀 무)
- VolumeRenderer 신규 entry: `loadFromVoxelSource(MmappedFile&&, ...)`
- volume_viewer가 NIfTI 파일 확장자 + 헤더 보고 자동 분기

### Step 5.4 — 측정 + 베이스라인 갱신 (~1-2h)

- `BASELINE_2026-06-07_DISK_PAGING.md` 갱신 또는 Step 5 별도 베이스라인
- 1024³ dense + 2048³ 합성 (만약 디스크 여유) 측정
- 기대: 정착 working set 2.4 GB → ~0.7 GB (atlas + 작은 brick 버퍼만 남음)
- 4 GB+ 임상 데이터 로드 가능성 확인 (실제 4 GB+ 합성 NIfTI 생성 후)

### 합계: ~8-12시간 (3-4 세션)

---

## 4. 영향 받는 파일

| 단계 | 파일 |
| --- | --- |
| 5.1 | `src/rendering/BrickedVolume.{hpp,cpp}` |
| 5.2 | `src/rendering/BrickedVolume.{hpp,cpp}` (build 변형 + empty-brick 일반화) |
| 5.3 | `src/assets/NiftiFile.{hpp,cpp}`, `src/rendering/VolumeRenderer.{hpp,cpp}`, `tests/volume_viewer.cpp` |
| 5.4 | `docs/current/medical-volume/BASELINE_*.md`, `scripts/make_synthetic_nii.py` (큰 합성 옵션) |

엔진 코어 (RHI, 셰이더) 변경 없음.

---

## 5. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| 포맷 분기 분기 비용 (pack 핫루프) | VoxelSource::read 인라인 + 포맷 const propagation (컴파일러가 분기 제거) |
| Int16 빈-brick 스캔 비용 (2 GB 순회) | 한 번만 로드 시. mmap 순차 액세스라 OS 페이지인 자연스러움 |
| Mmap lifetime — BrickedVolume이 소유 | unique_ptr<MmappedFile> 또는 직접 멤버. 이동 시 명확한 소유권 |
| Big-endian NIfTI (`ni1`) | 현재 미지원 그대로. 헤더에서 감지되면 기존 float 경로 fallback |

---

## 6. 후속 (이 계획 밖)

- DICOM int16 mmap (Phase C from DISK_PAGING_PLAN): 슬라이스 어셈블 캐시 후 mmap
- DICOM 압축 디코드 캐시 사이드카: RLE/JPEG 2000 디코드 결과 디스크 보존
- Predictive prefetch
- VoxelSource를 셰이더 측에서도 활용 (atlas 자체를 int16 storage로?)

---

## 7. 다음 진입점

Step 5.1 (VoxelSource 추상화, HalfFloat 전용 무회귀) 부터 진행.
