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
    like high signal). Test the MR window/level behaviour.

Two shapes supported:

  --shape sphere (default)
    Two concentric spheres: outer (soft tissue / mid signal) and inner
    (bone / high signal). Same shape both modalities, intensities differ.

  --shape brain
    Four-shell brain phantom: outermost CSF, GM (gray-matter cortex), WM
    (white-matter core), innermost CSF (ventricle). Intensities pulled
    from per-modality profile (T1 contrast on MR, low-contrast on CT).
    Best paired with --modality mr to see WM/GM/CSF separation.

Content: x is the fastest-varying axis, matching the engine's
(z*H + y)*W + x layout. Each xy slice is a 2D annulus (single z plane =
circle), so we fill each row with at most 3 byte-aligned slice() assignments
(background | outer/inner | background) instead of iterating per voxel.
Per-row inner-radius math is O(1). Net cost is O(H * D) Python ops, which
keeps 1024^3 generation feasible (under a minute) without numpy.

Usage:
    python scripts/make_synthetic_nii.py [out.nii] [W H D] [outer_frac] [inner_frac]
                                          [--modality ct|mr] [--shape sphere|brain]
Defaults: synthetic_ct.nii  96 96 48  0.85  0.35  --modality ct  --shape sphere
"""
import struct
import sys


# Per-modality intensity profile. CT values are HU; MR values are T1-weighted
# signal intensity (unitless, scaled to a plausible 0..1000 dynamic range so
# the existing window/level path picks them up the same way as HU). Brain
# bands (csf/gm/wm) are only consumed by --shape brain; sphere uses outer/
# inner. CT brain values are realistic-but-low-contrast (clinical CT brain
# differentiates WM/GM weakly); MR T1 brain values reflect typical contrast.
MODALITY_PROFILES = {
    "ct": {
        "background": -1000,
        "outer":         40,   # soft tissue
        "inner":        800,   # bone core
        "csf":            0,   # ventricle / extra-axial CSF
        "gm":            40,   # cortical gray matter
        "wm":            30,   # subcortical white matter
        "label":        "HU air=-1000 tissue=40 bone=800 (brain: csf=0 gm=40 wm=30)",
    },
    "mr": {
        "background":     0,
        "outer":        300,
        "inner":        800,
        "csf":          100,   # T1 dark CSF
        "gm":           400,   # T1 mid GM
        "wm":           800,   # T1 bright WM
        "label":        "T1 bg=0 outer=300 inner=800 (brain: csf=100 gm=400 wm=800)",
    },
}


# Per-shape concentric-shell layout: list of (radius_frac_of_half, profile_band_key),
# sorted OUTERMOST first. Outer-first order means inner shells overwrite outer
# ones in their range during fill, naturally producing nested rings.
SHAPE_LAYOUTS = {
    "sphere": None,    # special-cased; uses outer_frac / inner_frac args
    "brain":  [
        (0.75, "csf"),   # outer CSF (extra-axial)
        (0.65, "gm"),    # gray-matter cortex
        (0.50, "wm"),    # white-matter core
        (0.10, "csf"),   # central CSF (ventricle)
    ],
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


def fill_row_shells(row_buf, w, x_ranges, bg_bytes, shell_bytes_list):
    """Fill a single x-row (int16 voxels, 2*w bytes) as nested concentric shells.

    x_ranges  : list of (x_lo, x_hi) inclusive, sorted OUTERMOST first. -1/-1
                means the shell does not reach this row.
    bg_bytes  : 2-byte packed int16 background value.
    shell_bytes_list : 2-byte packed int16 per shell, same order as x_ranges.

    Strategy: init the whole row to background, then walk shells outer-first.
    Each shell's x range OVERWRITES whatever is there, so inner shells
    naturally end up nested inside outer ones. Slightly more write traffic
    than the old 2-band per-segment fill but bytearray slice assignment is
    byte-block memcpy (very fast); for 1024^3 the extra cost is <1s total.
    """
    # No shell touches this row -> just fill background.
    if not any(x_lo >= 0 and x_hi >= x_lo for x_lo, x_hi in x_ranges):
        row_buf[0:2 * w] = bg_bytes * w
        return
    # Init full row to background, then layer shells outer -> inner.
    row_buf[0:2 * w] = bg_bytes * w
    for (x_lo, x_hi), val_bytes in zip(x_ranges, shell_bytes_list):
        if x_lo < 0 or x_hi < x_lo:
            continue
        n = x_hi - x_lo + 1
        row_buf[2 * x_lo : 2 * (x_hi + 1)] = val_bytes * n


def parse_args(argv):
    """Strip optional --modality / --shape flags. Keep the legacy positional
    interface [out W H D outer_frac inner_frac] intact so old callers don't
    break."""
    modality = "ct"
    shape    = "sphere"
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] == "--modality" and i + 1 < len(argv):
            modality = argv[i + 1].lower()
            i += 2
        elif argv[i] == "--shape" and i + 1 < len(argv):
            shape = argv[i + 1].lower()
            i += 2
        else:
            rest.append(argv[i])
            i += 1
    if modality not in MODALITY_PROFILES:
        raise SystemExit(f"unknown --modality '{modality}'; valid: ct, mr")
    if shape not in SHAPE_LAYOUTS:
        raise SystemExit(f"unknown --shape '{shape}'; valid: sphere, brain")
    return modality, shape, rest


def resolve_shells(shape, profile, outer_frac, inner_frac):
    """Return list of (radius_frac_of_half, intensity_int) outermost first."""
    if shape == "sphere":
        return [(outer_frac, profile["outer"]),
                (inner_frac, profile["inner"])]
    # brain (or any future shape) pulls per-band intensity from profile.
    layout = SHAPE_LAYOUTS[shape]
    return [(frac, profile[band]) for frac, band in layout]


def main():
    modality, shape, argv = parse_args(sys.argv[1:])
    profile  = MODALITY_PROFILES[modality]
    bg_bytes = struct.pack("<h", profile["background"])

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

    shells_def = resolve_shells(shape, profile, outer_frac, inner_frac)
    shell_radii    = [frac * half for frac, _ in shells_def]
    shell_radii_sq = [r * r for r in shell_radii]
    shell_bytes_list = [struct.pack("<h", v) for _, v in shells_def]

    row_bytes = 2 * w
    total_bytes = row_bytes * h * d
    voxels = bytearray(total_bytes)

    for z in range(d):
        dz = z - cz
        # Projected radius per shell at this z slice (sqrt(R^2 - dz^2)).
        shell_r_z = [
            (rsq - dz * dz) ** 0.5 if rsq > dz * dz else -1.0
            for rsq in shell_radii_sq
        ]

        for y in range(h):
            dy = y - cy
            # Inclusive x range per shell at this y. -1/-1 = absent on this row.
            x_ranges = []
            for r_z in shell_r_z:
                if r_z > 0.0:
                    y2 = r_z * r_z - dy * dy
                    if y2 > 0.0:
                        hx = y2 ** 0.5
                        xlo = max(0,     int(cx - hx + 0.5))
                        xhi = min(w - 1, int(cx + hx + 0.5) - 1)
                        x_ranges.append((xlo, xhi) if xhi >= xlo else (-1, -1))
                        continue
                x_ranges.append((-1, -1))

            row_off = ((z * h) + y) * row_bytes
            row_view = memoryview(voxels)[row_off : row_off + row_bytes]
            fill_row_shells(row_view, w, x_ranges, bg_bytes, shell_bytes_list)

    hdr = build_header(w, h, d, sx, sy, sz, datatype=4, bitpix=16)  # 4 = int16
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(b"\x00\x00\x00\x00")   # extension flag -> data starts at 352
        f.write(voxels)
    print(f"wrote {out}: {w}x{h}x{d} int16, spacing ({sx},{sy},{sz})mm, "
          f"modality={modality}, shape={shape}, {profile['label']}")


if __name__ == "__main__":
    main()
