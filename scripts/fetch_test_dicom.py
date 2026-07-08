#!/usr/bin/env python3
"""Fetch a small collection of public DICOM samples into test_data/dicom/.

Used by the R3 runtime-upload flow to give the developer a set of series to
drag into the browser's file picker. All samples are public and small enough
to run through the WASM memfs write path without stress.

Usage:
    python scripts/fetch_test_dicom.py

Idempotent -- an existing file larger than 1 KB is left alone. Delete the
whole test_data/ directory to force a fresh fetch.

Each series lives in its own subdirectory so the runtime upload can be tested
via the "Pick DICOM folder" button. Individual files can also be selected via
"Pick DICOM files".
"""
import os
import sys
import urllib.request

# One entry per series. Each series is a list of (filename, url) tuples that
# land in test_data/dicom/<series_name>/.
SAMPLES = {
    # Multi-frame fMRI (10 frames, 64x64). Same file the viewer preloads by
    # default, kept here so the runtime upload path can be exercised against a
    # known-good series.
    "mr_emri_small": [
        ("emri_small.dcm",
         "https://github.com/pydicom/pydicom-data/raw/master/data_store/data/emri_small.dcm"),
    ],
    # Real Siemens MR single slice with overlays. ~511 KB.
    "mr_siemens_slice": [
        ("MR-SIEMENS-DICOM-WithOverlays.dcm",
         "https://github.com/pydicom/pydicom-data/raw/master/data_store/data/MR-SIEMENS-DICOM-WithOverlays.dcm"),
    ],
    # JPEG 2000 lossy CT slice (pydicom's classic sample, ~3.6 KB).
    "ct_jpeg2000_small": [
        ("693_J2KI.dcm",
         "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/693_J2KI.dcm"),
    ],
    # 16-bit JPEG Lossless P14 mammography, ~119 KB. Same file the viewer
    # preloads as a secondary fallback; useful for A/B against the MR series.
    "mammo_jpeg_lossless": [
        ("JPEG-LL.dcm",
         "https://github.com/pydicom/pydicom-data/raw/master/data_store/data/JPEG-LL.dcm"),
    ],
}


def fetch(url: str, dest: str) -> None:
    if os.path.isfile(dest) and os.path.getsize(dest) > 1000:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "Mini-Engine/1.0"})
    with urllib.request.urlopen(req) as resp, open(dest, "wb") as out:
        out.write(resp.read())
    print(f"[fetch] {os.path.relpath(dest)} ({os.path.getsize(dest)} bytes)")


def main() -> int:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "test_data", "dicom"))
    print(f"[fetch] target directory: {root}")
    for series_name, files in SAMPLES.items():
        for fname, url in files:
            dest = os.path.join(root, series_name, fname)
            try:
                fetch(url, dest)
            except Exception as e:
                print(f"[skip] {series_name}/{fname}: {e}", file=sys.stderr)
    print("[fetch] done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
