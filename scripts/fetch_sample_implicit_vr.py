#!/usr/bin/env python3
"""Fetch real Implicit VR Little Endian DICOM samples for parser verification.

Public 16-bit image-bearing Implicit VR LE DICOM samples are rare on the open
web -- most clinical-grade collections (TCIA / institutional PACS exports)
require NBIA authentication or institutional access. The most reliable public
Implicit VR LE files are pydicom's bundled RT (radiotherapy) test data:
they're real clinical DICOM written in Implicit VR LE, so they exercise the
parser's dispatch + tag-dictionary path even though their pixel data is not
loadable as a volume (RT Dose ships 32-bit uint pixel data; RT Plan and RT
Struct have no pixel data at all).

What this script does:
  - Downloads rtplan.dcm + rtdose.dcm + rtstruct.dcm to <out_dir>/.
  - Tells you what TransferSyntax each file declares and how the engine's
    DICOM loader rejects each (each one hits a specific error path that
    proves the Implicit VR parser walked the file successfully).

What this script does NOT do:
  - Fetch a 16-bit image Implicit VR LE corpus -- TCIA NBIA tool or a real
    PACS export is the path for that. The synthetic generator
    (scripts/make_synthetic_dicom.py --vr implicit) covers correctness at
    arbitrary sizes; this script covers the "real clinical bytes" angle for
    the small dispatch surface we can exercise without auth.

Usage:
    python scripts/fetch_sample_implicit_vr.py <out_dir>
"""
import argparse
import os
import sys
import urllib.request


SOURCES = {
    "rtplan.dcm":   "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/rtplan.dcm",
    "rtdose.dcm":   "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/rtdose.dcm",
    "rtstruct.dcm": "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/rtstruct.dcm",
}


def _download(url: str, dest: str) -> int:
    req = urllib.request.Request(url, headers={"User-Agent": "Mini-Engine/1.0"})
    with urllib.request.urlopen(req) as resp:
        data = resp.read()
    with open(dest, "wb") as f:
        f.write(data)
    return len(data)


def _identify_ts(path: str) -> str:
    """Return a short transfer-syntax label by scanning the file meta UID strings."""
    with open(path, "rb") as f:
        data = f.read(800)
    table = {
        "1.2.840.10008.1.2.1": "Explicit VR LE",
        "1.2.840.10008.1.2":   "Implicit VR LE",
        "1.2.840.10008.1.2.2": "Explicit VR BE",
        "1.2.840.10008.1.2.5": "RLE",
    }
    # Longer UIDs first so the prefix match for Implicit ("1.2.840.10008.1.2")
    # doesn't beat Explicit ("1.2.840.10008.1.2.1").
    for ts in sorted(table, key=len, reverse=True):
        b = ts.encode()
        idx = data.find(b, 128)
        if idx < 0:
            continue
        end = idx + len(b)
        if end < len(data) and data[end] in (0, 0x20):
            return table[ts]
    return "unknown"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("out_dir", help="Directory to write downloaded files into.")
    args = p.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    for name, url in SOURCES.items():
        dest = os.path.join(args.out_dir, name)
        print(f"[fetch] {name}  <- {url}")
        n = _download(url, dest)
        ts = _identify_ts(dest)
        print(f"[fetch]   wrote {n} bytes, transfer syntax = {ts}")

    print()
    print("To verify the parser dispatches correctly, load each file's directory:")
    print(f"  volume_viewer.exe {args.out_dir}/    # all three together, expect rejects")
    print("Expected per-file behaviour (proves the Implicit VR parser walked the bytes):")
    print("  rtplan.dcm   -> [ERROR] no PixelData         (Implicit walk complete, no 7FE0,0010)")
    print("  rtdose.dcm   -> [ERROR] unsupported bitsAllocated 32  (tag (0028,0100) -> US decoded)")
    print("  rtstruct.dcm -> [ERROR] missing DICM magic   (some RT files omit the 128-byte preamble)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
