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
| P2.3 | Multi-iteration cascade (stride 1/2/4, ping-pong) | ✅ `5f6723a` |
| P3.1 | Accumulation N cap (denoise-coupled, HUD + reset) | ✅ `d50e76f` |
| P3.2 | Adaptive SPP by camera motion | 🟡 **deferred** |
| P3.3 | Temporal reprojection (SVGF-style) | 🟡 **deferred** |
| P4 (optional) | HDR equirect environment map upgrade | 🔲 backlog |

### Track status: paused after P3.1

P3.1 delivered the whole point of the P3 sub-track -- "spatial denoise stays
visible at rest" -- by capping the temporal N when the spatial filter is on.
P3.2 (adaptive SPP by motion) and P3.3 (SVGF temporal reprojection) remain in
the plan but are **explicitly deferred**: they are optimisation + polish on
top of a payoff that is already realised, and the surrounding project has
shifted priority to the real-MRI verification track (see
[REAL_MRI_VERIFICATION_PLAN.md](REAL_MRI_VERIFICATION_PLAN.md)). Resume this
track when either (a) the real-MRI track exposes noise pathologies that
adaptive SPP would fix, or (b) the polish story becomes portfolio-critical.

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

### P2.3 -- Multi-iteration cascade (`5f6723a`)

- Two ping-pong denoise textures + three tiny stride UBOs (16 B each,
  values baked to {1, 2, 4}). Single pipeline drives all three passes
  via three iteration-indexed bind groups.
- Iter 0 has two variants keyed by the accumulation ping-pong; iter 1
  and iter 2 are fixed (denoise[0]->denoise[1], denoise[1]->denoise[0]).
  Final result lands in denoise[0]; the display bind group samples that
  fixed slot.
- Shipped alongside a resize-race bug fix (dangling `m_swapchain` raw
  pointer after `RendererBridge::onResize()`) that was blocking stable
  verification -- see the commit message for the full trace.
- Verified: SPP=1 + continuous camera drag shows an unmistakable
  denoise on/off difference; the P2.2 single-iter stride=4 blur was
  subtle at the same resolution.

### P3.1 -- Accumulation N cap (`d50e76f`)

- `m_maxAccumSamples` default 32; `advanceAccumulationFrame()` clamps
  `m_pathSampleCount` at the cap only when denoise is enabled. Denoise
  off = uncapped (v1 behaviour, temporal has to carry convergence
  alone).
- `setDenoiseEnabled()` now resets accumulation on transition so the
  new cap policy is visible from the very next frame.
- WASM viewer + shell HTML: `resetAccum()`, `accumN()`, `accumCap()`,
  `setAccumCap()` JS bindings; stats panel gains an `Accum: N=... (cap
  32 | uncapped)` line; new "Accum cap" slider + "Reset accumulation"
  button gated on Path-traced mode.
- Verified: denoise on ramps N to 32 then locks; denoise off flips to
  "uncapped" and N resumes unbounded growth. Spatial denoise on/off
  now visibly changes the image even after the camera has been still
  for many seconds -- the whole point of P3.

---

## 3. Remaining (deferred)

### P3.2 -- Adaptive SPP by camera motion (~1 session, deferred)

- Detect camera motion (already tracked for accumulation reset). While
  the camera is moving, force SPP=1 so path-trace stays interactive.
  At rest, ramp SPP up over a few frames so the running mean settles
  fast without spiking frame time on the very first still frame.
- Rationale for deferral: the payoff is performance (higher FPS in
  motion, faster convergence at rest), not visual quality. P3.1 has
  already made the spatial denoiser carry a permanent visual role, so
  adaptive SPP is optimisation on a stable baseline.

### P3.3 -- Temporal reprojection (~2-3 sessions, deferred)

- Reproject the previous frame's denoised output into the current
  camera via view/proj history, blend with the current denoised output
  weighted by depth/normal consistency. SVGF-style temporal filter on
  top of the spatial A-trous.
- Would need a new render target from the path-trace pass carrying
  first-hit position (or depth) so disocclusion detection is possible.
- Rationale for deferral: significant infrastructure cost (new render
  target, reprojection math, disocclusion heuristics) for a payoff
  that is now incremental after P3.1's cap. Revisit if a real-MRI
  workload exposes noise pathologies that spatial-only cannot handle.

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
