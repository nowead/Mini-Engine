# Medical Volume Rendering — Folder Index

Documents for Mini-Engine's medical volume renderer track (2026-05-29~).

---

## Start here

- **[MEDICAL_VOLUME_ROADMAP.md](MEDICAL_VOLUME_ROADMAP.md)** — strategy, M1~M4
  milestones, progress log, next candidates. **Read this first if you are
  new to the track.**
- **[VIEWERS.md](VIEWERS.md)** — user guide. How to build, control, and
  read the feature/tech stack of `volume_viewer` (native) and
  `volume_viewer_wasm` (browser).

---

## Learning

For a structured introduction to volume rendering and a walk-through of how
Mini-Engine implements it:

- **[learning/VOLUME_RENDERING_PRIMER.md](learning/VOLUME_RENDERING_PRIMER.md)** —
  from fundamentals (ray marching, transfer functions, gradient shading) to
  Mini-Engine-specific techniques (brick atlas + page table, LOD streaming,
  path tracing). Every concept points to the source file that implements it.

---

## Architecture decisions ([decisions/](decisions/))

| Document | Decision |
| --- | --- |
| [ADR-2026-07-22_MOBILE_TARGET_TIERING.md](decisions/ADR-2026-07-22_MOBILE_TARGET_TIERING.md) | Per-tier volume-size targets: iPhone → ~256 MB (chest CT), iPad+ → ~1 GB (full chest/abdomen), desktop → multi-GB. iPhone whole-body CT deferred as future work; disk-paging architecture (Y2/v1-γ) not pursued now. |

---

## Track plans ([plans/](plans/))

| Document | Track |
| --- | --- |
| [M3-3_V1_STREAMING_PLAN.md](plans/M3-3_V1_STREAMING_PLAN.md) | M3-3 v1-α streaming design + completion log |
| [V1_BETA_AND_MR_PLAN.md](plans/V1_BETA_AND_MR_PLAN.md) | M3-3 v1-β LOD + MR + CPU pack (β-1~β-6) |
| [DICOM_IMPLICIT_VR_PLAN.md](plans/DICOM_IMPLICIT_VR_PLAN.md) | DICOM Implicit VR LE parser + tag dictionary |
| [DICOM_COMPRESSED_PLAN.md](plans/DICOM_COMPRESSED_PLAN.md) | DICOM compressed transfer syntax (RLE + JPEG 2000) |
| [DISK_PAGING_PLAN.md](plans/DISK_PAGING_PLAN.md) | Disk paging Steps 1-3 (VoxelSource + NIfTI mmap + on-the-fly mip) |
| [DISK_PAGING_STEP5_PLAN.md](plans/DISK_PAGING_STEP5_PLAN.md) | Disk paging Step 5 (mmap int16 -> brick-pack conversion) |
| [WASM_OPENJPEG_PLAN.md](plans/WASM_OPENJPEG_PLAN.md) | WASM OpenJPEG build (browser JPEG 2000 DICOM) |
| [DICOM_JPEG_LEGACY_PLAN.md](plans/DICOM_JPEG_LEGACY_PLAN.md) | DICOM JPEG Baseline / Extended / Lossless P14·SV1 (libjpeg-turbo, native) |
| [WASM_LIBJPEG_TURBO_PLAN.md](plans/WASM_LIBJPEG_TURBO_PLAN.md) | WASM libjpeg-turbo build (browser JPEG legacy) |
| [PATH_TRACE_POLISH_PLAN.md](plans/PATH_TRACE_POLISH_PLAN.md) | M4 v2 path-trace polish — IBL + A-trous denoiser + accumulation cap (P1..P3.1 done; P3.2/P3.3 deferred) |
| [REAL_MRI_VERIFICATION_PLAN.md](plans/REAL_MRI_VERIFICATION_PLAN.md) | real MR bundle + preset tuning + runtime DICOM upload + FPS/memory HUD (R1..R4) — 완료 |
| [X1_MOBILE_WEBGPU_MATRIX.md](plans/X1_MOBILE_WEBGPU_MATRIX.md) | (축 X 저사양/모바일) — mobile WebGPU 지원 매트릭스 + 엔진 스택 호환성 진단 + X3 실측 방법론 |
| [Y1F_CHUNKED_STAGING_PLAN.md](plans/Y1F_CHUNKED_STAGING_PLAN.md) | (축 Y) — Z-slab staging 로 iPhone 192 MB 재도전. **완료 2026-07-22** — [BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md](baselines/BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md) |
| [Y1C_FULL_PROGRESSIVE_PLAN.md](plans/Y1C_FULL_PROGRESSIVE_PLAN.md) | (축 Y) — Volume3D intensity 우회 · 브릭 progressive fill. **완료 2026-07-22** — [BASELINE_2026-07-22_Y1C_FULL.md](baselines/BASELINE_2026-07-22_Y1C_FULL.md) |

---

## Timestamped baselines ([baselines/](baselines/))

