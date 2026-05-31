#include "DicomFile.hpp"
#include "src/utils/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <vector>

namespace assets {

namespace fs = std::filesystem;

namespace {

constexpr const char* EXPLICIT_VR_LE = "1.2.840.10008.1.2.1";

// Long VRs use a 4-byte length (with 2 reserved bytes); all others use 2-byte.
bool isLongVR(const char* vr) {
    static const char* long_vrs[] = {"OB", "OW", "OF", "SQ", "UT", "UN"};
    for (const char* v : long_vrs) {
        if (vr[0] == v[0] && vr[1] == v[1]) return true;
    }
    return false;
}

// Trim trailing NUL / space padding from a DICOM string value.
std::string trimDicomString(const uint8_t* data, size_t len) {
    while (len > 0 && (data[len - 1] == 0x00 || data[len - 1] == 0x20)) --len;
    return std::string(reinterpret_cast<const char*>(data), len);
}

double parseDsAsDouble(const uint8_t* data, size_t len) {
    try { return std::stod(trimDicomString(data, len)); } catch (...) { return 0.0; }
}

// Parse a backslash-separated list of numbers (PixelSpacing, ImagePositionPatient).
std::vector<double> parseDsList(const uint8_t* data, size_t len) {
    std::vector<double> out;
    std::string s = trimDicomString(data, len);
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '\\')) {
        try { out.push_back(std::stod(token)); } catch (...) { out.push_back(0.0); }
    }
    return out;
}

template <typename T>
T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof(T)); return v; }

struct Slice {
    uint32_t rows = 0, cols = 0;
    int      instanceNumber = 0;
    int      bitsAllocated = 16;
    bool     signedPixels = true;
    double   pixelSpacingY = 1.0, pixelSpacingX = 1.0;
    double   sliceThickness = 1.0;
    double   imagePosZ = 0.0;
    double   rescaleSlope = 1.0;
    double   rescaleIntercept = 0.0;
    std::string seriesUid;
    std::string transferSyntaxUid;
    const uint8_t* pixelData = nullptr;
    size_t         pixelBytes = 0;
};

// Parse one .dcm file. `fileBuf` owns the file bytes; `out.pixelData` is a view
// into it so the caller keeps `fileBuf` alive while assembling the series.
bool parseSlice(const std::string& path, std::vector<uint8_t>& fileBuf, Slice& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const auto size = static_cast<size_t>(f.tellg());
    if (size < 132) { LOG_ERROR("Dicom") << path << ": file too small"; return false; }
    f.seekg(0, std::ios::beg);
    fileBuf.resize(size);
    f.read(reinterpret_cast<char*>(fileBuf.data()), size);

    if (std::memcmp(fileBuf.data() + 128, "DICM", 4) != 0) {
        LOG_ERROR("Dicom") << path << ": missing DICM magic";
        return false;
    }

    size_t off = 132;
    while (off + 8 <= size) {
        const uint16_t group   = rd<uint16_t>(fileBuf.data() + off + 0);
        const uint16_t element = rd<uint16_t>(fileBuf.data() + off + 2);
        const char* vr = reinterpret_cast<const char*>(fileBuf.data() + off + 4);
        size_t   valueOff;
        uint32_t valueLen;
        if (isLongVR(vr)) {
            if (off + 12 > size) break;
            valueLen = rd<uint32_t>(fileBuf.data() + off + 8);
            valueOff = off + 12;
        } else {
            valueLen = rd<uint16_t>(fileBuf.data() + off + 6);
            valueOff = off + 8;
        }
        if (valueLen == 0xFFFFFFFFu) {
            LOG_ERROR("Dicom") << path << ": undefined-length element -- compressed pixel data unsupported";
            return false;
        }
        if (valueOff + valueLen > size) {
            LOG_ERROR("Dicom") << path << ": element truncated";
            return false;
        }
        const uint8_t* v = fileBuf.data() + valueOff;

        if (group == 0x0002 && element == 0x0010) {
            out.transferSyntaxUid = trimDicomString(v, valueLen);
        } else if (group == 0x0018 && element == 0x0050) {
            out.sliceThickness = parseDsAsDouble(v, valueLen);
        } else if (group == 0x0020 && element == 0x000E) {
            out.seriesUid = trimDicomString(v, valueLen);
        } else if (group == 0x0020 && element == 0x0013) {
            try { out.instanceNumber = std::stoi(trimDicomString(v, valueLen)); } catch (...) {}
        } else if (group == 0x0020 && element == 0x0032) {
            auto xyz = parseDsList(v, valueLen);
            if (xyz.size() >= 3) out.imagePosZ = xyz[2];
        } else if (group == 0x0028 && element == 0x0010) {
            out.rows = rd<uint16_t>(v);
        } else if (group == 0x0028 && element == 0x0011) {
            out.cols = rd<uint16_t>(v);
        } else if (group == 0x0028 && element == 0x0030) {
            auto ps = parseDsList(v, valueLen);
            if (ps.size() >= 2) { out.pixelSpacingY = ps[0]; out.pixelSpacingX = ps[1]; }
        } else if (group == 0x0028 && element == 0x0100) {
            out.bitsAllocated = rd<uint16_t>(v);
        } else if (group == 0x0028 && element == 0x0103) {
            out.signedPixels = (rd<uint16_t>(v) == 1);
        } else if (group == 0x0028 && element == 0x1052) {
            out.rescaleIntercept = parseDsAsDouble(v, valueLen);
        } else if (group == 0x0028 && element == 0x1053) {
            out.rescaleSlope = parseDsAsDouble(v, valueLen);
        } else if (group == 0x7FE0 && element == 0x0010) {
            out.pixelData  = v;
            out.pixelBytes = valueLen;
            break;   // PixelData is the last thing we need
        }

        off = valueOff + valueLen;
    }

    if (!out.pixelData) { LOG_ERROR("Dicom") << path << ": no PixelData"; return false; }
    if (out.transferSyntaxUid != EXPLICIT_VR_LE) {
        LOG_ERROR("Dicom") << path << ": unsupported transfer syntax '"
                           << out.transferSyntaxUid << "' (only Explicit VR LE)";
        return false;
    }
    if (out.bitsAllocated != 16) {
        LOG_ERROR("Dicom") << path << ": unsupported bitsAllocated " << out.bitsAllocated;
        return false;
    }
    if (out.rows == 0 || out.cols == 0) { LOG_ERROR("Dicom") << path << ": missing rows/cols"; return false; }
    const size_t expected = static_cast<size_t>(out.rows) * out.cols * 2;
    if (out.pixelBytes < expected) {
        LOG_ERROR("Dicom") << path << ": pixel data " << out.pixelBytes << "B < expected " << expected << "B";
        return false;
    }
    return true;
}

}  // namespace

