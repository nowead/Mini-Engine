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

---

## Timestamped baselines ([baselines/](baselines/))

| Document | Measurement |
| --- | --- |
| [BASELINE_2026-06-03.md](baselines/BASELINE_2026-06-03.md) | M3-3 v0 + M4 v1 reference |
| [BASELINE_2026-06-04_V1_ALPHA.md](baselines/BASELINE_2026-06-04_V1_ALPHA.md) | v1-α auto-size, streaming auto-engage, honest limits |
| [BASELINE_2026-06-07_V1_BETA.md](baselines/BASELINE_2026-06-07_V1_BETA.md) | v1-β LOD (missing brick -86%, LOD seam limit) |
| [BASELINE_2026-06-07_DISK_PAGING.md](baselines/BASELINE_2026-06-07_DISK_PAGING.md) | Disk paging Steps 1-3 (1024³ settled RAM -11%, peak 6.57 GB) |
| [BASELINE_2026-06-10_DISK_PAGING_STEP5.md](baselines/BASELINE_2026-06-10_DISK_PAGING_STEP5.md) | Disk paging Step 5 (1024³ peak 6.57 -> 2.30 GB, -65%) |

---

## Folder rules

- New track plans: `plans/*_PLAN.md` (e.g. `JPEG_LOSSLESS_PLAN.md`).
- Timestamped measurements: `baselines/BASELINE_YYYY-MM-DD_*.md`.
- Learning material: `learning/*.md`.
- When a track closes, add an entry to MEDICAL_VOLUME_ROADMAP's progress log
  and to the table in this README.
