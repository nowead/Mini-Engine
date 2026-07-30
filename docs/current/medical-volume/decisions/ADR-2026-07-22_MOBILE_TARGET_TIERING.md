# ADR 2026-07-22 — Mobile target tiering (per-device volume-size targets)

**Status**: Accepted (2026-07-22)
**Track**: Y (browser large-volume paging)
**Supersedes**: Implicit prior direction of "iPhone must render every volume size
that a workstation renders"

## Context

The medical volume renderer's original goal is *accessibility differentiation*:
show that WebGPU + WASM can render real clinical CT/MRI on low-spec PCs,
tablets, and mobile browsers -- workloads that historically require dedicated
workstations.

Where we are on 2026-07-22:

- Y1f complete: iPhone Safari successfully renders a **192 MB CT** (chest
  range). [BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md](../baselines/BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md)
- Empirical desktop test: Chrome / RTX 4070 successfully renders a **512 MB
  synthetic CT** (Static mode with auto-shrunk atlas), CPU 2.56 ms/frame.
- Empirical iPhone test at the same 512 MB volume: **crash** at load time.

The gap: iPhone Safari's process memory budget is ~600-650 MB (established by
[BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md](../baselines/BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md)),
and a 500 MB+ volume's peak (halfData + staging + GPU atlas + wasm runtime)
fundamentally exceeds that budget with the current architecture. Making iPhone
render arbitrary volume sizes requires a working-set-streaming architecture
with browser-side disk paging (IndexedDB / OPFS), which is a 9-13 session
project.

Before committing to that architecture project, we re-examined whether it
matches the actual clinical workflow and the resource situation.

## Decision

**Adopt per-device tiered volume-size targets. Do NOT pursue an
iPhone-must-render-everything architecture.**

Per-tier volume-size support target:

| Tier | Representative device | Volume-size target | Rationale |
|---|---|---|---|
| **Mobile phone** | iPhone Safari (iOS 18+, 6-8 GB RAM) | Head CT + thin-slice + chest CT (up to ~256 MB) | Process budget ~600 MB; workflow rarely 3D on phones |
| **Tablet / low-spec desktop** | iPad Pro (M-chip), Chromebook, Intel iGPU laptop | Full chest CT + abdominal CT (up to ~1 GB) | 8-16+ GB RAM, wasm heap headroom, WebGPU perf adequate |
| **Desktop / workstation** | Discrete GPU laptop, dedicated workstation | Whole-body CT + multi-GB (500 MB - multi-GB) | Native mmap path already supports this on the native build; wasm build inherits Streaming + LOD |

The next code work (Y1c-full) is scoped to move the mobile-phone target from
~192 MB to ~256 MB, closing out the practical clinical-CT range for that tier.

## Reasoning

### 1. Clinical workflow

- **Radiologists** always use dedicated workstations for 3D review (large
  screen, high-spec GPU, professional viewers). Phone-based 3D is not part of
  their diagnostic workflow.
- **Referring physicians** consult results on phones/tablets mostly in 2D
  slice form (PACS-integrated viewers deliver 2D thumbnails and MPR views on
  mobile).
- **Patients** viewing their own studies on phones typically see 2D previews
  or interactive slice viewers.
- 3D volume rendering on a phone is an *impressive-demo* capability, not a
  workflow-critical one.

Conclusion: the pressure to render whole-body 3D CT on iPhone comes from
"complete parity with workstation" narratives, not from actual clinical use.

### 2. Device tier as natural spectrum

The device landscape already stratifies by memory budget:

| Class | Available memory for renderer | Match |
|---|---:|---|
| iPhone Safari (WKWebView) | ~500-700 MB total | Chest CT (192-256 MB) |
| iPad Pro (M-chip) | 4-8 GB effective | Full chest / abdominal CT |
| Chromebook / low-spec laptop | 4-8 GB browser | Same as iPad tier |
| Discrete-GPU laptop / desktop | 12+ GB | Whole-body CT |

The volume sizes match the tiers **without needing per-device architecture
gymnastics**. Trying to force whole-body CT onto iPhone specifically is
fighting the natural device gradient.

### 3. Investment vs value

| Path | Sessions | Delivers |
|---|---:|---|
| Y1c-full only (mobile → 256 MB) | 3-4 | Standard clinical CT on phone |
| Full disk-paging arch (mobile → any) | 9-13 | Whole-body CT on phone |
| Ratio | ~3x | Marginal clinical value |

The 3x session cost buys a capability with limited clinical demand. Better
directed at other axes (X3 Android/Intel iGPU coverage, Y2 UX cache, engine
polish/demo).

### 4. Existing infrastructure covers the tiered targets

