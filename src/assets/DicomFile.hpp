#pragma once

// ============================================================================
// Application-layer DICOM series loader (NOT engine core).
//
// Reads a directory of .dcm files (one slice per file), parses Explicit VR Little
// Endian uncompressed CT/MR pixel data, applies RescaleSlope/Intercept (HU for
// CT), sorts slices by ImagePositionPatient, and assembles them into the generic
// Volume3D consumed by the engine (loadFromFloatData).
//
// Scope of this first cut:
//   - Transfer syntax: Explicit VR Little Endian ("1.2.840.10008.1.2.1") only.
//   - Pixel data: 16-bit signed (CT) / unsigned (some MR), single-frame per file.
//   - Multi-frame DICOM, JPEG / JPEG2000 / RLE encapsulated pixel data, and the
//     Implicit-VR transfer syntax are unsupported and logged as errors.
// ============================================================================

#include "NiftiFile.hpp"   // Volume3D

#include <string>

namespace assets {

// Load all .dcm files in `dirPath` as one series. The output's spacing follows
// PixelSpacing (xy) and the median slice-position delta (z). Returns false if the
// directory is missing / empty / mixed series / unsupported transfer syntax.
bool loadDicomSeries(const std::string& dirPath, Volume3D& out);

} // namespace assets
