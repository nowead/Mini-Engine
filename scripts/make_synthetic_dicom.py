#!/usr/bin/env python3
"""Generate a synthetic DICOM series for testing the DICOM volume loader.

Pure stdlib (no pydicom). Writes <out_dir>/slice_NNN.dcm files in Explicit VR
Little Endian transfer syntax, one int16 axial slice per file. Same synthetic CT
shape as scripts/make_synthetic_nii.py (air -1000, soft tissue +40, bone +800)
so the two loaders can be cross-checked.

Usage:
    python scripts/make_synthetic_dicom.py <out_dir> [W H D] [tissue_frac bone_frac]
Defaults: 96 96 48,  tissue 0.85, bone 0.35.
"""
import os
import struct
import sys

# ---------------------------------------------------------------------------
# Explicit VR Little Endian element encoding
# ---------------------------------------------------------------------------
# Most VRs: 4-byte Tag (group, element) + 2-byte VR + 2-byte Length + Value.
# Long VRs (OB/OW/OF/SQ/UT/UN): 4-byte Tag + 2-byte VR + 2 reserved + 4-byte Length.

LONG_VRS = {"OB", "OW", "OF", "SQ", "UT", "UN"}


def elem(group: int, element: int, vr: str, value: bytes) -> bytes:
    # Value must be even length; pad with VR-appropriate byte.
    if len(value) & 1:
        pad = b"\x00" if vr in ("UI", "OB", "OW") else b" "
        value = value + pad
    out = struct.pack("<HH", group, element) + vr.encode("ascii")
    if vr in LONG_VRS:
        out += b"\x00\x00" + struct.pack("<I", len(value))
    else:
        out += struct.pack("<H", len(value))
    return out + value


def s(text: str) -> bytes:
    return text.encode("ascii")


def us(n: int) -> bytes:  return struct.pack("<H", n)
def ul(n: int) -> bytes:  return struct.pack("<I", n)


# ---------------------------------------------------------------------------
# DICOM file build
# ---------------------------------------------------------------------------

EXPLICIT_VR_LE = "1.2.840.10008.1.2.1"
CT_IMAGE_SOP   = "1.2.840.10008.5.1.4.1.1.2"   # CT Image Storage
IMPL_UID       = "1.2.826.0.1.3680043.9.7236.1"

# Generate unique UIDs by appending a counter to a prefix.
_UID_PREFIX = "1.2.826.0.1.9999.99"
_uid_counter = [1]
def new_uid() -> str:
    u = f"{_UID_PREFIX}.{_uid_counter[0]}"
    _uid_counter[0] += 1
    return u


def build_file_meta(sop_instance_uid: str) -> bytes:
    # Build the (0002,xxxx) group first WITHOUT the group-length, then prepend it.
    body = b""
    body += elem(0x0002, 0x0001, "OB", b"\x00\x01")
    body += elem(0x0002, 0x0002, "UI", s(CT_IMAGE_SOP))
    body += elem(0x0002, 0x0003, "UI", s(sop_instance_uid))
    body += elem(0x0002, 0x0010, "UI", s(EXPLICIT_VR_LE))
    body += elem(0x0002, 0x0012, "UI", s(IMPL_UID))
    group_len = elem(0x0002, 0x0000, "UL", ul(len(body)))
    return group_len + body


