# M3-3 v1-β Baseline Measurements — 2026-06-07

**Build**: commit `b94b31c` (LOD β-4 + β-5: multi-LOD streaming + shader sampling).
**GPU**: NVIDIA GeForce RTX 4070 (Vulkan native).
**OS**: Windows 11 Home.
**측정 도구**: ImGui 패널의 `Brick storage (M3-3)` 섹션. v1-β에서 stats 표시
분모 버그 수정(streaming은 `kLodLevels * single-LOD slots` 기준).
**전임 baseline**: [BASELINE_2026-06-04_V1_ALPHA.md](BASELINE_2026-06-04_V1_ALPHA.md).

이 문서의 목적: **M3-3 v1-β LOD (multi-resolution brick streaming + shader sampling)**
위에서 v1-α의 핵심 한계 — "visible >> atlas slots ⇒ 시각 hole" — 가 어느
정도 본질 해결되었는지 정직히 기록한다.

---

## 1. 측정 매트릭스

v1-α와 동일한 3 케이스 × Lambert 모드. 카메라 정지 후 ~2초 안정화
(Streaming은 +5초 더).

| Case | 데이터 | dim | Dense MB |
| --- | --- | --- | --- |
| A | `test_1024.nii` (default sphere) | 1024×1024×512 | 1024.0 |
| B | `test_1024.nii --atlas-cap 2` (강제 stress) | 1024×1024×512 | 1024.0 |
| C | `test_dense_1024.nii` (dense full cube) | 1024×1024×1024 | 2048.0 |

---

## 2. Atlas + 모드

v1-β에서는 Streaming 모드일 때 L0..L3 4개의 atlas texture가 나란히 할당된다
(각각 `atlasGrid` 슬롯 그리드). 합계 메모리는 L0 단독의 ~1.16배(1 + 1/8 +
1/64 + 1/512 = 1.158).

| Case | Page grid | Atlas grid | Slots used / total (×L0..L3) | Atlas alloc MB | Mode |
| --- | --- | --- | --- | --- | --- |
| A | 16×16×8 (2048) | 7×7×7 (343) | 304/343 (88.6%) | 188.1 | Static |
| B | 16×16×8 (2048) | 2×2×2 (8) | 32/32 (100%, 4 LOD) | ~5.1 | **Streaming** |
| C | 16×16×16 (4096) | 9×10×10 (900) | 1414/3600 (39.3%, 4 LOD) | ~572 | **Streaming** |

### v1-α stats 표시 버그(이번에 같이 수정)

v1-α 뷰어는 `usedSlots()`가 모든 LOD 합인데 분모는 단일 LOD의 `totalSlots()`만
썼다. 결과적으로 streaming 모드에서 "32/8 (400%)" / "1414/900 (157%)" 같은
비정상 값이 표시됐다. v1-β 빌드부터는 streaming일 때 분모를 `total *
kLodLevels`로, allocated bytes도 ×1.16으로 보정해 출력한다.

### Streaming + LOD 자동 동작 (Case C)

- 1024³ dense, 2728 non-empty bricks
- 단일 LOD atlas 900 slots → v1-α에서는 visible 4096 중 최대 900까지만 채움(나머지 hole)
- v1-β: 4 LOD atlas 3600 slots → 카메라 정착 시 거의 다 들어감(아래 §3)

---

## 3. Streaming 동적 통계

### Case A (Static — 회귀 무)

| Mode | 304/343 slots, 188.1 MB, 461.3 FPS, render CPU 0.40 ms |

v1-α와 동일. Static path 변경 없음 확인.

### Case B (atlas-cap 2 강제 stress)

| 항목 | 값 |
| --- | --- |
| Mode | Streaming |
| Atlas | 2×2×2 (8 slots × 4 LOD = 32 total) |
| Visible non-empty | 183 |
| Resident | 32 (모든 LOD atlas 가득) |
| Missing | 151 (시각 hole) |
| this frame | +0 / -0 (LRU가 visible 보호) |
| LOD distribution(selection) | L0=183, L1=L2=L3=0 |
| FPS | 178.1 |

**결과**: 시각적으로 확실한 hole. 하지만 이는 atlas-cap 2가 강제한 본질적
한계 — 4 LOD 합 32 슬롯에 183개 brick은 들어갈 수 없다. v1-α와 동일하게
"명시적 stress 케이스에서 hole이 발생함"을 보여주는 정직 측정값.

### Case C (dense 1024³ Streaming)

| 시점 | Resident | Missing | LOD(selection) | FPS |
| --- | --- | --- | --- | --- |
| zoom-in (front 절반 frustum) | 334 | 2394 | L0=0 L1=2285 L2=443 | 12.6 |
| zoom-out (전체보기) | 1219 | 326 | L0=731 L1=814 L2=0 | 19.3 |

**v1-α → v1-β 비교 (Case C zoom-out)**

| 지표 | v1-α | v1-β | 변화 |
| --- | --- | --- | --- |
| Resident bricks | 408 | 1219 | **+199%** |
| Missing bricks | 2320 (~85% hole) | 326 (~21% hole) | **-86%** |
| Atlas slots 활용 | 408/900 (단일 LOD) | 1414/3600 (4 LOD) | 약 3.5배 |
| 시각 결과 | 화면 대부분 hole | 대부분 가시 (가장자리 hole + LOD seam) | **v1-β 본질 목표 달성** |

