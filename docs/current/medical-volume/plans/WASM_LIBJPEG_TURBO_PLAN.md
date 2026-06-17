# WASM libjpeg-turbo Plan

**Date**: 2026-06-17
**Goal**: Mirror the native DICOM JPEG legacy track
([DICOM_JPEG_LEGACY_PLAN.md](DICOM_JPEG_LEGACY_PLAN.md)) into the
WebGPU/WASM viewer so the browser sibling can decode JPEG Baseline,
Extended, and Lossless P14 / SV1 transfer syntaxes.

**Roadmap alignment**: Direct extension of the
[WASM_OPENJPEG_PLAN.md](WASM_OPENJPEG_PLAN.md) pattern -- FetchContent
the library, statically link into `volume_viewer_wasm`, preload a sample
DICOM, exercise the decode path in-browser. Native already ships the
parser side; WASM was deliberately deferred so the native path could
land first.

---

## 1. Scope

Native commit range `6ff902c` -> `6157da4` (Steps J1..J4) delivered the
shared parser code in `src/assets/DicomFile.cpp`. The cpp file already
includes `<jpeglib.h>` unconditionally and calls `jpeg_read_scanlines`,
`jpeg12_read_scanlines`, and `jpeg16_read_scanlines`. The WASM build
currently fails-fast at link time on these symbols (the
`volume_viewer_wasm` target links `openjp2` but not libjpeg-turbo). All
we need is to wire the library on the WASM side; no per-platform code
forks.

---

## 2. Atomic Steps

### Step W1 -- Plan + FetchContent + linkage probe (~1-2 h)

- Add a libjpeg-turbo FetchContent block in the Emscripten branch of
  `CMakeLists.txt`, right after the OpenJPEG block.
  - GIT_REPOSITORY: `https://github.com/libjpeg-turbo/libjpeg-turbo.git`
  - GIT_TAG: `3.1.2` (match the vcpkg-installed version on native to
    keep the decode behaviour identical across builds)
  - Flags:
    - `WITH_SIMD=OFF` -- no x86 SIMD on WASM
    - `WITH_12BIT=ON` -- needed for `jpeg12_*` and `jpeg16_*` symbols
      (native vcpkg port also ships with this on)
    - `ENABLE_SHARED=OFF`, `ENABLE_STATIC=ON`
    - `WITH_TURBOJPEG=OFF` -- only the libjpeg API is used
    - `BUILD_TESTING=OFF`
- Link `jpeg-static` (the static target libjpeg-turbo exposes when
  `ENABLE_STATIC=ON`) into `volume_viewer_wasm`.
- Probe: log `JPEG_LIB_VERSION` from `volume_viewer_wasm.cpp` at startup
  the same way `tests/volume_viewer.cpp` does (J1 pattern).
- Verify: `wasm.ps1 build` -> open console -> see
  `[INFO][VolumeViewer] libjpeg-turbo linked, JPEG_LIB_VERSION=62`.
- WASM size delta measured.

### Step W2 -- Bundled JPEG-compressed DICOM sample + dispatch verify (~1-2 h)

- Extend `scripts/fetch_wasm_dicom_sample.py` (or add a sibling script)
  to fetch a JPEG-encoded DICOM (preference: `JPGExtended.dcm` for
  12-bit DCT coverage; `JPEG-LL.dcm` for 16-bit lossless coverage --
  ideally both, total preload < 500 KB).
- Update the `volume_viewer_wasm` PRE_LINK command and
  `--preload-file` list.
- Update the wasm shell HTML to surface the sample (selector dropdown
  or button), or simply route it through the existing dropdown.
- Verify: browser loads sample -> console log
  `[Dicom] loaded ... range [0, NNN] ...` -> volume renders.

### Step W3 -- Docs roll-up (~1 h)

- Update VIEWERS.md to mark JPEG legacy as supported on WASM too.
- Add the delivered-work entry to MEDICAL_VOLUME_ROADMAP.md (commit
  range, samples verified, WASM size delta).
- Strike off W1..W3 in this plan.

### Total: ~3-5 h (1-2 sessions)

---

## 3. Files Touched

| Step | File |
| --- | --- |
| W1 | `CMakeLists.txt`, `tests/volume_viewer_wasm.cpp`, new `plans/WASM_LIBJPEG_TURBO_PLAN.md` |
| W2 | `CMakeLists.txt`, `scripts/fetch_wasm_dicom_sample.py` (extend) or new sibling, `tests/volume_viewer_shell.html` (optional sample selector) |
| W3 | `docs/current/medical-volume/VIEWERS.md`, `MEDICAL_VOLUME_ROADMAP.md`, this plan |

No engine-core changes; `DicomFile.cpp` already handles all three
precision branches and the multi-fragment merge path.

---

## 4. Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| libjpeg-turbo CMake target name differs across versions (`jpeg-static` vs `JPEG::JPEG`) | Pin to 3.1.2; inspect actual target name from FetchContent's CMake output before linking. |
| WASM build picks up SIMD intrinsics via `__SSE2__` / `__ARM_NEON` macros and fails to compile | Set `WITH_SIMD=OFF` explicitly. Emscripten doesn't define those macros by default unless `-msimd128` is on. |
| `WITH_12BIT` builds an additional `jpeg12-static` target instead of folding symbols into the main library | If the symbols come from a separate target, link both `jpeg-static` and `jpeg12-static`. Verify with `wasm-objdump -x` after W1 build. |
| Build size > +400 KB | Acceptable; OpenJPEG was +345 KB. If much larger, audit which subsections of libjpeg-turbo are pulled in -- `-ffunction-sections -fdata-sections` should let the linker strip unused decoders. |
| FetchContent's libjpeg-turbo configures *itself* with `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, redirecting our `.wasm` output (same trap as OpenJPEG) | Same fix already in place for `volume_viewer_wasm` via the `RUNTIME_OUTPUT_DIRECTORY` override; verify it still pins the output. |

---

## 5. Out of Scope

- JPEG-LS (`1.2.840.10008.1.2.4.80` / `.81`). Different library (charls).
  Separate track per the parent plan.
- 8-bit medical volumes (engine-side `bitsAllocated != 16` lift). Not a
  WASM-track concern.
- Multi-frame multi-fragment DICOM. Current merge logic handles only
  the single-frame case; multi-frame would need BOT parsing or
  SOI-marker sniffing. Same scope as native.

---

## 6. Next Entry Point

Step W1 -- FetchContent block + link + linkage probe, single commit.
