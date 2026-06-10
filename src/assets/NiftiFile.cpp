#include "NiftiFile.hpp"
#include "src/utils/Logger.hpp"
#include "src/utils/MmappedFile.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace assets {
namespace {
// Read a little-endian scalar from a byte buffer (host is little-endian).
template <typename T>
T rd(const uint8_t* p, size_t off) {
    T v;
    std::memcpy(&v, p + off, sizeof(T));
    return v;
}
}  // namespace

bool loadNifti(const std::string& path, Volume3D& out) {
    // Memory-map the file read-only. The OS pages voxel data in on demand
    // during the per-voxel walk below, so even a multi-GB NIfTI does not need
    // a transient std::ifstream buffer the size of the whole file.
    utils::MmappedFile mapping;
    if (!mapping.open(path)) return false;  // missing -> caller falls back to its default
    const size_t fileSize = mapping.size();
    if (fileSize < 352) {
        LOG_ERROR("Nifti") << path << ": file too small (" << fileSize << "B)";
        return false;
    }
    const uint8_t* buf = mapping.data();

    // sizeof_hdr == 348 only when read with the file's native endianness. A
    // byte-swapped value means a big-endian volume, which we don't handle yet.
    const int32_t sizeofHdr = rd<int32_t>(buf, 0);
    if (sizeofHdr != 348) {
        LOG_ERROR("Nifti") << path << ": sizeof_hdr=" << sizeofHdr
                           << " (expected 348; big-endian NIfTI not supported yet)";
        return false;
    }
    if (std::memcmp(buf + 344, "n+1\0", 4) != 0) {
        LOG_ERROR("Nifti") << path << ": magic is not 'n+1' (only single-file NIfTI-1 supported)";
        return false;
    }

    const int16_t ndim = rd<int16_t>(buf, 40);
    const int16_t dx   = rd<int16_t>(buf, 42);
    const int16_t dy   = rd<int16_t>(buf, 44);
    const int16_t dz   = rd<int16_t>(buf, 46);
    if (ndim < 3 || dx <= 0 || dy <= 0 || dz <= 0) {
        LOG_ERROR("Nifti") << path << ": bad dims (ndim=" << ndim << ", " << dx << "x" << dy << "x" << dz << ")";
        return false;
    }

    const int16_t datatype = rd<int16_t>(buf, 70);
    const float   px        = rd<float>(buf, 76 + 4 * 1);   // pixdim[1..3] = spacing
    const float   py        = rd<float>(buf, 76 + 4 * 2);
    const float   pz        = rd<float>(buf, 76 + 4 * 3);
    const float   voxOffset = rd<float>(buf, 108);
    float         sclSlope  = rd<float>(buf, 112);
    const float   sclInter  = rd<float>(buf, 116);
    if (sclSlope == 0.0f) sclSlope = 1.0f;   // NIfTI spec: slope 0 means no scaling

    const uint32_t w = static_cast<uint32_t>(dx);
    const uint32_t h = static_cast<uint32_t>(dy);
    const uint32_t d = static_cast<uint32_t>(dz);
    const size_t   voxels = static_cast<size_t>(w) * h * d;

    size_t bytesPer = 0;
    switch (datatype) {
        case 2: case 256:          bytesPer = 1; break;  // uint8 / int8
        case 4: case 512:          bytesPer = 2; break;  // int16 / uint16
        case 8: case 768: case 16: bytesPer = 4; break;  // int32 / uint32 / float32
        default:
            LOG_ERROR("Nifti") << path << ": unsupported datatype " << datatype;
            return false;
    }

    const size_t dataStart = static_cast<size_t>(voxOffset);
    if (dataStart + voxels * bytesPer > fileSize) {
        LOG_ERROR("Nifti") << path << ": voxel data truncated (need "
                           << (dataStart + voxels * bytesPer) << "B, have " << fileSize << "B)";
        return false;
    }

    out.w = w; out.h = h; out.d = d;
    out.spacingX = (px > 0.0f) ? px : 1.0f;
    out.spacingY = (py > 0.0f) ? py : 1.0f;
    out.spacingZ = (pz > 0.0f) ? pz : 1.0f;
    out.intensity.resize(voxels);

    const uint8_t* dp = buf + dataStart;
    float mn = std::numeric_limits<float>::max();
    float mx = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i < voxels; ++i) {
        float raw;
        switch (datatype) {
            case 2:   raw = static_cast<float>(dp[i]); break;
            case 256: raw = static_cast<float>(reinterpret_cast<const int8_t*>(dp)[i]); break;
            case 4:   raw = static_cast<float>(rd<int16_t>(dp, i * 2)); break;
            case 512: raw = static_cast<float>(rd<uint16_t>(dp, i * 2)); break;
            case 8:   raw = static_cast<float>(rd<int32_t>(dp, i * 4)); break;
            case 768: raw = static_cast<float>(rd<uint32_t>(dp, i * 4)); break;
            case 16:  raw = rd<float>(dp, i * 4); break;
            default:  raw = 0.0f; break;
        }
        const float v = sclSlope * raw + sclInter;
        out.intensity[i] = v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    out.dataMin = mn;
    out.dataMax = mx;

    LOG_INFO("Nifti") << "loaded " << path << " (" << w << "x" << h << "x" << d
                      << ", spacing " << out.spacingX << "/" << out.spacingY << "/" << out.spacingZ
                      << "mm, range [" << mn << "," << mx << "], datatype " << datatype << ")";
    return true;
}

