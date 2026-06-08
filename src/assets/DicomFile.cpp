#include "DicomFile.hpp"
#include "src/utils/Logger.hpp"

#include <openjpeg.h>

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
constexpr const char* IMPLICIT_VR_LE = "1.2.840.10008.1.2";

// Compressed transfer syntaxes the parser will recognise. Step 1 collects the
// encapsulated frame byte ranges and rejects with a "decoder not implemented"
// message; later steps wire up RLE (no external dep) and JPEG 2000 (openjpeg).
constexpr const char* RLE_LOSSLESS       = "1.2.840.10008.1.2.5";
constexpr const char* JPEG2000_LOSSLESS  = "1.2.840.10008.1.2.4.90";
constexpr const char* JPEG2000_LOSSY     = "1.2.840.10008.1.2.4.91";

bool isCompressedTransferSyntax(const std::string& ts) {
    return ts == RLE_LOSSLESS || ts == JPEG2000_LOSSLESS || ts == JPEG2000_LOSSY;
}

// Long VRs use a 4-byte length (with 2 reserved bytes); all others use 2-byte.
bool isLongVR(const char* vr) {
    static const char* long_vrs[] = {"OB", "OW", "OF", "SQ", "UT", "UN"};
    for (const char* v : long_vrs) {
        if (vr[0] == v[0] && vr[1] == v[1]) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Implicit VR LE tag -> VR dictionary.
// In Implicit VR LE the element header is just (tag 4B + length 4B) with no VR
// bytes, so the parser must know the VR a priori to interpret the value. We
// only need entries for the tags parseSlice actually reads (plus a few SQ tags
// so undefined-length sequence handling works). Anything not in the table is
// treated as VR=UN: 4-byte length, value skipped.
// ---------------------------------------------------------------------------
struct ImplicitVrEntry { uint16_t group; uint16_t element; const char* vr; };
constexpr ImplicitVrEntry kImplicitVrTable[] = {
    // Tags parseSlice consumes
    {0x0018, 0x0050, "DS"}, // SliceThickness
    {0x0020, 0x000E, "UI"}, // SeriesInstanceUID
    {0x0020, 0x0013, "IS"}, // InstanceNumber
    {0x0020, 0x0032, "DS"}, // ImagePositionPatient
    {0x0028, 0x0008, "IS"}, // NumberOfFrames
    {0x0028, 0x0010, "US"}, // Rows
    {0x0028, 0x0011, "US"}, // Columns
    {0x0028, 0x0030, "DS"}, // PixelSpacing
    {0x0028, 0x0100, "US"}, // BitsAllocated
    {0x0028, 0x0103, "US"}, // PixelRepresentation
    {0x0028, 0x1052, "DS"}, // RescaleIntercept
    {0x0028, 0x1053, "DS"}, // RescaleSlope
    {0x7FE0, 0x0010, "OW"}, // PixelData
    // Common SQ tags so undefined-length sequence handling still works
    // (Implicit VR LE has no VR on the wire to spot SQ; we recognise the few
    // that show up in single-frame CT/MR datasets).
    {0x0008, 0x1110, "SQ"}, // ReferencedStudySequence
    {0x0008, 0x1115, "SQ"}, // ReferencedSeriesSequence
    {0x0008, 0x1140, "SQ"}, // ReferencedImageSequence
    {0x0008, 0x2218, "SQ"}, // AnatomicRegionSequence
    {0x0008, 0x9215, "SQ"}, // DerivationCodeSequence
    {0x0040, 0x0275, "SQ"}, // RequestAttributesSequence
};

const char* lookupImplicitVR(uint16_t group, uint16_t element) {
    for (const auto& e : kImplicitVrTable) {
        if (e.group == group && e.element == element) return e.vr;
    }
    return "UN";  // unknown -> 4-byte length, skipped
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

// ---------------------------------------------------------------------------
// Undefined-length sequence/item skipping (Explicit VR LE).
//
// A sequence (SQ) with length 0xFFFFFFFF terminates at (FFFE,E0DD). It contains
// items; each item starts with (FFFE,E000) and has its own length. An item with
// length 0xFFFFFFFF terminates at (FFFE,E00D) and contains nested data elements
// that may themselves be undefined-length sequences. A naive scan for (FFFE,E0DD)
// trips over inner sequences' delimitations -- we have to walk items properly.
// ---------------------------------------------------------------------------
size_t skipUndefSeq(const uint8_t* buf, size_t size, size_t off);

size_t walkItemUntilEnd(const uint8_t* buf, size_t size, size_t off) {
    while (off + 8 <= size) {
        const uint16_t g = rd<uint16_t>(buf + off + 0);
        const uint16_t e = rd<uint16_t>(buf + off + 2);
        if (g == 0xFFFEu && e == 0xE00Du) return off + 8;   // Item Delimitation Item
        // Otherwise a regular Explicit VR LE element inside the item.
        if (off + 8 > size) return size;
        const char* vr = reinterpret_cast<const char*>(buf + off + 4);
        size_t   valueOff;
        uint32_t valueLen;
        if (isLongVR(vr)) {
            if (off + 12 > size) return size;
            valueLen = rd<uint32_t>(buf + off + 8);
            valueOff = off + 12;
        } else {
            valueLen = rd<uint16_t>(buf + off + 6);
            valueOff = off + 8;
        }
        if (valueLen == 0xFFFFFFFFu) {
            off = skipUndefSeq(buf, size, valueOff);
            if (off >= size) return size;
        } else {
            if (valueOff + valueLen > size) return size;
            off = valueOff + valueLen;
        }
    }
    return size;
}

size_t skipUndefSeq(const uint8_t* buf, size_t size, size_t off) {
    while (off + 8 <= size) {
        const uint16_t g = rd<uint16_t>(buf + off + 0);
        const uint16_t e = rd<uint16_t>(buf + off + 2);
        if (g == 0xFFFEu && e == 0xE0DDu) return off + 8;   // Sequence Delimitation Item
        if (g == 0xFFFEu && e == 0xE000u) {
            const uint32_t itemLen = rd<uint32_t>(buf + off + 4);
            off += 8;
            if (itemLen == 0xFFFFFFFFu) {
                off = walkItemUntilEnd(buf, size, off);
                if (off >= size) return size;
            } else {
                if (off + itemLen > size) return size;
                off += itemLen;
            }
        } else {
            // Malformed -- bail.
            return size;
        }
    }
    return size;
}

// One compressed pixel-data item -- a single frame's encoded bytes living
// inside the file buffer (no copy).
struct EncapsulatedFrame { const uint8_t* data; uint32_t size; };

struct Slice {
    uint32_t rows = 0, cols = 0;
    uint32_t numberOfFrames = 1;   // multi-frame DICOM packs many frames into one file
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
    const uint8_t* pixelData = nullptr;   // uncompressed path: view into file buffer
    size_t         pixelBytes = 0;
    std::vector<EncapsulatedFrame> frames; // compressed path: per-frame raw byte ranges
    // For compressed transfer syntaxes the decoder writes its output here and
    // points pixelData at it. Keeping the decoded buffer owned by the Slice
    // lets all downstream code (frame indexing, pixel sample loop in
    // loadDicomSeries) stay on the single uncompressed code path.
    std::vector<uint8_t> decompressedBuffer;
};

// ---------------------------------------------------------------------------
// Walk encapsulated pixel data starting just past the (7FE0,0010) OB header
// (i.e. at the first item tag). Items are (FFFE,E000) length-prefixed; the
// first item is the Basic Offset Table (BOT) which we skip -- BOT length may
// be 0 (offsets unknown) or contain per-frame offsets but pydicom-style
// readers ignore it because the item walk is already cheap. Stops at the
// Sequence Delimitation Item (FFFE,E0DD). Each non-BOT item becomes one
// EncapsulatedFrame entry; for multi-frame DICOM the caller maps these to
// NumberOfFrames z slices the same way the uncompressed path does.
// ---------------------------------------------------------------------------
bool walkEncapsulatedPixelData(const std::string& path,
                               const uint8_t* buf, size_t size, size_t off,
                               std::vector<EncapsulatedFrame>& out) {
    bool sawBot = false;
    while (off + 8 <= size) {
        const uint16_t g = rd<uint16_t>(buf + off + 0);
        const uint16_t e = rd<uint16_t>(buf + off + 2);
        if (g == 0xFFFEu && e == 0xE0DDu) return true;  // Sequence Delimitation Item
        if (g != 0xFFFEu || e != 0xE000u) {
            LOG_ERROR("Dicom") << path << ": encapsulated pixel data: bad item tag";
            return false;
        }
        const uint32_t itemLen = rd<uint32_t>(buf + off + 4);
        const size_t   itemOff = off + 8;
        if (itemOff + itemLen > size) {
            LOG_ERROR("Dicom") << path << ": encapsulated pixel data: item overruns file";
            return false;
        }
        if (!sawBot) {
            sawBot = true;  // first item is BOT (may have length 0)
        } else if (itemLen > 0) {
            out.push_back({buf + itemOff, itemLen});
        }
        off = itemOff + itemLen;
    }
    LOG_ERROR("Dicom") << path << ": encapsulated pixel data: no sequence delimiter";
    return false;
}

// ---------------------------------------------------------------------------
// PackBits decoder (DICOM Annex G.5 / Apple PackBits).
// Control byte n:
//   0..127     -> copy next (n+1) literal bytes
//   -127..-1   -> replicate next byte (1 - n) times = 2..128
//   -128 (NOP) -> no-op marker, skip
// Returns true iff exactly dstSize bytes were produced.
// ---------------------------------------------------------------------------
bool unpackBits(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    // Loop until the output is filled; DICOM RLE may pad the encoded segment to
    // an even byte boundary, so trailing src bytes past the meaningful PackBits
    // content are normal and must be ignored once di == dstSize.
    size_t si = 0, di = 0;
    while (di < dstSize) {
        if (si >= srcSize) return false;
        const int8_t n = static_cast<int8_t>(src[si++]);
        if (n == -128) {
            continue;  // NOP
        }
        if (n >= 0) {
            const size_t count = static_cast<size_t>(n) + 1;
            if (si + count > srcSize || di + count > dstSize) return false;
            std::memcpy(dst + di, src + si, count);
            si += count;
            di += count;
        } else {
            const size_t count = static_cast<size_t>(1 - n);
            if (si >= srcSize || di + count > dstSize) return false;
            std::memset(dst + di, src[si++], count);
            di += count;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Decode one RLE-encoded 16-bit monochrome DICOM frame into rows*cols
// little-endian uint16 pixels (2 bytes each).
//
// DICOM PS3.5 Annex G.5 frame layout:
//   - 64-byte header: 16 uint32 LE
//       [0]   = number of segments
//       [1..] = byte offsets to segment 0, 1, ... from the start of the frame
//   - For 16-bit monochrome there are 2 segments: segment 0 holds the high
//     byte (MSB) of each pixel in row-major order, segment 1 holds the low
//     byte. Each segment is PackBits-encoded.
// ---------------------------------------------------------------------------
bool decodeRleFrame16(const std::string& path,
                      const EncapsulatedFrame& frame,
                      uint32_t rows, uint32_t cols,
                      uint8_t* dst /* rows*cols*2 bytes */) {
    if (frame.size < 64) {
        LOG_ERROR("Dicom") << path << ": RLE frame header truncated";
        return false;
    }
    const uint32_t numSegments = rd<uint32_t>(frame.data + 0);
    if (numSegments != 2) {
        LOG_ERROR("Dicom") << path << ": RLE expected 2 segments for 16-bit, got " << numSegments;
        return false;
    }
    const uint32_t off0 = rd<uint32_t>(frame.data + 4);
    const uint32_t off1 = rd<uint32_t>(frame.data + 8);
    if (off0 < 64 || off1 < 64 || off0 > frame.size || off1 > frame.size || off0 >= off1) {
        LOG_ERROR("Dicom") << path << ": RLE bad segment offsets " << off0 << "/" << off1;
        return false;
    }
    const size_t plane = static_cast<size_t>(rows) * cols;
    std::vector<uint8_t> msbPlane(plane);
    std::vector<uint8_t> lsbPlane(plane);
    if (!unpackBits(frame.data + off0, off1 - off0, msbPlane.data(), plane)) {
        LOG_ERROR("Dicom") << path << ": RLE PackBits failed on segment 0 (MSB)";
        return false;
    }
    if (!unpackBits(frame.data + off1, frame.size - off1, lsbPlane.data(), plane)) {
        LOG_ERROR("Dicom") << path << ": RLE PackBits failed on segment 1 (LSB)";
        return false;
    }
    // Combine MSB/LSB into little-endian uint16 = [lsb, msb] per pixel, which
    // matches the byte layout the uncompressed path produces.
    for (size_t i = 0; i < plane; ++i) {
        dst[i * 2 + 0] = lsbPlane[i];
        dst[i * 2 + 1] = msbPlane[i];
    }
    return true;
}

// ---------------------------------------------------------------------------
// JPEG 2000 frame decoder via OpenJPEG memory stream.
//
// Wraps the standard opj_create_decompress -> opj_read_header -> opj_decode
// pipeline. The encoded frame bytes are fed through a custom memory stream
// (so we don't write the encapsulated item to a temp file). The decoded
// component data is stored as OPJ_INT32; we truncate to 16-bit LE pixels
// because the downstream pixel loop in loadDicomSeries reads 16-bit and
// interprets the sign via PixelRepresentation.
// ---------------------------------------------------------------------------
struct OpjMemStream { const uint8_t* data; size_t size; size_t pos; };

OPJ_SIZE_T opjMemRead(void* buffer, OPJ_SIZE_T n, void* user_data) {
    auto* ms = static_cast<OpjMemStream*>(user_data);
    const size_t remaining = ms->size - ms->pos;
    if (remaining == 0) return static_cast<OPJ_SIZE_T>(-1);
    const size_t toRead = (n < remaining) ? n : remaining;
    std::memcpy(buffer, ms->data + ms->pos, toRead);
    ms->pos += toRead;
    return toRead;
}

OPJ_OFF_T opjMemSkip(OPJ_OFF_T n, void* user_data) {
    auto* ms = static_cast<OpjMemStream*>(user_data);
    if (n <= 0) return 0;
    const size_t remaining = ms->size - ms->pos;
    const size_t want = static_cast<size_t>(n);
    const size_t toSkip = (want < remaining) ? want : remaining;
    ms->pos += toSkip;
    return static_cast<OPJ_OFF_T>(toSkip);
}

OPJ_BOOL opjMemSeek(OPJ_OFF_T n, void* user_data) {
    auto* ms = static_cast<OpjMemStream*>(user_data);
    if (n < 0 || static_cast<size_t>(n) > ms->size) return OPJ_FALSE;
    ms->pos = static_cast<size_t>(n);
    return OPJ_TRUE;
}

// Silent error/warning handlers so a corrupt frame doesn't spam stderr; we
// surface failures via LOG_ERROR with the file path context.
void opjQuietMsg(const char*, void*) {}

bool decodeJpeg2000Frame16(const std::string& path,
                           const EncapsulatedFrame& frame,
                           uint32_t rows, uint32_t cols,
                           uint8_t* dst /* rows*cols*2 bytes LE */) {
    OpjMemStream ms{frame.data, frame.size, 0};
    opj_stream_t* stream = opj_stream_default_create(OPJ_TRUE /* is_read_stream */);
    if (!stream) {
        LOG_ERROR("Dicom") << path << ": JPEG2000 stream create failed";
        return false;
    }
    opj_stream_set_read_function(stream, opjMemRead);
    opj_stream_set_skip_function(stream, opjMemSkip);
    opj_stream_set_seek_function(stream, opjMemSeek);
    opj_stream_set_user_data(stream, &ms, nullptr);
    opj_stream_set_user_data_length(stream, frame.size);

    opj_codec_t* codec = opj_create_decompress(OPJ_CODEC_J2K);
    if (!codec) {
        opj_stream_destroy(stream);
        LOG_ERROR("Dicom") << path << ": JPEG2000 codec create failed";
        return false;
    }
    opj_set_info_handler(codec, opjQuietMsg, nullptr);
    opj_set_warning_handler(codec, opjQuietMsg, nullptr);
    opj_set_error_handler(codec, opjQuietMsg, nullptr);

    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);
    if (!opj_setup_decoder(codec, &params)) {
        opj_destroy_codec(codec); opj_stream_destroy(stream);
        LOG_ERROR("Dicom") << path << ": JPEG2000 decoder setup failed";
        return false;
    }

    opj_image_t* image = nullptr;
    if (!opj_read_header(stream, codec, &image)) {
        if (image) opj_image_destroy(image);
        opj_destroy_codec(codec); opj_stream_destroy(stream);
        LOG_ERROR("Dicom") << path << ": JPEG2000 header read failed";
        return false;
    }
    if (!opj_decode(codec, stream, image) ||
        !opj_end_decompress(codec, stream)) {
        opj_image_destroy(image);
        opj_destroy_codec(codec); opj_stream_destroy(stream);
        LOG_ERROR("Dicom") << path << ": JPEG2000 decode failed";
        return false;
    }

    bool ok = true;
    if (image->numcomps != 1) {
        LOG_ERROR("Dicom") << path << ": JPEG2000 expected 1 component, got " << image->numcomps;
        ok = false;
    } else if (image->comps[0].w != cols || image->comps[0].h != rows) {
        LOG_ERROR("Dicom") << path << ": JPEG2000 decoded "
                           << image->comps[0].w << "x" << image->comps[0].h
                           << " differs from header " << cols << "x" << rows;
        ok = false;
    } else {
        const OPJ_INT32* src = image->comps[0].data;
        const size_t plane = static_cast<size_t>(rows) * cols;
        for (size_t i = 0; i < plane; ++i) {
            const int16_t v = static_cast<int16_t>(src[i]);
            dst[i * 2 + 0] = static_cast<uint8_t>(v & 0xFF);
            dst[i * 2 + 1] = static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF);
        }
    }
    opj_image_destroy(image);
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return ok;
}

// Dispatch one parsed (tag, value) pair into the Slice fields. Shared between
// Explicit and Implicit VR LE dataset walks (the value bytes are identical, only
// the element header encoding differs).
void applyElement(uint16_t group, uint16_t element,
                  const uint8_t* v, uint32_t valueLen, Slice& out) {
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
    } else if (group == 0x0028 && element == 0x0008) {
        try { out.numberOfFrames = std::max(1, std::stoi(trimDicomString(v, valueLen))); } catch (...) {}
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
    }
}

// Walk an Explicit VR LE dataset starting at `off`, populating `out` and
// returning true once PixelData (7FE0,0010) is consumed (or the buffer ends).
bool walkExplicitDataset(const std::string& path, const uint8_t* buf, size_t size,
                         size_t off, Slice& out) {
    while (off + 8 <= size) {
        const uint16_t group   = rd<uint16_t>(buf + off + 0);
        const uint16_t element = rd<uint16_t>(buf + off + 2);
        const char* vr = reinterpret_cast<const char*>(buf + off + 4);
        size_t   valueOff;
        uint32_t valueLen;
        if (isLongVR(vr)) {
            if (off + 12 > size) break;
            valueLen = rd<uint32_t>(buf + off + 8);
            valueOff = off + 12;
        } else {
            valueLen = rd<uint16_t>(buf + off + 6);
            valueOff = off + 8;
        }
        if (valueLen == 0xFFFFFFFFu) {
            if (group == 0x7FE0 && element == 0x0010) {
                // Encapsulated pixel data -- valid only when the transfer syntax
                // is one of the compressed ones. Walk the items here so the
                // caller sees per-frame ranges; the actual decode comes later.
                if (!isCompressedTransferSyntax(out.transferSyntaxUid)) {
                    LOG_ERROR("Dicom") << path
                        << ": encapsulated PixelData but transfer syntax '"
                        << out.transferSyntaxUid << "' is uncompressed";
                    return false;
                }
                return walkEncapsulatedPixelData(path, buf, size, valueOff, out.frames);
            }
            const size_t skipped = skipUndefSeq(buf, size, valueOff);
            if (skipped >= size) {
                LOG_ERROR("Dicom") << path << ": malformed undefined-length sequence";
                return false;
            }
            off = skipped;
            continue;
        }
        if (valueOff + valueLen > size) {
            LOG_ERROR("Dicom") << path << ": element truncated";
            return false;
        }
        const uint8_t* v = buf + valueOff;
        if (group == 0x7FE0 && element == 0x0010) {
            out.pixelData  = v;
            out.pixelBytes = valueLen;
            return true;
        }
        applyElement(group, element, v, valueLen, out);
        off = valueOff + valueLen;
    }
    return true;  // ran off the end without hitting PixelData -- caller validates
}

// Walk an Implicit VR LE dataset. Same shape as Explicit but each element is
// just (tag 4B + length 4B); VR comes from the kImplicitVrTable lookup.
bool walkImplicitDataset(const std::string& path, const uint8_t* buf, size_t size,
                         size_t off, Slice& out) {
    while (off + 8 <= size) {
        const uint16_t group   = rd<uint16_t>(buf + off + 0);
        const uint16_t element = rd<uint16_t>(buf + off + 2);
        const uint32_t valueLen = rd<uint32_t>(buf + off + 4);
        const size_t   valueOff = off + 8;
        const char* vr = lookupImplicitVR(group, element);

        if (valueLen == 0xFFFFFFFFu) {
            if (group == 0x7FE0 && element == 0x0010) {
                if (!isCompressedTransferSyntax(out.transferSyntaxUid)) {
                    LOG_ERROR("Dicom") << path
                        << ": encapsulated PixelData but transfer syntax '"
                        << out.transferSyntaxUid << "' is uncompressed (implicit)";
                    return false;
                }
                return walkEncapsulatedPixelData(path, buf, size, valueOff, out.frames);
            }
            // Undefined-length sequence/item -- same skip logic for both
            // Explicit and Implicit (skipUndefSeq only reads group/element/length,
            // not VR, so it works either way).
            const size_t skipped = skipUndefSeq(buf, size, valueOff);
            if (skipped >= size) {
                LOG_ERROR("Dicom") << path << ": malformed undefined-length sequence (implicit)";
                return false;
            }
            off = skipped;
            continue;
        }
        if (valueOff + valueLen > size) {
            LOG_ERROR("Dicom") << path << ": element truncated (implicit)";
            return false;
        }
        const uint8_t* v = buf + valueOff;
        if (group == 0x7FE0 && element == 0x0010) {
            out.pixelData  = v;
            out.pixelBytes = valueLen;
            return true;
        }
        // SQ in Implicit VR with defined length: skip the whole thing (we don't
        // need any tag inside a sequence for this loader).
        if (vr[0] == 'S' && vr[1] == 'Q') {
            off = valueOff + valueLen;
            continue;
        }
        applyElement(group, element, v, valueLen, out);
        off = valueOff + valueLen;
    }
    return true;
}

// Parse one .dcm file. `fileBuf` owns the file bytes; `out.pixelData` is a view
// into it so the caller keeps `fileBuf` alive while assembling the series.
// The DICOM file meta header (group 0002) is ALWAYS Explicit VR LE per the
// standard. We walk it with the Explicit reader to grab TransferSyntaxUID, then
// switch to either Explicit or Implicit for the dataset that follows.
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

    // Walk only the file meta (group 0002) with Explicit VR; stop when we leave
    // group 0002 so we know where the dataset starts.
    size_t off = 132;
    while (off + 8 <= size) {
        const uint16_t group   = rd<uint16_t>(fileBuf.data() + off + 0);
        const uint16_t element = rd<uint16_t>(fileBuf.data() + off + 2);
        if (group != 0x0002) break;  // dataset begins here

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
        if (valueOff + valueLen > size) {
            LOG_ERROR("Dicom") << path << ": file meta truncated";
            return false;
        }
        if (element == 0x0010) {
            out.transferSyntaxUid = trimDicomString(fileBuf.data() + valueOff, valueLen);
        }
        off = valueOff + valueLen;
    }

    // Per DICOM PS3.5 Annex A, all compressed transfer syntaxes encode the
    // dataset itself in Explicit VR Little Endian and only the pixel data is
    // encapsulated -- so we dispatch them to the Explicit walker, which calls
    // walkEncapsulatedPixelData on hitting (7FE0,0010) with length 0xFFFFFFFF.
    bool ok;
    if (out.transferSyntaxUid == EXPLICIT_VR_LE ||
        isCompressedTransferSyntax(out.transferSyntaxUid)) {
        ok = walkExplicitDataset(path, fileBuf.data(), size, off, out);
    } else if (out.transferSyntaxUid == IMPLICIT_VR_LE) {
        ok = walkImplicitDataset(path, fileBuf.data(), size, off, out);
    } else {
        LOG_ERROR("Dicom") << path << ": unsupported transfer syntax '"
                           << out.transferSyntaxUid
                           << "' (supported: Explicit VR LE, Implicit VR LE, "
                           << "RLE Lossless, JPEG 2000)";
        return false;
    }
    if (!ok) return false;

    const bool isCompressed = isCompressedTransferSyntax(out.transferSyntaxUid);
    if (!isCompressed && !out.pixelData) {
        LOG_ERROR("Dicom") << path << ": no PixelData";
        return false;
    }
    if (isCompressed && out.frames.empty()) {
        LOG_ERROR("Dicom") << path << ": compressed pixel data has no frames";
        return false;
    }
    if (out.bitsAllocated != 16) {
        LOG_ERROR("Dicom") << path << ": unsupported bitsAllocated " << out.bitsAllocated;
        return false;
    }
    if (out.rows == 0 || out.cols == 0) { LOG_ERROR("Dicom") << path << ": missing rows/cols"; return false; }
    if (!isCompressed) {
        const size_t expected = static_cast<size_t>(out.rows) * out.cols * 2 * out.numberOfFrames;
        if (out.pixelBytes < expected) {
            LOG_ERROR("Dicom") << path << ": pixel data " << out.pixelBytes << "B < expected " << expected
                               << "B (" << out.rows << "x" << out.cols << " x " << out.numberOfFrames << " frames)";
            return false;
        }
        return true;
    }

    // Compressed path: decode each frame into out.decompressedBuffer laid out
    // identically to the uncompressed multi-frame format (frame 0 bytes, frame 1
    // bytes, ...), then point out.pixelData at it so the downstream pixel
    // assembly stays on the single code path.
    if (out.frames.size() != out.numberOfFrames) {
        LOG_ERROR("Dicom") << path << ": encapsulated frame count " << out.frames.size()
                           << " != NumberOfFrames " << out.numberOfFrames;
        return false;
    }
    const size_t frameBytes = static_cast<size_t>(out.rows) * out.cols * 2;
    out.decompressedBuffer.resize(frameBytes * out.numberOfFrames);

    if (out.transferSyntaxUid == RLE_LOSSLESS) {
        for (size_t i = 0; i < out.frames.size(); ++i) {
            if (!decodeRleFrame16(path, out.frames[i], out.rows, out.cols,
                                  out.decompressedBuffer.data() + i * frameBytes)) {
                return false;
            }
        }
    } else if (out.transferSyntaxUid == JPEG2000_LOSSLESS ||
               out.transferSyntaxUid == JPEG2000_LOSSY) {
        for (size_t i = 0; i < out.frames.size(); ++i) {
            if (!decodeJpeg2000Frame16(path, out.frames[i], out.rows, out.cols,
                                       out.decompressedBuffer.data() + i * frameBytes)) {
                return false;
            }
        }
    } else {
        LOG_ERROR("Dicom") << path << ": transfer syntax '" << out.transferSyntaxUid
                           << "' recognised but no decoder bound";
        return false;
    }
    out.pixelData  = out.decompressedBuffer.data();
    out.pixelBytes = out.decompressedBuffer.size();
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

    // Flatten frames: each output z is one (file, frame) pair. For a normal series
    // each file has 1 frame; for multi-frame DICOM (NumberOfFrames > 1) a single
    // file contributes that many consecutive z slices. Sort by z position then
    // frame index then instance number.
    struct Entry { size_t fileIdx; uint32_t frameIdx; double zPos; int instanceNumber; };
    std::vector<Entry> order;
    for (size_t i = 0; i < slices.size(); ++i) {
        const auto& s = slices[i];
        for (uint32_t f = 0; f < s.numberOfFrames; ++f) {
            // Multi-frame files don't carry per-frame ImagePositionPatient in this
            // first cut, so we space frames along z by SliceThickness from the
            // file's base position.
            const double zp = s.imagePosZ + static_cast<double>(f) * s.sliceThickness;
            order.push_back({ i, f, zp, s.instanceNumber });
        }
    }
    std::sort(order.begin(), order.end(), [](const Entry& a, const Entry& b) {
        if (a.zPos != b.zPos) return a.zPos < b.zPos;
        if (a.instanceNumber != b.instanceNumber) return a.instanceNumber < b.instanceNumber;
        return a.frameIdx < b.frameIdx;
    });

    const uint32_t w = ref.cols;
    const uint32_t h = ref.rows;
    const uint32_t d = static_cast<uint32_t>(order.size());

    double zSpacing = ref.sliceThickness;
    if (d >= 2) {
        const double dz = order[1].zPos - order[0].zPos;
        if (std::abs(dz) > 1e-6) zSpacing = std::abs(dz);
    }

    out.w = w; out.h = h; out.d = d;
    out.spacingX = static_cast<float>(ref.pixelSpacingX);
    out.spacingY = static_cast<float>(ref.pixelSpacingY);
    out.spacingZ = static_cast<float>(zSpacing);
    out.intensity.assign(static_cast<size_t>(w) * h * d, 0.0f);

    const size_t frameBytes = static_cast<size_t>(w) * h * 2;
    float mn = std::numeric_limits<float>::max();
    float mx = std::numeric_limits<float>::lowest();
    for (uint32_t z = 0; z < d; ++z) {
        const Slice& s = slices[order[z].fileIdx];
        const uint8_t* px = s.pixelData + static_cast<size_t>(order[z].frameIdx) * frameBytes;
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
                      << "mm, range [" << mn << "," << mx << "], "
                      << paths.size() << (paths.size() == 1 ? " file" : " files")
                      << ", " << d << (d == 1 ? " frame" : " frames") << ")";
    return true;
}

} // namespace assets
