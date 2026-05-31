#pragma once

// ============================================================================
// Application-layer NIfTI-1 (.nii) volume loader (NOT engine core).
//
// One data source for the engine's generic float-intensity volume path. NIfTI is
// a common neuroimaging / research CT-MRI format: a 348-byte header followed by
// the voxel array. This loader reads single-file NIfTI-1 ("n+1") little-endian,
// applies scl_slope/scl_inter so the output is in physical units (Hounsfield for
// CT), and reports voxel spacing so the caller can build an aspect-correct AABB.
//
// Kept out of the engine for the same reason as VolumeFile: domain formats must
// not bloat the core. A product that doesn't read NIfTI doesn't link this.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// Generic float-intensity volume: the lingua-franca between any app-layer loader
// (NIfTI, DICOM, ...) and the engine's loadFromFloatData seam.
struct Volume3D {
    uint32_t w = 0, h = 0, d = 0;
    float spacingX = 1.0f, spacingY = 1.0f, spacingZ = 1.0f;   // mm per voxel
    float dataMin = 0.0f, dataMax = 0.0f;                      // intensity range (scl-applied)
    std::vector<float> intensity;                              // w*h*d, x fastest, scl-applied
};

// Load a single-file little-endian NIfTI-1 volume. Returns false if the file is
// missing (caller falls back), or logs and returns false on an unsupported/
// malformed file. Supported datatypes: uint8/int8, int16/uint16, int32/uint32,
// float32. Big-endian and the .hdr/.img pair ("ni1") are not supported yet.
bool loadNifti(const std::string& path, Volume3D& out);

} // namespace assets
