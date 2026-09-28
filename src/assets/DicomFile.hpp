#pragma once

// ============================================================================
// Application-layer DICOM series loader (NOT engine core).
//
// Reads a directory of .dcm files (including multi-frame images), decodes
// supported CT/MR pixel data, applies RescaleSlope/Intercept (HU for CT),
// sorts slices by ImagePositionPatient, and emits float/half Volume3D data
// or progressive half slices for the renderer.
//
// Supported transfer syntaxes:
//   - Explicit VR Little Endian ("1.2.840.10008.1.2.1")
//   - Implicit VR Little Endian ("1.2.840.10008.1.2") -- clinical PACS default
//   - RLE Lossless ("1.2.840.10008.1.2.5") -- DICOM PackBits, in-tree decoder
//   - JPEG 2000 Lossless ("1.2.840.10008.1.2.4.90") -- via OpenJPEG
//   - JPEG 2000 Lossy ("1.2.840.10008.1.2.4.91") -- via OpenJPEG
//   - JPEG Baseline / Extended / Lossless P14 / SV1 -- via libjpeg-turbo
//
// The file meta header (group 0002) is ALWAYS Explicit VR LE per the DICOM
// standard regardless of the dataset transfer syntax, so the parser switches
// encodings at the group boundary. Compressed transfer syntaxes encode the
// dataset itself in Explicit VR LE (per DICOM PS3.5 Annex A); only PixelData
// becomes encapsulated (one item per frame).
//
// Out of scope (return false + LOG_ERROR):
//   - JPEG-LS transfer syntaxes
//   - Big Endian transfer syntaxes
//   - Pixel data wider than 16-bit
// ============================================================================

#include "NiftiFile.hpp"   // Volume3D

#include <cstdint>
#include <functional>
#include <string>

namespace assets {

// Y1c-full (2026-07-22) sink for progressive DICOM decode. Called once per
// output z-slice with the already-packed uint16 half data (row-major,
// x-fastest, length metaOut.w * metaOut.h). The pointer is valid only for
// the duration of the call; the sink must copy anything it wants to keep.
using DicomProgressiveSliceSink = std::function<void(uint32_t z, const uint16_t* sliceHalf)>;

// Load all .dcm files in `dirPath` as one series. The output's spacing follows
// PixelSpacing (xy) and the first sorted pair's delta (z), falling back to
// SliceThickness when the first pair is coincident. Returns false if the
// directory is missing / empty / mixed series / unsupported transfer syntax.
bool loadDicomSeries(const std::string& dirPath, Volume3D& out);

// Y1c-lite (2026-07-22) — half-direct output variant. Same parse pipeline as
// loadDicomSeries but the per-voxel rescale-then-half-pack happens inline
// during Pass 2, filling out.intensityHalf directly instead of the float
// out.intensity + a subsequent float->half conversion in VolumeRenderer.
// Skips one full-volume-sized buffer at load time (~128 MB saved on a
// 128 MB volume, ~192 MB saved on 192 MB) — the peak that pushed iPhone
// Safari past its process budget on 192 MB DICOM. NIfTI still uses the
// float path (loadNifti + loadFromFloatData). Only intensityHalf is
// populated; out.intensity stays empty.
bool loadDicomSeriesHalf(const std::string& dirPath, Volume3D& out);

// Y1c-full (2026-07-22) — progressive variant. Same parse pipeline as
// loadDicomSeriesHalf but instead of accumulating each z slice into
// out.intensityHalf, hands the slice to `sink` and drops it. metaOut is
// filled with w/h/d/spacing before the sink starts firing (so the sink
// can size downstream buffers) and dataMin/Max are updated at return
// time. out.intensityHalf stays empty. Frees the source DICOM file after
// its last frame is consumed so the wasm heap never holds both the
// compressed source AND a full-volume half buffer simultaneously.
bool loadDicomSeriesProgressive(const std::string& dirPath,
                                Volume3D& metaOut,
                                const DicomProgressiveSliceSink& sink);

} // namespace assets
