# Volume Rendering Primer — A Tour of Mini-Engine's Medical Volume Track

Audience: someone new to volume rendering, or someone who wants to read
Mini-Engine's medical-volume code without having to reverse-engineer the
pipeline from scratch.

Each section explains a **concept** and then points to the **Mini-Engine
implementation**. Reading with the source file open in a side window is the
fastest way through.

---

## 0. The Big Picture

3D medical data (CT / MRI) is usually a **voxel grid** — a W×H×D array of
intensity values laid out in memory. To put it on screen, there are two
broad families:

- **Surface (iso-surface) rendering**: extract a mesh from the voxel data
  (e.g. marching cubes) and rasterise it with a regular 3D pipeline. Fast,
  but internal structure is lost.
- **Direct Volume Rendering (DVR)**: map voxel values to colour and opacity
  through a transfer function, then accumulate along camera rays. This is
  what the track covers. It is also the clinical standard — the same data
  shows bone, soft tissue, or lung simply by switching the window/level.

Mini-Engine implements DVR with optional volumetric shadows, plus a
path-traced cinematic mode (M4), on two backends (Vulkan + WebGPU).

---

## 1. From Voxels to Pixels: Ray Marching

### 1.1 Build the camera ray

- Screen pixel → NDC → world-space ray (camera position + direction).
- Intersect the ray with the volume AABB to get `(tNear, tFar)`.

### 1.2 Sample along the ray

Step through `[tNear, tFar]` and read voxels with trilinear interpolation.
At each sample:

1. `density = sampleVolume(uvw)` — voxel intensity at the sample point.
2. `(color, alpha) = transferFunction(density)` — map intensity to colour.
3. **Front-to-back compositing**: add the alpha-weighted contribution to
   the accumulated colour.
4. **Early ray termination**: once the accumulated alpha is close to 1.0
   no further sample can be seen, so the loop exits.

### 1.3 Beer-Lambert (extinction)

Each step crosses a medium of thickness `dt`. With absorption coefficient
`σt = density * extinction`:

- Step transmittance: `T_step = exp(-σt * dt)`.
- Accumulated alpha: `α += (1 - T_step) * (1 - α_prev)`.
- Accumulated colour: `C += (1 - T_step) * color * (1 - α_prev)`.

Using `exp()` keeps the integration stable when a single step absorbs a lot.

**In Mini-Engine:**

- Shaders: `shaders/volume_march.{frag.glsl, wgsl}` (one algorithm, two
  languages).
- `intersectAABB(ro, rd, bmin, bmax)` handles the ray/box test.
- The main loop steps with `params.stepSize`, applies the `tf.x` density
  threshold, and looks up colour through the LUT.
- The `extinction` UBO field scales σt.

---

## 2. Transfer Function — Mapping Voxels to Colour

Raw HU (Hounsfield Unit) values are meaningless on their own. A **transfer
function (TF)** defines `(density → colour + opacity)`. The clinical standard
is a two-stage mapping.

### 2.1 Window/level (linear remap)

- Two parameters: `windowCenter (C)`, `windowWidth (W)`.
- `n = clamp((HU - (C - W/2)) / W, 0, 1)` — map `[C-W/2, C+W/2]` to `[0, 1]`.
- The same data lights up bone (C=300, W=1500), lung (C=-600, W=1500),
  soft tissue (C=40, W=400), or brain (C=40, W=80) simply by switching the
  preset.

### 2.2 LUT (256×1 RGBA table)

Look up `n` in a 256-element LUT to get colour and alpha. Different presets
ship different LUTs (bone goes through a white ramp, soft tissue a warm
beige, lung a dim blue) and the visual classification falls out.

**In Mini-Engine:**

- UBO `window.xy = (center, width)` (`VolumeRenderer.cpp`).
- `volume_march` applies the windowing then samples `tfLUT`.
- Presets live in `VolumeRenderer::TFPreset` (CT-Bone / CT-Lung /
  Soft-Tissue / Brain / MR-T1 / MR-T2 / Custom).

---

## 3. Gradient Shading — Restoring Depth Cues

Pure absorption gives X-ray-like output with no surface cue. Using the
density **gradient** as a normal lets us add Lambert / Blinn-Phong shading.

### 3.1 Density gradient as surface normal

Central differences per voxel:

