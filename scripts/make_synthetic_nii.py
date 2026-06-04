#!/usr/bin/env python3
"""Generate a synthetic NIfTI-1 (.nii) volume for testing the volume pipeline.

Pure stdlib (no numpy). Writes a single-file .nii (magic "n+1") with int16
voxel data, anisotropic voxel spacing (to exercise the physical-aspect AABB),
and scl_slope/scl_inter = 1/0 (data already in the chosen unit).

Two modalities supported:

  --modality ct  (default)
    CT-like Hounsfield Units. Air background (-1000 HU), soft-tissue sphere
    (+40 HU) with an inner bone core (+800 HU). Test the bone-window TF.

  --modality mr
    T1-weighted-style brain signal intensity. Background = 0 (no signal),
    outer shell = 300 (gray-matter-like), inner core = 800 (white-matter-
    like high signal). Test the MR window/level behaviour. The shape stays
    a sphere shell -- MR-B2 (brain phantom WM/GM/CSF shells) is a follow-up.

Content: x is the fastest-varying axis, matching the engine's
(z*H + y)*W + x layout. Each xy slice is a 2D annulus (single z plane =
circle), so we fill each row with at most 3 byte-aligned slice() assignments
(background | outer/inner | background) instead of iterating per voxel.
Per-row inner-radius math is O(1). Net cost is O(H * D) Python ops, which
keeps 1024^3 generation feasible (under a minute) without numpy.

Usage:
    python scripts/make_synthetic_nii.py [out.nii] [W H D] [outer_frac] [inner_frac] [--modality ct|mr]
Defaults: synthetic_ct.nii  96 96 48  0.85  0.35  --modality ct
"""
import struct
import sys


# Per-modality intensity profile. CT values are HU; MR values are T1-weighted
# signal intensity (unitless, scaled to a plausible 0..1000 dynamic range so
# the existing window/level path picks them up the same way as HU).
MODALITY_PROFILES = {
    "ct": {
        "background": -1000,   # air
        "outer":         40,   # soft tissue
        "inner":        800,   # bone core
        "label":        "HU air=-1000 tissue=40 bone=800",
    },
    "mr": {
        "background":     0,   # no signal
        "outer":        300,   # gray-matter-ish T1 mid-signal
        "inner":        800,   # white-matter-ish T1 high-signal
        "label":        "T1 bg=0 outer=300 inner=800",
    },
}


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


def fill_row(row_buf, w, x_outer_lo, x_outer_hi, x_inner_lo, x_inner_hi,
             bg_bytes, outer_bytes, inner_bytes):
    """Fill a single x-row (int16 voxels, 2*w bytes) as
    background | (outer (inner) outer) | background.

    All x_* are inclusive integer voxel indices; -1 means "no such band on this
    row" (radius didn't reach this y). row_buf is a bytearray view of length 2*w.
    bg/outer/inner_bytes are pre-packed 2-byte int16 values (per modality).
    """
    bg_left  = x_outer_lo if x_outer_lo >= 0 else w
    bg_right = (x_outer_hi + 1) if x_outer_hi >= 0 else 0
    # Left background band [0, bg_left)
    if bg_left > 0:
        row_buf[0:2 * bg_left] = bg_bytes * bg_left
    if x_outer_lo < 0:
        return   # row outside the outer radius entirely -> all background, done

    if x_inner_lo < 0:
        # Outer only on this row.
        n = (x_outer_hi - x_outer_lo + 1)
        row_buf[2 * x_outer_lo : 2 * (x_outer_hi + 1)] = outer_bytes * n
    else:
        # Outer (left), inner (middle), outer (right).
        n_left = (x_inner_lo - x_outer_lo)
        if n_left > 0:
            row_buf[2 * x_outer_lo : 2 * x_inner_lo] = outer_bytes * n_left
        n_inner = (x_inner_hi - x_inner_lo + 1)
        row_buf[2 * x_inner_lo : 2 * (x_inner_hi + 1)] = inner_bytes * n_inner
        n_right = (x_outer_hi - x_inner_hi)
        if n_right > 0:
            row_buf[2 * (x_inner_hi + 1) : 2 * (x_outer_hi + 1)] = outer_bytes * n_right

    # Right background band [bg_right, w)
    n_right_bg = w - bg_right
    if n_right_bg > 0:
        row_buf[2 * bg_right : 2 * w] = bg_bytes * n_right_bg


