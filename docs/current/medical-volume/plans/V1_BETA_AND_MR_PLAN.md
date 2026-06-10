# M3-3 v1-β + MR 데이터 전환 계획서

**작성일**: 2026-06-04
**상태**: 미진입 (코드 작업 전 설계 합의용)
**전제 구조**: M3-3 v1-α 완료 (`d748814`, `f6f1561`, `68b3a2b`).
**baseline 비교점**: [BASELINE_2026-06-04_V1_ALPHA.md](../baselines/BASELINE_2026-06-04_V1_ALPHA.md)

---

## 1. 목표와 범위

### 들어가는 것

- **MR 데이터 트랙**: 합성 phantom + 실 임상 코퍼스 추가. CT 트랙과 동일한
  "합성(인너 루프) + 실데이터(검증)" 패턴.
- **CPU pack 최적화**: v1-α Case C(2 GB dense, 12.6 FPS)에서 발견된 brick CPU
  pack 비용을 row-memcpy fast path로 10× 개선.
- **LOD (multi-resolution brick)**: streaming의 본질적 한계(visible >>
  atlas) 해결. 가까운 brick = full res, 먼 brick = downsampled.

### 들어가지 않는 것 (v1-γ 이후로 연기)

- **디스크 페이징**: 4 GB+ 임상 데이터의 CPU RAM 한계
- **Predictive prefetch**: 카메라 속도 기반 미리 page-in
- **GPU 측 mip 생성**: build time mip chain은 CPU에서. GPU 측 compute로 옮기는
  최적화는 LOD 안정화 후
- **DICOM 압축 transfer syntax**: 별도 트랙

### 헤드라인 목표

| 지표 | v1-α 현재 | v1-β 목표 |
| --- | --- | --- |
| Case C (2 GB dense) FPS | 12.6 | **100+** (CPU pack 최적화) |
| Visible >> atlas hole | 시각 결손 | **부드러운 LOD 전환** |
| MR 데이터 지원 | 엔진에선 가능, preset 미비 | **임상 워크플로 동등** (T1/T2 preset, brain phantom 데모) |

---

## 2. 의존 구조와 진행 순서

```text
[1단계 MR-B1·C1]  합성 sphere(mr 모드) + 실 코퍼스 fetch    ←──── 독립
       ↓
[2단계 MR-B2·B3]  뇌 phantom + T1/T2 preset + 문서          ←──── 1단계 위
       ↓
[3단계 CPU pack]  row-memcpy fast path                       ←──── 데이터 무관, 어디서든
       ↓
[4단계 LOD]       β-1 mip build → β-6 검증                  ←──── 3단계가 끝나야 의미
```

**의존성 논리**:
- MR 데이터는 모든 후속 작업의 검증 환경. 먼저 깔아 둠.
- CPU pack은 LOD의 build time 영향(mip 생성도 brick pack 호출함)을 사전 해소.
- LOD는 마지막. 가장 큰 작업 + 가장 큰 위험.

---

## 3. Track A — MR 데이터 (1단계 + 2단계)

### 왜 합성 phantom + 실 임상 둘 다인가

**B 합성 phantom (인너 루프)**:
- 1초 안에 생성 가능 → dev 회전 빠름
- 크기·밀도 제어 가능 → atlas overflow stress test 즉시 가능
- 결정적 출력 → regression test에 사용 가능
- 임상 디테일(noise, motion artifact 등) 미반영 → 진단 신뢰성 입증 못 함

**C 실 임상 코퍼스 (최종 검증)**:
- 실 매체 강도 분포, 노이즈, 해상도 다양성
- pydicom 공개 corpus → 라이선스 부담 없음
- 다운로드 + 디스크 비용 → 인너 루프 부적합
- 진단 가치 입증의 본체

**두 데이터의 역할 비**: dev 회전 90% 합성, 마일스톤 검증 10% 실데이터.

### MR vs CT 데이터 차이

