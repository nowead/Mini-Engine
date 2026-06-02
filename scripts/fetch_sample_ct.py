#!/usr/bin/env python3
"""Download a publicly-hosted sample volume for engine testing.

The default points at the NIH NIfTI archive's averaged MR brain template
(small, stable URL, real clinical-grade data even if it is MR rather than
CT). Override with --url if the default rots or you want a different volume.

For `.nii.gz` downloads the script gunzips to a `.nii` so the engine's loader
(which only reads uncompressed NIfTI-1 today) can pick it up directly. For
`.tar.gz` / `.zip` downloads it extracts under the output directory so the
contents can be passed to volume_viewer as a DICOM series directory.

Usage:
    python scripts/fetch_sample_ct.py <output_path>
    python scripts/fetch_sample_ct.py <output_path> --url <URL>

Examples once data is in place:
    .\\build-windows\\volume_viewer.exe sample_clinical/avg152T1_LR_nifti.nii
    .\\build-windows\\volume_viewer.exe sample_clinical/dicom_dir
"""
import argparse
import gzip
import os
import shutil
import sys
import tarfile
import urllib.request
import zipfile


# pydicom's bundled test data: a real DICOM CT slice (39 KB) hosted directly on
# GitHub. This is a single slice -- the loader treats it as a degenerate D=1
# series and the result is a thin slab, but it is real clinical-format data so
# it exercises the parser end-to-end. For a fuller volume you can override --url;
# the engine roadmap lists other candidates.
DEFAULT_URL = "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/CT_small.dcm"


def _download(url: str, dest: str) -> None:
    print(f"[fetch] GET {url}")
    print(f"[fetch] -> {dest}")
    req = urllib.request.Request(url, headers={"User-Agent": "Mini-Engine/1.0"})
    with urllib.request.urlopen(req) as resp:
        total = int(resp.headers.get("Content-Length", 0))
        with open(dest, "wb") as out:
            done = 0
            chunk = 65536
            while True:
                buf = resp.read(chunk)
                if not buf:
                    break
                out.write(buf)
                done += len(buf)
                if total > 0:
                    sys.stdout.write(
                        f"\r[fetch] {done * 100 // total:3d}%  ({done / 1e6:.1f} MB)"
                    )
                    sys.stdout.flush()
        if total > 0:
            sys.stdout.write("\n")
    print("[fetch] download complete")


def _gunzip(src: str, dst: str) -> None:
    print(f"[fetch] gunzip {src} -> {dst}")
    with gzip.open(src, "rb") as fin, open(dst, "wb") as fout:
        shutil.copyfileobj(fin, fout)


def _extract_archive(src: str, out_dir: str) -> None:
    print(f"[fetch] extract {src} -> {out_dir}/")
    os.makedirs(out_dir, exist_ok=True)
    lower = src.lower()
    if lower.endswith((".tar.gz", ".tgz", ".tar")):
        with tarfile.open(src) as tar:
            tar.extractall(out_dir)
    elif lower.endswith(".zip"):
        with zipfile.ZipFile(src) as z:
            z.extractall(out_dir)
    else:
        raise RuntimeError(f"unknown archive type: {src}")


def main() -> int:
    p = argparse.ArgumentParser(description="Download a sample clinical volume.")
    p.add_argument("output", help="Output path. For a single file, the destination file; "
                                  "for an archive, the destination directory.")
    p.add_argument("--url", default=DEFAULT_URL, help="Override download URL.")
    args = p.parse_args()

    url = args.url
    base = os.path.basename(url.split("?")[0])
    lower = base.lower()

    if lower.endswith(".nii.gz"):
        # Single file: gunzip in place to <output>.
        out_path = args.output
        if os.path.isdir(out_path) or out_path.endswith(os.sep):
            os.makedirs(out_path, exist_ok=True)
            out_path = os.path.join(out_path, base[: -len(".gz")])
        else:
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        tmp = out_path + ".gz"
        _download(url, tmp)
        _gunzip(tmp, out_path)
        os.remove(tmp)
        print(f"[fetch] ready: {out_path}")
        print("[fetch] load with: volume_viewer.exe", out_path)
    elif lower.endswith((".nii", ".nrrd", ".raw")):
        # Single uncompressed file.
        out_path = args.output
        if os.path.isdir(out_path) or out_path.endswith(os.sep):
            os.makedirs(out_path, exist_ok=True)
            out_path = os.path.join(out_path, base)
        else:
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        _download(url, out_path)
        print(f"[fetch] ready: {out_path}")
        print("[fetch] load with: volume_viewer.exe", out_path)
    elif lower.endswith(".dcm"):
        # Single DICOM file -- place it inside the output dir so the viewer can
        # treat the directory as a (degenerate) series.
        out_dir = args.output
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, base)
        _download(url, out_path)
        print(f"[fetch] ready: {out_path}")
        print("[fetch] load with: volume_viewer.exe", out_dir)
    elif lower.endswith((".tar.gz", ".tgz", ".tar", ".zip")):
        # Archive (e.g. DICOM series). Download to a temp file then extract under output dir.
        out_dir = args.output
        os.makedirs(out_dir, exist_ok=True)
        tmp = os.path.join(out_dir, base)
        _download(url, tmp)
        _extract_archive(tmp, out_dir)
        os.remove(tmp)
        print(f"[fetch] extracted to: {out_dir}/")
        print("[fetch] load with: volume_viewer.exe", out_dir)
    else:
        print(f"[fetch] unknown extension on {base}; downloading raw to {args.output}")
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        _download(url, args.output)

    return 0


if __name__ == "__main__":
    sys.exit(main())
