# DICOM JPEG Baseline / Lossless Support Plan

**Date**: 2026-06-10
**Goal**: Add support for JPEG Baseline and JPEG Lossless transfer syntaxes
to the DICOM loader. These two cover the bulk of legacy PACS exports that
the current RLE + JPEG 2000 path misses.

**Roadmap alignment**: Follow-up to the DICOM_COMPRESSED track. The
existing engine + Volume3D plumbing stays — this is parser-only.

---

## 1. JPEG Transfer Syntaxes — What and Why

| UID | Name | Bits | Coding | Library |
| --- | --- | --- | --- | --- |
| `1.2.840.10008.1.2.4.50` | JPEG Baseline (Process 1) | 8 | DCT lossy | libjpeg-turbo |
| `1.2.840.10008.1.2.4.51` | JPEG Extended (Process 2 & 4) | 12 | DCT lossy | libjpeg-turbo (12-bit) |
| `1.2.840.10008.1.2.4.57` | JPEG Lossless (Process 14) | 2..16 | predictive lossless | libjpeg-turbo 3.0+ |
| `1.2.840.10008.1.2.4.70` | JPEG Lossless Process 14 SV1 | 2..16 | predictive lossless | libjpeg-turbo 3.0+ |

Clinical relevance:

- **Baseline (.50)** — colour ultrasound / pathology screenshots, RGB 8-bit.
  Not the most common medical-image format but ubiquitous historically.
- **Lossless SV1 (.70)** — legacy hospital PACS CT / MR archives. 12- or
  16-bit predictive lossless, bit-exact to the source. **Most valuable for
  the medical-volume path.**
- Extended (.51) and Lossless P14 (.57) are rare in modern PACS but cheap
  to include once libjpeg-turbo is linked.

---

## 2. Dependency — libjpeg-turbo

vcpkg port: `libjpeg-turbo`. Exposes `JPEG::jpeg` and (since 3.0) a 12-bit
variant for the lossless path. Same dependency pattern as OpenJPEG:

- Native (Vulkan build): add to `vcpkg.json`, `find_package(JPEG)` in the
  native branch of `CMakeLists.txt`.
- WASM: `FetchContent` from upstream + static link (see OpenJPEG W1 for
  the pattern). The 12-bit lossless support needs a `--with-12bit` build
  flag — TBD whether the FetchContent CMake supports it cleanly.

WASM linkage is deferred to a separate sub-step so the native path lands
first and gets verified before the browser surface area grows.

---

## 3. Atomic Steps

### Step J1 — Plan + libjpeg-turbo vcpkg dep (~1-2 h)   ✅ `6ff902c`

- Add `libjpeg-turbo` to `vcpkg.json`.
- `find_package(JPEG CONFIG REQUIRED)` in the native branch.
- `target_link_libraries(volume_viewer PRIVATE JPEG::jpeg)`.
- Linkage probe: log `LIBJPEG_TURBO_VERSION` at startup.

### Step J2 — JPEG Baseline + Extended decoder (~2-3 h)   ✅ `e7a80e2`

- New helper `decodeJpegFrame16(frame, rows, cols, dst)` in DicomFile.cpp.
- libjpeg memory source manager wrapping the encapsulated frame bytes.
- 8-bit baseline: read JPEG, output rows, promote to 16-bit via
  `pixel << 8` (preserves dynamic range to the engine's R16Float atlas).
- 12-bit extended: libjpeg-turbo 12-bit API (`jpeg12_*`) — same flow.
- Route `JPEG_BASELINE` and `JPEG_EXTENDED` UIDs through the new helper.

### Step J3 — JPEG Lossless (Process 14 + SV1) (~3-4 h)   ✅ `6157da4`

- libjpeg-turbo 3.0+ decodes lossless streams transparently through the
  standard `jpeg_decompress_struct` flow -- no `JPROC_LOSSLESS` opt-in
  needed. The vcpkg port ships 3.1.2 with full lossless support (Process
  14 + SV1).
- `decodeJpegFrame16` precision dispatch widened from exact equality
  (`== 8`, `== 12`) to ranges (`<= 8`, `<= 12`, `<= 16`); the new 16-bit
  branch calls `jpeg16_read_scanlines` and writes `J16SAMPLE` straight
  to int16 -- bit pattern preserved so PixelRepresentation
  signed/unsigned interpretation stays the engine's responsibility.
- Bonus fix: single-frame multi-fragment handling. Empty-BOT encapsulated
  streams often split one JPEG into 2+ `(FFFE,E000)` items. `parseSlice`
  concatenates them into an owned `Slice::mergedFrameBuffer` and rewrites
  `frames[]` to one contiguous view before dispatch.

### Step J4 — Verification + docs (~1-2 h)   ✅ `6157da4` (this commit)

- Verified on pydicom-data:
  - `JPGExtended.dcm` (12-bit DCT, precision=12, 256×1024) -> range `[0, 264]`.
  - `JPEG-LL.dcm` (16-bit lossless, precision=16, 256×1024, 2-fragment merge)
    -> range `[0, 278]`. Exercises both the 16-bit `jpeg16` branch and
    the new fragment-merge path.
  - `JPGLosslessP14SV1_1s_1f_8b.dcm` (8-bit lossless via SV1) -> decoder
    runs; rejected later by the pre-existing `bitsAllocated != 16` engine
    guard (RGB secondary-capture limitation; lifting it is engine-side
    work outside this track).
- Regression unchanged: JPEG 2000 (`693_J2KR` `[-3024, 1468]`), RLE
  Lossless, Explicit / Implicit VR LE.
- JPEG-LS (`1.2.840.10008.1.2.4.80`) seen in samples (`emri_small_jpeg_ls_lossless`)
  but correctly reported as unsupported -- separate charls track per the
  plan.
- Updated `VIEWERS.md` (capability matrix + dispatch list) and
  `MEDICAL_VOLUME_ROADMAP.md` (delivered-work log).

### Total: ~7-11 h (2-3 sessions)

---

## 4. Files Touched

| Step | File |
| --- | --- |
| J1 | `vcpkg.json`, `CMakeLists.txt` (native branch) |
| J2 | `src/assets/DicomFile.cpp` (decoder + dispatch) |
| J3 | `src/assets/DicomFile.cpp` (lossless path) |
| J4 | `docs/current/medical-volume/VIEWERS.md`, `MEDICAL_VOLUME_ROADMAP.md` |

No engine-core changes.

---

## 5. Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| vcpkg port lacks 12-bit / lossless build flags | Pin the port version; if blocked, drop J3 to a follow-up. |
| JPEG colour-space conversion (RGB / YCbCr) in baseline samples | Force greyscale path; reject true-colour as out of medical scope. |
| Pixel bit-depth mismatch (8 vs 12 vs 16) | Promote 8-bit to 16-bit by `<< 8`. 12-bit lossless writes int16 directly. |
| libjpeg memory source manager edge cases (partial buffer, restart markers) | Use the standard `jpeg_mem_src` helper that ships with libjpeg-turbo. |

---

## 6. Out of Scope (Future Tracks)

- WASM libjpeg-turbo build (browser legacy-JPEG DICOM). Same pattern as
  the WASM OpenJPEG track; do separately once the native path is solid.
- JPEG-LS (.80 / .81) — different library (charls). Distinct track.
- JPEG 2000 Part-2 extensions (.92 / .93). Almost no medical use.

---

## 7. Next Entry Point

Step J1 (vcpkg dep + linkage probe). Each step ends with a commit so the
build graph stays bisectable.