| 항목 | CT (Hounsfield Unit) | MR (signal intensity) |
| --- | --- | --- |
| 단위 | -1000 ~ +3000 HU, 표준화 | 임의 0 ~ 수천, 시퀀스 의존 |
| 배경 | -1000 (공기) | 0 (no signal) |
| 뼈 | +800~1000 (밝음) | low signal (피질골 black) |
| 연조직 | +40 | mid (T1 weighted: white matter > gray) |
| 윤곽 강조 | window/level (bone, soft, lung) | T1 weighted(WM/GM) / T2(water) / FLAIR |
| 전형 해상도 | 512² × 100-500 slices | 256-1024² × 100-300 slices |

### MR-B1 — 합성 sphere 생성기 MR 모드

**작업**: `scripts/make_synthetic_nii.py`에 `--modality mr` 플래그 추가.

CT 모드(기존): air=-1000, tissue=+40, bone=+800 (HU).
MR 모드(신규): 배경=0, 외피=300, 핵심=800 (T1 강도 모사).

**핵심 변경 면적**: 강도값 + 배경값만 다름. shape는 동일 sphere. `air` 변수의
의미가 modality에 따라 달라짐(mr에선 0이 "empty" 기준값).

`uploadHalf`에 전달하는 `emptyValueHalf`는 `loadFromFloatData`에서 자동 감지
(데이터 min) — modality와 무관하게 작동.

**검증**: 96³ MR 생성 → 뷰어로 띄움 → window 자동 조정으로 셰이딩 정상 확인.

### MR-C1 — 실 임상 MR 다운로드 스크립트

**작업**: `scripts/fetch_sample_mr.py` 신규 (또는 `fetch_sample_ct.py` 일반화
해서 `fetch_sample_dicom.py`).

다운로드 대상 (pydicom 공개 corpus):
- T1-weighted brain MR (single-frame)
- T2-weighted brain MR
- (선택) DWI, FLAIR

**검증**: 다운로드 → 뷰어 디렉토리 dispatch로 로드 → window/level로 임상 구조 확인.

### MR-B2 — 뇌 phantom 생성기

**작업**: sphere 패턴을 **WM/GM/CSF shell**로 발전.

```text
  중심       (CSF, 측뇌실 가정)    intensity = 100
  +
  Shell A    (WM, 백질)            intensity = 800
  +
  Shell B    (GM, 회백질 피질)     intensity = 400
  +
  외각       (CSF, 두개골 외 공간) intensity = 100
```

생성기 옵션: `--shape brain` (또는 `--brain`). 기본 sphere는 그대로 유지.

**검증**: T1 preset으로 띄움 → WM이 가장 밝게, GM이 중간, CSF 어둡게 → 임상
T1 영상과 시각적으로 유사한지 확인.

### MR-B3 — T1/T2 TF preset + 문서

**작업**:

1. `VolumeRenderer::TFPreset` 에 `MR_T1`, `MR_T2` 추가 (현재: Custom, Cloud,
   Fire, CTBone, CTSoftTissue)
2. preset 별 LUT 생성 로직 추가 — T1은 high signal(WM) 강조, T2는 water 강조
3. 뷰어 ImGui Combo + HTML select에 새 preset 노출
4. `VIEWERS.md` MR 사용법 섹션 추가
5. `MEDICAL_VOLUME_GRAPHICS.md` 데이터 표현 섹션에 MR 단락 추가

**검증**: 뇌 phantom + T1 preset → WM 밝게 표시. T2 전환 → CSF 밝게 (water).

---

## 4. Track B — CPU pack 최적화 (3단계)

### 진단 (v1-α Case C 측정)

```text
1024³ dense, atlas (9,10,10)=900 slots, Streaming
Render CPU: 76.97 ms / frame
브레이크다운 추정:
  - 8 brick CPU pack ≈ 70-75 ms (~9-10 ms/brick)
  - GPU submit + page table push ≈ 1-3 ms
  - 셰이더 march (visible 4096) ≈ 미미 (대부분 sentinel)
```

**현재 pack 함수 (단순 4중 루프)**:

```cpp
for (lz = 0..65)
  for (ly = 0..65)
    for (lx = 0..65)
      dst[lz][ly][lx] = srcVoxel(x+lx, y+ly, z+lz);  // clamp ×3, array access
```