bool loadNiftiAsMmappedSource(const std::string& path, MmappedNiftiSource& out) {
    utils::MmappedFile mapping;
    if (!mapping.open(path)) return false;
    const size_t fileSize = mapping.size();
    if (fileSize < 352) {
        LOG_ERROR("Nifti") << path << ": file too small for mmap source (" << fileSize << "B)";
        return false;
    }
    const uint8_t* buf = mapping.data();

    const int32_t sizeofHdr = rd<int32_t>(buf, 0);
    if (sizeofHdr != 348) return false;
    if (std::memcmp(buf + 344, "n+1\0", 4) != 0) return false;

    const int16_t ndim = rd<int16_t>(buf, 40);
    const int16_t dx   = rd<int16_t>(buf, 42);
    const int16_t dy   = rd<int16_t>(buf, 44);
    const int16_t dz   = rd<int16_t>(buf, 46);
    if (ndim < 3 || dx <= 0 || dy <= 0 || dz <= 0) return false;

    const int16_t datatype = rd<int16_t>(buf, 70);
    // Step 5.3 fast path: only int16 (4) / uint16 (512). Anything else falls
    // back to the float decode in loadNifti.
    if (datatype != 4 && datatype != 512) return false;

    const float px        = rd<float>(buf, 76 + 4 * 1);
    const float py        = rd<float>(buf, 76 + 4 * 2);
    const float pz        = rd<float>(buf, 76 + 4 * 3);
    const float voxOffset = rd<float>(buf, 108);
    float       sclSlope  = rd<float>(buf, 112);
    const float sclInter  = rd<float>(buf, 116);
    if (sclSlope == 0.0f) sclSlope = 1.0f;

    const uint32_t w = static_cast<uint32_t>(dx);
    const uint32_t h = static_cast<uint32_t>(dy);
    const uint32_t d = static_cast<uint32_t>(dz);
    const size_t   voxels = static_cast<size_t>(w) * h * d;
    const size_t   dataStart = static_cast<size_t>(voxOffset);
    if (dataStart + voxels * 2 > fileSize) {
        LOG_ERROR("Nifti") << path << ": voxel data truncated for mmap source";
        return false;
    }

    // One scan pass to find raw min/max; OS streams pages in lazily through
    // the mmap so this stays sequential I/O.
    const uint8_t* dp = buf + dataStart;
    int32_t rawMin, rawMax;
    if (datatype == 4) {
        const int16_t* p = reinterpret_cast<const int16_t*>(dp);
        auto [mnIt, mxIt] = std::minmax_element(p, p + voxels);
        rawMin = *mnIt;
        rawMax = *mxIt;
    } else {
        const uint16_t* p = reinterpret_cast<const uint16_t*>(dp);
        auto [mnIt, mxIt] = std::minmax_element(p, p + voxels);
        rawMin = *mnIt;
        rawMax = *mxIt;
    }
    const float dataMin = sclSlope * static_cast<float>(rawMin) + sclInter;
    const float dataMax = sclSlope * static_cast<float>(rawMax) + sclInter;

    out.mmap       = std::move(mapping);
    out.dataOffset = dataStart;
    out.w = w; out.h = h; out.d = d;
    out.spacingX = (px > 0.0f) ? px : 1.0f;
    out.spacingY = (py > 0.0f) ? py : 1.0f;
    out.spacingZ = (pz > 0.0f) ? pz : 1.0f;
    out.slope    = sclSlope;
    out.intercept = sclInter;
    out.isSigned = (datatype == 4);
    out.rawMin   = rawMin;
    out.rawMax   = rawMax;
    out.dataMin  = dataMin;
    out.dataMax  = dataMax;

    LOG_INFO("Nifti") << "mmapped " << path << " (" << w << "x" << h << "x" << d
                      << ", spacing " << out.spacingX << "/" << out.spacingY << "/" << out.spacingZ
                      << "mm, range [" << dataMin << "," << dataMax
                      << "], datatype " << datatype
                      << (out.isSigned ? " int16" : " uint16") << ")";
    return true;
}

} // namespace assets
