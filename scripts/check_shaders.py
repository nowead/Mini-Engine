"""Validate shader sources without building the engine or opening a window.

- WGSL: parsed and validated with naga (WebGPU shaders are otherwise only
  checked when the browser compiles them at runtime).
- GLSL: compiled with glslc to /dev/null (same stage flags as CMake).

Missing tools are an error, not a skip, so a green run means both were checked.
Install naga with: cargo install naga-cli --locked
(rustc 1.86 needs an older release: cargo install naga-cli --version "~26" --locked)

Run from any working directory: python scripts/check_shaders.py
"""

from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "shaders"
GLSL_STAGES = {".vert.glsl": "vertex", ".frag.glsl": "fragment", ".comp.glsl": "compute"}


def find_tool(name, extra_dirs=()):
    found = shutil.which(name)
    if found:
        return found
    for directory in extra_dirs:
        candidate = Path(directory).expanduser() / name
        if candidate.is_file():
            return str(candidate)
    return None


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    return result.returncode, (result.stdout + result.stderr).strip()


def main():
    naga = find_tool("naga", ["~/.cargo/bin"])
    vulkan_sdk = os.environ.get("VULKAN_SDK")
    glslc = find_tool("glslc", [f"{vulkan_sdk}/bin"] if vulkan_sdk else [])
    missing = [name for name, path in (("naga", naga), ("glslc", glslc)) if not path]
    if missing:
        print(f"Missing tools: {', '.join(missing)} (see header of {Path(__file__).name})",
              file=sys.stderr)
        return 2

    failures = []
    wgsl = sorted(SHADERS.glob("*.wgsl"))
    for source in wgsl:
        code, output = run([naga, str(source)])
        if code != 0:
            failures.append(f"{source.relative_to(ROOT)}:\n{output}")

    glsl = sorted(p for p in SHADERS.glob("*.glsl") if any(p.name.endswith(s) for s in GLSL_STAGES))
    for source in glsl:
        stage = next(v for s, v in GLSL_STAGES.items() if source.name.endswith(s))
        code, output = run([glslc, f"-fshader-stage={stage}", str(source), "-o", os.devnull])
        if code != 0:
            failures.append(f"{source.relative_to(ROOT)}:\n{output}")

    if failures:
        print("\n\n".join(failures), file=sys.stderr)
        print(f"\n{len(failures)} shader(s) failed.", file=sys.stderr)
        return 1
    print(f"Validated {len(wgsl)} WGSL (naga) and {len(glsl)} GLSL (glslc) shaders.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