비용 분석:
- per voxel: clamp ×3 (분기), 1D 인덱스 계산, array read, array write
- 66³ = 287,496 voxels × ~5-10 ns/voxel = 1.4 - 2.9 ms per brick (이론치)
- 실측 9-10 ms → 캐시 미스 또는 분기 예측 실패가 dominant

### 최적화 전략

#### B-1 (필수) — Row-memcpy fast path

대부분 brick은 **interior brick** (소스 볼륨 내부). 이 경우 srcX, srcY, srcZ
모두 clamp 불필요 → row 단위 `std::memcpy` 가능.

```cpp
if (interiorBrick) {  // bx >= 1 && bx < pageGrid.x - 1, etc.
    for (lz = 0..65)
        for (ly = 0..65) {
            const uint16_t* srcRow = &src[(srcZ * H + srcY) * W + srcX0];
            std::memcpy(dst + ..., srcRow, kBrickStored * 2);
        }
} else {
    // boundary brick: clamp 필요, 기존 per-voxel 경로
    ...
}
```

- 1024³ default 304 non-empty 중 **~80-90%가 interior** (모서리만 boundary)
- per-voxel 1 ns × 287K = 287μs vs memcpy 66 rows × 66 calls × ~1μs = ~4ms
- 추정 효과: ~8-10× speedup. Case C 77 ms → ~10 ms 기대.

#### B-2 (선택, 후속) — SIMD (AVX2)

interior 경로의 memcpy는 이미 SIMD 활용 (`std::memcpy`는 컴파일러가 자동
vectorize). boundary brick의 clamp 경로는 수동 SIMD가 어렵고 비중도 작음
→ B-2는 v1-γ 이후 후보로 분리.

#### B-3 (선택, 후속) — Async pack (worker thread)

main thread는 GPU submit만, CPU pack은 별도 스레드. main 프레임이 빨라지지만
구현 복잡도 ↑. v1-β 범위에서는 보류.

### 검증

1. v1-α Case C 재측정 → Render CPU 77 → ~10 ms 기대
2. FPS 12.6 → 70+ 기대 (GPU도 streaming-bound라 100+는 아닐 수 있음)
3. 시각 결과 무변화 (memcpy로 동일 voxel 복사)
4. interior vs boundary brick 분기 정확성: edge case 검증 (1×1×1 atlas 강제,
   1×N×N 비큐브 atlas 등)

### 작업량

- B-1 코드 작성 + 분기: 2-3시간
- 검증 + Case C 재측정: 1-2시간
- 합계: **~4-5시간**

---

## 5. Track C — LOD (4단계, 본체)

### 설계 결정

#### C-1: Mip 레벨 수 = 4

```text
Level 0: 64³  (575 KB)  원본
Level 1: 32³   (72 KB)  1/8 메모리
Level 2: 16³    (9 KB)  1/64
Level 3:  8³    (1 KB)  1/512
```

4 레벨이 흔한 선택. 8 레벨까지 가면 brick 자체가 1 voxel 되는 극단.

#### C-2: Mip 생성 = build time, CPU

GPU compute로 mip 생성 가능하나 build 시 한 번이라 CPU 충분. 추가 복잡도 회피.

box filter (2×2×2 평균) — 의료 데이터에 일반적. tent filter도 옵션이지만 box가
충분.

#### C-3: Atlas 구조 = 통합 LOD atlas

옵션:
- **A. 통합 LOD atlas**: 각 LOD level별 별도 atlas 텍스처. selection이 어떤
  atlas를 sample할지 결정.
- **B. Per-brick LOD in one atlas**: 한 slot에 multi-LOD packed. 셰이더가 LOD에
  따라 sub-region sample.

추천: **A (통합 LOD atlas)**. 셰이더 변경 적음, mip 별 메모리 독립 관리.

#### C-4: LOD selection 기준 = world-extent / screen-pixel

매 visible brick:
- world size in viewing frustum / screen pixel coverage
- 1 voxel ≥ 1 pixel: Level 0 필요
- 1 voxel ≥ 2 pixel: Level 1 충분
- ...

매 frame CPU에서 계산. 4096 brick 기준 < 1 ms.

### 단계 분할

#### β-1 — Build time mip chain 생성

