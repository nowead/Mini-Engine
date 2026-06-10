# M3-3 v1-α Baseline Measurements — 2026-06-04

**Build**: commit `d748814` (v1-3 핵심 본체 + auto-size 개선 + WARN).
**GPU**: NVIDIA GeForce RTX 4070 (Vulkan native).
**OS**: Windows 11 Home.
**측정 도구**: ImGui 패널의 D3 Phase 1 stats 섹션 + v1-α 신규 4줄 (mode /
visible resident-missing / 매 프레임 uploaded-evicted).
**전임 baseline**: [BASELINE_2026-06-03.md](BASELINE_2026-06-03.md) (v0 + M4 v1).

이 문서의 목적: **M3-3 v1-α (LRU streaming + memory-budget auto-size)** 위에서
같은 매트릭스를 다시 측정해 v0 대비 진전 + 한계를 정직히 기록한다. v1-β
(streaming 성능 최적화 / LOD) 작업의 비교점.

---

## 1. 측정 매트릭스

3 케이스 × Lambert 모드. 카메라 정지 후 ~2초 안정화 + (Streaming 케이스의
경우) 추가 ~5초 더 (점진 페이지인 수렴 위해).

| Case | 데이터 | dim | Dense MB |
| --- | --- | --- | --- |
| A | `test_1024.nii` (default sphere) | 1024×1024×512 | 1024.0 |
| B | `test_1024.nii` + `--atlas-cap 2` (강제 stress) | 1024×1024×512 | 1024.0 |
| C | `test_dense_1024.nii` (dense full cube, tissue_frac=1.0) | 1024×1024×1024 | 2048.0 |

---

## 2. Atlas + 모드

| Case | Page grid | Atlas grid | Slots used / total | Atlas alloc MB | Saving vs dense | Mode |
| --- | --- | --- | --- | --- | --- | --- |
| A | 16×16×8 (2048) | **7×7×7** (343) | 304/343 (88.6%) | **188.1** | **+81.6%** | Static |
| B | 16×16×8 (2048) | 2×2×2 (8) | 8/8 (100%) | 4.4 | +99.6% | **Streaming** |
| C | 16×16×16 (4096) | **9×10×10** (900) | 408/900 (45%) | **493.5** | **+75.9%** | **Streaming** |

### v0 → v1-α 비교 (Case A 기준)

| 지표 | v0 (2026-06-03) | v1-α (2026-06-04) | 변화 |
| --- | --- | --- | --- |
| Atlas grid | 8×8×8 (auto cap) | 7×7×7 (cbrt fit) | -33% slot 수 |
| Atlas alloc MB | 280.8 | 188.1 | **-33% 메모리** |
| Mode | Static | Static | 동일 (회귀 0) |
| 시각 결과 | 정상 | 정상 | 동일 |

→ Auto-size 재작성의 직접 결과. **시각 손상 없이 188 MB로 1024³ × 81.6% sparse
저장 유지**.

### Streaming 자동 진입 (Case C)

- 1024×1024×1024 = 2 GB dense → pageGrid 4096 brick
- 이전 v0 auto cap (8,8,8) = 292 MB로는 표현 불가 → atlas-full 에러 반환
- v1-α: 자동 산정이 budget(512 MB) 안에서 (9,10,10)=900 slots = 493 MB로 사이징
- 2728 non-empty > 900 slots → **자동으로 Streaming 진입 + WARN 로그 출력**:

```text
[WARN][BrickedVolume] streaming mode (atlas too small): 2728 non-empty bricks vs 900 atlas slots.
When the camera sees more than 900 bricks at once, the excess will render as empty.
Pass atlasGrid (14,14,14) = 2744 slots (~1504 MB) for guaranteed Static rendering.
Streaming is best when total bricks > atlas but visible-set << atlas (zoom-in workflows on large volumes).
```

권장값 정확히 계산됨 (`ceil(cbrt(2728)) = 14`).

---

## 3. Streaming 동적 통계 (카메라 정지 후)

| Case | Frustum-visible | non-empty | resident | missing | uploaded/frame | evicted/frame |
| --- | --- | --- | --- | --- | --- | --- |
| A | 2048 | 304 | (Static, 항상 304) | 0 | n/a | n/a |
| B | 2048 | 304 | **8** | **296** | 0 | 0 |
| C | 4096 | 2728 | **408** | 2320 | **8** | 0 |

### 발견 1: LRU가 가시 brick 보호

