# Baseline 2026-07-08 — Real MRI Verification (R4)

Milestone: `REAL_MRI_VERIFICATION_PLAN.md` Step R4 completion measurement.
Captures the first end-to-end numbers on real public DICOM series (not
synthetic), driven through the R3 runtime upload flow with the R4 HUD
extensions in place.

## Environment

- Host GPU: NVIDIA RTX 4070 (per initial WASM adapter log)
- Backend: WebGPU (Chromium, Emscripten) — validation ON
- Build: `volume_viewer_wasm` (WASM 1.42 MB approx)
- Render mode: Lambert + shadow (M4 v1); denoise + accum-cap N=32 default
- Canvas: variable (1770x1305 or 2560x1305 depending on browser window)
- Every series driven through the R3 runtime picker (not build-time preload)

## Test series (all in `test_data/dicom/`)

| Folder | Source | Modality | Dims | Range | Transfer syntax |
| --- | --- | --- | --- | --- | --- |
| `mr_emri_small/`       | pydicom-data | MR (fMRI, multi-frame) | 64×64×10  | [0, 467]     | Explicit VR LE |
| `mr_siemens_slice/`    | pydicom-data | MR (Siemens single slice)| 484×484×1 | [0, 1123]    | Explicit VR LE |
| `ct_jpeg2000_small/`   | pydicom      | CT (JPEG 2000 lossy)   | 512×512×1 | [-3995, 1812]| JPEG 2000 (`.2.4.91`) |
| `mammo_jpeg_lossless/` | pydicom-data | Mammography (JPEG-LL)  | 256×1024×1| [0, 278]     | JPEG Lossless P14 (`.2.4.57`) |

## Measurements (Lambert + shadow, 500 ms sample interval, ~60 samples window)

| Series | Dense MB | Atlas alloc | Overhead | CPU inst | CPU mean | CPU max | Mode | Visible / non-empty | Auto preset | Auto W/L |
| --- | ---:| ---:| ---:| ---:| ---:| ---:| --- | --- | --- | --- |
| mr_emri_small       | 0.1 | 0.5  | +602%   |  6.73 |  6.56 |   7.15 | Static | 1 / 1     | MR-T1   | c=234 / w=467  |
| mr_siemens_slice    | 0.4 | 35.1 | +7754%  |  3.54 |  3.05 |   4.23 | Static | 64 / 56   | MR-T1   | c=562 / w=1123 |
| ct_jpeg2000_small   | 0.5 | 35.1 | +6919%  |  1.73 | 12.07 | 255.40 | Static | 64 / 64   | CT-Bone | c=300 / w=1500 |
| mammo_jpeg_lossless | 0.5 | 35.1 | +6919%  |  3.67 |  2.37 |   3.67 | Static | 64 / 56   | Cloud   | c=139 / w=278  |

CPU columns are ms/frame. "inst" = last sample (stable steady-state moment
of the screenshot); "mean" / "max" = window-averaged over the last ~30 s of
frames after upload completes.

## Observations (honest)

### 1. Runtime upload path — clean

All four series load through the JS→memfs→`queueUserDicomReload()` flow
without validation errors, without WASM crashes, and without ASYNCIFY
"multiple async ops" aborts. The deferred-reload pattern (JS parks a flag,
render() picks it up before `beginFrame()` inside the main loop's
`_wasmBusy=true` window) is required — the earlier direct-call attempt
crashed because embind returns `undefined` the moment wasm suspends in
`queue->waitIdle()` during texture upload, and the JS finally-block
therefore cleared the busy flag prematurely.

### 2. Memory overhead — dominated by atlas padding on thin volumes

The dense storage figure is `w*h*d*2 B` (R16Float). The atlas allocates a
padded 66×66×66 brick per slot regardless of how much of that brick
actually holds data. Consequences:

- `mr_emri_small` (64×64×10) — truly 3D-shaped 1 brick fills the atlas
  nearly fully. Overhead only **+602%**.
- All three single-slice series (`d=1`) waste ~64 voxels of atlas depth
  per brick. Overhead **+6919% to +7754%** — worst case for the current
  bricking scheme.

This is not a regression; it is the honest cost of a uniform 3D brick
atlas on 2D-shaped inputs. A per-axis-flexible brick shape (e.g. 64×64×1
for `d=1`) would fix it but was out of scope for M3-3.

### 3. CPU frame time — content, not just dim, drives cost

Even though three of the four series produce nearly identical atlases
(~35 MB), steady-state CPU varies by **~2x** because the shader work
depends on the transmittance path through the actual volume:

- `mammo_jpeg_lossless` (soft-tissue 256×1024×1, thin slab after halfExtent
  padding) — **2.4 ms** mean, cheapest.
- `mr_siemens_slice`   — **3.05 ms** mean.
- `mr_emri_small` (true 3D 64×64×10) — **6.56 ms** mean — highest, because
  the ray actually marches through 10 slices worth of density.

### 4. Load-time CPU spike visible in `max`

`ct_jpeg2000_small` shows the extreme: **mean 12.07 ms, max 255.4 ms**.
The 255 ms max is the very first frame after the runtime upload settles,
where texture upload + occupancy grid rebuild + pipeline bind-group
refresh all land on one frame. Steady state (`inst`) drops to 1.73 ms
after the accumulator warms up. Not a bug — the HUD ring buffer surfacing
this spike is the R4 payoff.

### 5. Preset auto-pick works across modalities

The dataMin heuristic promoted in R2 correctly steers each series:

- `dataMin < -500` (CT_JPEG2000, -3995) → **CT-Bone**, HU window 300/1500.
- `0 ≤ data ≤ 4096` (both MR + fMRI) → **MR-T1**, auto-fit window from
  `loadFromFloatData`.
- Mammography [0, 278] falls out of the MR upper cap → **Cloud** fallback,
  which is fine for X-ray-like intensity range.

## Files touched by R4

- `tests/volume_viewer_shell.html` — frame time ring buffer with mean/max,
  memory display fix for `+overhead` case (double-negative "--%" was a
  cosmetic bug on thin volumes), load latency in the upload status line,
  reusable `syncFromWasm()` called after every reload.
- `tests/volume_viewer_wasm.cpp` — deferred `queueUserDicomReload` +
  `lastReloadStatus` plumbing that the R3 upload flow now goes through.
- `docs/current/medical-volume/baselines/BASELINE_2026-07-08_REAL_MRI.md`
  (this file).

## What R4 does NOT claim

- These numbers are single-machine, single-browser, no repeated trials.
  Treat as smoke-test bounds, not a benchmark suite.
- No path-traced numbers here — Lambert is the primary interactive mode
  and enough to prove the utility story.
- Large real-clinical multi-slice CT/MR series (200-400 MB) were not
  measured; the R4 setup accepts them (`ALLOW_MEMORY_GROWTH=1` up to
  4 GB) but public samples of that size need a separate track (TCIA
  credentialing or similar).

## Next candidates

- `M4 v2 P4` (HDR equirect env), `M4 v2 P3.2/P3.3` (adaptive SPP +
  temporal reprojection) — deferred per the pivot in `55f7613`.
- Brick-shape flexibility for thin volumes (would collapse the +7754%
  overhead outliers to something closer to the fMRI +602%).
- Per-axis padding of `d=1` volumes — currently the AABB is padded to a
  0.1 minimum half-extent (a3a4133) but the atlas storage is not; the
  wasted atlas depth is measured here for the first time.
