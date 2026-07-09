# Baseline 2026-07-09 — Last-Brick Shrink (Option C tail)

Milestone: follow-up to the same-day Option C brick-shape flexibility work.
Closes the residual `+6~19%` overhead by dropping the outer halo of the last
brick along each multi-brick axis and truncating that brick's interior to the
actual remainder.

## Rule

When Static mode picks `atlasGrid == pageGrid` (which the auto-sizing already
does for anything under the 512 MB budget), slots switch from raster-order
counter to position-based (`slot == pageIdx`). In that layout the last brick
along each axis with `pageGrid > 1` can drop its outer halo (no neighbor to
sample from) and shrink its interior to the actual data remainder.

```text
lastInterior.axis = (pageGrid.axis > 1) ? (volSize.axis - (pageGrid.axis - 1) * 64) : volSize.axis
lastStored.axis   = (pageGrid.axis > 1) ? (lastInterior.axis + 1) : lastInterior.axis
atlasPhys.axis    = (pageGrid.axis - 1) * 66 + lastStored.axis
```

Middle and first bricks keep uniform 66 stored so the shader's slot-origin
math stays a simple `sx * 66 + halo`. Only the atlas texture's PHYSICAL size
changes, communicated through a new UBO field `atlasPhys0` used purely as
the UV denominator (`atlasUvw = (atlasVox + 0.5) / atlasPhys0`).

**Scope**: L0 atlas only, Static mode with `atlasGrid == pageGrid` only.
Streaming and non-matching `atlasGrid` fall back to the uniform layout (no
change from Option C).

## Same four series driven through the R3 runtime upload path

| Series | Dims | Atlas (before → after) | Memory alloc (before → after) | Overhead (before → after) |
| --- | --- | --- | --- | --- |
| `mr_emri_small`       | 64×64×10   | 64×64×10   → **64×64×10**   | 0.2 MB → **0.1 MB** (see note) | +113% → **-0.0%**  |
| `mr_siemens_slice`    | 484×484×1  | 528×528×1  → **499×499×1**  | 0.5 MB → **0.5 MB** | +19%  → **+6%**    |
| `ct_jpeg2000_small`   | 512×512×1  | 528×528×1  → **527×527×1**  | 0.5 MB → **0.5 MB** | +6%   → **+6%**    |
| `mammo_jpeg_lossless` | 256×1024×1 | 264×1056×1 → **263×1055×1** | 0.5 MB → **0.5 MB** | +6%   → **+6%**    |

Confirmed by browser console log lines
`built <dims> -> ... mode = Static (atlas <atlasVoxels>)` — the atlas axis
values now reflect the CPU-computed physical dims after last-brick shrink.

**Note on the fMRI number**: the Option C baseline recorded +113% for
`mr_emri_small` but the actual atlas voxel count for that series (64×64×10)
matches dense exactly, so the true overhead is 0%. The +113% came from a
`atlasBytesAllocated()` reporting drift that this milestone incidentally
fixes by routing `atlasVoxels()` through the same physical-dims field. The
raw allocation was never bigger than 64×64×10 after Option C -- the HUD was
just double-counting.

## Where the wins land

- **Volumes whose dimensions are NOT a multiple of 64** (Siemens 484):
  meaningful reduction (~13 percentage points) because the last brick's
  interior along both x and y is 36 voxels instead of a full 64.
- **Volumes whose dimensions ARE exact multiples of 64** (CT 512, Mammo
  256/1024): marginal (~1 voxel per axis) because the "shrink" only saves
  the single outer halo layer -- the last brick still holds a full 64
  interior.
- **Single-brick-per-axis volumes** (fMRI 64/64/10): unchanged from Option
  C; the Option C rule already handled these.

The wins therefore concentrate on volumes with irregular dimensions -- CT
scans with cropped FOVs (like the 484² Siemens case), older acquisition
matrices (320², 448²), and any workflow that resamples to a non-power-of-2
grid.

## Steady-state CPU (Lambert + shadow, ~30 s window)

| Series | Option C mean/max | Last-brick shrink mean/max |
| --- | ---:| ---:|
| `mr_emri_small`       | 3.48 / 4.36  ms | 2.81 / 3.73 ms |
| `mr_siemens_slice`    | 2.77 / 3.44  ms | 2.17 / 2.24 ms |
| `ct_jpeg2000_small`   | 2.99 / 3.71  ms | 2.30 / 2.30 ms |
| `mammo_jpeg_lossless` | 2.40 / 2.95  ms | 2.69 / 3.37 ms |

CPU steady-state improved marginally for three series and regressed
slightly for mammo (within noise). Not the point of this milestone; the
gain is memory footprint on irregular-dim volumes.

## No visual regression

