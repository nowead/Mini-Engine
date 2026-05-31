#include "NiftiFile.hpp"
#include "src/utils/Logger.hpp"

#include <cstring>
#include <fstream>
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
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;  // missing -> caller falls back to its default
    const std::streamsize fileSize = f.tellg();
    f.seekg(0, std::ios::beg);
    if (fileSize < 352) { LOG_ERROR("Nifti") << path << ": file too small (" << fileSize << "B)"; return false; }

    std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
    f.read(reinterpret_cast<char*>(buf.data()), fileSize);
    f.close();

    // sizeof_hdr == 348 only when read with the file's native endianness. A
    // byte-swapped value means a big-endian volume, which we don't handle yet.
    const int32_t sizeofHdr = rd<int32_t>(buf.data(), 0);
    if (sizeofHdr != 348) {
        LOG_ERROR("Nifti") << path << ": sizeof_hdr=" << sizeofHdr
                           << " (expected 348; big-endian NIfTI not supported yet)";
        return false;
    }
    if (std::memcmp(buf.data() + 344, "n+1\0", 4) != 0) {
        LOG_ERROR("Nifti") << path << ": magic is not 'n+1' (only single-file NIfTI-1 supported)";
        return false;
    }

    const int16_t ndim = rd<int16_t>(buf.data(), 40);
    const int16_t dx   = rd<int16_t>(buf.data(), 42);
    const int16_t dy   = rd<int16_t>(buf.data(), 44);
    const int16_t dz   = rd<int16_t>(buf.data(), 46);
    if (ndim < 3 || dx <= 0 || dy <= 0 || dz <= 0) {
        LOG_ERROR("Nifti") << path << ": bad dims (ndim=" << ndim << ", " << dx << "x" << dy << "x" << dz << ")";
        return false;
    }

    const int16_t datatype = rd<int16_t>(buf.data(), 70);
    const float   px        = rd<float>(buf.data(), 76 + 4 * 1);   // pixdim[1..3] = spacing
    const float   py        = rd<float>(buf.data(), 76 + 4 * 2);
    const float   pz        = rd<float>(buf.data(), 76 + 4 * 3);
    const float   voxOffset = rd<float>(buf.data(), 108);
    float         sclSlope  = rd<float>(buf.data(), 112);
    const float   sclInter  = rd<float>(buf.data(), 116);
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
    if (dataStart + voxels * bytesPer > buf.size()) {
        LOG_ERROR("Nifti") << path << ": voxel data truncated (need "
                           << (dataStart + voxels * bytesPer) << "B, have " << buf.size() << "B)";
        return false;
    }

    out.w = w; out.h = h; out.d = d;
    out.spacingX = (px > 0.0f) ? px : 1.0f;
    out.spacingY = (py > 0.0f) ? py : 1.0f;
    out.spacingZ = (pz > 0.0f) ? pz : 1.0f;
    out.intensity.resize(voxels);

    const uint8_t* dp = buf.data() + dataStart;
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

} // namespace assets
