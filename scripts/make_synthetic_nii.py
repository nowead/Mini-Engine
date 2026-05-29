#!/usr/bin/env python3
"""Generate a synthetic NIfTI-1 (.nii) volume for testing the volume pipeline.

Pure stdlib (no numpy). Writes a single-file .nii (magic "n+1") with int16 voxel
data carrying CT-like Hounsfield Units, anisotropic voxel spacing (to exercise
the physical-aspect AABB), and scl_slope/scl_inter = 1/0 (data already in HU).

Content: air background (-1000 HU), a soft-tissue sphere (+40 HU) with an inner
bone core (+800 HU). x is the fastest-varying axis, matching the engine's
(z*H + y)*W + x layout.

Usage:
    python scripts/make_synthetic_nii.py [out.nii] [W H D]
Defaults: synthetic_ct.nii  96 96 48
"""
import struct
import sys


def build_header(w, h, d, sx, sy, sz, datatype, bitpix):
    hdr = bytearray(348)
    struct.pack_into("<i", hdr, 0, 348)          # sizeof_hdr
    hdr[38] = ord('r')                            # regular = 'r'
    # dim[8]: dim[0]=ndims, dim[1..3]=W,H,D, rest=1
    struct.pack_into("<8h", hdr, 40, 3, w, h, d, 1, 1, 1, 1)
    struct.pack_into("<h", hdr, 70, datatype)     # datatype
    struct.pack_into("<h", hdr, 72, bitpix)       # bitpix
    # pixdim[8]: pixdim[0] unused, pixdim[1..3]=spacing (mm)
    struct.pack_into("<8f", hdr, 76, 1.0, sx, sy, sz, 0.0, 0.0, 0.0, 0.0)
    struct.pack_into("<f", hdr, 108, 352.0)       # vox_offset
    struct.pack_into("<f", hdr, 112, 1.0)         # scl_slope
    struct.pack_into("<f", hdr, 116, 0.0)         # scl_inter
    hdr[123] = 2                                  # xyzt_units: 2 = mm
    hdr[344:348] = b"n+1\x00"                      # magic (single-file NIfTI-1)
    return hdr


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "synthetic_ct.nii"
    if len(sys.argv) >= 5:
        w, h, d = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    else:
        w, h, d = 96, 96, 48
    sx, sy, sz = 1.0, 1.0, 2.5   # anisotropic z to test the aspect-correct AABB

    cx, cy, cz = (w - 1) / 2.0, (h - 1) / 2.0, (d - 1) / 2.0
    # Radii in normalized space (fraction of the smallest half-extent).
    half = min(cx, cy, cz)
    r_tissue = 0.85 * half
    r_bone = 0.35 * half

    voxels = bytearray(w * h * d * 2)
    i = 0
    for z in range(d):
        for y in range(h):
            for x in range(w):
                dx, dy, dz = x - cx, y - cy, z - cz
                dist = (dx * dx + dy * dy + dz * dz) ** 0.5
                if dist <= r_bone:
                    hu = 800       # bone core
                elif dist <= r_tissue:
                    hu = 40        # soft tissue
                else:
                    hu = -1000     # air
                struct.pack_into("<h", voxels, i, hu)
                i += 2

    hdr = build_header(w, h, d, sx, sy, sz, datatype=4, bitpix=16)  # 4 = int16
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(b"\x00\x00\x00\x00")   # extension flag -> data starts at 352
        f.write(voxels)
    print(f"wrote {out}: {w}x{h}x{d} int16, spacing ({sx},{sy},{sz})mm, "
          f"HU air=-1000 tissue=40 bone=800")


if __name__ == "__main__":
    main()