95% of the streaming architecture already exists (v1-β multi-LOD, LRU
Streaming, VoxelSource + mmap, K=64 upload budget, Y1f slab staging). What is
missing (persistent brick storage, LOD seam mitigation, predictive prefetch)
is only strictly required to push mobile past ~256 MB. For iPad+ tiers the
existing pieces already work as evidenced by the desktop-Chrome 512 MB test.

### 5. Original goal reinterpretation

The stated goal is "accessibility differentiation via WebGPU". A tiered
reading — "each device tier renders the volume sizes appropriate to its
class, and every tier can render more than it could without WebGPU" — is
a legitimate satisfaction of that goal. The maximalist reading — "any
device renders any size" — is aspirational rather than intrinsic to the
goal statement.

## Consequences

### Positive

- **Focused effort**: Y1c-full (3-4 sessions) closes out the mobile-phone
  tier at the practical clinical ceiling. Total Y-axis session count drops
  to ~5-6 including baseline + polish.
- **Honest positioning**: documentation states per-tier limits explicitly.
  Prevents future confusion about "why doesn't iPhone do whole-body?".
- **Resource redirect**: the ~6-8 sessions saved go to axes with real
  outstanding work (X3 Android/iGPU baselines, engine demo polish, Z2
  async pack).
- **Matches industry practice**: OHIF/cornerstone3D and similar viewers
  also tier their 3D capability by device class.

### Negative

- **iPhone cannot render whole-body CT** as a headline demo. If a
  business/portfolio narrative later requires "iPhone whole-body CT",
  this ADR would be revisited and the full disk-paging track re-opened.
- **Documentation burden**: per-tier limits need explicit UI/messaging
  (e.g. "This volume is too large for the current device. Try opening
  it on tablet or desktop.").

### Neutral

- Keeps the disk-paging architecture as *future work*, not
  actively-blocked. If a strategic pivot requires iPhone whole-body CT,
  the Y1c-full + Y2 + Streaming + v1-γ path is still viable.
- Native side (via mmap Step 5) already handles multi-GB volumes;
  this ADR does not touch native capability.

## Alternatives considered

### Option 2 — Aggressive multi-LOD on mobile (rejected)

Force iPhone tier to always render at L2 or L3 (8-64x downsampled).

- **Pro**: any volume size fits in ~10 MB atlas + runtime.
- **Con**: clinical-relevant fine structure permanently invisible unless
  user manually requests higher resolution. Diagnostic quality concern.
- **Verdict**: rejected. Quality compromise not appropriate for a viewer
  meant to be taken seriously in a medical context.

### Option 3 — Full browser disk-paging architecture (deferred)

Build IndexedDB/OPFS brick storage + Y1c-full + force Streaming +
v1-γ seam mitigation + predictive prefetch.

- **Pro**: definitive answer for arbitrary volume size on any device.
- **Con**: 9-13 sessions for a capability with limited clinical demand.
  LOD seam artifacts (v1-β issue) get more visually prominent on mobile
  small screens where zoom-out is the default view mode.
- **Verdict**: deferred. Re-evaluate if a specific business/portfolio
  narrative requires iPhone whole-body CT.

### Option 4 — Server-assisted rendering (rejected)

Server pre-decodes DICOM + streams packed bricks; client only uploads to
GPU.

- **Pro**: any device size, simplest client, fastest load.
- **Con**: violates the project's identity ("brower-only, no server GPU
  or server-side decode"). Loses the WebGPU differentiation narrative.
- **Verdict**: rejected. Original goal explicitly rules this out.

## Follow-up

- **Immediate**: proceed with Y1c-full (~3-4 sessions) to close the
  mobile-phone tier at ~256 MB. Baseline the result.
- **Then**: iPad Pro + low-spec desktop baseline (~1-2 sessions) to
  empirically confirm the tiered targets.
- **Documentation**: MEDICAL_VOLUME_ROADMAP.md next-candidates section
  reframed to reflect this ADR. Y1F/Y1C-FULL plans updated to note the
  target ceiling.
- **UI**: eventual "volume too large for this device" messaging when a
  load exceeds the tier's target size.

## References

- [BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md](../baselines/BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md) — iPhone process budget wall
- [BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md](../baselines/BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md) — iPhone 192 MB breakthrough
- [Y1F_CHUNKED_STAGING_PLAN.md](../plans/Y1F_CHUNKED_STAGING_PLAN.md)
- [Y1C_FULL_PROGRESSIVE_PLAN.md](../plans/Y1C_FULL_PROGRESSIVE_PLAN.md)
- [MEDICAL_VOLUME_ROADMAP.md](../MEDICAL_VOLUME_ROADMAP.md) — scope reset (2026-07-10) established the original goal statement this ADR reinterprets
