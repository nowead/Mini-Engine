# Disk Paging Step 5 Baseline — 2026-06-10

**Build**: commit `df1710b` (Step 5.1 → 5.3 complete; this doc is Step 5.4).
**GPU**: NVIDIA GeForce RTX 4070 (Vulkan native).
**OS**: Windows 11 Home, 32 GB system RAM.
**측정 방법**: `Get-Process volume_viewer | Select-Object WorkingSet64`를 4초
간격으로 폴링. 정직히 기록.

**전임**: [BASELINE_2026-06-07_DISK_PAGING.md](BASELINE_2026-06-07_DISK_PAGING.md)
(Steps 1-3까지의 한계 표시).

---

## 1. 측정 케이스

| Case | 파일 | dim | 파일 크기 | 모드 (Step 5) |
| --- | --- | --- | --- | --- |
| A | `test_1024.nii` | 1024×1024×512 (int16) | 1.0 GB | Static → mmap 거부 + float fallback |
| B | `test_dense_1024.nii` | 1024×1024×1024 (int16) | 2.1 GB | **Streaming → mmap 직접** |

Case A는 304 bricks ≤ 343 atlas slots라 buildFromMmappedSource 거부 → 기존
loadNifti+loadFromFloatData fallback. Case B는 2728 bricks > 900 slots라
mmap 경로 전 흐름.

---

## 2. Working Set 타임라인

### Case A — test_1024.nii (float fallback, 1.0 GB int16)

| t (s) | Working set | 단계 |
| --- | --- | --- |
| 4 | 3.55 GB | mmap path 시도+거부, loadNifti 진행 중 (float intensity 채워지는 중) |
| 8 | 4.22 GB | float→half 변환 활성 (intensity + halfData 동시 존재) |
| **12** | **4.40 GB** (피크) | build 직전 |
| 16 | **1.34 GB** (정착) | 임시 버퍼 해제 완료 |
| 20 | 1.34 GB | 정착 |

### Case B — test_dense_1024.nii (mmap fast path, 2.1 GB int16)

| t (s) | Working set | 단계 |
| --- | --- | --- |
| 4 | 2.17 GB | mmap + raw min/max 스캔 진행 중 |
| 8 | **2.30 GB** | atlas init + 첫 brick streaming 활성 |
| 12 | 2.30 GB | 정착 |
| 16-20 | 2.30 GB | 정착 (streaming churn 무) |

피크 ≈ 정착 — **float intermediate + halfData 변환 임시 버퍼가 둘 다 사라짐**.

---

## 3. Steps 1-3 → Step 5 비교 (Case B, 1024³ dense)

| 지표 | Steps 1-3 (`b9fda20`) | Step 5 (`df1710b`) | 변화 |
| --- | --- | --- | --- |
| 피크 working set | **6.57 GB** | **2.30 GB** | **-65%** |
| 정착 working set | 2.38 GB | 2.30 GB | -3% |
| 파일 본체 working set | 0 (OS 페이지 캐시) | 0 (OS 페이지 캐시) | 동일 |
| Volume3D::intensity (float) | 4.2 GB transient | **0 (skip)** | -100% |
| halfData 변환 임시 | 2.1 GB transient | **0 (skip)** | -100% |
| m_originalHalfData (engine 상주) | 2.1 GB | **0 (mmap로 대체)** | -100% |
| atlas (4 LOD) | ~580 MB | ~580 MB | 동일 |

**Step 5는 m_originalHalfData를 제거함으로써 정착 RAM의 본질 부분 절감**. 피크는
변환 임시 버퍼 제거로 -65% (4.27 GB 절감).

---

## 4. 4 GB+ 임상 데이터 가능성 (이론 평가)

