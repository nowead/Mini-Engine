# M4 v2 -- Path-Trace Polish Plan

**Date**: 2026-06-17
**Goal**: Lift the path-traced volumetric renderer (M4 v0/v1) from a
proof of concept to a usable cinematic preview. Three sub-tracks:
environment lighting (P1), spatial denoiser (P2), adaptive sampling +
temporal reprojection (P3).

**Roadmap alignment**: Builds on M4 v1 (progressive accumulation). v2
is the "polish before WASM denoiser becomes a portfolio talking point"
arc. All sub-steps are dual-backend (WGSL + GLSL) so the WASM and
native viewers stay in lockstep with the existing Vulkan/Emscripten
build matrix.

---

## 1. Sub-Track Map

| Sub-step | Title | Status |
| --- | --- | --- |
| P1 | Environment lighting (miss-ray IBL) | ✅ `521a3f8` |
| P2.1 | Denoise pass plumbing (pass-through identity) | ✅ `c34d85c` |
| P2.2 | Single-iteration A-trous (color guide, stride=4) | ✅ `74b2473` |
| P2.3 | Multi-iteration cascade (stride 1/2/4, ping-pong) | 🔲 next |
| P3 | Adaptive SPP + temporal reprojection | 🔲 |
| P4 (optional) | HDR equirect environment map upgrade | 🔲 backlog |

---

## 2. Delivered

### P1 -- Environment lighting (`521a3f8`)

- New UBO slots `envTop` (rgb + intensity) and `envBot` (rgb + enable
  flag). Layout vec4-aligned so the append is binary-safe.
- `sampleEnvironment(dir)` -- top-bottom gradient sky, gated by the
  enable flag so default zero-init UBOs preserve the v0/v1 black
  background.
- `tracePath()` calls `sampleEnvironment(rd)` at both miss branches:
  primary-miss (background) and post-bounce escape (IBL contribution
  for multi-scatter throughput).
- Symmetric WGSL and GLSL implementations; setter + JS binding +
  default-on in both viewers.

### P2.1 -- Denoise pass plumbing (`c34d85c`)

- New fragment pipeline between path-trace and display. Shader body is
  a pass-through identity copy so the new pass is exercised every PT
  frame without changing the image.
- `m_denoiseTexture` + `m_denoiseView` (single RGBA16Float; multi-
  iteration ping-pong lands in P2.3).
- `m_pathDenoiseBindGroups[2]` -- one per accum ping-pong slot.
- `m_pathDenoiseDisplayBindGroup` -- reuses the existing display
  layout; `getDisplayBindGroup()` routes to it when denoise is on.
- Caller flow in both viewers: optional denoise pass between path-
  trace and display when enabled.

### P2.2 -- Single-iteration A-trous (`74b2473`)

- 5x5 cross-bilateral kernel: binomial (1,4,6,4,1) outer product
  weights with a per-tap color-similarity factor
  `exp(-|c_n - c_c|^2 / sigmaC^2)` (sigmaC=0.35). Stride=4 covers a
  ~32x32 px window with 25 taps -- gentle on purpose so it
  complements the temporal accumulation instead of fighting it.
- WASM JS binding `setDenoise(bool)` + an "A-trous denoise" checkbox
  in `volume_viewer_shell.html`, disabled outside Path-traced mode.

---

## 3. Remaining

### P2.3 -- Multi-iteration cascade (~2-3 h)

- Add a second denoise texture so the pass can ping-pong: iter 0
  writes denoise[0], iter 1 reads denoise[0] + writes denoise[1],
  iter 2 reads denoise[1] + writes denoise[0]. Display reads whichever
  slot the last iteration wrote to.
- Stride source: small dedicated uniform (16 B vec4 with the int
  packed in) -- avoids touching VolumeUBO. Three bind groups per
  iteration carry input + uniform.
- Cascade strides 1 / 2 / 4 -- the standard A-trous schedule. Coarse
  iterations clean the low-frequency noise, fine iterations clean
  high-frequency grain.
- Verification: toggle denoise during camera motion at SPP=1 ->
  visible noise vs. smooth comparison.

### P3 -- Adaptive SPP + temporal reprojection (~3-4 h)

- Adaptive SPP: detect camera motion (already detected for
  accumulation reset). During motion run SPP=1 + denoiser; at rest
  ramp SPP up over a few frames so the renderer prioritises
  responsiveness in motion and converges quickly at rest.
- Temporal reprojection: reuse the previous frame's denoised output
  with camera-history reprojection. SVGF-style temporal filtering on
  top of the spatial A-trous.
- Cap accumulation N so the spatial pass always carries some weight
  (the v1 progressive accumulation otherwise drives noise to zero on
  its own and hides the denoiser's contribution).
- Verification: noise reduction curve vs. frame count -- measure how
  many frames to reach a target PSNR with adaptive SPP + denoise on
  vs. plain SPP-N accumulation.

### P4 -- HDR equirect environment map (backlog)

- Replace the procedural top-bottom gradient with a sampled HDR
  equirect. The engine already has IBL infrastructure in
  `src/rendering/IBLManager.cpp` (Vulkan-only) -- this would bring it
  to WASM too. Out of scope for the current polish track but the
  payoff (real studio-light cinematic look) is high.

---

## 4. Files Touched

| Step | File |
| --- | --- |
| P1 | `src/rendering/VolumeRenderer.{hpp,cpp}`, `shaders/volume_pathtrace.{wgsl,frag.glsl}`, both viewers |
| P2.1 | `shaders/volume_pathtrace_denoise.{wgsl,frag.glsl}` (new), `src/rendering/VolumeRenderer.{hpp,cpp}`, `CMakeLists.txt`, both viewers |
| P2.2 | `shaders/volume_pathtrace_denoise.{wgsl,frag.glsl}`, `tests/volume_viewer_wasm.cpp`, `tests/volume_viewer_shell.html` |
| P2.3 | `src/rendering/VolumeRenderer.{hpp,cpp}` (+1 texture, +stride uniform), shaders (stride uniform), both viewers (3 denoise passes per frame) |
| P3 | `src/rendering/VolumeRenderer.{hpp,cpp}` (adaptive SPP state, history texture for temporal reprojection), shaders, both viewers |

---

## 5. Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| WASM ASYNCIFY interaction with new compute passes (P2 + P3) | All P2 / P3 work stays on fragment shaders. Compute would require a separate ASYNCIFY suspension audit. |
| Denoiser is invisible in steady state because progressive accumulation already converges | P3 caps accumulation N, restoring the denoiser's visual role. Until then "verified by motion at SPP=1" is the honest claim. |
| Multi-iteration cost on a 1700+ wide canvas | Each pass is ~25 taps, ~6M pixels, ~150M reads/frame at 60 Hz. WebGPU on modern GPUs handles this without a sweat; if it bottlenecks on weaker hardware, drop one iteration. |
| Camera-history reprojection edge cases (disocclusion, fast pans) | Fall back to spatial-only output for pixels where temporal reuse fails (history weight = 0). |

---

## 6. Next Entry Point

Step P2.3 -- ping-pong denoise textures + stride uniform + 3 iterations
at strides 1/2/4 cascading from the same accumulation input.
