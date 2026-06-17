#!/usr/bin/env python3
"""Download a small DICOM file for the WASM viewer preload.

The WASM build pipeline needs a stable URL so the browser viewer has a real
encapsulated PixelData stream to feed through the decoder. Current samples:

* pydicom's `693_J2KI.dcm` -- JPEG 2000 lossy, ~3.6 KB. Exercises the
  OpenJPEG path.
* pydicom-data's `JPEG-LL.dcm` -- 16-bit JPEG Lossless P14, ~119 KB.
  Exercises the libjpeg-turbo `jpeg16_read_scanlines` branch and the
  single-frame multi-fragment merge path.

Usage:
    python scripts/fetch_wasm_dicom_sample.py <output_path> [<url>]

If the destination already exists and is larger than 1 KB the script is a
no-op. The build target calls it as a PRE_LINK step so a fresh checkout
fetches once; rebuilds reuse the cached file.
"""
import os
import sys
import urllib.request

DEFAULT_URL = "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/693_J2KI.dcm"


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("usage: fetch_wasm_dicom_sample.py <output_path> [<url>]", file=sys.stderr)
        return 2
    dest = sys.argv[1]
    url = sys.argv[2] if len(sys.argv) == 3 else DEFAULT_URL
    if os.path.isfile(dest) and os.path.getsize(dest) > 1000:
        return 0
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "Mini-Engine/1.0"})
    with urllib.request.urlopen(req) as resp, open(dest, "wb") as out:
        out.write(resp.read())
    print(f"[fetch] wrote {dest} ({os.path.getsize(dest)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