bool loadDicomSeries(const std::string& dirPath, Volume3D& out) {
    std::error_code ec;
    if (!fs::is_directory(dirPath, ec)) return false;

    std::vector<std::string> paths;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".dcm") paths.push_back(entry.path().string());
    }
    if (paths.empty()) { LOG_ERROR("Dicom") << dirPath << ": no .dcm files"; return false; }

    std::vector<std::vector<uint8_t>> buffers(paths.size());
    std::vector<Slice> slices(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        if (!parseSlice(paths[i], buffers[i], slices[i])) return false;
    }

    const auto& ref = slices.front();
    for (size_t i = 1; i < slices.size(); ++i) {
        const auto& s = slices[i];
        if (s.rows != ref.rows || s.cols != ref.cols) {
            LOG_ERROR("Dicom") << dirPath << ": inconsistent rows/cols";
            return false;
        }
        if (s.seriesUid != ref.seriesUid) {
            LOG_ERROR("Dicom") << dirPath << ": mixed series in directory";
            return false;
        }
    }

    // Sort by ImagePositionPatient.z (primary), InstanceNumber (fallback).
    std::vector<size_t> order(slices.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (slices[a].imagePosZ != slices[b].imagePosZ)
            return slices[a].imagePosZ < slices[b].imagePosZ;
        return slices[a].instanceNumber < slices[b].instanceNumber;
    });

    const uint32_t w = ref.cols;
    const uint32_t h = ref.rows;
    const uint32_t d = static_cast<uint32_t>(slices.size());

    double zSpacing = ref.sliceThickness;
    if (d >= 2) {
        const double dz = slices[order[1]].imagePosZ - slices[order[0]].imagePosZ;
        if (std::abs(dz) > 1e-6) zSpacing = std::abs(dz);
    }

    out.w = w; out.h = h; out.d = d;
    out.spacingX = static_cast<float>(ref.pixelSpacingX);
    out.spacingY = static_cast<float>(ref.pixelSpacingY);
    out.spacingZ = static_cast<float>(zSpacing);
    out.intensity.assign(static_cast<size_t>(w) * h * d, 0.0f);

    float mn = std::numeric_limits<float>::max();
    float mx = std::numeric_limits<float>::lowest();
    for (uint32_t z = 0; z < d; ++z) {
        const Slice& s = slices[order[z]];
        const uint8_t* px = s.pixelData;
        const float slope = static_cast<float>(s.rescaleSlope);
        const float inter = static_cast<float>(s.rescaleIntercept);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const size_t srcIdx = (static_cast<size_t>(y) * w + x) * 2;
                int16_t raw_i16;
                std::memcpy(&raw_i16, px + srcIdx, 2);
                const float raw = s.signedPixels
                    ? static_cast<float>(raw_i16)
                    : static_cast<float>(static_cast<uint16_t>(raw_i16));
                const float v = slope * raw + inter;
                out.intensity[(static_cast<size_t>(z) * h + y) * w + x] = v;
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
        }
    }
    out.dataMin = mn;
    out.dataMax = mx;

    LOG_INFO("Dicom") << "loaded " << dirPath << " (" << w << "x" << h << "x" << d
                      << ", spacing " << out.spacingX << "/" << out.spacingY << "/" << out.spacingZ
                      << "mm, range [" << mn << "," << mx << "], " << paths.size() << " slices)";
    return true;
}

} // namespace assets
