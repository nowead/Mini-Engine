# M3-3 v1-β 디스크 페이징 계획

**작성일**: 2026-06-07
**목표**: BrickedVolume이 전체 voxel을 CPU RAM에 보유하는 현재 구조를 개선,
**볼륨 데이터가 시스템 RAM을 초과해도 동작**하도록 한다. 4 GB+ 임상 데이터
(CBCT, 다중상 CT, 다중모달 fusion) unlock + 로드맵 §3 미해결 헤드라인
"1 GB 60fps 실 CT" 측정의 정직한 토대.

**로드맵 정렬**: §5 line 318 "M3-3 v1-β 디스크 페이징 — 4 GB+ 임상 데이터
(CPU RAM 한계 초과)" 명시 후보.

---

## 1. 현재 RAM 의존성

```text
[NIfTI / DICOM loader]
    -> std::vector<uint16_t> m_originalHalfData  // 전체 볼륨 (CPU RAM)
    -> std::vector<uint16_t> m_mipChain[3]       // L1+L2+L3 (CPU RAM, ~14% 추가)
        -> packBrickToStaging reads from these
        -> uploads to GPU atlas
```

병목:
- 1024³ R16 = 2.1 GB raw + mip 280 MB = **2.4 GB 최소 RAM**
- 4096³ → 137 GB raw → **불가능** (32GB RAM 한계 초과)
- 2048³ → 17 GB raw → 32GB RAM에 빠듯, mip 빌드 시 OOM 위험

---

## 2. 설계 — 두 단계

### Phase A: NIfTI mmap (가장 단순, 무압축 데이터)

NIfTI는 헤더 + voxel 평면 (row-major). 무압축이면 **`mmap`으로 voxel 영역을 직접
가상 메모리 매핑** 가능. OS가 페이지 단위로 lazy load + LRU eviction.

- `m_originalHalfData`를 `std::span<const uint16_t>` 또는 abstraction으로 일반화
- NIfTI 로더: 파일을 mmap → span을 voxel 영역으로 설정
- 효과: **CPU RAM 사용량 ≈ 가시 brick에 필요한 페이지만큼**

### Phase B: Mip chain lazy / disk-backed

L1+L2+L3 mip chain은 currently `packBrickToStaging` 호출 전에 전체 빌드. 큰
볼륨에서는 mip 자체가 RAM 초과 (4096³ → L1 17GB, L2 2GB).

옵션:
1. **Mip을 디스크에 사이드카로 저장** — `volume.nii` 옆에 `volume.nii.mip` 파일 생성
   (첫 로드 시), 이후 mmap. 동일 mmap 전략.
2. **Mip을 필요 시 brick 단위로 즉석 계산** — 다운샘플은 박스 필터 평균 → 빠름,
   추가 RAM 0. 단점: 매 streaming upload마다 mip 계산 비용.

옵션 2가 단순. brick CPU pack 시 lod 별로 downsample-on-the-fly.

### Phase C (선택): DICOM mmap

DICOM 무압축은 각 슬라이스가 별도 파일 → 슬라이스 단위 mmap 가능. 단, 슬라이스를
정렬해 연속 voxel array로 만드는 어셈블 비용 있음. 첫 로드 시 어셈블된 binary
캐시 파일을 만들고 mmap (사이드카 캐시 패턴).

DICOM 압축 (RLE, JPEG 2000)은 mmap만으로 안 됨 — 디코드 결과를 캐시 사이드카로
저장해 mmap. 별도 단계 (Phase D), 본 계획에서 deferred.

---

## 3. Atomic 단계

### Step 1 — Voxel source abstraction (~3-4h)

`m_originalHalfData` 직접 접근을 추상화. 두 백엔드:
- `OwnedHalfData` — `std::vector<uint16_t>` (현재 동작 유지)
- `MmappedHalfData` — POSIX `mmap` / Windows `MapViewOfFile` (Phase A 진입점)

인터페이스 최소화: `const uint16_t* data()`, `size_t size()`.

**작업**: BrickedVolume 멤버 변경 + mipData(0) 경로 + 빌더 인터페이스 갱신.
무회귀 (현재 vector 경로 그대로 가능).

### Step 2 — NIfTI mmap 로더 (~3-4h)

`assets::NiftiFile`에서 `--mmap` 옵션 또는 자동 (파일 > 임계) 으로 mmap 경로 사용.
- 헤더만 fread (작음)
- voxel 영역은 mmap, span으로 노출
- 엔디안 변환 필요 시 deferred (page-level)

**작업**: NiftiFile 분기 + Volume3D에 mmap 핸들 보존.

### Step 3 — On-the-fly mip 계산 (~4-5h)

`mipData(lod)` 호출 제거하거나 lazy. 대신 `packBrickToStaging`이 LOD 파라미터를
받고 즉석에서 다운샘플:
- L0: 직접 voxel copy
- L1+: 2^lod 박스 필터 평균 (소스 voxel 8/64/512개 합산 ÷ 8/64/512)

**작업**: BrickedVolume::packBrickToStaging 시그니처 변경 + mip 멤버 제거.
v1-β LOD 동작 무회귀 (시각 결과 비트 동일).

### Step 4 — 대용량 검증 (~2-3h)

- 합성 2 GB NIfTI 생성 (1024×1024×1024 R16)
- mmap 로드 + 메모리 사용량 측정 (RAM 사용량 << 파일 크기여야 정상)
- 시각 결과 무회귀 (기존 Case A/B/C 동일)
- M3-2 헤드라인 측정 (가능하면 실 CT 시리즈 사용)

**작업**: 합성 생성기 + 측정 + BASELINE 갱신.

### 합계: ~12-16시간 (3-4 세션)

---

## 4. 영향 받는 파일

| 단계 | 파일 |
| --- | --- |
| Step 1 | `src/rendering/BrickedVolume.{hpp,cpp}` — voxel source 추상화 |
| Step 2 | `src/assets/NiftiFile.{hpp,cpp}`, `src/assets/VolumeFile.{hpp,cpp}` — mmap |
| Step 3 | `src/rendering/BrickedVolume.cpp` — packBrickToStaging on-the-fly mip |
| Step 4 | `scripts/make_synthetic_nii.py`, `docs/current/medical-volume/BASELINE_*.md` |

엔진 코어 (Renderer, RHI) 변경 없음.

---

## 5. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| Windows mmap (`MapViewOfFile`)과 POSIX (`mmap`) 차이 | OS별 분기 또는 `<filesystem>` + 자체 wrapper |
| 큰 mmap 영역의 page fault 비용 (random access) | streaming은 brick 단위 순차 → cache 친화 |
| Mip on-the-fly 비용 (CPU pack 시간 증가) | row-memcpy fast path가 L0만 유효. L1+는 박스 필터 (~3-4x 비용). K=64/frame이면 여전히 ~ms |
| 시각 결과 변화 (mip 계산 방식 다름) | 박스 필터 = 현재 mipChain 빌드 방식과 동일 알고리즘 → 비트 동일 |

---

## 6. 후속 (이 계획 밖)

- DICOM mmap (Phase C) — 슬라이스 어셈블 캐시 + mmap
- DICOM 압축 디코드 캐시 (Phase D) — RLE/JPEG 2000 디코드 결과 사이드카
- 비동기 prefetch — 카메라 속도 기반 다음 frame 가시 brick 미리 페이지인
- 메타데이터 인덱스 캐시 — 대용량 다중 시리즈의 빠른 시작

---

## 7. 다음 진입점

Step 1 (voxel source 추상화) 부터 진행. 각 단계 끝에 커밋.
