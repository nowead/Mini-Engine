#pragma once

// ============================================================================
// Application-layer DICOM series loader (NOT engine core).
//
// Reads a directory of .dcm files (one slice per file), parses uncompressed
// CT/MR pixel data, applies RescaleSlope/Intercept (HU for CT), sorts slices
// by ImagePositionPatient, and assembles them into the generic Volume3D
// consumed by the engine (loadFromFloatData).
//
// Supported transfer syntaxes:
//   - Explicit VR Little Endian ("1.2.840.10008.1.2.1")
//   - Implicit VR Little Endian ("1.2.840.10008.1.2") -- clinical PACS default
//   - RLE Lossless ("1.2.840.10008.1.2.5") -- DICOM PackBits, in-tree decoder
//   - JPEG 2000 Lossless ("1.2.840.10008.1.2.4.90") -- via OpenJPEG
//   - JPEG 2000 Lossy ("1.2.840.10008.1.2.4.91") -- via OpenJPEG
//
// The file meta header (group 0002) is ALWAYS Explicit VR LE per the DICOM
// standard regardless of the dataset transfer syntax, so the parser switches
// encodings at the group boundary. Compressed transfer syntaxes encode the
// dataset itself in Explicit VR LE (per DICOM PS3.5 Annex A); only PixelData
// becomes encapsulated (one item per frame).
//
// Out of scope (return false + LOG_ERROR):
//   - JPEG Baseline / Lossless / JPEG-LS transfer syntaxes
//   - Big Endian transfer syntaxes
//   - Pixel data wider than 16-bit
// ============================================================================

#include "NiftiFile.hpp"   // Volume3D

#include <string>

namespace assets {

// Load all .dcm files in `dirPath` as one series. The output's spacing follows
// PixelSpacing (xy) and the median slice-position delta (z). Returns false if the
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

} // namespace assets