def parse_args(argv):
    """Strip the optional --modality X flag from argv and return (modality, rest).
    Keeps the existing positional [out W H D outer_frac inner_frac] interface
    intact so old callers don't break."""
    modality = "ct"
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] == "--modality" and i + 1 < len(argv):
            modality = argv[i + 1].lower()
            i += 2
        else:
            rest.append(argv[i])
            i += 1
    if modality not in MODALITY_PROFILES:
        raise SystemExit(f"unknown --modality '{modality}'; valid: ct, mr")
    return modality, rest


def main():
    modality, argv = parse_args(sys.argv[1:])
    profile = MODALITY_PROFILES[modality]
    bg_bytes    = struct.pack("<h", profile["background"])
    outer_bytes = struct.pack("<h", profile["outer"])
    inner_bytes = struct.pack("<h", profile["inner"])

    default_name = "synthetic_ct.nii" if modality == "ct" else "synthetic_mr.nii"
    out = argv[0] if len(argv) > 0 else default_name
    if len(argv) >= 4:
        w, h, d = int(argv[1]), int(argv[2]), int(argv[3])
    else:
        w, h, d = 96, 96, 48
    outer_frac = float(argv[4]) if len(argv) >= 5 else 0.85
    inner_frac = float(argv[5]) if len(argv) >= 6 else 0.35
    sx, sy, sz = 1.0, 1.0, 2.5   # anisotropic z to test the aspect-correct AABB

    cx, cy, cz = (w - 1) / 2.0, (h - 1) / 2.0, (d - 1) / 2.0
    half = min(cx, cy, cz)
    r_outer = outer_frac * half
    r_inner = inner_frac * half
    r_outer_sq = r_outer * r_outer
    r_inner_sq = r_inner * r_inner

    row_bytes = 2 * w
    total_bytes = row_bytes * h * d
    voxels = bytearray(total_bytes)

    for z in range(d):
        dz = z - cz
        # For this z slice, the radii in 2D are sqrt(R^2 - dz^2). If dz exceeds
        # R the band doesn't appear in this slice.
        outer_z2 = r_outer_sq - dz * dz
        inner_z2 = r_inner_sq - dz * dz
        rt_z = (outer_z2 ** 0.5) if outer_z2 > 0.0 else -1.0
        rb_z = (inner_z2 ** 0.5) if inner_z2 > 0.0 else -1.0

        for y in range(h):
            dy = y - cy
            outer_y2 = (rt_z * rt_z - dy * dy) if rt_z > 0.0 else -1.0
            inner_y2 = (rb_z * rb_z - dy * dy) if rb_z > 0.0 else -1.0

            # Inclusive x ranges for outer and inner bands. -1 = absent.
            if outer_y2 > 0.0:
                hx = outer_y2 ** 0.5
                xt_lo = max(0,     int(cx - hx + 0.5))
                xt_hi = min(w - 1, int(cx + hx + 0.5) - 1)
                if xt_hi < xt_lo: xt_lo, xt_hi = -1, -1
            else:
                xt_lo = xt_hi = -1

            if inner_y2 > 0.0:
                hx = inner_y2 ** 0.5
                xb_lo = max(0,     int(cx - hx + 0.5))
                xb_hi = min(w - 1, int(cx + hx + 0.5) - 1)
                if xb_hi < xb_lo: xb_lo, xb_hi = -1, -1
            else:
                xb_lo = xb_hi = -1

            row_off = ((z * h) + y) * row_bytes
            # memoryview avoids slice-copy overhead on the big bytearray.
            row_view = memoryview(voxels)[row_off : row_off + row_bytes]
            fill_row(row_view, w, xt_lo, xt_hi, xb_lo, xb_hi,
                     bg_bytes, outer_bytes, inner_bytes)

    hdr = build_header(w, h, d, sx, sy, sz, datatype=4, bitpix=16)  # 4 = int16
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(b"\x00\x00\x00\x00")   # extension flag -> data starts at 352
        f.write(voxels)
    print(f"wrote {out}: {w}x{h}x{d} int16, spacing ({sx},{sy},{sz})mm, "
          f"modality={modality}, {profile['label']}")


if __name__ == "__main__":
    main()