`+64/-63` 균형은 카메라 움직임 중의 churn(visible-set 변동 시 LRU가 과거
가시 brick을 evict + 새 가시 brick upload). 정지 + K=64/frame이면 ~5 프레임
안에 326 missing 채워진다.

---

## 4. 렌더 성능

| Case | Mode | FPS | render CPU (ms) | 비고 |
| --- | --- | --- | --- | --- |
| A | Static | 461.3 | 0.40 | v1-α와 동일 |
| B | Streaming | 178.1 | 0.52 | atlas-cap 2, 거의 빈 화면 |
| C zoom-in | Streaming, 채움 중 | 12.6 | 72.11 | brick CPU pack 비용 |
| C zoom-out | Streaming, 거의 가득 | 19.3 | 38.69 | 채움 진행 + LOD mix |

**관찰**

- Render CPU의 큰 비중은 여전히 brick CPU pack (`packBrickToStaging` ~10 ms/brick × K=64 = 50-70 ms). v1-β 본 작업의 범위 밖. 차기 후보: row-memcpy + SIMD (계획서 §4 CPU pack 트랙).
- K=8 → K=64 budget 증가로 초기 채움 5.6 s → ~0.7 s. 카메라 회전 시 churn도 더 빨리 흡수.

---

## 5. 시각 품질 — 한계와 트레이드오프

v1-β LOD가 "visible >> atlas"의 hole 케이스는 본질 해결했지만, multi-LOD
특유의 새로운 시각 이슈가 등장한다. 정직히 기록한다.

### 5.1 LOD 경계 seam

L0 brick과 L1 brick이 인접할 때, 두 brick의 샘플링 해상도가 다르므로 ray가
경계를 가로지를 때 density 값 / gradient 불연속이 발생 → **수직/수평 선 형태의
시각적 seam**.

스크린샷 Case C zoom-out에서 확연. 의료 영상 워크플로에서 "이 선이 해부
구조인가, 렌더링 아티팩트인가"가 헷갈리는 지점.

**완화 방향(β-6 범위 밖)**:
- 인접 LOD를 동시 sample → 부드럽게 blending (성능 비용 ×2)
- 경계 영역을 강제로 같은 LOD로 통일 (region-based LOD selection)
- 경계 brick만 L0로 유지 (LOD selection bias)

### 5.2 No-migration 결과 stale-LOD blur

설계 결정: 한 번 atlas에 들어간 brick은 LOD 변경되어도 그대로 둠 (시각
안정성 우선, churn 회피). 결과적으로 카메라가 줌인된 후 일부 가까운 brick이
여전히 L1 잔류 → 약간 흐릿. **hole보다는 흐릿함이 받아들이기 쉬움** (사용자
검증 시 명시적 선호).

### 5.3 Case C zoom-in 시 L0=0

카메라가 줌인되어 frustum 안 brick이 모두 default 위치의 L1 잔류 → selection
이 L1 위주. 새로 frustum에 들어오는 brick만 L0 선택. 시각 영향 작음 (가까이
보이는 영역은 이미 L0 residue + 약간의 L1 mix).

---

## 6. 헤드라인 (v1-β 이후)

> RTX 4070 / Vulkan 네이티브 / 1280×720 기준:
>
> - **2 GB 합성 CT dense, 전체보기**: v1-α에서 ~85% hole이던 케이스를
>   v1-β는 **~21% hole**로 축소(미싱 brick 2320 → 326). 줌아웃 워크플로의
>   본질 한계 해결.
> - **메모리 비용**: L0 단독 493.5 MB → 4 LOD 합 572 MB(+16%). 추가 LOD 해상도가
>   1/8 + 1/64 + 1/512 = 14.4% 부피이기 때문에 메모리 오버헤드 작음.
> - **알려진 한계**: LOD 경계 seam(시각 아티팩트), stale-LOD blur(시각 안정성과
>   trade), Case B 같은 극단 stress(atlas 32 슬롯)는 여전히 hole 발생.

---

## 7. v1-β 마감 + 다음

v1-β 범위 (β-1 mip build → β-2 multi-LOD atlas → β-3 per-brick selection →
β-4 streaming honors LOD + LOD fallback + no migration + K=64 →
β-5 shader samples chosen LOD) 완전 동작. 양 백엔드(Vulkan + WebGPU) 동등
검증.

**v1-γ / 후속 후보**:
- LOD 경계 seam 완화 — dual-LOD blending 또는 region-based selection
- CPU pack 최적화 — row-memcpy + SIMD (계획서 §4 CPU pack 트랙, ~10× 가능)
- 디스크 페이징 — 4 GB+ 임상 데이터 (CPU RAM 한계 초과)
- Predictive prefetch — 카메라 속도 기반 미리 페이지인
- Screen-space pixel-aware LOD selection — 현재 distance 기반을 더 정밀화

---

## 8. 재현 명령

```powershell
# Case A
.\build-windows\volume_viewer.exe test_1024.nii

# Case B (강제 stress)
.\build-windows\volume_viewer.exe test_1024.nii --atlas-cap 2

# Case C (1024^3 dense 자동 streaming + LOD)
python scripts\make_synthetic_nii.py test_dense_1024.nii 1024 1024 1024 1.0 0.4
.\build-windows\volume_viewer.exe test_dense_1024.nii
```

ImGui 패널의 `Brick storage (M3-3)` 섹션에서 mode / slots / LOD distribution을
관찰할 수 있다.