- `BrickedVolume` 내 `m_originalHalfData` 위에서 mip 4 level 생성
- 각 level별 별도 CPU buffer 또는 단일 buffer offset 관리
- build 시간 ~+30% 예상 (4 level downsample)
- 작업량 4-6시간

#### β-2 — 다중 LOD atlas 구조

- atlas 텍스처를 4개 (level별) 또는 하나의 큰 atlas에 multi-LOD 영역
- atlas slot이 어떤 LOD를 담는지 추적
- `m_slotStates`에 `currentLOD` 필드 추가
- 작업량 6-8시간

#### β-3 — LOD selection 매 frame

- 매 visible brick: world size, camera distance, screen coverage 계산
- LOD 결정 → updateStreaming에 LOD 정보 전달
- 작업량 3-4시간

#### β-4 — Streaming LOD-aware

- updateStreaming이 (pageIdx, targetLOD) 쌍을 받음
- 같은 brick이 다른 LOD로 page-in 가능
- LOD transition (Level 1 → Level 0) 처리
- 작업량 6-8시간

#### β-5 — 셰이더 LOD-aware sampling

- `sampleVolume(uvw)` 헬퍼가 brick의 current LOD에 맞춰 sample
- LOD 별 atlas texture binding 추가 (또는 통합 atlas에서 sub-region)
- (선택) dual-LOD blending — 인접 LOD 동시 sample 후 보간으로 transition 부드럽게
- 작업량 3-4시간

#### β-6 — 검증 + 측정 + 문서

- Case C 재측정 — visible 4096이 모두 표현되는지 (hole 0)
- 메모리 효과: 493 MB atlas로 1500 non-empty 다 표현 가능?
- 시각 비교: full-res vs LOD 같은 view에서
- 새 BASELINE 문서
- 작업량 4-6시간

총 LOD 작업량: **~30-40시간**.

---

## 6. 영향 받는 파일

| 단계 | 파일 | 변경 종류 |
| --- | --- | --- |
| MR-B1 | `scripts/make_synthetic_nii.py` | `--modality mr` 플래그 |
| MR-C1 | `scripts/fetch_sample_mr.py` (신규) 또는 `fetch_sample_dicom.py` 일반화 | 신규 스크립트 |
| MR-B2 | `scripts/make_synthetic_nii.py` | `--shape brain` 옵션 |
| MR-B3 | `src/rendering/VolumeRenderer.{hpp,cpp}` | `TFPreset::MR_T1`, `MR_T2` 추가 + LUT 생성 |
| MR-B3 | `tests/volume_viewer.cpp`, `tests/volume_viewer_shell.html` | preset 드롭다운 항목 추가 |
| MR-B3 | `docs/current/medical-volume/VIEWERS.md` | MR 사용법 섹션 |
| MR-B3 | `learning/MEDICAL_VOLUME_GRAPHICS.md` | §1 데이터 표현에 MR 단락 |
| CPU pack | `src/rendering/BrickedVolume.cpp` | `packBrickToStaging` 분기 + interior fast path |
| LOD β-1 | `src/rendering/BrickedVolume.{hpp,cpp}` | mip 생성 함수, m_mipChain 멤버 |
| LOD β-2 | `src/rendering/BrickedVolume.{hpp,cpp}` | LOD별 atlas 또는 통합 atlas 구조 |
| LOD β-3 | `src/rendering/VolumeRenderer.cpp` | LOD selection 로직 (`updateBrickStreaming` 안) |
| LOD β-4 | `src/rendering/BrickedVolume.cpp` | updateStreaming LOD-aware |
| LOD β-5 | `shaders/volume_march.{frag.glsl,wgsl}`, `shaders/volume_pathtrace.*`, `shaders/volume_occupancy.comp.*` | `sampleVolume` LOD-aware |
| LOD β-6 | docs/ + BASELINE 신규 | 측정 + 문서 |

---

## 7. 검증 계획

각 마일스톤에 atomic 검증.

### MR 트랙

- MR-B1: 96³ MR sphere → 시각적으로 흰 구체 + 진한 코어 (T1 가정)
- MR-C1: 실 brain MR DICOM 로드 → 임상 해부 인지 가능
- MR-B2: 뇌 phantom T1 시각 → WM 밝게, GM 중간, CSF 어둡게
- MR-B3: T1/T2 preset 전환 시 콘트라스트 즉시 반전 (T1 WM 밝음 ↔ T2 CSF 밝음)