| Document | Measurement |
| --- | --- |
| [BASELINE_2026-06-03.md](baselines/BASELINE_2026-06-03.md) | M3-3 v0 + M4 v1 reference |
| [BASELINE_2026-06-04_V1_ALPHA.md](baselines/BASELINE_2026-06-04_V1_ALPHA.md) | v1-α auto-size, streaming auto-engage, honest limits |
| [BASELINE_2026-06-07_V1_BETA.md](baselines/BASELINE_2026-06-07_V1_BETA.md) | v1-β LOD (missing brick -86%, LOD seam limit) |
| [BASELINE_2026-06-07_DISK_PAGING.md](baselines/BASELINE_2026-06-07_DISK_PAGING.md) | Disk paging Steps 1-3 (1024³ settled RAM -11%, peak 6.57 GB) |
| [BASELINE_2026-06-10_DISK_PAGING_STEP5.md](baselines/BASELINE_2026-06-10_DISK_PAGING_STEP5.md) | Disk paging Step 5 (1024³ peak 6.57 -> 2.30 GB, -65%) |
| [BASELINE_2026-07-08_REAL_MRI.md](baselines/BASELINE_2026-07-08_REAL_MRI.md) | Real MRI verification (R4): 4 public series, runtime upload path, memory + CPU baselines |
| [BASELINE_2026-07-09_BRICK_SHAPE.md](baselines/BASELINE_2026-07-09_BRICK_SHAPE.md) | Brick shape flexibility (Option C): thin-volume overhead +7754% → +6~19%, 70× memory reduction on single-slice cases |
| [BASELINE_2026-07-09_LAST_BRICK_SHRINK.md](baselines/BASELINE_2026-07-09_LAST_BRICK_SHRINK.md) | Last-brick shrink (Option C tail): Siemens +19% → +6%; all four R4 series now ≤ +6% vs dense |
| [BASELINE_2026-07-14_MOBILE_MATRIX.md](baselines/BASELINE_2026-07-14_MOBILE_MATRIX.md) | X3 mobile matrix + Z1 + X4 v1 검증: RTX 4070 (T-high) + iPhone iOS 18.7 두 브라우저 (KakaoTalk WKWebView + Safari.app 26.5). iOS 18 WebGPU 기본 활성 · 저부하 iPhone > RTX 역전 · pt_spp4 Safari.app 3× 격차 · X4 policy 로 iPhone 첫 클릭 40ms → 15.7ms (7×). |
| [BASELINE_2026-07-15_SYNTHETIC_128MB.md](baselines/BASELINE_2026-07-15_SYNTHETIC_128MB.md) | Y3 first-cut (desktop): 512×512×256 (128 MB) 합성 CT 로드 · Static 모드 (139 MB atlas, +9% overhead) · read+write 10.4 s + decode 842 ms · CPU 5.34 ms/frame · Y1b 필요 여부 판정을 모바일 재실측으로 연기. |
| [BASELINE_2026-07-20_MOBILE_Y3_BISECT.md](baselines/BASELINE_2026-07-20_MOBILE_Y3_BISECT.md) | **Y3 mobile 바이섹션**: iPhone Safari 실용 상한 **96 MB 확정, 128 MB 크래시** (Safari kill+reload / Chrome freeze). Y1b (per-slice streaming) 필요성 정량 입증 · 예상 임팩트 memory peak 780→520 MB (-260 MB). |
| [BASELINE_2026-07-21_Y1B_MOBILE_128MB.md](baselines/BASELINE_2026-07-21_Y1B_MOBILE_128MB.md) | **Y1b 성과 실증**: `loadDicomSeries` two-pass refactor → iPhone Safari 128 MB **로드 성공, 63 fps 안정** (07-20 baseline 이 예측한 상한 확장 정확 실현). 192 MB 는 여전히 freeze (intensity + atlas 동시 alive peak 남음). 원 목표 "저사양 모바일 대용량 실 CT" 첫 실증. |
| [BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md](baselines/BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md) | **Y1c-lite + Y1d + Y1e 조합 실측**: mobile peak 956 → 668 MB (-30%) 축소. 192 MB 는 여전히 crash → iPhone 물리 프로세스 예산 ~600-650 MB 확정. 128 MB 는 안정 유지. 다음 후보 Y1f (chunked staging) · Y1c-full (intensity 우회). |
| [BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md](baselines/BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md) | **Y1f Z-slab staging**: `BrickedVolume::build()` Static path 를 slab (~44 MB WASM) 단위로 재구성. peak 668 → 599 MB (-69 MB). **iPhone Safari 192 MB CT (512×512×384) 로드 최초 실증** — 흉부/복부 임상 CT 범위 진입. 128 MB regression 무. |
| [BASELINE_2026-07-22_Y1C_FULL.md](baselines/BASELINE_2026-07-22_Y1C_FULL.md) | **Y1c-full progressive**: DICOM slice-by-slice → BrickedVolume ring buffer (66 slice × W × H) → row-완성 시 즉시 upload. Peak 701 → 606 MB (-95 MB) on 256 MB volume. **iPhone Safari 256 MB CT (512³) 로드 성공** — ADR-2026-07-22 iPhone 티어 목표 달성. Static-only (Streaming 은 Y2 대기). UI safe-area 픽스 포함. |

---

## Folder rules

- New track plans: `plans/*_PLAN.md` (e.g. `JPEG_LOSSLESS_PLAN.md`).
- Timestamped measurements: `baselines/BASELINE_YYYY-MM-DD_*.md`.
- Learning material: `learning/*.md`.
- When a track closes, add an entry to MEDICAL_VOLUME_ROADMAP's progress log
  and to the table in this README.
