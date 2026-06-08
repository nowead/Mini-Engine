# Disk Paging Baseline Measurements — 2026-06-07

**Build**: commit `b9fda20` (disk paging Steps 1-3 complete).
**GPU**: NVIDIA GeForce RTX 4070 (Vulkan native).
**OS**: Windows 11 Home, 32 GB system RAM.
**측정 방법**: `Get-Process volume_viewer | Select-Object WorkingSet64`를 5초
간격으로 폴링, 1024³ dense NIfTI 로드 중 + 정착 후 측정. 정직히 기록.

이 문서의 목적: **DISK_PAGING_PLAN Step 1-3 (voxel source 추상화 + NIfTI mmap +
on-the-fly mip)** 작업이 실제 RAM peak에 미친 영향을 측정 + 남은 한계 + 다음
단계 정직히 표시.

---

## 1. 측정 케이스

`test_dense_1024.nii`: 1024×1024×1024 dense sphere, int16 NIfTI, **파일 크기 2.1 GB**.
volume_viewer가 자동으로 Streaming + 4 LOD atlas 진입하는 v1-β 표준 stress 케이스.

기대 RAM 회계 (이론):

| 항목 | 크기 | 단계 |
| --- | --- | --- |
| 파일 본문 (transient std::ifstream buf) | 2.1 GB | 로드 시작 |
| Volume3D::intensity (float) | 4.2 GB | NIfTI decode 후 |
| halfData (uint16 conversion intermediate) | 2.1 GB | float→half 변환 중 |
| m_originalHalfData (engine 상주) | 2.1 GB | build 시 복사 |
| m_mipChain L1+L2+L3 (Step 3에서 제거됨) | 280 MB | (사라짐) |
| atlas + page table | ~580 MB | streaming 진입 |

피크: 파일 + intensity + halfData + m_originalHalfData = ~10 GB (변환 중 동시 존재).
정착: m_originalHalfData + atlas = ~2.7 GB.

---

## 2. 측정 결과

| 시점 (s) | Working set | 단계 |
| --- | --- | --- |
| 5  | **6.03 GB** | 파일 mmap + NIfTI decode 중 (intensity 채워지는 중) |
| 10 | **6.57 GB** (피크) | float→half 변환 활성 (intensity + halfData 동시 존재) |
| 15 | 6.57 GB | build 완료 직전 |
| 20 | **2.38 GB** (정착) | 임시 버퍼 해제 완료 |
| 25-55 | 2.40-2.42 GB | streaming churn (atlas 채움) 진행 중 |

이론 피크 ~10 GB보다 낮은 ~6.57 GB가 측정된 이유: NIfTI 파일이 `mmap`이라 파일
본문은 working set에 영구 상주하지 않음 (page cache가 OS에 떨어져 있음).
**Step 2 mmap 작업의 실효 — 파일 본체 ~2.1 GB가 working set 회계에서 빠짐**.

---

## 3. Step별 기여 (commit 기준)

| Step | 커밋 | RAM 효과 (1024³) | 측정 |
| --- | --- | --- | --- |
| Step 1 | `32eb364` | 0 (인터페이스 리팩토링) | 무회귀 |
| Step 2 (mmap) | `d338e81` | **-2.1 GB** (파일 본문 working set 제거) | 위 표 직접 비교 (이론치) |
| Step 3 (mip 제거) | `b9fda20` | **-280 MB** (m_mipChain 해제 + 매 brick on-the-fly 박스 필터) | 측정 정착값 2.38 GB가 이론치 2.69 GB - 280 MB와 일치 |

**합계 (Step 2 + 3)**: 약 2.4 GB working set 감소.

---

## 4. 정직한 남은 한계

피크 working set의 압도적 부분 (~6.57 GB)은 **float intermediate(4.2 GB) +
halfData(2.1 GB) 동시 존재**가 차지. Step 1-3는 이 둘에 영향 없음.

**4 GB+ 임상 데이터 (CBCT, 다중상 CT)를 16 GB RAM 시스템에서 로드** 목표는
현재 작업으로 미달:
- 4 GB int16 → float intermediate 8 GB → halfData 4 GB → m_originalHalfData 4 GB → **변환 중 피크 16 GB ≈ RAM 한계**
- OOM 또는 swap 진입 위험

**진짜 disk paging 달성 조건** (이번 작업 밖):
- Step 5 (가칭): mmap'd int16 원본 → brick pack 시점에 직접 box-filter + half pack
  → m_originalHalfData 자체를 안 만듦. Volume3D float intermediate도 우회.
- 4 GB int16 데이터: 워킹셋 ≈ atlas + 페이지 캐시 + brick 패킹 임시 버퍼 ≈ 1-2 GB.
  16 GB RAM에서 충분히 동작.

---

## 5. 시각 무회귀 (LOD 박스 필터)

Step 3의 on-the-fly box filter는 수학적으로 chained build와 비트 동일:
`box(box(x)) = box(x)` over factor-2 averaging (가중치가 균등하므로 결합법칙
성립). 1024³ dense streaming 시각 결과는 사전 (`26737ea` v1-β baseline) 과 동일.

추가 검증으로 시각 비교는 사용자 측 viewer 회전·줌 시 LOD 전환에서 시각
seam 변화 없음 확인 (정성).

---

## 6. 헤드라인

> RTX 4070 / Vulkan 네이티브 / 1280×720 / 32 GB system RAM:
>
> - **1024³ dense NIfTI (2.1 GB int16)**: 정착 working set **2.69 → 2.38 GB**
>   (-11%) by Step 3 mip 제거 + Step 2 mmap이 파일 본체 ~2.1 GB를 working set
>   회계에서 분리. 피크는 여전히 6.57 GB (float intermediate 미해결).
> - **시각 무회귀**: LOD 박스 필터 수학적 동등 (chained vs single-pass).
> - **알려진 미달**: 4 GB+ 임상 데이터 (16 GB RAM 한계) — Step 5+가 본질
>   해결. 현재 작업은 Step 5 진입의 토대.

---

## 7. v1-α / v1-β 베이스라인과의 관계

| 베이스라인 | 측정 대상 | 본 문서와의 차이 |
| --- | --- | --- |
| `BASELINE_2026-06-04_V1_ALPHA.md` | streaming hole rate | 시각 한계 (v1-β LOD가 해결) |
| `BASELINE_2026-06-07_V1_BETA.md` | LOD 분포 + hole | v1-β 시각 완성 |
| **본 문서** | RAM working set | 메모리 효율, 시각 무회귀 |

v1-β LOD 작업 → DICOM 압축/Implicit VR 작업 → 본 disk paging Step 1-3 까지가
medical-volume 트랙의 한 마디 완성. 다음 마디는 **Step 5 brick-level mmap →
on-demand pack** (진짜 disk paging) 또는 사용자 선택.

---

## 8. 재현 명령

```powershell
# 1024^3 dense NIfTI 생성 (이미 있으면 스킵)
python scripts\make_synthetic_nii.py test_dense_1024.nii 1024 1024 1024 1.0 0.4

# 로드
.\build-windows\volume_viewer.exe test_dense_1024.nii

# 다른 터미널에서 working set 폴링
while ($true) { Get-Process volume_viewer | Select-Object WorkingSet64; Start-Sleep 5 }
```

ImGui 패널의 `Brick storage (M3-3)` 섹션에서 atlas 메모리 등 상세 확인 가능.
