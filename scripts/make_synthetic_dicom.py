#!/usr/bin/env python3
"""Generate a synthetic DICOM series for testing the DICOM volume loader.

pydicom + numpy backed (v2, 2026-07-14). The previous stdlib-only path
was fine for the 96^3 default but too slow (~67 s) for the 512x512x256
stress-test volumes used to exercise the R3 upload + brick pack + WebGPU
path in the medical-volume Y3 measurements. pydicom gives a robust
DICOM Part 10 writer; numpy generates the pixel grid in one vectorised
pass (~2-3 s for 512^3).

The file format is fully DICOM Part 10 compliant Explicit VR Little Endian
CT Image Storage (or MR Image Storage with --modality mr), so any
conformant viewer (OsiriX, 3D Slicer, RadiAnt, ...) opens it. Only the
pixel content is synthetic (nested spheres emulating air / soft tissue /
bone / cavity in Hounsfield Units); NOT suitable for evaluating clinical
image quality -- use real TCIA data for that.

Same synthetic CT shape family as scripts/make_synthetic_nii.py (air
-1000, soft tissue +40, bone +800) so the two loaders can be
cross-checked.

Usage (backward-compat with pre-pydicom CLI):
    python scripts/make_synthetic_dicom.py <out_dir> [W H D]
                                           [tissue_frac bone_frac]
                                           [--vr explicit|implicit]
                                           [--modality ct|mr]
Defaults: out_dir required, 96 96 48, tissue 0.85, bone 0.35,
          vr explicit, modality ct.

Examples:
    # Small default (fast, for compatibility with existing tests).
    python scripts/make_synthetic_dicom.py synth_ct_default

    # Large stress-test (~128 MB, ~10 s on a modern desktop).
    python scripts/make_synthetic_dicom.py synth_ct_large 512 512 256

    # Implicit VR LE (clinical PACS default transfer syntax).
    python scripts/make_synthetic_dicom.py synth_ct_implicit --vr implicit
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np
from pydicom.dataset import Dataset, FileDataset, FileMetaDataset
from pydicom.uid import (
    CTImageStorage,
    MRImageStorage,
    ExplicitVRLittleEndian,
    ImplicitVRLittleEndian,
    generate_uid,
)


def make_slice_pixels(w: int, h: int, d: int, z: int,
                      tissue_frac: float, bone_frac: float,
                      modality: str) -> np.ndarray:
    """One slice's int16 pixel array. Nested-sphere phantom matching
    make_synthetic_nii.py: outer 'tissue' sphere, inner 'bone' sphere,
    background air. For CT the HU values are (air -1000, tissue 40,
    bone 800); for MR the range is shifted into T1-like values."""
    cx, cy, cz = (w - 1) / 2.0, (h - 1) / 2.0, (d - 1) / 2.0
    half = min(cx, cy, cz)
    r_tissue = tissue_frac * half
    r_bone   = bone_frac   * half

    # 2D distance grid for this z-slice, computed with numpy broadcasting.
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dz = z - cz
    dist2 = (xx - cx) ** 2 + (yy - cy) ** 2 + dz * dz

    if modality.upper() == "MR":
        # T1-like: air 0, tissue 800, bone 300. Positive-only range.
        air, tissue, bone = 0, 800, 300
    else:
        air, tissue, bone = -1000, 40, 800

    img = np.full((h, w), air, dtype=np.int16)
    img[dist2 <= r_tissue ** 2] = tissue
    img[dist2 <= r_bone   ** 2] = bone
    return img


def make_slice_dataset(*, z: int, w: int, h: int, d: int,
                       spacing_xy: float, spacing_z: float,
                       modality: str, transfer_syntax: str,
                       study_uid: str, series_uid: str,
                       frame_of_reference_uid: str,
                       pixels: np.ndarray) -> FileDataset:
    modality_code = "MR" if modality.upper() == "MR" else "CT"
    sop_class_uid = MRImageStorage if modality_code == "MR" else CTImageStorage
    ts_uid = ImplicitVRLittleEndian if transfer_syntax == "implicit" else ExplicitVRLittleEndian
    sop_instance_uid = generate_uid()

    file_meta = FileMetaDataset()
    file_meta.MediaStorageSOPClassUID = sop_class_uid
    file_meta.MediaStorageSOPInstanceUID = sop_instance_uid
    file_meta.TransferSyntaxUID = ts_uid
    file_meta.ImplementationClassUID = generate_uid()
    file_meta.ImplementationVersionName = "MiniEngineSynth"

    ds = FileDataset(
        filename_or_obj=None,
        dataset={},
        file_meta=file_meta,
        preamble=b"\x00" * 128,
    )

    # Identification (fake but structurally valid).
    ds.PatientName = "Synthetic^Test"
    ds.PatientID = "SYN-001"
    ds.PatientBirthDate = ""
    ds.PatientSex = "O"
    ds.StudyDate = "20260714"
    ds.StudyTime = "000000"
    ds.AccessionNumber = ""
    ds.ReferringPhysicianName = ""
    ds.Manufacturer = "Mini-Engine"
    ds.StudyInstanceUID = study_uid
    ds.SeriesInstanceUID = series_uid
    ds.SOPClassUID = sop_class_uid
    ds.SOPInstanceUID = sop_instance_uid
    ds.FrameOfReferenceUID = frame_of_reference_uid
    ds.StudyID = "1"
    ds.SeriesNumber = 1
    ds.InstanceNumber = z + 1
    ds.Modality = modality_code
    ds.SeriesDescription = f"Synthetic {modality_code} phantom {w}x{h}x{d}"
    ds.PatientPosition = "HFS"

    # Geometry.
    ds.Rows = h
    ds.Columns = w
    ds.PixelSpacing = [float(spacing_xy), float(spacing_xy)]
    ds.SliceThickness = float(spacing_z)
    ds.SpacingBetweenSlices = float(spacing_z)
    ds.ImageOrientationPatient = [1, 0, 0, 0, 1, 0]
    ds.ImagePositionPatient = [0.0, 0.0, float(z) * float(spacing_z)]
    ds.SliceLocation = float(z) * float(spacing_z)

    # Pixel data.
    ds.SamplesPerPixel = 1
    ds.PhotometricInterpretation = "MONOCHROME2"
    ds.NumberOfFrames = 1
    ds.BitsAllocated = 16
    ds.BitsStored = 16
    ds.HighBit = 15
    ds.PixelRepresentation = 1   # 1 = signed
    ds.RescaleIntercept = "0"
    ds.RescaleSlope = "1"
    if modality_code == "CT":
        ds.RescaleType = "HU"
        ds.WindowCenter = "40"
        ds.WindowWidth = "400"
    ds.PixelData = pixels.tobytes()

    ds.is_little_endian = True
    ds.is_implicit_VR = (transfer_syntax == "implicit")
    return ds


def _parse_backcompat(argv: list[str]) -> argparse.Namespace:
    # Backward-compat positional CLI:
    #   out_dir [W H D] [tissue_frac bone_frac] [--vr ...] [--modality ...]
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--vr", choices=["explicit", "implicit"], default="explicit")
    ap.add_argument("--modality", choices=["ct", "mr", "CT", "MR"], default="ct")
    ap.add_argument("-h", "--help", action="store_true")
    ns, rest = ap.parse_known_args(argv)
    if ns.help or not rest:
        print(__doc__)
        sys.exit(0 if ns.help else 2)
    ns.out_dir = rest[0]
    ns.dim = (96, 96, 48)
    ns.tissue_frac = 0.85
    ns.bone_frac = 0.35
    if len(rest) >= 4:
        try:
            ns.dim = (int(rest[1]), int(rest[2]), int(rest[3]))
        except ValueError:
            print(f"error: dims must be integers, got {rest[1:4]}")
            sys.exit(2)
    if len(rest) >= 6:
        try:
            ns.tissue_frac = float(rest[4])
            ns.bone_frac = float(rest[5])
        except ValueError:
            print(f"error: fractions must be floats, got {rest[4:6]}")
            sys.exit(2)
    return ns


def main() -> int:
    ns = _parse_backcompat(sys.argv[1:])
    w, h, d = ns.dim
    if min(w, h, d) < 2:
        print("error: each dimension must be >= 2")
        return 2
    sx = 1.0
    sz = 2.5

    os.makedirs(ns.out_dir, exist_ok=True)
    study_uid = generate_uid()
    series_uid = generate_uid()
    for_uid = generate_uid()

    approx_bytes = (w * h * 2 + 4096) * d
    print(
        f"Generating {w}x{h}x{d} {ns.modality.upper()} series (vr={ns.vr}, "
        f"~{approx_bytes / (1024 * 1024):.1f} MB across {d} files) to "
        f"{ns.out_dir}/ ..."
    )

    pad = max(4, len(str(d)))
    step = max(1, d // 20)
    for z in range(d):
        pixels = make_slice_pixels(w, h, d, z,
                                   ns.tissue_frac, ns.bone_frac, ns.modality)
        ds = make_slice_dataset(
            z=z, w=w, h=h, d=d,
            spacing_xy=sx, spacing_z=sz,
            modality=ns.modality, transfer_syntax=ns.vr,
            study_uid=study_uid, series_uid=series_uid,
            frame_of_reference_uid=for_uid,
            pixels=pixels,
        )
        fname = os.path.join(ns.out_dir, f"slice_{z:0{pad}d}.dcm")
        ds.save_as(fname, write_like_original=False)
        if (z + 1) % step == 0 or z + 1 == d:
            print(f"  {z + 1}/{d} slices written", flush=True)

    # Sanity roundtrip: reopen first slice + confirm key fields survived.
    try:
        import pydicom
        chk = pydicom.dcmread(os.path.join(ns.out_dir, f"slice_{0:0{pad}d}.dcm"))
        assert chk.Rows == h and chk.Columns == w
        assert chk.Modality == ns.modality.upper()
        assert chk.SeriesInstanceUID == series_uid
        print(
            f"OK  first-slice roundtrip: {chk.Rows}x{chk.Columns} "
            f"{chk.Modality}, transfer syntax {chk.file_meta.TransferSyntaxUID.name}"
        )
    except Exception as e:
        print(f"WARN  post-write sanity check failed: {e}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