```text
gx = (sample(x+e) - sample(x-e)) / (2e)
gy = ...
gz = ...
gradient = (gx, gy, gz)
```

A large `|gradient|` means an edge, small means homogenous interior.
Normalise it to get a surface normal usable in `n·L`.

### 3.2 Volumetric soft shadow

At every camera sample, shoot a **secondary ray** toward the light and
accumulate transmittance:

- The shadow ray steps with the same sigma_t.
- The resulting `T_light` modulates the camera sample's contribution.

The shadow loop is expensive, so it uses a coarser step + early-out.

**In Mini-Engine:**

- The gradient helper in `volume_march` uses `shade.z` as the epsilon.
- UBO `light.xyz` (direction) and `shade.xyzw` (ambient, diffuse, gradEps).
- Shadow knobs live in `shadow.xyzw` (enable, stepSize, maxSteps, strength).

---

## 4. Empty-Space Skipping — Don't Walk Through Air

Medical data is mostly air. If you can skip whole voxel runs you save a lot.

### 4.1 Occupancy grid

Tile the volume into macrocells (e.g. 8³) and precompute each cell's
`(minDensity, maxDensity)`. If the cell's range falls completely outside the
active window/threshold, you can jump over it without changing the result.

### 4.2 Build on the GPU

A 256³ scan on the CPU is too slow, so an `occupancy.comp` compute shader
fills the grid in one dispatch sized `(volW/8, volH/8, volD/8)`.

### 4.3 Skip in the march loop

At each sample the march computes the macrocell index and, if the cell is
inactive for the current window, jumps to the cell boundary instead of
stepping voxel by voxel.

**In Mini-Engine:**

- Compute: `shaders/volume_occupancy.comp.{glsl, wgsl}`.
- March branch: `volume_march`'s `occ.w` flag + the `occCells` storage
  buffer.
- Build entry point: `VolumeRenderer::buildOccupancy()` in
  `VolumeRenderer.cpp`.

---

## 5. Brick Atlas + Page Table — Getting Big Volumes onto the GPU

A 1024³ R16Float dataset is 2.1 GB. A single dense 3D texture is wasteful
(huge fraction is air) and runs into VRAM limits. The **brick atlas** is
the workaround.

### 5.1 Bricks

Split the volume into 64³ voxel chunks (bricks) with a 1-voxel halo per
side for filtering. The on-GPU footprint is therefore 66³ per brick.

### 5.2 Atlas + page table

- **Atlas**: a small 3D texture that holds only the non-empty bricks,
  packed slot-by-slot.
- **Page table**: a uint32 storage buffer mapping virtual brick index ->
  atlas slot. Empty bricks store a sentinel (`0xFFFFFFFF`) so the shader
  can short-circuit to zero with one storage read.

### 5.3 What the shader sees

```text
uvw (normalised) -> virtual brick index -> page table -> slot index
slot index -> atlas-local coords -> textureSampleLevel
```

`sampleVolume(uvw)` hides the whole indirection.

**In Mini-Engine:**

- Storage layer: `src/rendering/BrickedVolume.{hpp, cpp}` (atlas + page
  table allocation and lifetime).
- Shader helper: `sampleVolume()` in `shaders/volume_march.{frag.glsl, wgsl}`.
- Empty-brick scan: `isInteriorEmpty()` — a brick whose entire 64³ interior
  equals the empty value stays out of the atlas.

---

## 6. LOD — Multi-Resolution Bricks

When you zoom out to see the whole volume, every brick is visible at once
and the atlas can't hold them all at full resolution. The fix is to keep
distant bricks at a lower resolution.

### 6.1 Four LOD levels

- L0 = original (64³ interior, 66³ stored).
- L1 = 2× downsample (32³ interior, 34³ stored, 1/8 the voxels).
- L2 = 4× downsample (16³, 18³, 1/64).
- L3 = 8× downsample (8³, 10³, 1/512).

Total memory ≈ L0 × (1 + 1/8 + 1/64 + 1/512) ≈ 1.16 × L0.

### 6.2 Distance-based LOD selection

For each brick, take `ratio = (camera distance / brick world extent)` and
pick a level by threshold (e.g. ratio < 10 → L0, < 30 → L1, < 70 → L2,
else L3).

### 6.3 Page-table encoding

The page table packs `(lod << 30) | slot` in one uint32. The shader
decodes the LOD bits and samples the matching atlas.

