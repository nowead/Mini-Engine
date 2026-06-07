#!/usr/bin/env python3
"""Generate a synthetic DICOM series for testing the DICOM volume loader.

Pure stdlib (no pydicom). Writes <out_dir>/slice_NNN.dcm files. Transfer syntax
is selectable via --vr {explicit,implicit} (default: explicit). The DICOM file
meta header (group 0002) is ALWAYS Explicit VR LE per the standard; only the
dataset after it switches encoding.

Same synthetic CT shape as scripts/make_synthetic_nii.py (air -1000, soft
tissue +40, bone +800) so the two loaders can be cross-checked.

Usage:
    python scripts/make_synthetic_dicom.py <out_dir> [W H D] [tissue_frac bone_frac]
                                           [--vr explicit|implicit]
Defaults: 96 96 48,  tissue 0.85, bone 0.35, vr explicit.
"""
import os
import struct
import sys

# ---------------------------------------------------------------------------
# Element encoding -- both Explicit VR LE and Implicit VR LE
# ---------------------------------------------------------------------------
# Explicit VR LE:
#   Short VRs: 4-byte Tag + 2-byte VR + 2-byte Length + Value
#   Long  VRs: 4-byte Tag + 2-byte VR + 2 reserved + 4-byte Length + Value
# Implicit VR LE (no VR in header):
#   All:       4-byte Tag + 4-byte Length + Value

LONG_VRS = {"OB", "OW", "OF", "SQ", "UT", "UN"}


def _pad_value(vr: str, value: bytes) -> bytes:
    if len(value) & 1:
        pad = b"\x00" if vr in ("UI", "OB", "OW") else b" "
        return value + pad
    return value


def elem(group: int, element: int, vr: str, value: bytes) -> bytes:
    """Explicit VR LE element."""
    value = _pad_value(vr, value)
    out = struct.pack("<HH", group, element) + vr.encode("ascii")
    if vr in LONG_VRS:
        out += b"\x00\x00" + struct.pack("<I", len(value))
    else:
        out += struct.pack("<H", len(value))
    return out + value


def elem_implicit(group: int, element: int, vr: str, value: bytes) -> bytes:
    """Implicit VR LE element. VR is dropped from the wire; we still take it to
    decide the padding byte (UI/OB/OW use NUL, others use space)."""
    value = _pad_value(vr, value)
    return struct.pack("<HHI", group, element, len(value)) + value


def s(text: str) -> bytes:
    return text.encode("ascii")


def us(n: int) -> bytes:  return struct.pack("<H", n)
def ul(n: int) -> bytes:  return struct.pack("<I", n)


# ---------------------------------------------------------------------------
# DICOM file build
# ---------------------------------------------------------------------------

EXPLICIT_VR_LE = "1.2.840.10008.1.2.1"
IMPLICIT_VR_LE = "1.2.840.10008.1.2"
CT_IMAGE_SOP   = "1.2.840.10008.5.1.4.1.1.2"   # CT Image Storage
IMPL_UID       = "1.2.826.0.1.3680043.9.7236.1"

# Generate unique UIDs by appending a counter to a prefix.
_UID_PREFIX = "1.2.826.0.1.9999.99"
_uid_counter = [1]
def new_uid() -> str:
    u = f"{_UID_PREFIX}.{_uid_counter[0]}"
    _uid_counter[0] += 1
    return u


def build_file_meta(sop_instance_uid: str, transfer_syntax_uid: str) -> bytes:
    # Build the (0002,xxxx) group first WITHOUT the group-length, then prepend it.
    # File meta is ALWAYS Explicit VR LE per the DICOM standard, regardless of
    # the transfer syntax used for the dataset that follows.
    body = b""
    body += elem(0x0002, 0x0001, "OB", b"\x00\x01")
    body += elem(0x0002, 0x0002, "UI", s(CT_IMAGE_SOP))
    body += elem(0x0002, 0x0003, "UI", s(sop_instance_uid))
    body += elem(0x0002, 0x0010, "UI", s(transfer_syntax_uid))
    body += elem(0x0002, 0x0012, "UI", s(IMPL_UID))
    group_len = elem(0x0002, 0x0000, "UL", ul(len(body)))
    return group_len + body