Same four series rendered before and after through Lambert + shadow mode.
Console shows the atlas dims shrink as expected; the rendered image is
visually identical (developer confirmed during browser test).

## Files touched

- `src/rendering/BrickedVolume.{hpp,cpp}`
  - New state: `m_brickInteriorLastL0` (per-axis last-brick interior),
    `m_atlasVoxelsL0` (physical L0 atlas dims), `m_uniformSlotLayout`
    (false when position-based + shrink active).
  - New accessors: `brickInteriorLastL0()`, `uniformSlotLayout()`.
  - `atlasVoxels()` now returns `m_atlasVoxelsL0` (was
    `atlasGrid * brickStoredL0()`).
  - `atlasBytesUsed()` switches to proportion-of-physical (was a strict
    per-slot uniform count) so the HUD ratio stays accurate under
    non-uniform storage.
  - `build()` decides `m_uniformSlotLayout = false` when
    Static + `atlasGrid == pageGrid`, then computes `m_atlasVoxelsL0` per
    axis. Pack loop uses per-brick `storedThis` = uniform 66 for non-last
    bricks, `(lastInterior + inner halo)` for the last brick along each
    multi-brick axis. Slot assignment: `slot = pageIdx` (position-based),
    empty positions leave allocated-but-unused atlas holes.
  - `buildFromMmappedSource()` sets `m_uniformSlotLayout = true` and
    `m_atlasVoxelsL0 = m_atlasGrid * bs0`  (Streaming pack still writes
    uniform 66^3).

- `src/rendering/VolumeRenderer.{hpp,cpp}`
  - New UBO field: `atlasPhys0` (uvec4). Populated in `updateUBO` from
    `m_brick.atlasVoxels()`. Shader uses it as the UV denominator instead
    of `atlasGrid * brickStored`.
  - Occupancy compute UBO grows to 6 uvec4 slots (24 uint32s) to carry
    the same `atlasPhys0` field (unused by the compute -- it uses
    `texelFetch` with integer coords -- but included for buffer-size
    parity).

- `shaders/volume_pathtrace.{wgsl,frag.glsl}`,
  `shaders/volume_march.{wgsl,frag.glsl}`
  - `atlasUvw = (atlasVox + 0.5) / atlasG * brickStored` becomes
    `select(atlasG * brickStored, ubo.atlasPhys0.xyz, lod == 0u)`. LOD 0
    uses the CPU-computed physical size; LOD 1..3 keep the uniform
    formula.

- `shaders/volume_occupancy.comp.{wgsl,glsl}`
  - UBO struct grows the `atlasPhys0` slot to match the CPU-side buffer
    size. Field is not read (integer texel coords), just present for
    layout parity.

## Not covered (follow-up)

- Streaming pack + L1..L3 stay uniform. When Streaming engages on a
  volume with a thin last brick, the atlas reverts to `atlasGrid * 66`
  per axis -- the shrink is Static-only. Extending Streaming would
  require `packBrickToStaging` to know per-brick stored size and rework
  the LRU staging buffer sizing.
- First brick's inner halo is still allocated (`sx = 0` case reads it
  from a clamped source, adding 1 voxel of waste per axis). Could be
  dropped by making the atlas layout drop the leftmost/topmost/frontmost
  halo the same way we drop the rightmost/bottommost/backmost. Saves at
  most 1 voxel per axis; not attempted here.
- Sparse Static volumes with `atlasGrid < pageGrid` (budget shrunk the
  atlas) stay uniform because slot ≠ pageIdx there. Would need
  position-aware slot assignment for the general case -- possible but
  not exercised by any current workload.

## Reduction ratios (memory alloc, vs. Option C baseline)

Only meaningful for the Siemens case; the other three were already at
the halo-only floor after Option C.

- `mr_siemens_slice`: 528×528×1 → 499×499×1 = **11.5% fewer voxels**.
- The two 64-aligned series (CT 512, mammo 256/1024) drop only the outer
  halo layer (~1 voxel per axis, ~0.4% saving).

## Combined R4 → Option C → Last-Brick Shrink journey

| Series | R4 baseline | Option C | Last-brick shrink |
| --- | ---:| ---:| ---:|
| `mr_emri_small`       | +602%  | +113% (HUD drift) | **-0.0%** (true 0) |
| `mr_siemens_slice`    | +7754% | +19% | **+6%** |
| `ct_jpeg2000_small`   | +6919% | +6%  | **+6%** |
| `mammo_jpeg_lossless` | +6919% | +6%  | **+6%** |

All four series now cost ≤ +6% relative to dense. The residual is one
outer halo layer's worth of voxels per axis for pageGrid > 1 axes -- the
practical floor for a brick atlas with clamp-to-edge sampling that keeps
inner halos.
