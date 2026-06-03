#!/usr/bin/env python3
"""Generate a synthetic NIfTI-1 (.nii) volume for testing the volume pipeline.

Pure stdlib (no numpy). Writes a single-file .nii (magic "n+1") with int16 voxel
data carrying CT-like Hounsfield Units, anisotropic voxel spacing (to exercise
the physical-aspect AABB), and scl_slope/scl_inter = 1/0 (data already in HU).

Content: air background (-1000 HU), a soft-tissue sphere (+40 HU) with an inner
bone core (+800 HU). x is the fastest-varying axis, matching the engine's
(z*H + y)*W + x layout.

Each xy slice is a 2D annulus (single z plane = circle), so we fill each row
with at most 3 byte-aligned slice() assignments (air | tissue/bone | air)
instead of iterating per voxel. Per-row inner-radius math is O(1). Net cost
is O(H * D) Python ops, which keeps 1024^3 generation feasible (under a
minute) without numpy.

Usage:
    python scripts/make_synthetic_nii.py [out.nii] [W H D] [tissue_frac] [bone_frac]
Defaults: synthetic_ct.nii  96 96 48  0.85  0.35
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


# Pack the three HU values once -- per-row fill is then byte-block multiplication
# (bytes * N), which the CPython interpreter dispatches to a C memcpy. The
# previous version's per-voxel struct.pack_into dominated wall time for big W.
AIR    = struct.pack("<h", -1000)   # b"\x18\xfc"
TISSUE = struct.pack("<h",    40)   # b"\x28\x00"
BONE   = struct.pack("<h",   800)   # b"\x20\x03"


def fill_row(row_buf, w, x_tissue_lo, x_tissue_hi, x_bone_lo, x_bone_hi):
    """Fill a single x-row (int16 voxels, 2*w bytes) as air | (tissue (bone) tissue) | air.

    All x_* are inclusive integer voxel indices; -1 means "no such band on this
    row" (radius didn't reach this y). row_buf is a bytearray view of length 2*w.
    """
    air_left  = x_tissue_lo if x_tissue_lo >= 0 else w
    air_right = (x_tissue_hi + 1) if x_tissue_hi >= 0 else 0
    # Left air band [0, air_left)
    if air_left > 0:
        row_buf[0:2 * air_left] = AIR * air_left
    if x_tissue_lo < 0:
        return   # row outside the tissue radius entirely -> all air, done

    if x_bone_lo < 0:
        # Tissue only on this row.
        n = (x_tissue_hi - x_tissue_lo + 1)
        row_buf[2 * x_tissue_lo : 2 * (x_tissue_hi + 1)] = TISSUE * n
    else:
        # Tissue (left), bone (middle), tissue (right).
        n_left = (x_bone_lo - x_tissue_lo)
        if n_left > 0:
            row_buf[2 * x_tissue_lo : 2 * x_bone_lo] = TISSUE * n_left
        n_bone = (x_bone_hi - x_bone_lo + 1)
        row_buf[2 * x_bone_lo : 2 * (x_bone_hi + 1)] = BONE * n_bone
        n_right = (x_tissue_hi - x_bone_hi)
        if n_right > 0:
            row_buf[2 * (x_bone_hi + 1) : 2 * (x_tissue_hi + 1)] = TISSUE * n_right

    # Right air band [air_right, w)
    n_right_air = w - air_right
    if n_right_air > 0:
        row_buf[2 * air_right : 2 * w] = AIR * n_right_air


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "synthetic_ct.nii"
    if len(sys.argv) >= 5:
        w, h, d = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    else:
        w, h, d = 96, 96, 48
    tissue_frac = float(sys.argv[5]) if len(sys.argv) >= 6 else 0.85
    bone_frac   = float(sys.argv[6]) if len(sys.argv) >= 7 else 0.35
    sx, sy, sz = 1.0, 1.0, 2.5   # anisotropic z to test the aspect-correct AABB

    cx, cy, cz = (w - 1) / 2.0, (h - 1) / 2.0, (d - 1) / 2.0
    half = min(cx, cy, cz)
    r_tissue = tissue_frac * half
    r_bone   = bone_frac   * half
    r_tissue_sq = r_tissue * r_tissue
    r_bone_sq   = r_bone   * r_bone

    row_bytes = 2 * w
    total_bytes = row_bytes * h * d
    voxels = bytearray(total_bytes)

    for z in range(d):
        dz = z - cz
        # For this z slice, the radii in 2D are sqrt(R^2 - dz^2). If dz exceeds
        # R the band doesn't appear in this slice.
        tissue_z2 = r_tissue_sq - dz * dz
        bone_z2   = r_bone_sq   - dz * dz
        rt_z = (tissue_z2 ** 0.5) if tissue_z2 > 0.0 else -1.0
        rb_z = (bone_z2   ** 0.5) if bone_z2   > 0.0 else -1.0

        for y in range(h):
            dy = y - cy
            tissue_y2 = (rt_z * rt_z - dy * dy) if rt_z > 0.0 else -1.0
            bone_y2   = (rb_z * rb_z - dy * dy) if rb_z > 0.0 else -1.0

            # Inclusive x ranges for tissue and bone bands. -1 = absent.
            if tissue_y2 > 0.0:
                hx = tissue_y2 ** 0.5
                xt_lo = max(0,     int(cx - hx + 0.5))
                xt_hi = min(w - 1, int(cx + hx + 0.5) - 1)
                if xt_hi < xt_lo: xt_lo, xt_hi = -1, -1
            else:
                xt_lo = xt_hi = -1

            if bone_y2 > 0.0:
                hx = bone_y2 ** 0.5
                xb_lo = max(0,     int(cx - hx + 0.5))
                xb_hi = min(w - 1, int(cx + hx + 0.5) - 1)
                if xb_hi < xb_lo: xb_lo, xb_hi = -1, -1
            else:
                xb_lo = xb_hi = -1

            row_off = ((z * h) + y) * row_bytes
            # memoryview avoids slice-copy overhead on the big bytearray.
            row_view = memoryview(voxels)[row_off : row_off + row_bytes]
            fill_row(row_view, w, xt_lo, xt_hi, xb_lo, xb_hi)

    hdr = build_header(w, h, d, sx, sy, sz, datatype=4, bitpix=16)  # 4 = int16
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(b"\x00\x00\x00\x00")   # extension flag -> data starts at 352
        f.write(voxels)
    print(f"wrote {out}: {w}x{h}x{d} int16, spacing ({sx},{sy},{sz})mm, "
          f"HU air=-1000 tissue=40 bone=800")


if __name__ == "__main__":
    main()