def build_slice(w, h, voxels_le_int16: bytes, *, series_uid: str, study_uid: str,
                instance_number: int, image_pos_z_mm: float,
                pixel_spacing_xy: float, slice_thickness: float,
                transfer_syntax: str = "explicit") -> bytes:
    sop_uid = new_uid()
    out = b"\x00" * 128 + b"DICM"
    ts_uid = EXPLICIT_VR_LE if transfer_syntax == "explicit" else IMPLICIT_VR_LE
    out += build_file_meta(sop_uid, ts_uid)
    # Dataset: choose encoding based on transfer_syntax.
    E = elem if transfer_syntax == "explicit" else elem_implicit
    out += E(0x0008, 0x0016, "UI", s(CT_IMAGE_SOP))
    out += E(0x0008, 0x0018, "UI", s(sop_uid))
    out += E(0x0008, 0x0060, "CS", s("CT"))
    out += E(0x0010, 0x0010, "PN", s("Synthetic^Test"))
    out += E(0x0010, 0x0020, "LO", s("SYN-001"))
    out += E(0x0018, 0x0050, "DS", s(f"{slice_thickness}"))
    out += E(0x0020, 0x000D, "UI", s(study_uid))
    out += E(0x0020, 0x000E, "UI", s(series_uid))
    out += E(0x0020, 0x0011, "IS", s("1"))
    out += E(0x0020, 0x0013, "IS", s(str(instance_number)))
    out += E(0x0020, 0x0032, "DS", s(f"0\\0\\{image_pos_z_mm}"))
    out += E(0x0020, 0x0037, "DS", s("1\\0\\0\\0\\1\\0"))
    out += E(0x0028, 0x0002, "US", us(1))
    out += E(0x0028, 0x0004, "CS", s("MONOCHROME2"))
    out += E(0x0028, 0x0010, "US", us(h))   # Rows
    out += E(0x0028, 0x0011, "US", us(w))   # Columns
    out += E(0x0028, 0x0030, "DS", s(f"{pixel_spacing_xy}\\{pixel_spacing_xy}"))
    out += E(0x0028, 0x0100, "US", us(16))
    out += E(0x0028, 0x0101, "US", us(16))
    out += E(0x0028, 0x0102, "US", us(15))
    out += E(0x0028, 0x0103, "US", us(1))   # 1 = signed
    out += E(0x0028, 0x1052, "DS", s("0"))
    out += E(0x0028, 0x1053, "DS", s("1"))
    # Pixel Data (OW for 16-bit). Length must be even (it is: w*h*2).
    out += E(0x7FE0, 0x0010, "OW", voxels_le_int16)
    return out


# ---------------------------------------------------------------------------
# Synthetic CT pattern (matches make_synthetic_nii.py)
# ---------------------------------------------------------------------------

def main():
    args = list(sys.argv[1:])
    # Pull out --vr {explicit,implicit}.
    transfer_syntax = "explicit"
    if "--vr" in args:
        i = args.index("--vr")
        if i + 1 >= len(args) or args[i + 1] not in ("explicit", "implicit"):
            print("error: --vr requires 'explicit' or 'implicit'")
            sys.exit(2)
        transfer_syntax = args[i + 1]
        del args[i:i + 2]
    if len(args) < 1:
        print("usage: make_synthetic_dicom.py <out_dir> [W H D] [tissue_frac bone_frac] "
              "[--vr explicit|implicit]")
        sys.exit(2)
    out_dir = args[0]
    if len(args) >= 4:
        w, h, d = int(args[1]), int(args[2]), int(args[3])
    else:
        w, h, d = 96, 96, 48
    tissue_frac = float(args[4]) if len(args) >= 5 else 0.85
    bone_frac   = float(args[5]) if len(args) >= 6 else 0.35
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
            transfer_syntax=transfer_syntax,
        )
        path = os.path.join(out_dir, f"slice_{z:04d}.dcm")
        with open(path, "wb") as f:
            f.write(dcm)
    print(f"wrote {d} slice files to {out_dir}/ "
          f"({w}x{h}x{d} int16, spacing ({sx},{sy},{sz})mm, vr={transfer_syntax})")


if __name__ == "__main__":
    main()
