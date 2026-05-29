#include "VolumeFile.hpp"
#include "src/utils/Logger.hpp"

#include <fstream>

namespace assets {

bool loadRawVolume(const std::string& path, uint32_t w, uint32_t h, uint32_t d,
                   uint32_t bytesPerVoxel, std::vector<uint8_t>& out) {
    if (w == 0 || h == 0 || d == 0 || (bytesPerVoxel != 1 && bytesPerVoxel != 2)) {
        LOG_ERROR("VolumeFile") << "invalid dims/bytesPerVoxel";
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        // Missing file is not an error here -- the caller falls back to a default.
        return false;
    }
    const std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    const size_t voxels = static_cast<size_t>(w) * h * d;
    const size_t needed = voxels * bytesPerVoxel;
    if (static_cast<size_t>(fileSize) < needed) {
        LOG_ERROR("VolumeFile") << path << ": " << fileSize << " bytes < needed " << needed
                                << " (" << w << "x" << h << "x" << d << " @" << bytesPerVoxel << "B)";
        return false;
    }

    std::vector<char> raw(needed);
    file.read(raw.data(), static_cast<std::streamsize>(needed));
    file.close();

    out.resize(voxels);
    if (bytesPerVoxel == 1) {
        std::copy(raw.begin(), raw.begin() + needed,
                  reinterpret_cast<char*>(out.data()));
    } else {
        // uint16 little-endian -> normalize [min,max] to [0,255].
        const uint8_t* b = reinterpret_cast<const uint8_t*>(raw.data());
        uint16_t mn = 0xFFFF, mx = 0;
        for (size_t i = 0; i < voxels; ++i) {
            const uint16_t v = static_cast<uint16_t>(b[i*2] | (b[i*2 + 1] << 8));
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        const float range = (mx > mn) ? static_cast<float>(mx - mn) : 1.0f;
        for (size_t i = 0; i < voxels; ++i) {
            const uint16_t v = static_cast<uint16_t>(b[i*2] | (b[i*2 + 1] << 8));
            out[i] = static_cast<uint8_t>((static_cast<float>(v - mn) / range) * 255.0f + 0.5f);
        }
    }

    LOG_INFO("VolumeFile") << "loaded " << path << " (" << w << "x" << h << "x" << d
                           << " @" << bytesPerVoxel << "B)";
    return true;
}

} // namespace assets