### CPU pack 트랙

- 동일 데이터 (Case C 재현) → Render CPU 77 → 10 ms 기대
- FPS 12.6 → 70+ 기대
- 시각 결과 비트 동일 (memcpy = 동일 voxel 복사)
- Boundary brick 정확성 — clamp 경로 무회귀

### LOD 트랙

- β-1: mip 4 level 생성 후 메모리 사용량 측정 (전체 ~14% 추가)
- β-2: 통합 LOD atlas 메모리 + slot 관리 정확성
- β-3: 카메라 줌인/줌아웃 시 LOD 자연스럽게 전환 (콘솔 로그로 검증)
- β-4: streaming이 변경된 LOD로 page-in/out
- β-5: 시각 결과 — 가까운 brick crisp, 먼 brick 부드러움. seam artifact 0
- β-6: Case C 모든 visible brick 표현, hole 0

---

## 8. 위험과 대응

### 8.1 LOD seam artifact

- 증상: 인접 brick이 다른 LOD면 경계에서 불연속
- 완화: trilinear interpolation은 brick 내부에서만, 경계는 LOD 일치하도록 selection 보정
- 또는 dual-LOD blending (셰이더 변경 +시간)

### 8.2 LOD transition popping

- 증상: 카메라 회전 시 LOD가 점핑 → 해상도 step 가시화
- 완화: hysteresis (선택 threshold에 dead zone), 또는 LOD transition을 frame 거쳐 점진
- v1-β 범위: hysteresis만. dual-LOD blending은 v1-γ.

### 8.3 MR preset 결정

- T1/T2 모두 임상 표준. FLAIR, DWI는 후속.
- T1을 default로 (기존 CT Bone 위치 대체). brain phantom 기본 + T1 = "와우" 시연.

### 8.4 build time 증가

- MR brain phantom: 합성이라 무관 (1초)
- LOD mip 생성: 4 level CPU downsample = +30% build time 추정
- 실 임상 1 GB CT: 현재 build ~2초 → ~3초. 수용 가능.

### 8.5 CPU pack 분기 분기 예측

- interior vs boundary 분기가 호출자(updateStreaming)에서 조건 알면 즉시 분기
- per-brick 결정이라 branch predictor 친화적

---

## 9. 마일스톤 + 시간 추정

| Phase | Atomic 단계 | 작업 | 시간 |
| --- | --- | --- | --- |
| **MR 트랙** | MR-B1 | sphere mr 모드 | 1h |
| | MR-C1 | fetch_sample_mr.py | 1h |
| | MR-B2 | 뇌 phantom | 1-2h |
| | MR-B3 | T1/T2 preset + 문서 | 1-2h |
| **CPU pack** | B-1 | row-memcpy fast path + 검증 | 4-5h |
| **LOD** | β-1 | mip chain build | 4-6h |
| | β-2 | 다중 LOD atlas 구조 | 6-8h |
| | β-3 | LOD selection | 3-4h |
| | β-4 | streaming LOD-aware | 6-8h |
| | β-5 | 셰이더 LOD-aware | 3-4h |
| | β-6 | 검증 + baseline + 문서 | 4-6h |
| **합계** | | | **~35-50시간** |

세션 묶음 권장:

- 세션 1: MR-B1 + MR-C1 (한 커밋)
- 세션 2: MR-B2 + MR-B3 (한 커밋)
- 세션 3: CPU pack 최적화 (한 커밋)
- 세션 4~6: LOD β-1 → β-6 (3-4 커밋, atomic 단계마다)

---

## 10. 시작 신호

이 계획서를 사용자가 승인하면 **세션 1 (MR-B1 + MR-C1)** 부터 진입. 각 세션
종료 시점에 빌드 + 검증 + 커밋 + 다음 진입 확인. 중간 어디서든 일시 정지 +
재조정 가능. LOD 진입 시점에서 별도 세부 계획서 작성 가능 (β-1~β-6 atomic
구조 + 셰이더 변경 상세).