### 6.4 On-the-fly box filter (Disk-paging Step 3)

Building the L1..L3 mip chain at load time adds ~14% RAM on top of L0 — and
that overhead dominates the RAM budget for big volumes. Instead, during
brick packing, average `(1<<lod)^3` source voxels per output voxel. Box
filter is associative (`box(box(x)) = box(x)`), so a single pass is
mathematically equivalent to a chained build.

**In Mini-Engine:**

- Four atlases: `BrickedVolume`'s `m_atlasTexes[kLodLevels]`.
- Selection logic: the `pickLod` lambda inside
  `VolumeRenderer::updateBrickStreaming`.
- Shader decode: the LOD branch in `sampleVolume()`.
- Box filter at pack time: the `factor` loop in `packBrickToStaging`.

---

## 7. Streaming — When the Atlas Can't Hold Every Visible Brick

If `nonEmptyBrickCount > atlasSlots`, the build switches automatically to
**Streaming mode**: keep the bricks the camera currently sees, evict the
rest.

### 7.1 Frustum cull

Every frame, test every page-grid brick's AABB against the camera frustum
and collect the visible indices.

### 7.2 LRU eviction

- Every atlas slot tracks a `lastFrameUsed`.
- Visible bricks bump their slot's `lastFrameUsed` to the current frame.
- When a new brick needs a slot and none is free, evict the slot with the
  smallest `lastFrameUsed`.
- **Slots whose `lastFrameUsed == current frame` are never evicted** —
  this is the anti-thrash invariant.

### 7.3 Bounded uploads per frame

CPU brick packing is expensive (~9-10 ms per L0 brick on the legacy path).
The loop caps each frame at K uploads (currently 64) so frame time stays
bounded.

### 7.4 No LOD migration

Once a brick is resident at some LOD, it stays there even when the LOD
selection changes — trading occasional stale-LOD residents for visual
stability and no streaming churn. Only newly-visible bricks honour the
current selection.

**In Mini-Engine:**

- Mode decision: `BrickedVolume::build` (`m_mode = Static | Streaming`).
- Slot picking + eviction: `BrickedVolume::updateStreaming` (`pickSlotForLod`).
- Page-table GPU push: when entries change, the streaming loop stages the
  updated table and submits a `copyBufferToBuffer`.

---

## 8. Path Tracing — Cinematic Quality (M4)

DVR + Lambert is the workstation standard, but path-traced scattering is
what gives the renderer "WOW" depth cues. Volume path tracing models the
following.

### 8.1 Free-path sampling (Woodcock tracking)

Sample the next scattering distance in a heterogeneous medium:

- Inflate the medium to a homogeneous `σt_max` and sample
  `t = -log(ξ) / σt_max`.
- Compare the actual `σt(x)` at that point with `σt_max`; accept the
  collision with probability `σt(x) / σt_max`, otherwise it's a "null
  collision" and the loop continues.

### 8.2 Phase function (Henyey-Greenstein)

At each scattering, sample a new direction. HG is a one-parameter family
controlled by `g` (forward / isotropic / backward):

```text
phase(cosθ) = (1 - g²) / (4π · (1 + g² - 2g·cosθ)^(3/2))
```

### 8.3 Next-event estimation (NEE)

At every scattering vertex, sample the light direction directly and
accumulate its contribution. Drastically reduces variance.

### 8.4 Progressive accumulation

When the camera is still, average each frame's path-trace result into a
ping-pong RGBA16Float texture as a running mean. `N=0` skips the history.
Any camera or parameter change resets `N`.

**In Mini-Engine:**

- Shaders: `shaders/volume_pathtrace.{frag.glsl, wgsl}` + display
  `shaders/volume_pathtrace_display.{frag.glsl, wgsl}`.
- Accumulation textures: `VolumeRenderer::createAccumulationResources()`.
- Mode toggle: `VolumeRenderer::RenderMode::Lambert | PathTrace`.

---

## 9. Data Input — NIfTI · DICOM

### 9.1 NIfTI (.nii)

- 348-byte header followed by the voxel array.
- Important fields: dims, datatype, pixdim (spacing), scl_slope/inter,
  voxOffset.
- Single file, uncompressed. The neuroimaging standard.

### 9.2 DICOM (directory = one series)