B에서 정지 시 `+0/-0` — atlas 8 slots 모두 가시 brick이라 `lastFrameUsed`가 매
프레임 bumped → eviction 후보 0. **현재 보는 brick은 절대 evict되지 않음.**
의도된 정책.

### 발견 2: visible > atlas일 때 hole

B의 결과적 동작: 8개만 채워지고 나머지 296개는 영영 missing → 시각적으로 hole.
C도 같은 패턴(900 slot에 visible 4096 들어옴) — 진행 중 ~900까지 채우다 멈춤.

**이는 streaming의 한계 자체**:
- 적합: 총 brick > atlas BUT 매 시점 가시 brick << atlas (줌인 워크플로)
- 부적합: 매 시점 가시 brick > atlas (전체 보기) — 이 경우는 더 큰 atlas
  필요 (권장값 로그가 안내) 또는 LOD (v1-β)

---

## 4. 렌더 성능

| Case | Mode | FPS | ms/frame | Render CPU (ms) |
| --- | --- | --- | --- | --- |
| A | Static | 437.0 | 2.29 | 0.39 |
| B | Streaming (정지, no churn) | 745.3 | 1.34 | 0.43 |
| C | Streaming (적극 채움 중) | 12.6 | **79.66** | **76.97** |

### 관찰

**B의 FPS 745**: 화면 대부분이 hole(빠른 early termination) + per-frame
streaming 0 → render는 거의 무비용. 시각이 빈 만큼 빨라진 것 — **misleading
metric**.

**C의 12.6 FPS**: 매 프레임 brick 8개 CPU pack (66³ 좌표 변환 + std::clamp ×3
+ 3D 캐시 미스). 약 ~10 ms/brick × 8 = ~80 ms. Render CPU ≈ 전체 시간 →
**전적으로 streaming CPU 비용에 bound**. GPU march 자체는 부담 안 큼.

→ v1-β 최적화 후보:
- **row-memcpy fast path**: brick 내부(halo 안)는 src row가 연속 → memcpy
  가능. ~10× 개선 가능
- **SIMD pack**: AVX2로 한 row 32 voxel 병렬
- **async CPU pack**: streaming dispatch와 별도 스레드. main thread는 GPU
  submit만

v1-α 범위에서는 정확성·동작 + 검증까지로 마감.

---

## 5. 헤드라인 (v1-α 이후)

> RTX 4070 / Vulkan 네이티브 / 1280×720 기준:
>
> - **1 GB 합성 CT (sparse)**: atlas auto-size로 v0 대비 **메모리 -33%
>   (281 → 188 MB)**, Static 유지, 시각·성능 무회귀
> - **2 GB 합성 CT (dense)**: v0에서 atlas-full 실패하던 케이스를 v1-α는
>   **자동 Streaming 진입**(493 MB atlas) + 권장값 WARN. 정지·줌인 시 가시
>   영역 점진 채움
> - **명시적 atlas-cap stress**: streaming의 정직한 한계 노출 — 시각
>   부분 hole, FPS는 misleading(빈 화면이라 빠름), 권장값 WARN으로
>   recovery path 제시

---

## 6. M3-3 v1-α 마감 + 다음

v1-α 범위 (frustum cull + LRU + incremental upload + auto-size + WARN) 완전
동작. 양 백엔드 (Vulkan + WebGPU) 동등 검증.

**v1-β (별도 트랙) 후보**:
- Streaming CPU pack 최적화 (row-memcpy / SIMD / async) — Case C의 12.6 FPS
  본질적 해결
- **LOD (다중 해상도 brick)** — visible >> atlas 케이스를 hole 없이 표현. 멀리
  있는 brick은 1/8 다운샘플. 의료영상 진단 워크플로에서 가장 큰 가치
- **디스크 페이징** — 4 GB+ 임상 데이터 (CPU RAM 한계 초과)
- **Predictive prefetch** — 카메라 속도 기반 다음 프레임 가시 brick 미리 페이지인

---

## 7. 재현 명령

```powershell
# Case A
.\build-windows\volume_viewer.exe test_1024.nii

# Case B (강제 stress)
.\build-windows\volume_viewer.exe test_1024.nii --atlas-cap 2

# Case C (1024^3 dense 자동 streaming)
python scripts\make_synthetic_nii.py test_dense_1024.nii 1024 1024 1024 1.0 0.4
.\build-windows\volume_viewer.exe test_dense_1024.nii
```

ImGui 패널의 `Brick storage (M3-3)` 섹션 8줄 + WARN 콘솔 로그로 위 표가
재현된다.
