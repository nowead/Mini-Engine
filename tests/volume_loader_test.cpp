#include "TestSupport.hpp"
#include "src/assets/NiftiFile.hpp"
#include "src/assets/VolumeFile.hpp"

#include <bit>
#include <cstring>

namespace {
template<class T>
void put(std::vector<uint8_t>& bytes, size_t offset, T value) {
    static_assert(std::endian::native == std::endian::little);
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> nifti(int16_t datatype, float slope = 2.0f) {
    std::vector<uint8_t> bytes(352 + 8);
    put<int32_t>(bytes, 0, 348);
    put<int16_t>(bytes, 40, 3);
    put<int16_t>(bytes, 42, 2);
    put<int16_t>(bytes, 44, 1);
    put<int16_t>(bytes, 46, 2);
    put<int16_t>(bytes, 70, datatype);
    put<int16_t>(bytes, 72, 16);
    put<float>(bytes, 80, 0.5f);
    put<float>(bytes, 84, 0.75f);
    put<float>(bytes, 88, 2.0f);
    put<float>(bytes, 108, 352.0f);
    put<float>(bytes, 112, slope);
    put<float>(bytes, 116, -10.0f);
    std::memcpy(bytes.data() + 344, "n+1\0", 4);
    put<int16_t>(bytes, 352, -2);
    put<int16_t>(bytes, 354, 0);
    put<int16_t>(bytes, 356, 4);
    put<int16_t>(bytes, 358, 10);
    return bytes;
}

void testNifti(const std::filesystem::path& dir) {
    const auto path = (dir / "volume.nii").string();
    for (const auto datatype : {int16_t{4}, int16_t{512}}) {
        for (float slope : {2.0f, -2.0f}) {
            auto bytes = nifti(datatype, slope);
            test::write(path, bytes);
            assets::Volume3D decoded;
            assets::MmappedNiftiSource mapped;
            test::require(assets::loadNifti(path, decoded), "NIfTI decode failed");
            test::require(assets::loadNiftiAsMmappedSource(path, mapped), "NIfTI mmap failed");
            test::require(decoded.w == 2 && decoded.h == 1 && decoded.d == 2, "NIfTI dimensions");
            test::require(decoded.spacingX == 0.5f && decoded.spacingY == 0.75f && decoded.spacingZ == 2,
                          "NIfTI anisotropic spacing");
            const float firstRaw = datatype == 4 ? -2.0f : 65534.0f;
            test::require(decoded.intensity == std::vector<float>{slope * firstRaw - 10, -10,
                          slope * 4 - 10, slope * 10 - 10}, "NIfTI signedness and rescale");
            test::require(mapped.isSigned == (datatype == 4), "mmap signedness");
            test::require(mapped.dataMin == decoded.dataMin && mapped.dataMax == decoded.dataMax,
                          "mmap and decoded physical ranges must agree, including negative slope");
            test::require(mapped.dataOffset == 352 && mapped.mmap.size() == bytes.size(), "mapped voxel extent");
        }
    }

    auto valid = nifti(4);
    for (int invalid = 0; invalid < 5; ++invalid) {
        auto bytes = valid;
        switch (invalid) {
            case 0: bytes.resize(100); break;
            case 1: bytes[344] = 'x'; break;
            case 2: put<int16_t>(bytes, 42, 0); break;
            case 3: put<int16_t>(bytes, 70, 64); break;
            case 4: bytes.pop_back(); break;
        }
        test::write(path, bytes);
        assets::Volume3D decoded;
        assets::MmappedNiftiSource mapped;
        test::require(!assets::loadNifti(path, decoded), "Malformed NIfTI accepted");
        test::require(!assets::loadNiftiAsMmappedSource(path, mapped), "Malformed mmap NIfTI accepted");
    }
    assets::Volume3D missing;
    test::require(!assets::loadNifti((dir / "missing.nii").string(), missing), "Missing NIfTI accepted");
}

void testRaw(const std::filesystem::path& dir) {
    const auto path = (dir / "volume.raw").string();
    std::vector<uint8_t> out;
    test::write(path, {0, 127, 255});
    test::require(assets::loadRawVolume(path, 3, 1, 1, 1, out) &&
                  out == std::vector<uint8_t>{0, 127, 255}, "raw uint8 round trip");
    test::write(path, {0, 0, 0, 1, 0, 2});
    test::require(assets::loadRawVolume(path, 3, 1, 1, 2, out) &&
                  out == std::vector<uint8_t>{0, 128, 255}, "raw uint16 LE normalization");
    test::write(path, {42, 0, 42, 0});
    test::require(assets::loadRawVolume(path, 2, 1, 1, 2, out) &&
                  out == std::vector<uint8_t>{0, 0}, "constant raw volume");
    test::require(!assets::loadRawVolume(path, 3, 1, 1, 2, out), "truncated raw accepted");
    test::require(!assets::loadRawVolume(path, 0, 1, 1, 1, out), "zero-sized raw accepted");
    test::require(!assets::loadRawVolume(path, 1, 1, 1, 4, out), "unsupported raw depth accepted");
}
} // namespace

int main() {
    try {
        test::TempDirectory fixtures;
        testNifti(fixtures.path);
        testRaw(fixtures.path);
        std::cout << "All volume loader checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
