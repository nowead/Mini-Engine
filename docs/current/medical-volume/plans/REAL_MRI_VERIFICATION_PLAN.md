# Real MRI Verification Plan

**Date**: 2026-07-07
**Goal**: Close the gap between "the loader supports MR" and "we have shown
real MR data render correctly with clinical presets, at meaningful frame
rate and memory footprint". This is the direction the surrounding project
was actually set up for — displaying real MRI on screen with usable
frames and memory — and every prior track (M1..M4 v2 P3.1) has been
infrastructure toward it.

**Roadmap alignment**: Pivot from
[PATH_TRACE_POLISH_PLAN.md](PATH_TRACE_POLISH_PLAN.md). P3.1 delivered the
"spatial denoise stays visible at rest" milestone that was the whole point
of the P3 sub-track; P3.2 and P3.3 are optimisation on a stable baseline.
The real-MRI verification track has higher marginal value against the
stated project goal, and every sub-step produces an immediately visible
result.

---

## 1. Motivation

Current state assessed honestly:

- **Data pipeline**: loader supports NIfTI + DICOM 9 transfer syntaxes;
  MR-T1 / MR-T2 presets exist as LUT keys.
- **Memory pipeline**: 1024³ dense volumes fit within a 2.30 GB peak
  working set (disk paging Step 5). Atlas auto-sizes to a 512 MB budget.
- **Rendering**: Lambert mode is real-time; path-trace mode is
  progressive with a spatial denoiser and now a capped temporal average.

Gaps against the stated goal:

- **No real MR data has been visually verified end-to-end.** Most
  visual work has been CT (`693_J2KI`, synthetic CT) or the mammography
  outlier that surfaced the row-flip / preset guards. The MR presets
  exist but have never been tuned against real T1 / T2 signal ranges.
- **No user-facing input.** Everything is build-time preloaded. A
  claim of "displays MRI" implies the user can pick their own DICOM,
  not that the demo bundles one.
- **No live FPS / memory readback on real MR sizes.** Static baselines
  (BASELINE_2026-06-10 etc.) measured 1024³ dense synthetic; MR series
  can be 512x512x180+ (Siemens, GE) with real geometry.

---

## 2. Atomic Steps

### Step R1 -- Bundle a real MR series + verify default render (~0.5-1 session)   ✅ `933faa5`

- Pick a small public MR series with clear anatomy. Candidates:
  - pydicom-data `MR-SIEMENS-DICOM-WithOverlays.dcm` (single slice,
    smoke test).
  - IXI dataset excerpt (T1 / T2 brain, ~1-10 MB per subject after
    stripping).
  - OSF public MR datasets (search for CC0-licensed).
- Extend `scripts/fetch_wasm_dicom_sample.py` (already generic-ised in
  `7b129fd`) or add a sibling script to fetch the sample at build time,
  preload into `/sample_dicom_mr` on the WASM side.
- Native viewer: point at the same downloaded directory via CLI.
- Verify: the volume loads with reasonable range + spacing, MR-T1 (or
  T2) preset renders anatomy correctly.

**Expected outcome**: real brain / whatever anatomy displays. If the
default render looks off, Step R2 kicks in.

### Step R2 -- Tune MR-T1 / MR-T2 preset LUTs for real signal (~0.5-1 session, conditional)   ✅ `933faa5` (preset auto-pick sufficed; LUT retuning not needed)

- Only run if R1 exposes preset issues (contrast wrong, tissue not
  differentiated, alpha ramp misplaced).
- Adjust the LUT control points in
  `VolumeRenderer::applyPendingTFUpdate()` for TFPreset::MRT1 / MRT2
  to match the auto-fit window range that MR data lands in.
- Cross-check against clinical MR window/level conventions
  (CSF dark on T1, CSF bright on T2, WM bright on T1, etc.).
- Verify: same sample looks anatomically correct with either preset.

### Step R3 -- Runtime DICOM upload via `<input type="file">` (~2 sessions)   ✅ `91cf26a` (initial) + this commit (deferred-reload fix)

- WASM: HTML `<input type="file" multiple webkitdirectory>` for a
  DICOM directory. Or single .zip / .tar for the simpler case.
