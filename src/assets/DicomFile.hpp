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
//
// The file meta header (group 0002) is ALWAYS Explicit VR LE per the DICOM
// standard regardless of the dataset transfer syntax, so the parser switches
// encodings at the group boundary.
//
// Out of scope (return false + LOG_ERROR):
//   - Compressed pixel data (JPEG / JPEG2000 / RLE encapsulated)
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

} // namespace assets
