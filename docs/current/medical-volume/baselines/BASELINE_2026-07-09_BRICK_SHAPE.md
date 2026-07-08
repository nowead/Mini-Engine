# Baseline 2026-07-09 — Brick Shape Flexibility (Option C)

Milestone: Tier-1 track from the post-R4 candidate list. Directly closes the
`+7754%` overhead observation from `BASELINE_2026-07-08_REAL_MRI.md` by
letting each brick's stored size shrink per-axis when the whole volume fits
in a single brick along that axis (pageGrid.axis == 1).

## Rule

```text
brickInterior.axis = (pageGrid.axis == 1) ? volSize.axis : 64
brickHalo.axis     = (pageGrid.axis  > 1) ? 1            : 0
brickStored.axis   = brickInterior.axis + 2 * brickHalo.axis
```

`pageGrid.axis == 1` means there is no neighbor brick along that axis, so
the halo has nothing to sample from — clamp-to-edge sampling handles the
boundary. Multi-brick axes keep the uniform 64 + 1 halo either side = 66
storage (unchanged behaviour).

Scope: **L0 atlas only, Static mode only.** L1..L3 keep uniform storage; the
streaming pack path still writes 66³ per brick, so we revert L0 sizing back
to uniform when the mode decision picks Streaming. Multi-LOD per-axis
extension is a follow-up (Option A tail).

## Same four series driven through the R3 runtime upload path

| Series | Dims | Atlas (before → after) | Memory alloc (before → after) | Overhead (before → after) |
| --- | --- | --- | --- | --- |
| `mr_emri_small`       | 64×64×10   | 66×66×66     → **64×64×10**   | 0.5 MB → **0.2 MB**  | +602%  → **+113%** |
| `mr_siemens_slice`    | 484×484×1  | 528×528×66   → **528×528×1**  | 35.1 MB → **0.5 MB** | +7754% → **+19%**  |
| `ct_jpeg2000_small`   | 512×512×1  | 528×528×66   → **528×528×1**  | 35.1 MB → **0.5 MB** | +6919% → **+6%**   |
| `mammo_jpeg_lossless` | 256×1024×1 | 264×1056×66  → **264×1056×1** | 35.1 MB → **0.5 MB** | +6919% → **+6%**   |

Confirmed by browser console log lines of the form
`built <WxHxD> -> page <pageGrid> ... (atlas <atlasVoxels>)` where the last
axis dropped from 66 to the volume's actual thin-axis size.

## Steady-state CPU (Lambert + shadow, ~30 s window)

| Series | Baseline mean/max | Option C mean/max |
| --- | ---:| ---:|
| `mr_emri_small`       |  6.56 / 7.15   ms | 3.48 / 4.36  ms |
| `mr_siemens_slice`    |  3.05 / 4.23   ms | 2.77 / 3.44  ms |
| `ct_jpeg2000_small`   | 12.07 / 255.40 ms | 2.99 / 3.71  ms |
| `mammo_jpeg_lossless` |  2.37 / 3.67   ms | 2.40 / 2.95  ms |

CPU actually improved for three of the four series, most dramatically for
the JPEG 2000 CT case (the 255 ms load-frame spike is gone -- smaller L0
atlas -> smaller texture upload -> shorter first-frame stall). The fMRI
case's ~50% CPU drop is likely due to the smaller L0 atlas (64×64×10 vs.
66×66×66) fitting more comfortably in GPU cache. Not the point of the
milestone; recorded for honesty.

## Reduction ratios (memory alloc)

- `mr_emri_small`: 0.5 → 0.2 MB = **2.5×**
- `mr_siemens_slice`: 35.1 → 0.5 MB = **70×**
- `ct_jpeg2000_small`: 35.1 → 0.5 MB = **70×**
- `mammo_jpeg_lossless`: 35.1 → 0.5 MB = **70×**

The overhead percentages tell the same story:

- Multi-brick axes with pageGrid > 1 still pay halo cost (that's where the
  residual +6~+19% comes from). Fully collapsing this is a Multi-LOD
  streaming per-axis follow-up.
- Single-brick axes (pageGrid == 1) now cost **zero** extra voxels along
  that axis. All four series had pageGrid.z == 1; the atlas z dimension
  dropped from 66 to 1 (Siemens/CT/Mammo) or from 66 to 10 (fMRI).

## No visual regression

Same volumes rendered before and after. All four series render identically
after the change (visual check on Lambert + Path-traced modes, denoise on
and off). Confirmed by the developer during the browser test.

## Files touched

- `src/rendering/BrickedVolume.{hpp,cpp}`
  - New `m_brickInteriorL0` (glm::uvec3) + `m_brickHaloL0` (glm::uvec3)
    state and matching accessors (`brickInteriorL0()`, `brickHaloL0()`,
    `brickStoredL0()`).
  - `build()` computes per-axis sizing **after** the Static/Streaming
    mode decision -- Streaming resets to uniform because
    `packBrickToStaging` still writes 66³.
  - `atlasBytesAllocated()` now respects the mode (Static reports L0
    only; Streaming reports all four LODs). Was double-counting L1..L3
    phantoms in Static, inflating the HUD's overhead readout.

- `src/rendering/VolumeRenderer.{hpp,cpp}`
  - UBO grows a `brickInfo0` uvec4 slot (`xyz` = L0 interior per axis,
    `w` = halo bitmask 0b_zyx). Populated in updateUBO.
  - The occupancy compute UBO grows to 5 uvec4 slots (20 uint32s) to
    carry the same brick info.

- `shaders/volume_pathtrace.{wgsl,frag.glsl}`,
  `shaders/volume_march.{wgsl,frag.glsl}`,
  `shaders/volume_occupancy.comp.{wgsl,glsl}`
  - `sampleVolume` / `fetchVoxel` use per-axis stride + halo when
    `lod == 0`, uniform `(64 >> lod) + 2` when `lod > 0`.

## Not covered (follow-up)

- Streaming pack (`packBrickToStaging`) still writes 66³ regardless.
  When Streaming kicks in on a thin volume the L0 atlas reverts to
  uniform sizing to stay in sync.
- L1..L3 atlases (used by multi-LOD zoom-out) still allocate 34³ /
  18³ / 10³ per slot. For pageGrid == 1 volumes the LOD chain doesn't
  fire anyway (whole thing fits in one L0 brick), so this is only a
  potential waste for future large streaming workloads.
- The residual +6~+19% overhead on multi-brick axes could be reduced
  by also shrinking halo on the LAST brick along an axis whose interior
  is shorter than 64. Not attempted; the win from single-slice cases
  was the R4 pain point.
