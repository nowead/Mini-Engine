#pragma once

// ============================================================================
// Application-layer volume file loader (NOT engine core).
//
// The engine (VolumeRenderer) only knows "a 3D R8 density buffer". This loader
// is one data source for it: a headerless raw volume of known dimensions. Keeping
// it out of the engine keeps domain formats (CT/MRI raw, later DICOM/NIfTI) from
// bloating the core -- a product that doesn't need volume files doesn't link this.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// Load a headerless raw volume into an 8-bit density buffer (w*h*d bytes).
//   bytesPerVoxel == 1: copied as-is.
//   bytesPerVoxel == 2: little-endian uint16 (e.g. CT Hounsfield), min/max
//                       normalized to 0..255.
// Returns false if the file is missing or too small (caller falls back to its
// own default, e.g. a procedural volume).
bool loadRawVolume(const std::string& path, uint32_t w, uint32_t h, uint32_t d,
                   uint32_t bytesPerVoxel, std::vector<uint8_t>& out);

} // namespace assets