def build_slice(w, h, voxels_le_int16: bytes, *, series_uid: str, study_uid: str,
                instance_number: int, image_pos_z_mm: float,
                pixel_spacing_xy: float, slice_thickness: float) -> bytes:
    sop_uid = new_uid()
    out = b"\x00" * 128 + b"DICM"
    out += build_file_meta(sop_uid)
    # Dataset (Explicit VR LE, ascending tag order).
    out += elem(0x0008, 0x0016, "UI", s(CT_IMAGE_SOP))
    out += elem(0x0008, 0x0018, "UI", s(sop_uid))
    out += elem(0x0008, 0x0060, "CS", s("CT"))
    out += elem(0x0010, 0x0010, "PN", s("Synthetic^Test"))
    out += elem(0x0010, 0x0020, "LO", s("SYN-001"))
    out += elem(0x0018, 0x0050, "DS", s(f"{slice_thickness}"))
    out += elem(0x0020, 0x000D, "UI", s(study_uid))
    out += elem(0x0020, 0x000E, "UI", s(series_uid))
    out += elem(0x0020, 0x0011, "IS", s("1"))
    out += elem(0x0020, 0x0013, "IS", s(str(instance_number)))
    out += elem(0x0020, 0x0032, "DS", s(f"0\\0\\{image_pos_z_mm}"))
    out += elem(0x0020, 0x0037, "DS", s("1\\0\\0\\0\\1\\0"))
    out += elem(0x0028, 0x0002, "US", us(1))
    out += elem(0x0028, 0x0004, "CS", s("MONOCHROME2"))
    out += elem(0x0028, 0x0010, "US", us(h))   # Rows
    out += elem(0x0028, 0x0011, "US", us(w))   # Columns
    out += elem(0x0028, 0x0030, "DS", s(f"{pixel_spacing_xy}\\{pixel_spacing_xy}"))
    out += elem(0x0028, 0x0100, "US", us(16))
    out += elem(0x0028, 0x0101, "US", us(16))
    out += elem(0x0028, 0x0102, "US", us(15))
    out += elem(0x0028, 0x0103, "US", us(1))   # 1 = signed
    out += elem(0x0028, 0x1052, "DS", s("0"))
    out += elem(0x0028, 0x1053, "DS", s("1"))
    # Pixel Data (OW for 16-bit). Length must be even (it is: w*h*2).
    out += elem(0x7FE0, 0x0010, "OW", voxels_le_int16)
    return out


# ---------------------------------------------------------------------------
# Synthetic CT pattern (matches make_synthetic_nii.py)
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("usage: make_synthetic_dicom.py <out_dir> [W H D] [tissue_frac bone_frac]")
        sys.exit(2)
    out_dir = sys.argv[1]
    if len(sys.argv) >= 5:
        w, h, d = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    else:
        w, h, d = 96, 96, 48
    tissue_frac = float(sys.argv[5]) if len(sys.argv) >= 6 else 0.85
    bone_frac   = float(sys.argv[6]) if len(sys.argv) >= 7 else 0.35
    sx = sy = 1.0
    sz = 2.5

    os.makedirs(out_dir, exist_ok=True)
    cx, cy, cz = (w - 1) / 2.0, (h - 1) / 2.0, (d - 1) / 2.0
    half = min(cx, cy, cz)
    r_tissue = tissue_frac * half
    r_bone   = bone_frac   * half

    series_uid = new_uid()
    study_uid  = new_uid()

    for z in range(d):
        # Build the int16 slice in row-major (x fastest, y next).
        slice_bytes = bytearray(w * h * 2)
        i = 0
        dz = z - cz
        for y in range(h):
            dy = y - cy
            for x in range(w):
                dx = x - cx
                dist = (dx * dx + dy * dy + dz * dz) ** 0.5
                if dist <= r_bone:
                    hu = 800
                elif dist <= r_tissue:
                    hu = 40
                else:
                    hu = -1000
                struct.pack_into("<h", slice_bytes, i, hu)
                i += 2
        dcm = build_slice(
            w, h, bytes(slice_bytes),
            series_uid=series_uid, study_uid=study_uid,
            instance_number=z + 1, image_pos_z_mm=z * sz,
            pixel_spacing_xy=sx, slice_thickness=sz,
        )
        path = os.path.join(out_dir, f"slice_{z:04d}.dcm")
        with open(path, "wb") as f:
            f.write(dcm)
    print(f"wrote {d} slice files to {out_dir}/ "
          f"({w}x{h}x{d} int16, spacing ({sx},{sy},{sz})mm)")


if __name__ == "__main__":
    main()
