#!/usr/bin/env python3
"""Download a small JPEG 2000-encoded DICOM file for the WASM viewer preload.

The WASM build pipeline needs a stable URL so the browser viewer has a real
encapsulated PixelData stream to feed through OpenJPEG. pydicom's bundled
`693_J2KI.dcm` (CT 512x512, JPEG 2000 lossy, ~3.6 KB) is the smallest public
sample that exercises this path end to end.

Usage:
    python scripts/fetch_wasm_dicom_sample.py <output_path>

If the destination already exists the script is a no-op. The build target
calls it as a PRE_LINK step so a fresh checkout fetches once; rebuilds reuse
the cached file.
"""
import os
import sys
import urllib.request

URL = "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/693_J2KI.dcm"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fetch_wasm_dicom_sample.py <output_path>", file=sys.stderr)
        return 2
    dest = sys.argv[1]
    if os.path.isfile(dest) and os.path.getsize(dest) > 1000:
        return 0
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    req = urllib.request.Request(URL, headers={"User-Agent": "Mini-Engine/1.0"})
    with urllib.request.urlopen(req) as resp, open(dest, "wb") as out:
        out.write(resp.read())
    print(f"[fetch] wrote {dest} ({os.path.getsize(dest)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
