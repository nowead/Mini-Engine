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

#include "src/utils/MmappedFile.hpp"

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

// Disk paging Step 5.3 fast path. For NIfTI files whose datatype is int16 or
// uint16 the loader can hand the renderer a memory-mapped voxel region instead
// of decoding the whole thing into a `std::vector<float>` -- avoids the
// 4-bytes-per-voxel intermediate and (more importantly) the
// 2-bytes-per-voxel half-data buffer copy that VolumeRenderer normally
// materialises before the brick atlas build.
struct MmappedNiftiSource {
    utils::MmappedFile mmap;             // file mapping, owned
    size_t   dataOffset = 0;             // byte offset of voxel[0] inside the mapping
    uint32_t w = 0, h = 0, d = 0;
    float    spacingX = 1.0f, spacingY = 1.0f, spacingZ = 1.0f;
    float    slope = 1.0f, intercept = 0.0f;   // physical = slope * raw + intercept
    bool     isSigned = true;            // int16 (true) / uint16 (false)
    // Raw and physical min/max from the scan pass. The "raw" min doubles as
    // the empty-brick bit pattern (bricks whose interior is all "air").
    int32_t  rawMin = 0, rawMax = 0;
    float    dataMin = 0.0f, dataMax = 0.0f;
};

// Load a single-file little-endian NIfTI-1 volume. Returns false if the file is
// missing (caller falls back), or logs and returns false on an unsupported/
// malformed file. Supported datatypes: uint8/int8, int16/uint16, int32/uint32,
// float32. Big-endian and the .hdr/.img pair ("ni1") are not supported yet.
bool loadNifti(const std::string& path, Volume3D& out);

// Step 5.3: open a NIfTI int16 / uint16 file as a memory-mapped source. Returns
// false if the file is missing, malformed, or its datatype is anything other
// than int16 / uint16 -- the caller is expected to fall back to loadNifti.
bool loadNiftiAsMmappedSource(const std::string& path, MmappedNiftiSource& out);

} // namespace assets