- Each `.dcm` is one slice; sort by `ImagePositionPatient.z`.
- Rich metadata (rescale slope/intercept → HU, etc.).
- **Transfer syntax** controls the wire encoding:
  - Explicit VR LE — VR codes inline in the header.
  - **Implicit VR LE** — VR is omitted; the parser looks it up in a tag
    dictionary (the clinical PACS default).
  - **RLE Lossless** — PackBits compression.
  - **JPEG 2000 Lossless / Lossy** — decoded via OpenJPEG.

### 9.3 Hounsfield Unit (HU)

CT pixels are raw int16. Convert to physical units with
`HU = slope * raw + intercept`. Air ≈ -1000, water ≈ 0, bone ≈ 300~3000.

**In Mini-Engine:**

- NIfTI: `src/assets/NiftiFile.{hpp, cpp}`.
  - mmap fast path: `loadNiftiAsMmappedSource()` (int16/uint16 + trivial
    rescale → hand the mmap straight to `BrickedVolume`).
- DICOM: `src/assets/DicomFile.{hpp, cpp}`.
  - File meta (group 0002, always Explicit VR) → read TransferSyntaxUID,
    then dispatch.
  - Implicit VR walker + a small tag → VR dictionary.
  - Compressed path: in-tree RLE PackBits + OpenJPEG memory stream for
    JPEG 2000.

---

## 10. Disk Paging — When the Data Is Bigger Than RAM

How do you load a 16 GB volume on a 16 GB-RAM workstation?

### 10.1 mmap-backed source

Hand the NIfTI body to the OS page cache via `mmap`. The CPU walks the
data sequentially, so the OS pages it in lazily. No transient buffer the
size of the file.

### 10.2 Eliminate `m_originalHalfData`

The classic ingest pipeline is `NIfTI -> float vector -> half vector ->
m_originalHalfData` — all of them RAM-resident. At 16 GB the doubling is
fatal.

**Fix**: introduce a `VoxelSource` abstraction that lets the brick pack
read int16 voxels straight out of the mmap, apply slope/intercept, and
pack to half on the fly. `m_originalHalfData` is never materialised.

### 10.3 Result

For a 1024³ dense int16 NIfTI (2.1 GB on disk): peak working set drops
from 6.57 GB to 2.30 GB (-65%).

**In Mini-Engine:**

- `utils::MmappedFile` — portable wrapper (Windows `MapViewOfFile` +
  POSIX `mmap`).
- `BrickedVolume::VoxelSource` — { data, format, slope, intercept } view.
- `BrickedVolume::buildFromMmappedSource` — takes ownership of the
  `MmappedFile` and runs the on-the-fly conversion inside `packBrickToStaging`.

---

## 11. Reading the Codebase

A productive first pass through the source goes in this order:

1. **`tests/volume_viewer.cpp`** (native) or **`tests/volume_viewer_wasm.cpp`**
   (browser) — the whole flow. Read `render()` to see what one frame does.
2. **`src/rendering/VolumeRenderer.{hpp, cpp}`** — UBO layout, shader
   pipelines, bind groups, occupancy build, LOD selection.
3. **`src/rendering/BrickedVolume.{hpp, cpp}`** — atlas + page table +
   streaming.
4. **`shaders/volume_march.{frag.glsl, wgsl}`** — the ray marcher and the
   LOD decode.
5. **`shaders/volume_pathtrace.{frag.glsl, wgsl}`** — Woodcock + HG + NEE.
6. **`src/assets/NiftiFile.{hpp, cpp}`** and **`DicomFile.{hpp, cpp}`** —
   data ingest.

Pair each file with [VIEWERS.md](../VIEWERS.md) for how to drive it and
[MEDICAL_VOLUME_ROADMAP.md](../MEDICAL_VOLUME_ROADMAP.md) for the "why".

---

## 12. Further Reading

- External standards / books
  - "Real-Time Volume Graphics" (Engel, Hadwiger, Kniss, Rezk-Salama,
    Weiskopf).
  - DICOM PS3.5 — transfer syntax encoding.
  - NIfTI-1 spec — <https://nifti.nimh.nih.gov/>.
- Internal track docs: [plans/](../plans/), [baselines/](../baselines/).
- Debugging journeys (real-world traps):
  `docs/archive/changelogs/CHANGELOG_2026-06-04.md` and siblings (see
  CLAUDE.md §6 for the archive layout).