- Emscripten FS layer: on `change`, use `FileReader.readAsArrayBuffer`
  per file, then `FS.writeFile('/user_dicom/<name>.dcm', bytes)`.
- After all files land, call the existing `loadDicomSeries('/user_dicom')`
  path. Viewer reloads volume.
- Wire a "Load DICOM directory" button next to the existing controls.
- Verify: user picks their own directory (a real MR from Step R1
  works as the seed test); anatomy renders.

**Risk**: ASYNCIFY interactions with FileReader (JS -> wasm callbacks
mid-frame). Follow the `Module._wasmBusy` pattern documented in
[WASM guidance in CLAUDE.md](../../../../CLAUDE.md) -- gate the callback
that dispatches into wasm on `!Module._wasmBusy`, and split the file
read (JS async, wasm untouched) from the wasm-side write (single
synchronous call after all buffers ready).

**Memory risk**: a real MR series can be 200-400 MB. Streaming write
(one file at a time, free the JS ArrayBuffer after each `FS.writeFile`)
avoids doubling that in JS heap. Document the observed ceiling.

### Step R4 -- Live FPS + memory HUD on real MR sizes (~0.5-1 session)   ✅ (this commit)

- The stats panel already shows CPU ms/frame and atlas memory; extend
  with:
  - Real-MR volume dims + auto-derived isotropic reconstruction size.
  - `Streaming` mode indicator (already in R1's static baseline; make
    sure it fires correctly at MR scales).
  - Frame-time histogram or moving average over the last N seconds.
- Take a baseline measurement at a defined MR series and commit as
  `baselines/BASELINE_YYYY-MM-DD_REAL_MRI.md`.

### Total: ~3-4 sessions

---

## 3. Files Touched

| Step | File |
| --- | --- |
| R1 | `scripts/fetch_wasm_dicom_sample.py` (or sibling), `CMakeLists.txt` (preload), `tests/volume_viewer_wasm.cpp` (fallback chain), possibly `docs/current/medical-volume/VIEWERS.md` |
| R2 | `src/rendering/VolumeRenderer.cpp` (MRT1 / MRT2 LUT keys) |
| R3 | `tests/volume_viewer_shell.html` (file input + JS), `tests/volume_viewer_wasm.cpp` (reload API), `src/assets/DicomFile.cpp` (verify unchanged path works from `/user_dicom/`) |
| R4 | `tests/volume_viewer_wasm.cpp` (extra stats), `tests/volume_viewer_shell.html` (extra HUD), new `baselines/BASELINE_YYYY-MM-DD_REAL_MRI.md` |

---

## 4. Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| Public MR samples too small / not representative | R1 candidates ranked: single-slice smoke test first, then multi-slice T1 / T2 series. Fall back to IXI if pydicom-data samples are inadequate. |
| MR preset needs LUT redesign, not just tuning | R2 boxed as "conditional". If LUT keys need architectural change (new control points, non-monotonic alpha), that becomes its own atomic sub-step. |
| ASYNCIFY + FileReader interaction hangs | Established `Module._wasmBusy` pattern (already documented for the M3-3 v0 streaming fix). Split reads (JS async) from writes (wasm single call). |
| Runtime memory pressure from user's DICOM series | Stream per-file to memfs, free JS ArrayBuffers immediately. Set a soft upper bound (e.g., refuse > 500 MB total in-flight) and surface a friendly warning. |
| Series with unsupported transfer syntax | Loader dispatch already covers 9 UIDs; the JPEG-LS gap is the main one. Surface as a user-visible error rather than a silent black volume. |

---

## 5. Out of Scope

- **JPEG-LS (charls)** loader -- separate track. If a common MR PACS
  export uses it, revisit priority; for now not blocking.
- **Multi-frame DICOM concatenation** into a single volume (already
  supported for the same-series case; cross-series is out).
- **Server-side upload** or persistence -- WASM demo is client-side
  only.
- **8-bit medical volumes** -- engine still gates on
  `bitsAllocated != 16`.

---

## 6. Next Entry Point

Step R1 -- pick a public MR series, wire the preload script, verify the
default render on the WASM viewer. Commit the sample + build glue as
one atomic change so the demo can immediately show real MR anatomy.