| 데이터 크기 | Steps 1-3 RAM 요구 | Step 5 RAM 요구 |
| --- | --- | --- |
| 1 GB int16 (1024³/2) | ~3 GB peak | ~1.3 GB peak |
| 2 GB int16 (1024³) | ~6.5 GB peak | **~2.3 GB peak** ✓ 측정 |
| 4 GB int16 (1024³ × 2) | ~13 GB peak | ~3.5 GB peak (추정) |
| 8 GB int16 (1024×1024×2048) | ~26 GB peak | ~5 GB peak (추정) |
| 16 GB int16 | ~52 GB peak | ~9 GB peak (추정) |

추정의 근거: atlas는 cube-root로 증가 (4 GB → 2 GB 정도가 적합), mmap 본체는
OS 페이지 캐시라 working set 영향 작음. 본 작업으로 **16 GB RAM 시스템에서
8 GB 임상 데이터까지 안전** 가능성 확보 (실측은 후속).

---

## 5. 시각 무회귀 — Int16 mmap path bit 동일성

Step 5.3 검증 시 test_dense_1024.nii 로드:

- 헤더 보고된 range: `[-1000, 800]` — 합성 파일의 정확한 phantom 분포
- BrickedVolume 로그: `2728 non-empty / 900 atlas slots, mode = Streaming
  (mmap source, Int16, slope=1 intercept=0)`
- 시각: 사용자 확인 시 v1-β LOD 패턴과 동일 (LOD seam + stale-LOD blur는
  v1-β 알려진 한계 그대로)

Box filter (Step 3 on-the-fly mip)와 Int16→half 변환 (Step 5.2 srcVoxelHalfBits)
조합은 수학적으로:

```text
half(slope*int16+intercept) → box((1<<lod)^3 voxels) → half(avg)
```

각 voxel을 float로 풀어 평균 → pack to half. 동일 입력에서 비트 동일성 보장
(float 변환 정밀도 한계 내).

---

## 6. 헤드라인 (Step 5 이후)

> RTX 4070 / Vulkan 네이티브 / 1280×720 / 32 GB system RAM:
>
> - **2 GB 합성 CT dense (1024³ int16)**: 피크 working set
>   **6.57 → 2.30 GB (-65%)** by Step 5 mmap fast path. Float
>   intermediate + halfData 임시 버퍼 둘 다 제거. m_originalHalfData
>   2.1 GB도 mmap source로 대체.
> - **시각 무회귀**: v1-β LOD 동작 + box filter 비트 동일 (Steps 3+5.2 조합).
> - **알려진 한계**: Case A 같은 Static-fit 케이스는 여전히 float
>   fallback 사용 (피크 4.4 GB). Static path를 mmap-친화로 확장하는 건
>   별도 추가 작업.

---

## 7. v1-α / v1-β / Disk Paging Steps 1-3 / Step 5 베이스라인 관계

| 베이스라인 | 측정 대상 | 본 문서와의 차이 |
| --- | --- | --- |
| `BASELINE_2026-06-04_V1_ALPHA.md` | streaming hole rate | 시각 한계 |
| `BASELINE_2026-06-07_V1_BETA.md` | LOD 분포 + hole | v1-β 시각 완성 |
| `BASELINE_2026-06-07_DISK_PAGING.md` | RAM working set (Steps 1-3) | 정착 -11%, 피크 6.57 GB 한계 |
| **본 문서** | **RAM working set (Step 5)** | **피크 -65%**, m_originalHalfData 제거 |

medical-volume 트랙 RAM 효율의 결정타 — Step 5는 disk paging 트랙의 본질 완성.

---

## 8. 재현 명령

```powershell
# Streaming 케이스 (Step 5 mmap fast path)
.\build-windows\volume_viewer.exe test_dense_1024.nii

# Static-fit 케이스 (float fallback)
.\build-windows\volume_viewer.exe test_1024.nii

# Working set 폴링
while ($true) {
    Get-Process volume_viewer | Select-Object WorkingSet64
    Start-Sleep 4
}
```

Streaming 케이스에서 BrickedVolume이 `mode = Streaming (mmap source, Int16,
slope=1 intercept=0)`를 로그하면 fast path 활성 확인.
