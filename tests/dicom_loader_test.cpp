#include "TestSupport.hpp"
#include "src/assets/DicomFile.hpp"

#include <glm/gtc/packing.hpp>

namespace {
using Bytes = std::vector<uint8_t>;

void u16(Bytes& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}
void u32(Bytes& out, uint32_t value) {
    u16(out, static_cast<uint16_t>(value));
    u16(out, static_cast<uint16_t>(value >> 16));
}
Bytes textValue(std::string value, char padding = ' ') {
    if (value.size() % 2) value += padding;
    return Bytes(value.begin(), value.end());
}
Bytes shortValue(uint16_t value) {
    Bytes out;
    u16(out, value);
    return out;
}
void element(Bytes& out, uint16_t group, uint16_t tag, const std::string& vr,
             const Bytes& value, bool implicit = false) {
    u16(out, group);
    u16(out, tag);
    if (implicit) {
        u32(out, static_cast<uint32_t>(value.size()));
    } else {
        out.insert(out.end(), vr.begin(), vr.end());
        if (vr == "OW") {
            u16(out, 0);
            u32(out, static_cast<uint32_t>(value.size()));
        } else {
            u16(out, static_cast<uint16_t>(value.size()));
        }
    }
    out.insert(out.end(), value.begin(), value.end());
}

// Minimal synthetic parser fixtures, not clinical studies or codec fixtures.
Bytes dicom(bool implicit, int z, const std::string& uid = "1.2.826.0.1", int frames = 1) {
    Bytes out(128, 0);
    out.insert(out.end(), {'D', 'I', 'C', 'M'});
    element(out, 0x0002, 0x0010, "UI",
            textValue(implicit ? "1.2.840.10008.1.2" : "1.2.840.10008.1.2.1", '\0'));
    element(out, 0x0018, 0x0050, "DS", textValue("2"), implicit);
    element(out, 0x0020, 0x000E, "UI", textValue(uid, '\0'), implicit);
    element(out, 0x0020, 0x0013, "IS", textValue(std::to_string(10 - z)), implicit);
    element(out, 0x0020, 0x0032, "DS", textValue("0\\0\\" + std::to_string(z)), implicit);
    element(out, 0x0028, 0x0008, "IS", textValue(std::to_string(frames)), implicit);
    element(out, 0x0028, 0x0010, "US", shortValue(2), implicit);
    element(out, 0x0028, 0x0011, "US", shortValue(2), implicit);
    element(out, 0x0028, 0x0030, "DS", textValue("0.75\\0.5"), implicit);
    element(out, 0x0028, 0x0100, "US", shortValue(16), implicit);
    element(out, 0x0028, 0x0103, "US", shortValue(1), implicit);
    element(out, 0x0028, 0x1052, "DS", textValue("-10"), implicit);
    element(out, 0x0028, 0x1053, "DS", textValue("2"), implicit);
    Bytes pixels;
    for (int frame = 0; frame < frames; ++frame) {
        for (int raw : {-2, 0, 4, 10}) {
            u16(pixels, static_cast<uint16_t>(raw + (z + frame * 2) * 10));
        }
    }
    element(out, 0x7FE0, 0x0010, "OW", pixels, implicit);
    return out;
}

void checkPaths(const std::filesystem::path& dir) {
    assets::Volume3D decoded, half, meta;
    test::require(assets::loadDicomSeries(dir.string(), decoded), "DICOM float decode");
    test::require(assets::loadDicomSeriesHalf(dir.string(), half), "DICOM half decode");
    std::vector<uint16_t> progressive;
    uint32_t calls = 0;
    test::require(assets::loadDicomSeriesProgressive(dir.string(), meta,
        [&](uint32_t z, const uint16_t* slice) {
            test::require(meta.w == 2 && meta.h == 2 && meta.d == 2, "metadata available before callback");
            test::require(z == calls++, "progressive callback order");
            progressive.insert(progressive.end(), slice, slice + meta.w * meta.h);
        }), "DICOM progressive decode");
    // Known physical values verify sorting by position, signed rescale and row flip.
    const std::vector<float> expected{-2, 10, -14, -10, 38, 50, 26, 30};
    test::require(decoded.intensity == expected, "DICOM sorted, rescaled, flipped values");
    test::require(half.intensity.empty(), "half path allocated float output");
    test::require(half.intensityHalf == progressive && calls == 2, "half/progressive parity");
    for (size_t i = 0; i < expected.size(); ++i)
        test::require(glm::unpackHalf1x16(progressive[i]) == expected[i], "progressive physical value");
    for (const auto* v : {&decoded, &half, &meta}) {
        test::require(v->w == 2 && v->h == 2 && v->d == 2, "DICOM dimensions");
        test::require(v->spacingX == 0.5f && v->spacingY == 0.75f && v->spacingZ == 2,
                      "DICOM anisotropic spacing");
        test::require(v->dataMin == -14 && v->dataMax == 50, "DICOM physical range");
    }
    test::require(meta.intensity.empty() && meta.intensityHalf.empty(), "progressive path retained full volume");
    test::require(std::filesystem::exists(dir / "a.dcm"), "native source was removed");
}

void rejectAll(const std::filesystem::path& dir) {
    assets::Volume3D out;
    test::require(!assets::loadDicomSeries(dir.string(), out), "invalid float series accepted");
    test::require(!assets::loadDicomSeriesHalf(dir.string(), out), "invalid half series accepted");
    bool called = false;
    test::require(!assets::loadDicomSeriesProgressive(dir.string(), out,
                  [&](uint32_t, const uint16_t*) { called = true; }), "invalid progressive series accepted");
    test::require(!called, "invalid series reached sink");
}
} // namespace

int main() {
    try {
        test::TempDirectory fixtures;
        for (bool implicit : {false, true}) {
            auto dir = fixtures.path / (implicit ? "implicit" : "explicit");
            std::filesystem::create_directory(dir);
            // Filename and InstanceNumber order both oppose spatial order.
            test::write(dir / "a.dcm", dicom(implicit, 2));
            test::write(dir / "z.dcm", dicom(implicit, 0));
            checkPaths(dir);
            test::write(dir / "z.dcm", dicom(implicit, 0, "1.2.826.0.2"));
            rejectAll(dir);
        }
        auto multi = fixtures.path / "multi";
        std::filesystem::create_directory(multi);
        test::write(multi / "a.dcm", dicom(false, 0, "1.2.826.0.1", 2));
        checkPaths(multi);
        auto truncated = dicom(false, 0);
        truncated.pop_back();
        test::write(multi / "a.dcm", truncated);
        rejectAll(multi);
        rejectAll(fixtures.path); // no .dcm files at root
        std::cout << "All DICOM loader checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
