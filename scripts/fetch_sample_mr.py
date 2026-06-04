#!/usr/bin/env python3
"""Download a publicly-hosted sample MR volume for engine testing.

Parallel script to fetch_sample_ct.py, differing only in the DEFAULT_URL.
The plumbing (download, gunzip, archive extract) is duplicated rather than
shared via a common module -- the body is small and keeping the two scripts
self-contained avoids cross-script import surface.

For `.nii.gz` downloads the script gunzips to a `.nii` so the engine's loader
(uncompressed NIfTI-1 only today) can pick it up directly. For `.tar.gz` /
`.zip` downloads it extracts under the output directory so the contents can
be passed to volume_viewer as a DICOM series directory.

Usage:
    python scripts/fetch_sample_mr.py <output_path>
    python scripts/fetch_sample_mr.py <output_path> --url <URL>

Examples once data is in place:
    .\\build-windows\\volume_viewer.exe sample_mr/MR_small.dcm
    .\\build-windows\\volume_viewer.exe sample_mr/   (directory -> DICOM series)
"""
import argparse
import gzip
import os
import shutil
import sys
import tarfile
import urllib.request
import zipfile


# pydicom's bundled MR test slice (~40 KB) on GitHub. Single-slice; the engine
# loads it as a degenerate D=1 series. Override with --url for a multi-slice
# series (TCIA registration needed for full clinical brain MR volumes). For
# zero-friction iteration most v1-beta development can use the synthetic
# MR phantom from make_synthetic_nii.py --modality mr.
DEFAULT_URL = "https://raw.githubusercontent.com/pydicom/pydicom/main/src/pydicom/data/test_files/MR_small.dcm"


def _download(url: str, dest: str) -> None:
    print(f"[fetch-mr] GET {url}")
    print(f"[fetch-mr] -> {dest}")
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
                        f"\r[fetch-mr] {done * 100 // total:3d}%  ({done / 1e6:.1f} MB)"
                    )
                    sys.stdout.flush()
        if total > 0:
            sys.stdout.write("\n")
    print("[fetch-mr] download complete")


def _gunzip(src: str, dst: str) -> None:
    print(f"[fetch-mr] gunzip {src} -> {dst}")
    with gzip.open(src, "rb") as fin, open(dst, "wb") as fout:
        shutil.copyfileobj(fin, fout)


def _extract_archive(src: str, out_dir: str) -> None:
    print(f"[fetch-mr] extract {src} -> {out_dir}/")
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
    p = argparse.ArgumentParser(description="Download a sample clinical MR volume.")
    p.add_argument("output", help="Output path. For a single file, the destination file; "
                                  "for an archive, the destination directory.")
    p.add_argument("--url", default=DEFAULT_URL, help="Override download URL.")
    args = p.parse_args()

    url = args.url
    base = os.path.basename(url.split("?")[0])
    lower = base.lower()

    if lower.endswith(".nii.gz"):
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
        print(f"[fetch-mr] ready: {out_path}")
        print("[fetch-mr] load with: volume_viewer.exe", out_path)
    elif lower.endswith((".nii", ".nrrd", ".raw")):
        out_path = args.output
        if os.path.isdir(out_path) or out_path.endswith(os.sep):
            os.makedirs(out_path, exist_ok=True)
            out_path = os.path.join(out_path, base)
        else:
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        _download(url, out_path)
        print(f"[fetch-mr] ready: {out_path}")
        print("[fetch-mr] load with: volume_viewer.exe", out_path)
    elif lower.endswith(".dcm"):
        # Single DICOM file -- place it inside the output dir so the viewer can
        # treat the directory as a (degenerate) series.
        out_dir = args.output
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, base)
        _download(url, out_path)
        print(f"[fetch-mr] ready: {out_path}")
        print("[fetch-mr] load with: volume_viewer.exe", out_dir)
    elif lower.endswith((".tar.gz", ".tgz", ".tar", ".zip")):
        out_dir = args.output
        os.makedirs(out_dir, exist_ok=True)
        tmp = os.path.join(out_dir, base)
        _download(url, tmp)
        _extract_archive(tmp, out_dir)
        os.remove(tmp)
        print(f"[fetch-mr] extracted to: {out_dir}/")
        print("[fetch-mr] load with: volume_viewer.exe", out_dir)
    else:
        print(f"[fetch-mr] unknown extension on {base}; downloading raw to {args.output}")
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        _download(url, args.output)

    return 0


if __name__ == "__main__":
    sys.exit(main())
