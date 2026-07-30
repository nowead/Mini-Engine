#include "BrickedVolume.hpp"

#include "src/utils/Logger.hpp"

#include <glm/gtc/packing.hpp>   // packHalf1x16 / unpackHalf1x16 (v1-beta mip)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace rendering {

namespace {

// Sample one source voxel with edge clamping and return its R16Float bit
// pattern. For HalfFloat the source bits are already half so we just pass them
// through; for Int16 / Uint16 we apply slope/intercept and pack to half.
// Used by the Static-mode atlas pack so int16 sources flow through the same
// build path as the legacy half-data vector input.
inline uint16_t srcVoxelHalfBits(const BrickedVolume::VoxelSource& src,
                                 int x, int y, int z,
                                 uint32_t W, uint32_t H, uint32_t D) {
    using Format = BrickedVolume::VoxelSource::Format;
    x = std::clamp(x, 0, static_cast<int>(W) - 1);
    y = std::clamp(y, 0, static_cast<int>(H) - 1);
    z = std::clamp(z, 0, static_cast<int>(D) - 1);
    const size_t idx = (static_cast<size_t>(z) * H + y) * W + x;
    switch (src.format) {
    case Format::HalfFloat:
        return static_cast<const uint16_t*>(src.data)[idx];
    case Format::Int16: {
        const float v = src.slope * static_cast<float>(static_cast<const int16_t*>(src.data)[idx])
                      + src.intercept;
        return glm::packHalf1x16(v);
    }
    case Format::Uint16: {
        const float v = src.slope * static_cast<float>(static_cast<const uint16_t*>(src.data)[idx])
                      + src.intercept;
        return glm::packHalf1x16(v);
    }
    }
    return 0;
}

// True iff every interior voxel of the brick at (bx, by, bz) equals
// emptyValueRaw. "Interior" = the 64^3 voxels actually owned by the brick; the
// halo is ignored for the empty test (a brick whose interior is air but whose
// halo would have data is still empty: the *neighbour* brick is the one that
// needs the gradient, and ITS halo brings in our zeros). Generalised over the
// source's wire format (HalfFloat / Int16 / Uint16) by treating each voxel as
// 16 raw bits; the comparison is bit-exact so the caller picks the right raw
// pattern (e.g. NIfTI int16 -1000 reinterpreted as uint16).
bool isInteriorEmpty(const uint16_t* src,
                     uint32_t bx, uint32_t by, uint32_t bz,
                     uint32_t W, uint32_t H, uint32_t D,
                     uint16_t emptyValueRaw) {
    const uint32_t x0 = bx * BrickedVolume::kBrickSize;
    const uint32_t y0 = by * BrickedVolume::kBrickSize;
    const uint32_t z0 = bz * BrickedVolume::kBrickSize;
    const uint32_t x1 = std::min(x0 + BrickedVolume::kBrickSize, W);
    const uint32_t y1 = std::min(y0 + BrickedVolume::kBrickSize, H);
    const uint32_t z1 = std::min(z0 + BrickedVolume::kBrickSize, D);
    for (uint32_t z = z0; z < z1; ++z) {
        for (uint32_t y = y0; y < y1; ++y) {
            const size_t row = (static_cast<size_t>(z) * H + y) * W;
            for (uint32_t x = x0; x < x1; ++x) {
                if (src[row + x] != emptyValueRaw) return false;
            }
        }
    }
    return true;
}

// (Disk paging Step 3) The standalone box-filter helper used to build the
// L1..L3 mip chain at load time is gone -- packBrickToStaging now does the
// downsample on the fly per brick from the L0 source.

} // namespace

bool BrickedVolume::build(rhi::RHIDevice* device, rhi::RHIQueue* queue,
                          std::vector<uint16_t>&& halfDataIn,
                          uint32_t w, uint32_t h, uint32_t d,
                          uint16_t emptyValueHalf,
                          glm::uvec3 atlasGrid) {
    if (!device || !queue) return false;
    if (w == 0 || h == 0 || d == 0) return false;
    if (halfDataIn.size() < static_cast<size_t>(w) * h * d) {
        LOG_ERROR("BrickedVolume") << "halfData too small for " << w << "x" << h << "x" << d;
        return false;
    }
    // Cache for v1-3 updateStreaming.
    m_device = device;
    m_queue  = queue;

    // Y1e: take ownership of the caller's halfData into a local. Streaming
    // path moves this into m_originalHalfData below; Static path keeps it
    // for the Pass 2 pack then releases it just before the GPU transfer
    // begins -- one full-volume-sized buffer trimmed from the load peak on
    // top of Y1c-lite (float intensity) + Y1d (atlasBuf).
    std::vector<uint16_t> halfData = std::move(halfDataIn);

    // VoxelSource view over the local halfData. Static path reads through
    // this in the pack loop; Streaming path never touches it again after
    // the pre-scan + mode decision (m_originalHalfData takes over).
    const VoxelSource buildView{halfData.data(), halfData.size(),
                                VoxelSource::Format::HalfFloat,
                                1.0f, 0.0f};
    const uint16_t emptyValueRaw = emptyValueHalf;  // HalfFloat path: raw == half bits

    m_volSize   = glm::uvec3(w, h, d);
    m_pageGrid  = glm::uvec3((w + kBrickSize - 1) / kBrickSize,
                             (h + kBrickSize - 1) / kBrickSize,
                             (d + kBrickSize - 1) / kBrickSize);
    // Start with uniform L0 sizing. If we land in Static mode below, per-axis
    // shrinking kicks in for axes with pageGrid == 1 (Option C thin-volume
    // fix). Streaming keeps uniform because packBrickToStaging assumes 66^3.
    m_brickInteriorL0 = glm::uvec3(kBrickSize, kBrickSize, kBrickSize);
    m_brickHaloL0     = glm::uvec3(1u, 1u, 1u);
    const uint32_t totalPageEntries = m_pageGrid.x * m_pageGrid.y * m_pageGrid.z;

    // ------------------------------------------------------------------
    // v1-3 alpha pre-scan (moved earlier): we need the non-empty brick count
    // BEFORE atlas auto-sizing so the auto path can size atlas to fit the
    // visible set instead of capping at an arbitrary axis count. The same
    // bitmap also drives the mode decision and the streaming-time
    // pageHasData() query.
    // ------------------------------------------------------------------
    m_pageOccupancy.assign((totalPageEntries + 7u) >> 3, 0u);
    uint32_t nonEmptyCount = 0;
    for (uint32_t bz = 0; bz < m_pageGrid.z; ++bz) {
        for (uint32_t by = 0; by < m_pageGrid.y; ++by) {
            for (uint32_t bx = 0; bx < m_pageGrid.x; ++bx) {
                if (isInteriorEmpty(static_cast<const uint16_t*>(buildView.data),
                                    bx, by, bz, w, h, d, emptyValueRaw)) continue;
                const uint32_t pageIdx = (bz * m_pageGrid.y + by) * m_pageGrid.x + bx;
                m_pageOccupancy[pageIdx >> 3] |= static_cast<uint8_t>(1u << (pageIdx & 7));
                ++nonEmptyCount;
            }
        }
    }

    // Auto-size: start from the page grid (the largest useful atlas, holds
    // every non-empty brick by construction) and shrink the longest axis
    // until we fit kAutoAtlasBudgetBytes. This wins Static whenever the
    // budget is generous enough -- including for highly anisotropic page
    // grids like an 8x8x1 single-slice DICOM, where the prior cube-root
    // heuristic produced (4,4,1) = 16 slots for 64 bricks and forced a
    // permanent Streaming churn. Caller can pass an explicit atlasGrid to
    // override the policy entirely.
    if (atlasGrid.x == 0 || atlasGrid.y == 0 || atlasGrid.z == 0) {
        atlasGrid = m_pageGrid;
        const glm::uvec3 bs0Local = m_brickInteriorL0 + 2u * m_brickHaloL0;
        auto atlasBytesOf = [&](glm::uvec3 a) {
            const uint64_t vx = static_cast<uint64_t>(a.x) * bs0Local.x;
            const uint64_t vy = static_cast<uint64_t>(a.y) * bs0Local.y;
            const uint64_t vz = static_cast<uint64_t>(a.z) * bs0Local.z;
            return vx * vy * vz * 2;  // R16Float
        };
        while (atlasBytesOf(atlasGrid) > kAutoAtlasBudgetBytes &&
               (atlasGrid.x > kAutoAtlasMinAxis ||
                atlasGrid.y > kAutoAtlasMinAxis ||
                atlasGrid.z > kAutoAtlasMinAxis)) {
            if (atlasGrid.x >= atlasGrid.y && atlasGrid.x >= atlasGrid.z && atlasGrid.x > kAutoAtlasMinAxis) {
                --atlasGrid.x;
            } else if (atlasGrid.y >= atlasGrid.z && atlasGrid.y > kAutoAtlasMinAxis) {
                --atlasGrid.y;
            } else if (atlasGrid.z > kAutoAtlasMinAxis) {
                --atlasGrid.z;
            } else {
                break;
            }
        }
    }
    m_atlasGrid = atlasGrid;

    const uint32_t totalSlots = atlasGrid.x * atlasGrid.y * atlasGrid.z;

    // ------------------------------------------------------------------
    // Mode decision. Static when every non-empty brick fits the atlas
    // (v0 behaviour); Streaming when not. v1-3 alpha: the pre-scan already
    // ran (before atlas auto-sizing) so we just compare counts here.
    //
    // Option C thin-volume fix (this commit): once the mode is decided we
    // adjust m_brickInteriorL0 / m_brickHaloL0. Static-only because the
    // streaming pack still writes uniform 66^3 per brick; per-axis
    // streaming is a follow-up.
    // ------------------------------------------------------------------
    if (nonEmptyCount <= totalSlots) {
        m_mode = Mode::StaticFullyLoaded;
        m_brickInteriorL0 = glm::uvec3(
            (m_pageGrid.x == 1) ? w : kBrickSize,
            (m_pageGrid.y == 1) ? h : kBrickSize,
            (m_pageGrid.z == 1) ? d : kBrickSize);
        m_brickHaloL0 = glm::uvec3(m_pageGrid.x > 1 ? 1u : 0u,
                                    m_pageGrid.y > 1 ? 1u : 0u,
                                    m_pageGrid.z > 1 ? 1u : 0u);
        // Follow-up to Option C: last-brick shrink. When atlasGrid ==
        // pageGrid slots become position-based (slot == pageIdx), so the
        // last brick along each multi-brick axis can drop its outer halo
        // and truncate its interior to the actual remainder. Middle/first
        // bricks stay uniform 66 to keep the shader's slot-origin math a
        // simple sx * uniformStored + halo.
        m_uniformSlotLayout   = true;
        m_brickInteriorLastL0 = m_brickInteriorL0;
        if (m_atlasGrid == m_pageGrid) {
            m_uniformSlotLayout = false;
            m_brickInteriorLastL0 = glm::uvec3(
                (m_pageGrid.x > 1) ? (w - (m_pageGrid.x - 1) * kBrickSize) : m_brickInteriorL0.x,
                (m_pageGrid.y > 1) ? (h - (m_pageGrid.y - 1) * kBrickSize) : m_brickInteriorL0.y,
                (m_pageGrid.z > 1) ? (d - (m_pageGrid.z - 1) * kBrickSize) : m_brickInteriorL0.z);
        }
    } else {
        m_mode = Mode::Streaming;
        // m_brickInteriorL0 / m_brickHaloL0 already at uniform defaults.
        m_uniformSlotLayout   = true;
        m_brickInteriorLastL0 = m_brickInteriorL0;
        // Recommended atlasGrid for Static. Cube-root rounded up, clamped by
        // pageGrid. Memory estimate uses the same R16Float * kBrickStored^3
        // accounting as atlasBytesAllocated().
        const double cbrt = std::cbrt(static_cast<double>(nonEmptyCount));
        const uint32_t axisGuess = static_cast<uint32_t>(std::ceil(cbrt));
        const glm::uvec3 recommend(
            std::min(std::max(axisGuess, atlasGrid.x), m_pageGrid.x),
            std::min(std::max(axisGuess, atlasGrid.y), m_pageGrid.y),
            std::min(std::max(axisGuess, atlasGrid.z), m_pageGrid.z));
        const uint64_t recommendMB =
            (static_cast<uint64_t>(recommend.x) * recommend.y * recommend.z
             * kBrickStored * kBrickStored * kBrickStored * 2) >> 20;
        // WARN, not INFO: with atlas < estimated max-visible bricks, every
        // camera view that exceeds the atlas size will lose visible structure
        // (sentinel returns 0 for non-resident slots). Acceptable for total
        // > atlas + max-visible <= atlas, broken otherwise. For medical
        // imaging the caller should size atlas to fit visible at minimum.
        LOG_WARN("BrickedVolume") << "streaming mode (atlas too small): "
            << nonEmptyCount << " non-empty bricks vs " << totalSlots
            << " atlas slots. When the camera sees more than " << totalSlots
            << " bricks at once, the excess will render as empty. Pass atlasGrid"
            << " (" << recommend.x << "," << recommend.y << "," << recommend.z
            << ") = " << (recommend.x * recommend.y * recommend.z)
            << " slots (~" << recommendMB << " MB) for guaranteed Static "
            << "rendering. Streaming is best when total bricks > atlas but "
            << "visible-set << atlas (zoom-in workflows on large volumes).";
    }

    // Now that mode + per-axis L0 sizing are decided, compute the actual
    // atlas voxel dimensions. `bs0` is the uniform per-axis stored size
    // (Option C, 66 for multi-brick axes / volSize for pageGrid==1 axes);
    // in shrink mode we replace the last brick's contribution along each
    // multi-brick axis with (lastInterior + inner halo) instead of the
    // full 66. m_atlasVoxelsL0 becomes the L0 texture's physical size
    // and is used for allocation, HUD, and the shader's atlasPhys UBO.
    const glm::uvec3 bs0 = brickStoredL0();
    m_atlasVoxelsL0 = m_atlasGrid * bs0;
    if (!m_uniformSlotLayout) {
        if (m_pageGrid.x > 1) m_atlasVoxelsL0.x = (m_pageGrid.x - 1) * bs0.x + m_brickInteriorLastL0.x + m_brickHaloL0.x;
        if (m_pageGrid.y > 1) m_atlasVoxelsL0.y = (m_pageGrid.y - 1) * bs0.y + m_brickInteriorLastL0.y + m_brickHaloL0.y;
        if (m_pageGrid.z > 1) m_atlasVoxelsL0.z = (m_pageGrid.z - 1) * bs0.z + m_brickInteriorLastL0.z + m_brickHaloL0.z;
    }
    // Y1d: keep only the atlas-voxel dims for the LOG_INFO summary; the
    // row-stride + atlasBytes locals died with the atlasBuf allocation.
    // Each LOD's stride is recomputed inside the LOD loop below.
    const uint32_t atlasVoxelsX = m_atlasVoxelsL0.x;
    const uint32_t atlasVoxelsY = m_atlasVoxelsL0.y;
    const uint32_t atlasVoxelsZ = m_atlasVoxelsL0.z;

    std::vector<uint32_t> pageTable(totalPageEntries, kEmptySlot);
    // Y1d: no CPU-side atlasBuf. The Static-mode pack loop below runs INSIDE
    // the LOD staging loop and writes directly into the WebGPU staging
    // buffer's mapped memory. That eliminated a ~atlas-sized transient
    // (209 MB on 192 MB clinical CT) that Y1c-lite left as the peak
    // bottleneck (staging + atlasBuf both alive during the memcpy).
    m_usedSlotsPerLod = {0, 0, 0, 0};

    // ------------------------------------------------------------------
    // 1d. Streaming-mode state setup. m_usedSlots stays 0 (no resident
    // bricks yet); v1-3 will increment it as pages stream in.
    // ------------------------------------------------------------------
    if (m_mode == Mode::Streaming) {
        // Y1e: move (not copy) into m_originalHalfData. Streaming needs the
        // CPU-side source alive as a member for updateStreaming's per-frame
        // brick pack; moving skips the second full-volume-sized allocation
        // the previous copy-assignment triggered. halfData is empty after
        // this move; buildView (pointing to it) is not used further in the
        // Streaming branch (Streaming LOD loop fills empty only).
        m_originalHalfData = std::move(halfData);
        m_voxelSource = VoxelSource{m_originalHalfData.data(),
                                    m_originalHalfData.size(),
                                    VoxelSource::Format::HalfFloat,
                                    1.0f, 0.0f};
        m_emptyValueHalf   = emptyValueHalf;
        for (auto& states : m_slotStates) states.assign(totalSlots, AtlasSlotState{});
        m_pageToSlot.clear();
        m_pageToSlot.reserve(nonEmptyCount);      // upper bound for the lifetime
        m_lodTableHost.assign(totalPageEntries, 0);  // beta-3 writes; beta-4 uses

        // Disk paging Step 3: the mip chain L1..L3 is no longer materialised at
        // load time. Each streamed brick at LOD > 0 is box-filter-downsampled
        // from the L0 source inside packBrickToStaging.
    }

    // ------------------------------------------------------------------
    // 2. Allocate atlas texture(s) + upload L0 content.
    //
    // Y1f (2026-07-22): the Static-mode L0 path splits the atlas upload
    // into Z-slab-sized staging + copies. Before Y1f, a 192 MB volume
    // allocated one ~209 MB WebGPU staging buffer plus the atlas texture,
    // pushing the wasm process peak to ~668 MB -- past the iPhone Safari
    // ~600-650 MB budget confirmed by BASELINE_2026-07-21_Y1CDE. Now the
    // pack streams one atlas Z-row (~42 MB on the same volume) at a time
    // into a reused staging buffer, keeping the run-time slot state
    // (halfData + staging + GPU + baseline) below the iOS ceiling.
    //
    // Streaming mode still uses the single-staging empty-fill path per
    // LOD. Streaming's L0 staging is worth chunking too (Y1f-4) but the
    // usual streaming workload has visible-set << atlas so the initial
    // fill peak matters less than the Static case.
    // ------------------------------------------------------------------
    uint64_t totalAtlasBytes = 0;

    if (m_mode == Mode::StaticFullyLoaded) {
        // -------- Y1f Static L0: two-pass slot assign + slab-based upload
        const uint32_t brickStoredLod = kBrickStoredAtLod(0);
        const uint32_t voxelsX_lv = m_atlasVoxelsL0.x;
        const uint32_t voxelsY_lv = m_atlasVoxelsL0.y;
        const uint32_t voxelsZ_lv = m_atlasVoxelsL0.z;
        const uint32_t tightRowLv = voxelsX_lv * 2;
#ifdef __EMSCRIPTEN__
        const uint32_t paddedRowLv = (tightRowLv + 255u) & ~255u;
#else
        const uint32_t paddedRowLv = tightRowLv;
#endif
        totalAtlasBytes += static_cast<uint64_t>(paddedRowLv) * voxelsY_lv * voxelsZ_lv;

        // Pass 1: assign slots + write pageTable for every non-empty brick.
        // Extracted from the pack loop so the slab loop below can look up
        // slot by pageIdx without a monotonic counter (which would break if
        // the same brick is visited across multiple slab iterations).
        for (uint32_t bz = 0; bz < m_pageGrid.z; ++bz) {
            for (uint32_t by = 0; by < m_pageGrid.y; ++by) {
                for (uint32_t bx = 0; bx < m_pageGrid.x; ++bx) {
                    const uint32_t pageIdx = (bz * m_pageGrid.y + by) * m_pageGrid.x + bx;
                    const bool hasData = (m_pageOccupancy[pageIdx >> 3] >> (pageIdx & 7)) & 1u;
                    if (!hasData) continue;
                    uint32_t slot;
                    if (m_uniformSlotLayout) {
                        slot = m_usedSlotsPerLod[0]++;
                    } else {
                        slot = pageIdx;
                        ++m_usedSlotsPerLod[0];
                    }
                    pageTable[pageIdx] = encodePage(slot, 0);
                }
            }
        }

        // GPU atlas texture upfront -- the slab loop below issues one
        // copyBufferToTexture per slab, so the destination has to exist
        // before iteration begins. Peak at this instant: halfData 96 +
        // slab_staging 0 + GPU 209 + runtime 250 (on the 192 MB volume);
        // slab_staging gets its ~42 MB right after, taking peak to ~597,
        // and halfData drops once the loop completes.
        rhi::TextureDesc td{};
        td.size          = rhi::Extent3D{voxelsX_lv, voxelsY_lv, voxelsZ_lv};
        td.dimension     = rhi::TextureDimension::Texture3D;
        td.format        = rhi::TextureFormat::R16Float;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
        td.label         = "VolumeBrickAtlas";
        m_atlasTexes[0] = device->createTexture(td);
        if (!m_atlasTexes[0]) {
            LOG_ERROR("BrickedVolume") << "atlas texture L0 create failed"; return false;
        }

        // Initial layout transition (Undefined -> TransferDst) once,
        // before the slab copies. The final TransferDst -> ShaderReadOnly
        // happens after the slab loop.
        {
            auto enc = device->createCommandEncoder();
            enc->transitionTextureLayout(m_atlasTexes[0].get(),
                                         rhi::TextureLayout::Undefined,
                                         rhi::TextureLayout::TransferDst);
            auto cmd = enc->finish();
            queue->submit(cmd.get());
            queue->waitIdle();
        }

        // Slab granularity: 1 brick-row worth of atlas Z (brickStored voxels).
        // On a 192 MB volume that's ~42 MB WebGPU-padded per slab vs the
        // ~209 MB single staging Y1e allocated. Fewer, larger slabs would
        // trade iOS-friendlier peak for more copy calls; Y1f-1 picked 1 row.
        constexpr uint32_t kSlabAtlasRows = 1u;
        const uint32_t slabDepthVox = kSlabAtlasRows * brickStoredLod;
        const uint32_t numSlabs     = (voxelsZ_lv + slabDepthVox - 1) / slabDepthVox;
        const uint64_t slabBytes    = static_cast<uint64_t>(paddedRowLv) * voxelsY_lv * slabDepthVox;

        rhi::BufferDesc sbd{};
        sbd.size  = slabBytes;
        sbd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        sbd.label = "BrickAtlasSlabStaging";
        auto slabStaging = device->createBuffer(sbd);
        if (!slabStaging) {
            LOG_ERROR("BrickedVolume") << "slab staging create failed (" << (slabBytes >> 20) << " MB)"; return false;
        }
        LOG_INFO("BrickedVolume") << "Y1f slab upload: atlas " << voxelsX_lv << "x" << voxelsY_lv
            << "x" << voxelsZ_lv << ", slab " << voxelsX_lv << "x" << voxelsY_lv << "x"
            << slabDepthVox << " (" << (slabBytes >> 20) << " MB, " << numSlabs << " slabs)";

        for (uint32_t slabIdx = 0; slabIdx < numSlabs; ++slabIdx) {
            const uint32_t slabStartZ    = slabIdx * slabDepthVox;
            const uint32_t slabActualZ   = std::min(slabDepthVox, voxelsZ_lv - slabStartZ);

            uint8_t* stagingBytes = static_cast<uint8_t*>(slabStaging->map());
            if (!stagingBytes) {
                LOG_ERROR("BrickedVolume") << "slab staging map failed at slab " << slabIdx; return false;
            }

            // Empty-fill just this slab's Z range.
            for (uint32_t z = 0; z < slabActualZ; ++z) {
                for (uint32_t y = 0; y < voxelsY_lv; ++y) {
                    uint16_t* row = reinterpret_cast<uint16_t*>(
                        stagingBytes
                        + (static_cast<uint64_t>(z) * voxelsY_lv + y) * paddedRowLv);
                    std::fill(row, row + voxelsX_lv, emptyValueHalf);
                }
            }

            // Pack every non-empty brick whose atlas-Z origin falls in this
            // slab. slot -> (sx, sy, sz) uses the raster-decode formula for
            // both uniform and last-brick-shrink layouts (in shrink layout
            // atlasGrid == pageGrid AND slot == pageIdx, so the decode
            // yields (bx, by, bz) as expected).
            for (uint32_t bz = 0; bz < m_pageGrid.z; ++bz) {
                for (uint32_t by = 0; by < m_pageGrid.y; ++by) {
                    for (uint32_t bx = 0; bx < m_pageGrid.x; ++bx) {
                        const uint32_t pageIdx = (bz * m_pageGrid.y + by) * m_pageGrid.x + bx;
                        const uint32_t page    = pageTable[pageIdx];
                        if (page == kEmptySlot) continue;
                        const uint32_t slot = page & kPageSlotMask;
                        const uint32_t sx = slot % atlasGrid.x;
                        const uint32_t sy = (slot / atlasGrid.x) % atlasGrid.y;
                        const uint32_t sz = slot / (atlasGrid.x * atlasGrid.y);
                        const uint32_t aOriginZ = sz * bs0.z;
                        // 1-row slab + brick stored size <= bs0.z means each
                        // brick is entirely inside one slab -- no straddle
                        // to worry about for kSlabAtlasRows == 1.
                        if (aOriginZ < slabStartZ || aOriginZ >= slabStartZ + slabActualZ) continue;

                        glm::uvec3 storedThis = bs0;
                        if (!m_uniformSlotLayout) {
                            if (m_pageGrid.x > 1 && bx == m_pageGrid.x - 1u)
                                storedThis.x = m_brickInteriorLastL0.x + m_brickHaloL0.x;
                            if (m_pageGrid.y > 1 && by == m_pageGrid.y - 1u)
                                storedThis.y = m_brickInteriorLastL0.y + m_brickHaloL0.y;
                            if (m_pageGrid.z > 1 && bz == m_pageGrid.z - 1u)
                                storedThis.z = m_brickInteriorLastL0.z + m_brickHaloL0.z;
                        }

                        const uint32_t aOriginX  = sx * bs0.x;
                        const uint32_t aOriginY  = sy * bs0.y;
                        const uint32_t localOriginZ = aOriginZ - slabStartZ;
                        const int srcX0 = static_cast<int>(bx * kBrickSize)
                                        - static_cast<int>(m_brickHaloL0.x);
                        const int srcY0 = static_cast<int>(by * kBrickSize)
                                        - static_cast<int>(m_brickHaloL0.y);
                        const int srcZ0 = static_cast<int>(bz * kBrickSize)
                                        - static_cast<int>(m_brickHaloL0.z);
                        for (uint32_t lz = 0; lz < storedThis.z; ++lz) {
                            for (uint32_t ly = 0; ly < storedThis.y; ++ly) {
                                uint16_t* dstRow = reinterpret_cast<uint16_t*>(
                                    stagingBytes
                                    + (static_cast<uint64_t>(localOriginZ + lz) * voxelsY_lv
                                       + (aOriginY + ly)) * paddedRowLv
                                    + aOriginX * 2);
                                const int srcZ = srcZ0 + static_cast<int>(lz);
                                const int srcY = srcY0 + static_cast<int>(ly);
                                for (uint32_t lx = 0; lx < storedThis.x; ++lx) {
                                    const int srcXcoord = srcX0 + static_cast<int>(lx);
                                    dstRow[lx] = srcVoxelHalfBits(buildView, srcXcoord, srcY, srcZ, w, h, d);
                                }
                            }
                        }
                    }
                }
            }
            slabStaging->unmap();

            // Copy this slab into the atlas at Z offset slabStartZ.
            auto enc = device->createCommandEncoder();
            rhi::BufferTextureCopyInfo bufferCopy{};
            bufferCopy.buffer = slabStaging.get();
            bufferCopy.offset = 0;
#ifdef __EMSCRIPTEN__
            bufferCopy.bytesPerRow  = paddedRowLv;
            bufferCopy.rowsPerImage = voxelsY_lv;
#else
            bufferCopy.bytesPerRow  = 0;
            bufferCopy.rowsPerImage = 0;
#endif
            rhi::TextureCopyInfo texCopy{};
            texCopy.texture  = m_atlasTexes[0].get();
            texCopy.mipLevel = 0;
            texCopy.origin   = {0, 0, static_cast<int32_t>(slabStartZ)};
            texCopy.aspect   = 0;
            enc->copyBufferToTexture(bufferCopy, texCopy,
                                     rhi::Extent3D{voxelsX_lv, voxelsY_lv, slabActualZ});
            auto cmd = enc->finish();
            queue->submit(cmd.get());
            queue->waitIdle();
        }

        // All slabs uploaded. Final transition to ShaderReadOnly, release
        // halfData (last consumer done), create texture view.
        {
            auto enc = device->createCommandEncoder();
            enc->transitionTextureLayout(m_atlasTexes[0].get(),
                                         rhi::TextureLayout::TransferDst,
                                         rhi::TextureLayout::ShaderReadOnly);
            auto cmd = enc->finish();
            queue->submit(cmd.get());
            queue->waitIdle();
        }
        halfData = std::vector<uint16_t>();  // release capacity, not just size

        rhi::TextureViewDesc vd{};
        vd.format          = rhi::TextureFormat::R16Float;
        vd.dimension       = rhi::TextureViewDimension::View3D;
        vd.baseMipLevel    = 0;  vd.mipLevelCount   = 1;
        vd.baseArrayLayer  = 0;  vd.arrayLayerCount = 1;
        m_atlasViews[0] = m_atlasTexes[0]->createView(vd);
        if (!m_atlasViews[0]) {
            LOG_ERROR("BrickedVolume") << "atlas view L0 create failed"; return false;
        }
    } else {
        // -------- Streaming: legacy single-staging empty-fill per LOD.
        for (uint32_t lv = 0; lv < kLodLevels; ++lv) {
            const uint32_t brickStoredLod = kBrickStoredAtLod(lv);
            const uint32_t voxelsX_lv = (lv == 0) ? m_atlasVoxelsL0.x : (atlasGrid.x * brickStoredLod);
            const uint32_t voxelsY_lv = (lv == 0) ? m_atlasVoxelsL0.y : (atlasGrid.y * brickStoredLod);
            const uint32_t voxelsZ_lv = (lv == 0) ? m_atlasVoxelsL0.z : (atlasGrid.z * brickStoredLod);
            const uint32_t tightRowLv = voxelsX_lv * 2;
#ifdef __EMSCRIPTEN__
            const uint32_t paddedRowLv = (tightRowLv + 255u) & ~255u;
#else
            const uint32_t paddedRowLv = tightRowLv;
#endif
            const uint64_t bytesLv = static_cast<uint64_t>(paddedRowLv) * voxelsY_lv * voxelsZ_lv;
            totalAtlasBytes += bytesLv;

            rhi::BufferDesc bd{};
            bd.size  = bytesLv;
            bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
            bd.label = "BrickAtlasStaging";
            auto staging = device->createBuffer(bd);
            if (!staging) {
                LOG_ERROR("BrickedVolume") << "atlas staging L" << lv << " create failed"; return false;
            }
            uint8_t* stagingBytes = static_cast<uint8_t*>(staging->map());
            if (!stagingBytes) {
                LOG_ERROR("BrickedVolume") << "atlas staging L" << lv << " map failed"; return false;
            }
            for (uint32_t z = 0; z < voxelsZ_lv; ++z) {
                for (uint32_t y = 0; y < voxelsY_lv; ++y) {
                    uint16_t* row = reinterpret_cast<uint16_t*>(
                        stagingBytes
                        + (static_cast<uint64_t>(z) * voxelsY_lv + y) * paddedRowLv);
                    std::fill(row, row + voxelsX_lv, emptyValueHalf);
                }
            }
            staging->unmap();

            rhi::TextureDesc td{};
            td.size          = rhi::Extent3D{voxelsX_lv, voxelsY_lv, voxelsZ_lv};
            td.dimension     = rhi::TextureDimension::Texture3D;
            td.format        = rhi::TextureFormat::R16Float;
            td.mipLevelCount = 1;
            td.sampleCount   = 1;
            td.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
            td.label         = "VolumeBrickAtlas";
            m_atlasTexes[lv] = device->createTexture(td);
            if (!m_atlasTexes[lv]) {
                LOG_ERROR("BrickedVolume") << "atlas texture L" << lv << " create failed"; return false;
            }

            auto enc = device->createCommandEncoder();
            enc->transitionTextureLayout(m_atlasTexes[lv].get(),
                                         rhi::TextureLayout::Undefined,
                                         rhi::TextureLayout::TransferDst);
            rhi::BufferTextureCopyInfo bufferCopy{};
            bufferCopy.buffer = staging.get();
            bufferCopy.offset = 0;
#ifdef __EMSCRIPTEN__
            bufferCopy.bytesPerRow  = paddedRowLv;
            bufferCopy.rowsPerImage = voxelsY_lv;
#else
            bufferCopy.bytesPerRow  = 0;
            bufferCopy.rowsPerImage = 0;
#endif
            rhi::TextureCopyInfo texCopy{};
            texCopy.texture  = m_atlasTexes[lv].get();
            texCopy.mipLevel = 0;
            texCopy.origin   = {0, 0, 0};
            texCopy.aspect   = 0;
            enc->copyBufferToTexture(bufferCopy, texCopy,
                                     rhi::Extent3D{voxelsX_lv, voxelsY_lv, voxelsZ_lv});
            enc->transitionTextureLayout(m_atlasTexes[lv].get(),
                                         rhi::TextureLayout::TransferDst,
                                         rhi::TextureLayout::ShaderReadOnly);
            auto cmd = enc->finish();
            queue->submit(cmd.get());
            queue->waitIdle();

            rhi::TextureViewDesc vd{};
            vd.format          = rhi::TextureFormat::R16Float;
            vd.dimension       = rhi::TextureViewDimension::View3D;
            vd.baseMipLevel    = 0;  vd.mipLevelCount   = 1;
            vd.baseArrayLayer  = 0;  vd.arrayLayerCount = 1;
            m_atlasViews[lv] = m_atlasTexes[lv]->createView(vd);
            if (!m_atlasViews[lv]) {
                LOG_ERROR("BrickedVolume") << "atlas view L" << lv << " create failed"; return false;
            }
        }
        LOG_INFO("BrickedVolume") << "atlas total: "
            << (totalAtlasBytes >> 20) << " MB across " << kLodLevels
            << " LOD textures (L0 + L1 + L2 + L3)";
    }

    // ------------------------------------------------------------------
    // 3. Allocate + upload the page table storage buffer.
    // Storage-only buffers can't be mapped, so stage through a CopySrc/MapWrite
    // buffer and do a device-side copy. The page table is small (one uint32
    // per virtual brick) so the extra copy is negligible.
    // ------------------------------------------------------------------
    {
        const uint64_t pageBytes = static_cast<uint64_t>(totalPageEntries) * sizeof(uint32_t);

        rhi::BufferDesc dstDesc{};
        dstDesc.size  = pageBytes;
        dstDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        dstDesc.label = "VolumeBrickPageTable";
        m_pageTable = device->createBuffer(dstDesc);
        if (!m_pageTable) { LOG_ERROR("BrickedVolume") << "page table buffer create failed"; return false; }

        rhi::BufferDesc stagingDesc{};
        stagingDesc.size  = pageBytes;
        stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        stagingDesc.label = "PageTableStaging";
        auto staging = device->createBuffer(stagingDesc);
        if (!staging) { LOG_ERROR("BrickedVolume") << "page table staging create failed"; return false; }
        std::memcpy(staging->map(), pageTable.data(), pageBytes);
        staging->unmap();

        auto enc = device->createCommandEncoder();
        enc->copyBufferToBuffer(staging.get(), 0, m_pageTable.get(), 0, pageBytes);
        auto cmd = enc->finish();
        queue->submit(cmd.get());
        queue->waitIdle();

        // v1-1: keep a CPU mirror so the frustum-cull pass can query per-page
        // non-empty status without a GPU readback. v1-3 streaming will mutate
        // this in place as bricks page in/out.
        m_pageTableHost = std::move(pageTable);
    }

    LOG_INFO("BrickedVolume") << "built " << w << "x" << h << "x" << d
        << " -> page " << m_pageGrid.x << "x" << m_pageGrid.y << "x" << m_pageGrid.z
        << " (" << nonEmptyCount << " non-empty), "
        << usedSlots() << "/" << totalSlots << " atlas slots, mode = "
        << (m_mode == Mode::Streaming ? "Streaming" : "Static")
        << " (atlas " << atlasVoxelsX << "x" << atlasVoxelsY << "x" << atlasVoxelsZ << ")";
    return true;
}

// ---------------------------------------------------------------------------
// Y1c-full progressive build (2026-07-22, Y1c-full-2 implementation).
//
// beginProgressiveBuild() allocates the atlas texture, slab staging, and
// ring buffer. pushSlice() copies the slice into the ring at (z % 66) and
// triggers packAndUploadRow(bz) as each brick row's expected slice count
// is met; the ring's 66-slot window is always the last 66 slices received
// so a row that completes at slice Z=bz*64+64 finds all its source Z in
// [bz*64-1, bz*64+64] present. endProgressiveBuild() uploads the finalised
// page table, transitions the atlas to ShaderReadOnly, and destroys the
// context. Static mode only; Streaming stays on build() until Y2 lands.
// ---------------------------------------------------------------------------
namespace {

// Read one voxel from the ring buffer with edge clamping. The ring holds
// the last kProgressiveRingSlices slices; a source Z of x maps to ring slot
// (x % kProgressiveRingSlices). For clamped values (halo reads at z=-1 or
// z=d) the clamped Z is guaranteed to be within the currently-resident
// window because the pack only fires when the row's entire Z range is
// present.
inline uint16_t readRingClamped(const std::vector<uint16_t>& ring,
                                uint32_t w, uint32_t h, uint32_t d,
                                int x, int y, int z) {
    x = std::clamp(x, 0, static_cast<int>(w) - 1);
    y = std::clamp(y, 0, static_cast<int>(h) - 1);
    z = std::clamp(z, 0, static_cast<int>(d) - 1);
    const size_t slot = static_cast<size_t>(z) % BrickedVolume::kProgressiveRingSlices;
    return ring[(slot * h + y) * w + x];
}

// Is brick (bx, by, bz) all-empty? Same test as build()'s isInteriorEmpty
// but reads from the ring buffer via readRingClamped so we can decide
// non-emptiness at row-completion time (before finalisation).
bool isInteriorEmptyProgressive(const std::vector<uint16_t>& ring,
                                uint32_t bx, uint32_t by, uint32_t bz,
                                uint32_t W, uint32_t H, uint32_t D,
                                uint16_t emptyValueRaw) {
    const uint32_t x0 = bx * BrickedVolume::kBrickSize;
    const uint32_t y0 = by * BrickedVolume::kBrickSize;
    const uint32_t z0 = bz * BrickedVolume::kBrickSize;
    const uint32_t x1 = std::min(x0 + BrickedVolume::kBrickSize, W);
    const uint32_t y1 = std::min(y0 + BrickedVolume::kBrickSize, H);
    const uint32_t z1 = std::min(z0 + BrickedVolume::kBrickSize, D);
    for (uint32_t z = z0; z < z1; ++z) {
        const size_t slot = static_cast<size_t>(z) % BrickedVolume::kProgressiveRingSlices;
        for (uint32_t y = y0; y < y1; ++y) {
            const size_t row = (slot * H + y) * W;
            for (uint32_t x = x0; x < x1; ++x) {
                if (ring[row + x] != emptyValueRaw) return false;
            }
        }
    }
    return true;
}

} // namespace

BrickedVolume::ProgressiveContext BrickedVolume::beginProgressiveBuild(
    rhi::RHIDevice* device, rhi::RHIQueue* queue,
    uint32_t w, uint32_t h, uint32_t d,
    uint16_t emptyValueHalf,
    glm::uvec3 atlasGrid) {
    ProgressiveContext ctx{};
    if (!device || !queue || w == 0 || h == 0 || d == 0) {
        LOG_ERROR("BrickedVolume") << "beginProgressiveBuild: invalid args";
        ctx.failed = true;
        return ctx;
    }
    ctx.device = device;
    ctx.queue  = queue;
    ctx.w = w; ctx.h = h; ctx.d = d;
    ctx.emptyValueHalf = emptyValueHalf;

    m_device = device;
    m_queue  = queue;
    m_volSize  = glm::uvec3(w, h, d);
    m_pageGrid = glm::uvec3((w + kBrickSize - 1) / kBrickSize,
                             (h + kBrickSize - 1) / kBrickSize,
                             (d + kBrickSize - 1) / kBrickSize);
    ctx.pageGrid          = m_pageGrid;
    ctx.totalPageEntries  = m_pageGrid.x * m_pageGrid.y * m_pageGrid.z;

    // Atlas grid: default to pageGrid (shrink layout). Caller may override
    // but we still force shrink whenever it happens to equal pageGrid.
    if (atlasGrid.x == 0 || atlasGrid.y == 0 || atlasGrid.z == 0) {
        atlasGrid = m_pageGrid;
    }
    m_atlasGrid = atlasGrid;
    ctx.atlasGrid = atlasGrid;

    m_mode = Mode::StaticFullyLoaded;

    // Option C thin-volume sizing (same as build())
    m_brickInteriorL0 = glm::uvec3(
        (m_pageGrid.x == 1) ? w : kBrickSize,
        (m_pageGrid.y == 1) ? h : kBrickSize,
        (m_pageGrid.z == 1) ? d : kBrickSize);
    m_brickHaloL0 = glm::uvec3(m_pageGrid.x > 1 ? 1u : 0u,
                                m_pageGrid.y > 1 ? 1u : 0u,
                                m_pageGrid.z > 1 ? 1u : 0u);
    ctx.brickInteriorL0 = m_brickInteriorL0;
    ctx.brickHaloL0     = m_brickHaloL0;

    // Last-brick shrink when atlasGrid == pageGrid
    m_uniformSlotLayout   = !(m_atlasGrid == m_pageGrid);
    m_brickInteriorLastL0 = m_brickInteriorL0;
    if (!m_uniformSlotLayout) {
        m_brickInteriorLastL0 = glm::uvec3(
            (m_pageGrid.x > 1) ? (w - (m_pageGrid.x - 1) * kBrickSize) : m_brickInteriorL0.x,
            (m_pageGrid.y > 1) ? (h - (m_pageGrid.y - 1) * kBrickSize) : m_brickInteriorL0.y,
            (m_pageGrid.z > 1) ? (d - (m_pageGrid.z - 1) * kBrickSize) : m_brickInteriorL0.z);
    }
    ctx.brickInteriorLastL0 = m_brickInteriorLastL0;

    const glm::uvec3 bs0 = brickStoredL0();
    m_atlasVoxelsL0 = m_atlasGrid * bs0;
    if (!m_uniformSlotLayout) {
        if (m_pageGrid.x > 1) m_atlasVoxelsL0.x = (m_pageGrid.x - 1) * bs0.x + m_brickInteriorLastL0.x + m_brickHaloL0.x;
        if (m_pageGrid.y > 1) m_atlasVoxelsL0.y = (m_pageGrid.y - 1) * bs0.y + m_brickInteriorLastL0.y + m_brickHaloL0.y;
        if (m_pageGrid.z > 1) m_atlasVoxelsL0.z = (m_pageGrid.z - 1) * bs0.z + m_brickInteriorLastL0.z + m_brickHaloL0.z;
    }
    ctx.atlasVoxelsL0 = m_atlasVoxelsL0;

    // Per-row expected slice count. Row bz needs source Z in
    // [max(0, bz*64 - halo.z), min(d, bz*64 + 64 + halo.z)). For halo=0
    // (single-brick Z axis) it collapses to [0, d).
    ctx.rowExpected.assign(m_pageGrid.z, 0);
    for (uint32_t bz = 0; bz < m_pageGrid.z; ++bz) {
        const int z_start_i = static_cast<int>(bz * kBrickSize) - static_cast<int>(m_brickHaloL0.z);
        const int z_end_i   = static_cast<int>((bz + 1) * kBrickSize) + static_cast<int>(m_brickHaloL0.z);
        const uint32_t z_start = static_cast<uint32_t>(std::max(0, z_start_i));
        const uint32_t z_end   = std::min(d, static_cast<uint32_t>(std::max(0, z_end_i)));
        ctx.rowExpected[bz] = (z_end > z_start) ? (z_end - z_start) : 0u;
    }
    ctx.rowSliceCount.assign(m_pageGrid.z, 0);
    ctx.rowDone.assign(m_pageGrid.z, 0);
    ctx.pageTable.assign(ctx.totalPageEntries, kEmptySlot);
    ctx.pageOccupancy.assign((ctx.totalPageEntries + 7u) >> 3, 0u);

    // Ring buffer: w*h*kProgressiveRingSlices half values. Init to
    // emptyValueHalf so clamped reads outside the actual populated Z range
    // (shouldn't happen for a well-formed sequence but guards against
    // pushSlice() bugs) return sane values.
    ctx.ring.assign(static_cast<size_t>(w) * h * kProgressiveRingSlices, emptyValueHalf);

    // Slab staging (one atlas Z-row = bs0.z voxels deep).
    const uint32_t tightRow = m_atlasVoxelsL0.x * 2;
#ifdef __EMSCRIPTEN__
    ctx.paddedRowLv = (tightRow + 255u) & ~255u;
#else
    ctx.paddedRowLv = tightRow;
#endif
    ctx.slabDepthVox = bs0.z;
    ctx.slabBytes    = static_cast<uint64_t>(ctx.paddedRowLv) * m_atlasVoxelsL0.y * ctx.slabDepthVox;

    rhi::BufferDesc sbd{};
    sbd.size  = ctx.slabBytes;
    sbd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    sbd.label = "BrickAtlasProgressiveSlab";
    ctx.slabStaging = device->createBuffer(sbd);
    if (!ctx.slabStaging) {
        LOG_ERROR("BrickedVolume") << "progressive slab staging create failed ("
                                    << (ctx.slabBytes >> 20) << " MB)";
        ctx.failed = true;
        return ctx;
    }

    // GPU L0 atlas texture, allocated upfront so per-row copies can target it.
    rhi::TextureDesc td{};
    td.size          = rhi::Extent3D{m_atlasVoxelsL0.x, m_atlasVoxelsL0.y, m_atlasVoxelsL0.z};
    td.dimension     = rhi::TextureDimension::Texture3D;
    td.format        = rhi::TextureFormat::R16Float;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    td.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
    td.label         = "VolumeBrickAtlas";
    m_atlasTexes[0] = device->createTexture(td);
    if (!m_atlasTexes[0]) {
        LOG_ERROR("BrickedVolume") << "progressive atlas texture create failed";
        ctx.failed = true;
        return ctx;
    }
    {
        auto enc = device->createCommandEncoder();
        enc->transitionTextureLayout(m_atlasTexes[0].get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::TransferDst);
        auto cmd = enc->finish();
        queue->submit(cmd.get());
        queue->waitIdle();
    }

    m_usedSlotsPerLod = {0, 0, 0, 0};

    LOG_INFO("BrickedVolume") << "Y1c-full progressive begin: " << w << "x" << h << "x" << d
        << " -> page " << m_pageGrid.x << "x" << m_pageGrid.y << "x" << m_pageGrid.z
        << ", atlas " << m_atlasVoxelsL0.x << "x" << m_atlasVoxelsL0.y << "x" << m_atlasVoxelsL0.z
        << ", slab " << (ctx.slabBytes >> 20) << " MB, ring "
        << ((ctx.ring.size() * sizeof(uint16_t)) >> 20) << " MB";
    return ctx;
}

// Pack + upload every brick in row bz. Called by pushSlice once the row's
// expected slice count is met. Reads from ctx.ring, writes to
// ctx.slabStaging, copies staging -> atlas at dstOffset.z = bz * bs0.z.
static bool packAndUploadRow(BrickedVolume::ProgressiveContext& ctx,
                             BrickedVolume& self,
                             uint32_t bz,
                             std::array<uint32_t, 4>& usedSlotsPerLod,
                             rhi::RHITexture* atlasTex) {
    const uint32_t w = ctx.w, h = ctx.h, d = ctx.d;
    const glm::uvec3 bs0(ctx.brickInteriorL0 + 2u * ctx.brickHaloL0);
    const uint32_t voxelsY_lv = ctx.atlasVoxelsL0.y;
    const uint32_t voxelsX_lv = ctx.atlasVoxelsL0.x;
    const uint32_t paddedRowLv = ctx.paddedRowLv;
    const uint32_t slabDepthVox = ctx.slabDepthVox;
    const uint16_t emptyValueHalf = ctx.emptyValueHalf;

    uint8_t* stagingBytes = static_cast<uint8_t*>(ctx.slabStaging->map());
    if (!stagingBytes) {
        LOG_ERROR("BrickedVolume") << "progressive slab map failed at row " << bz;
        return false;
    }
    // Fill empty for the whole slab (row) first
    for (uint32_t z = 0; z < slabDepthVox; ++z) {
        for (uint32_t y = 0; y < voxelsY_lv; ++y) {
            uint16_t* row = reinterpret_cast<uint16_t*>(
                stagingBytes + (static_cast<uint64_t>(z) * voxelsY_lv + y) * paddedRowLv);
            std::fill(row, row + voxelsX_lv, emptyValueHalf);
        }
    }

    const bool shrink = !(ctx.atlasGrid == ctx.pageGrid) ? false : true;
    // Emit each brick's slot origin in atlas Z relative to the slab start
    for (uint32_t by = 0; by < ctx.pageGrid.y; ++by) {
        for (uint32_t bx = 0; bx < ctx.pageGrid.x; ++bx) {
            const uint32_t pageIdx = (bz * ctx.pageGrid.y + by) * ctx.pageGrid.x + bx;

            // Empty-brick test on the actual slice data now in ring
            if (isInteriorEmptyProgressive(ctx.ring, bx, by, bz, w, h, d, emptyValueHalf)) {
                continue;
            }
            ctx.pageOccupancy[pageIdx >> 3] |= static_cast<uint8_t>(1u << (pageIdx & 7));
            ++ctx.nonEmptyCount;

            // Slot assignment: shrink layout enforced when atlasGrid == pageGrid
            // (the common progressive case). Fallback to uniform counter if the
            // caller passed a different atlasGrid.
            uint32_t slot, sx, sy, sz;
            if (shrink) {
                slot = pageIdx;
                sx = bx; sy = by; sz = bz;
                ++usedSlotsPerLod[0];
            } else {
                slot = usedSlotsPerLod[0]++;
                sx = slot % ctx.atlasGrid.x;
                sy = (slot / ctx.atlasGrid.x) % ctx.atlasGrid.y;
                sz = slot / (ctx.atlasGrid.x * ctx.atlasGrid.y);
                // Slot's atlas Z must live inside this slab; else we've been
                // handed a bad atlasGrid for progressive. Skip brick with
                // warning (renders as empty).
                if (sz != bz) {
                    LOG_WARN("BrickedVolume") << "progressive: uniform slot layout "
                        "assigned brick " << bx << "," << by << "," << bz << " to atlas Z " << sz
                        << " -- brick will render empty (use shrink layout for progressive)";
                    continue;
                }
            }
            ctx.pageTable[pageIdx] = BrickedVolume::encodePage(slot, 0);

            // Per-brick stored dims (shrink truncates last brick's outer halo)
            glm::uvec3 storedThis = bs0;
            if (shrink) {
                if (ctx.pageGrid.x > 1 && bx == ctx.pageGrid.x - 1u)
                    storedThis.x = ctx.brickInteriorLastL0.x + ctx.brickHaloL0.x;
                if (ctx.pageGrid.y > 1 && by == ctx.pageGrid.y - 1u)
                    storedThis.y = ctx.brickInteriorLastL0.y + ctx.brickHaloL0.y;
                if (ctx.pageGrid.z > 1 && bz == ctx.pageGrid.z - 1u)
                    storedThis.z = ctx.brickInteriorLastL0.z + ctx.brickHaloL0.z;
            }

            const uint32_t aOriginX     = sx * bs0.x;
            const uint32_t aOriginY     = sy * bs0.y;
            const uint32_t localOriginZ = 0u; // slab covers only row bz
            const int srcX0 = static_cast<int>(bx * BrickedVolume::kBrickSize)
                              - static_cast<int>(ctx.brickHaloL0.x);
            const int srcY0 = static_cast<int>(by * BrickedVolume::kBrickSize)
                              - static_cast<int>(ctx.brickHaloL0.y);
            const int srcZ0 = static_cast<int>(bz * BrickedVolume::kBrickSize)
                              - static_cast<int>(ctx.brickHaloL0.z);
            for (uint32_t lz = 0; lz < storedThis.z; ++lz) {
                for (uint32_t ly = 0; ly < storedThis.y; ++ly) {
                    uint16_t* dstRow = reinterpret_cast<uint16_t*>(
                        stagingBytes
                        + (static_cast<uint64_t>(localOriginZ + lz) * voxelsY_lv
                           + (aOriginY + ly)) * paddedRowLv
                        + aOriginX * 2);
                    const int srcZ = srcZ0 + static_cast<int>(lz);
                    const int srcY = srcY0 + static_cast<int>(ly);
                    for (uint32_t lx = 0; lx < storedThis.x; ++lx) {
                        const int srcXcoord = srcX0 + static_cast<int>(lx);
                        dstRow[lx] = readRingClamped(ctx.ring, w, h, d, srcXcoord, srcY, srcZ);
                    }
                }
            }
        }
    }
    ctx.slabStaging->unmap();

    // Copy this slab to atlas at Z = bz * bs0.z. Depth = actual atlas Z
    // covered by this brick row (could be < slabDepthVox for the last row
    // if the last brick's stored Z is smaller than bs0.z, but for shrink
    // layout that shrink is on the last brick only, and the copy extent
    // is bounded by atlasVoxelsL0.z).
    const uint32_t rowAtlasZStart = bz * bs0.z;
    const uint32_t rowAtlasZEnd   = std::min(ctx.atlasVoxelsL0.z, rowAtlasZStart + slabDepthVox);
    const uint32_t rowAtlasDepth  = rowAtlasZEnd - rowAtlasZStart;

    auto enc = ctx.device->createCommandEncoder();
    rhi::BufferTextureCopyInfo bc{};
    bc.buffer = ctx.slabStaging.get();
    bc.offset = 0;
#ifdef __EMSCRIPTEN__
    bc.bytesPerRow  = paddedRowLv;
    bc.rowsPerImage = voxelsY_lv;
#else
    bc.bytesPerRow  = 0;
    bc.rowsPerImage = 0;
#endif
    rhi::TextureCopyInfo tc{};
    tc.texture  = atlasTex;
    tc.mipLevel = 0;
    tc.origin   = {0, 0, static_cast<int32_t>(rowAtlasZStart)};
    tc.aspect   = 0;
    enc->copyBufferToTexture(bc, tc,
                             rhi::Extent3D{voxelsX_lv, voxelsY_lv, rowAtlasDepth});
    auto cmd = enc->finish();
    ctx.queue->submit(cmd.get());
    ctx.queue->waitIdle();
    (void)self;   // BrickedVolume ref currently unused (state lives in ctx + members mutated at end)
    return true;
}

void BrickedVolume::pushSlice(ProgressiveContext& ctx, uint32_t z, const uint16_t* sliceHalf) {
    if (ctx.failed || !sliceHalf || z >= ctx.d) return;

    // Copy slice into ring at (z % kProgressiveRingSlices)
    const size_t sliceCount = static_cast<size_t>(ctx.w) * ctx.h;
    const size_t slot       = static_cast<size_t>(z) % kProgressiveRingSlices;
    std::memcpy(ctx.ring.data() + slot * sliceCount, sliceHalf, sliceCount * sizeof(uint16_t));

    // Bump row counters for every brick row this z contributes to
    for (uint32_t bz = 0; bz < ctx.pageGrid.z; ++bz) {
        if (ctx.rowDone[bz]) continue;
        const int z_start_i = static_cast<int>(bz * kBrickSize) - static_cast<int>(ctx.brickHaloL0.z);
        const int z_end_i   = static_cast<int>((bz + 1) * kBrickSize) + static_cast<int>(ctx.brickHaloL0.z);
        const uint32_t z_start = static_cast<uint32_t>(std::max(0, z_start_i));
        const uint32_t z_end   = std::min(ctx.d, static_cast<uint32_t>(std::max(0, z_end_i)));
        if (z < z_start || z >= z_end) continue;
        ++ctx.rowSliceCount[bz];
        if (ctx.rowSliceCount[bz] < ctx.rowExpected[bz]) continue;

        // Row bz complete -> pack + upload immediately (before the ring
        // slots start getting overwritten by later slices in this row's
        // neighbourhood).
        if (!packAndUploadRow(ctx, *this, bz, m_usedSlotsPerLod, m_atlasTexes[0].get())) {
            ctx.failed = true;
            return;
        }
        ctx.rowDone[bz] = 1;
    }
}

bool BrickedVolume::endProgressiveBuild(ProgressiveContext& ctx) {
    if (ctx.failed || !ctx.device || !ctx.queue) {
        ctx = ProgressiveContext{};
        return false;
    }
    // Verify every row completed
    for (uint32_t bz = 0; bz < ctx.pageGrid.z; ++bz) {
        if (!ctx.rowDone[bz]) {
            LOG_ERROR("BrickedVolume") << "endProgressiveBuild: row " << bz
                << " incomplete (" << ctx.rowSliceCount[bz] << "/" << ctx.rowExpected[bz] << ")";
            ctx = ProgressiveContext{};
            return false;
        }
    }

    // Transition atlas to ShaderReadOnly
    {
        auto enc = ctx.device->createCommandEncoder();
        enc->transitionTextureLayout(m_atlasTexes[0].get(),
                                     rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly);
        auto cmd = enc->finish();
        ctx.queue->submit(cmd.get());
        ctx.queue->waitIdle();
    }

    // Create texture view
    rhi::TextureViewDesc vd{};
    vd.format          = rhi::TextureFormat::R16Float;
    vd.dimension       = rhi::TextureViewDimension::View3D;
    vd.baseMipLevel    = 0;  vd.mipLevelCount   = 1;
    vd.baseArrayLayer  = 0;  vd.arrayLayerCount = 1;
    m_atlasViews[0] = m_atlasTexes[0]->createView(vd);
    if (!m_atlasViews[0]) {
        LOG_ERROR("BrickedVolume") << "endProgressiveBuild: atlas view create failed";
        ctx = ProgressiveContext{};
        return false;
    }

    // Upload page table (small buffer, storage-only so stage through
    // CopySrc/MapWrite -- identical pattern to build())
    m_pageOccupancy = std::move(ctx.pageOccupancy);
    const uint32_t nonEmpty = ctx.nonEmptyCount;
    const uint64_t pageBytes = static_cast<uint64_t>(ctx.totalPageEntries) * sizeof(uint32_t);
    {
        rhi::BufferDesc dstDesc{};
        dstDesc.size  = pageBytes;
        dstDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        dstDesc.label = "VolumeBrickPageTable";
        m_pageTable = ctx.device->createBuffer(dstDesc);
        if (!m_pageTable) {
            LOG_ERROR("BrickedVolume") << "endProgressiveBuild: page table buffer create failed";
            ctx = ProgressiveContext{};
            return false;
        }

        rhi::BufferDesc stagingDesc{};
        stagingDesc.size  = pageBytes;
        stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        stagingDesc.label = "PageTableStaging";
        auto staging = ctx.device->createBuffer(stagingDesc);
        if (!staging) {
            LOG_ERROR("BrickedVolume") << "endProgressiveBuild: page table staging create failed";
            ctx = ProgressiveContext{};
            return false;
        }
        std::memcpy(staging->map(), ctx.pageTable.data(), pageBytes);
        staging->unmap();

        auto enc = ctx.device->createCommandEncoder();
        enc->copyBufferToBuffer(staging.get(), 0, m_pageTable.get(), 0, pageBytes);
        auto cmd = enc->finish();
        ctx.queue->submit(cmd.get());
        ctx.queue->waitIdle();

        m_pageTableHost = std::move(ctx.pageTable);
    }

    LOG_INFO("BrickedVolume") << "Y1c-full progressive built " << ctx.w << "x" << ctx.h << "x" << ctx.d
        << " -> page " << m_pageGrid.x << "x" << m_pageGrid.y << "x" << m_pageGrid.z
        << " (" << nonEmpty << " non-empty), " << usedSlots() << " atlas slots";

    // Destroy context (frees ring + slab staging)
    ctx = ProgressiveContext{};
    return true;
}

// ---------------------------------------------------------------------------
// Disk paging Step 5.2: build from a memory-mapped Int16 / Uint16 source.
// Avoids the half-data vector materialisation that doubles peak RAM during
// the legacy build() ingest. Routes the persistent VoxelSource through the
// mmap region so packBrickToStaging reads file bytes lazily via the OS page
// cache. Only Streaming mode is supported here -- the target use case is
// volumes too large for a single dense atlas; small-volume Static builds
// still go through the legacy build() with halfData.
// ---------------------------------------------------------------------------
bool BrickedVolume::buildFromMmappedSource(rhi::RHIDevice* device, rhi::RHIQueue* queue,
                                           utils::MmappedFile&& mmap,
                                           size_t dataOffset,
                                           VoxelSource::Format format,
                                           float slope, float intercept,
                                           uint32_t w, uint32_t h, uint32_t d,
                                           uint16_t emptyValueRaw,
                                           uint16_t emptyValueHalf,
                                           glm::uvec3 atlasGrid) {
    if (!device || !queue) return false;
    if (w == 0 || h == 0 || d == 0) return false;
    if (!mmap.valid()) {
        LOG_ERROR("BrickedVolume") << "buildFromMmappedSource: mmap not valid";
        return false;
    }
    const size_t voxelCount = static_cast<size_t>(w) * h * d;
    if (dataOffset + voxelCount * 2 > mmap.size()) {
        LOG_ERROR("BrickedVolume") << "buildFromMmappedSource: mmap region too small ("
                                    << mmap.size() << " B, need " << (dataOffset + voxelCount * 2)
                                    << " B at offset " << dataOffset << ")";
        return false;
    }

    m_device = device;
    m_queue  = queue;
    m_mmappedSource = std::move(mmap);
    const uint8_t* raw = m_mmappedSource.data() + dataOffset;
    m_voxelSource = VoxelSource{raw, voxelCount, format, slope, intercept};

    m_volSize  = glm::uvec3(w, h, d);
    m_pageGrid = glm::uvec3((w + kBrickSize - 1) / kBrickSize,
                            (h + kBrickSize - 1) / kBrickSize,
                            (d + kBrickSize - 1) / kBrickSize);
    // Per-axis L0 sizing (see build() for rationale).
    m_brickInteriorL0 = glm::uvec3(
        (m_pageGrid.x == 1) ? w : kBrickSize,
        (m_pageGrid.y == 1) ? h : kBrickSize,
        (m_pageGrid.z == 1) ? d : kBrickSize);
    m_brickHaloL0 = glm::uvec3(m_pageGrid.x > 1 ? 1u : 0u,
                                m_pageGrid.y > 1 ? 1u : 0u,
                                m_pageGrid.z > 1 ? 1u : 0u);
    const uint32_t totalPageEntries = m_pageGrid.x * m_pageGrid.y * m_pageGrid.z;

    // Pre-scan: empty bricks identified by raw 16-bit pattern equality. The
    // mmap pages stream in sequentially during this walk -- OS page cache
    // handles the I/O lazily.
    m_pageOccupancy.assign((totalPageEntries + 7u) >> 3, 0u);
    uint32_t nonEmptyCount = 0;
    for (uint32_t bz = 0; bz < m_pageGrid.z; ++bz) {
        for (uint32_t by = 0; by < m_pageGrid.y; ++by) {
            for (uint32_t bx = 0; bx < m_pageGrid.x; ++bx) {
                if (isInteriorEmpty(static_cast<const uint16_t*>(m_voxelSource.data),
                                    bx, by, bz, w, h, d, emptyValueRaw)) continue;
                const uint32_t pageIdx = (bz * m_pageGrid.y + by) * m_pageGrid.x + bx;
                m_pageOccupancy[pageIdx >> 3] |= static_cast<uint8_t>(1u << (pageIdx & 7));
                ++nonEmptyCount;
            }
        }
    }

    // Atlas auto-size (same policy as build()): start from the page grid
    // and shrink the longest axis until kAutoAtlasBudgetBytes fits.
    if (atlasGrid.x == 0 || atlasGrid.y == 0 || atlasGrid.z == 0) {
        atlasGrid = m_pageGrid;
        const glm::uvec3 bs0Local = m_brickInteriorL0 + 2u * m_brickHaloL0;
        auto atlasBytesOf = [&](glm::uvec3 a) {
            const uint64_t vx = static_cast<uint64_t>(a.x) * bs0Local.x;
            const uint64_t vy = static_cast<uint64_t>(a.y) * bs0Local.y;
            const uint64_t vz = static_cast<uint64_t>(a.z) * bs0Local.z;
            return vx * vy * vz * 2;
        };
        while (atlasBytesOf(atlasGrid) > kAutoAtlasBudgetBytes &&
               (atlasGrid.x > kAutoAtlasMinAxis ||
                atlasGrid.y > kAutoAtlasMinAxis ||
                atlasGrid.z > kAutoAtlasMinAxis)) {
            if (atlasGrid.x >= atlasGrid.y && atlasGrid.x >= atlasGrid.z && atlasGrid.x > kAutoAtlasMinAxis) --atlasGrid.x;
            else if (atlasGrid.y >= atlasGrid.z && atlasGrid.y > kAutoAtlasMinAxis) --atlasGrid.y;
            else if (atlasGrid.z > kAutoAtlasMinAxis) --atlasGrid.z;
            else break;
        }
    }
    m_atlasGrid = atlasGrid;
    const uint32_t totalSlots = atlasGrid.x * atlasGrid.y * atlasGrid.z;

    if (nonEmptyCount <= totalSlots) {
        LOG_ERROR("BrickedVolume") << "buildFromMmappedSource: volume fits Static atlas ("
                                    << nonEmptyCount << " bricks <= " << totalSlots
                                    << " slots) -- use build(halfData) for this size";
        return false;
    }

    // Streaming mode setup. Mirror the build() Streaming branch. Streaming
    // always uses uniform slot layout because packBrickToStaging writes
    // 66^3 per brick; per-axis / last-brick shrink for streaming is a
    // follow-up.
    m_mode = Mode::Streaming;
    m_uniformSlotLayout   = true;
    m_brickInteriorLastL0 = m_brickInteriorL0;
    m_emptyValueHalf = emptyValueHalf;
    m_pageTableHost.assign(totalPageEntries, kEmptySlot);
    for (auto& states : m_slotStates) states.assign(totalSlots, AtlasSlotState{});
    m_pageToSlot.clear();
    m_pageToSlot.reserve(nonEmptyCount);
    m_lodTableHost.assign(totalPageEntries, 0);
    m_usedSlotsPerLod = {0, 0, 0, 0};

    // Allocate the four LOD atlases pre-filled with the empty value. Identical
    // to the corresponding block in build(); when build() is refactored to
    // share this, this duplicate goes away.
    const glm::uvec3 bs0Sf     = brickStoredL0();  // Option C L0 per-axis
    m_atlasVoxelsL0 = m_atlasGrid * bs0Sf;         // Streaming: uniform, no shrink
    const uint32_t lodLevelsToAlloc = kLodLevels;
    uint64_t totalAtlasBytes = 0;
    for (uint32_t lv = 0; lv < lodLevelsToAlloc; ++lv) {
        const uint32_t brickStoredLod = kBrickStoredAtLod(lv);
        const uint32_t voxelsX_lv = (lv == 0) ? m_atlasVoxelsL0.x : (atlasGrid.x * brickStoredLod);
        const uint32_t voxelsY_lv = (lv == 0) ? m_atlasVoxelsL0.y : (atlasGrid.y * brickStoredLod);
        const uint32_t voxelsZ_lv = (lv == 0) ? m_atlasVoxelsL0.z : (atlasGrid.z * brickStoredLod);
        const uint32_t tightRowLv = voxelsX_lv * 2;
#ifdef __EMSCRIPTEN__
        const uint32_t paddedRowLv = (tightRowLv + 255u) & ~255u;
#else
        const uint32_t paddedRowLv = tightRowLv;
#endif
        const uint64_t bytesLv = static_cast<uint64_t>(paddedRowLv) * voxelsY_lv * voxelsZ_lv;
        totalAtlasBytes += bytesLv;

        rhi::TextureDesc td{};
        td.size          = rhi::Extent3D{voxelsX_lv, voxelsY_lv, voxelsZ_lv};
        td.dimension     = rhi::TextureDimension::Texture3D;
        td.format        = rhi::TextureFormat::R16Float;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
        td.label         = "VolumeBrickAtlas";
        m_atlasTexes[lv] = device->createTexture(td);
        if (!m_atlasTexes[lv]) { LOG_ERROR("BrickedVolume") << "atlas L" << lv << " create failed"; return false; }

        // Y1d: fill empty-value directly into the mapped staging buffer,
        // no CPU-side initBufLocal duplicate. Streaming-only path, so no
        // per-brick pack -- updateStreaming pages bricks in later.
        rhi::BufferDesc bd{};
        bd.size = bytesLv;
        bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        bd.label = "BrickAtlasStaging";
        auto staging = device->createBuffer(bd);
        if (!staging) { LOG_ERROR("BrickedVolume") << "atlas staging L" << lv << " create failed"; return false; }
        uint8_t* stagingBytes = static_cast<uint8_t*>(staging->map());
        if (!stagingBytes) { LOG_ERROR("BrickedVolume") << "atlas staging L" << lv << " map failed"; return false; }
        for (uint32_t z = 0; z < voxelsZ_lv; ++z) {
            for (uint32_t y = 0; y < voxelsY_lv; ++y) {
                uint16_t* row = reinterpret_cast<uint16_t*>(
                    stagingBytes + (static_cast<uint64_t>(z) * voxelsY_lv + y) * paddedRowLv);
                std::fill(row, row + voxelsX_lv, emptyValueHalf);
            }
        }
        staging->unmap();

        auto enc = device->createCommandEncoder();
        enc->transitionTextureLayout(m_atlasTexes[lv].get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::TransferDst);
        rhi::BufferTextureCopyInfo bc{};
        bc.buffer = staging.get(); bc.offset = 0;
#ifdef __EMSCRIPTEN__
        bc.bytesPerRow = paddedRowLv; bc.rowsPerImage = voxelsY_lv;
#else
        bc.bytesPerRow = 0; bc.rowsPerImage = 0;
#endif
        rhi::TextureCopyInfo tc{};
        tc.texture = m_atlasTexes[lv].get();
        tc.mipLevel = 0; tc.origin = {0, 0, 0}; tc.aspect = 0;
        enc->copyBufferToTexture(bc, tc, rhi::Extent3D{voxelsX_lv, voxelsY_lv, voxelsZ_lv});
        enc->transitionTextureLayout(m_atlasTexes[lv].get(),
                                     rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly);
        auto cmd = enc->finish();
        queue->submit(cmd.get());
        queue->waitIdle();

        rhi::TextureViewDesc vd{};
        vd.format = rhi::TextureFormat::R16Float;
        vd.dimension = rhi::TextureViewDimension::View3D;
        vd.baseMipLevel = 0; vd.mipLevelCount = 1;
        vd.baseArrayLayer = 0; vd.arrayLayerCount = 1;
        m_atlasViews[lv] = m_atlasTexes[lv]->createView(vd);
        if (!m_atlasViews[lv]) { LOG_ERROR("BrickedVolume") << "atlas view L" << lv << " create failed"; return false; }
    }
    LOG_INFO("BrickedVolume") << "atlas total: " << (totalAtlasBytes >> 20)
        << " MB across " << kLodLevels << " LOD textures (L0 + L1 + L2 + L3)";

    // Page table buffer + initial all-sentinel upload (same shape as build()).
    const uint64_t pageBytes = static_cast<uint64_t>(totalPageEntries) * sizeof(uint32_t);
    {
        rhi::BufferDesc dstDesc{};
        dstDesc.size = pageBytes;
        dstDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        dstDesc.label = "BrickPageTable";
        m_pageTable = device->createBuffer(dstDesc);
        if (!m_pageTable) { LOG_ERROR("BrickedVolume") << "page table buffer create failed"; return false; }

        rhi::BufferDesc stagingDesc{};
        stagingDesc.size = pageBytes;
        stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        stagingDesc.label = "BrickPageTableStaging";
        auto staging = device->createBuffer(stagingDesc);
        if (!staging) { LOG_ERROR("BrickedVolume") << "page table staging create failed"; return false; }
        std::memcpy(staging->map(), m_pageTableHost.data(), pageBytes);
        staging->unmap();

        auto enc = device->createCommandEncoder();
        enc->copyBufferToBuffer(staging.get(), 0, m_pageTable.get(), 0, pageBytes);
        auto cmd = enc->finish();
        queue->submit(cmd.get());
        queue->waitIdle();
    }

    LOG_INFO("BrickedVolume") << "built " << w << "x" << h << "x" << d
        << " -> page " << m_pageGrid.x << "x" << m_pageGrid.y << "x" << m_pageGrid.z
        << " (" << nonEmptyCount << " non-empty), 0/" << totalSlots
        << " atlas slots, mode = Streaming (mmap source, "
        << (format == VoxelSource::Format::Int16 ? "Int16" : "Uint16")
        << ", slope=" << slope << " intercept=" << intercept << ")";
    return true;
}

// ---------------------------------------------------------------------------
// v1-3 streaming: per-frame brick page-in + LRU eviction + page-table push.
// ---------------------------------------------------------------------------
namespace {

// Per-brick staging byte layout, accounting for the WebGPU 256-byte row stride
// requirement (see CLAUDE.md section 9). Returned values are used both at
// staging-buffer allocation time and at copyBufferToTexture descriptor fill.
struct BrickStagingLayout {
    uint32_t brickStored      = 0;   // kBrickStored at this LOD (66/34/18/10)
    uint32_t paddedBytesPerRow = 0;
    uint64_t totalBytes        = 0;
};
inline BrickStagingLayout computeBrickLayout(uint32_t lod) {
    const uint32_t stored = BrickedVolume::kBrickStoredAtLod(lod);
    const uint32_t tightRow = stored * 2;  // R16Float = 2 B
#ifdef __EMSCRIPTEN__
    const uint32_t paddedRow = (tightRow + 255u) & ~255u;
#else
    const uint32_t paddedRow = tightRow;
#endif
    BrickStagingLayout L;
    L.brickStored       = stored;
    L.paddedBytesPerRow = paddedRow;
    L.totalBytes        = static_cast<uint64_t>(paddedRow) * stored * stored;
    return L;
}

// Pack one virtual brick at the requested LOD from the L0 voxel source into a
// freshly mapped staging buffer with the WebGPU-aligned row stride. For lod==0
// + HalfFloat + interior the brick is a direct slab copy; otherwise each output
// voxel is the box-filter average of factor^3 L0 voxels (factor = 1 << lod),
// reading each via VoxelSource::read so int16 / uint16 sources (Step 5.2) flow
// through the same loop with format dispatched at the per-voxel read.
// brickSizeLod = kBrickSize >> lod (64,32,16,8) and brickStoredLod = brickSizeLod
// + 2 are the per-brick voxel extents. srcW/srcH/srcD are always the L0 dims.
void packBrickToStaging(const BrickedVolume::VoxelSource& src,
                        uint32_t srcW, uint32_t srcH, uint32_t srcD,
                        uint32_t bx, uint32_t by, uint32_t bz,
                        uint8_t lod,
                        uint8_t* mapped,
                        uint32_t paddedBytesPerRow) {
    using Format = BrickedVolume::VoxelSource::Format;

    const uint32_t brickSizeLod   = BrickedVolume::kBrickSize >> lod;       // 64, 32, 16, 8
    const uint32_t brickStoredLod = brickSizeLod + 2;                       // 66, 34, 18, 10
    const uint32_t factor         = 1u << lod;                              // 1, 2, 4, 8
    const int srcX0 = static_cast<int>(bx * BrickedVolume::kBrickSize) - static_cast<int>(factor);
    const int srcY0 = static_cast<int>(by * BrickedVolume::kBrickSize) - static_cast<int>(factor);
    const int srcZ0 = static_cast<int>(bz * BrickedVolume::kBrickSize) - static_cast<int>(factor);
    const int srcSpan = static_cast<int>(brickStoredLod * factor);          // L0 voxels covered per axis

    const bool interior =
        srcX0 >= 0 && srcY0 >= 0 && srcZ0 >= 0 &&
        srcX0 + srcSpan <= static_cast<int>(srcW) &&
        srcY0 + srcSpan <= static_cast<int>(srcH) &&
        srcZ0 + srcSpan <= static_cast<int>(srcD);

    // HalfFloat + LOD 0 + interior: contiguous row-memcpy of raw 16-bit half
    // bytes -- the source bits are already in the destination wire format.
    // The fast path from commit 6c846fd, gated on the format check.
    if (lod == 0 && interior && src.format == Format::HalfFloat) {
        const uint16_t* halfSrc = static_cast<const uint16_t*>(src.data);
        const size_t rowBytes = static_cast<size_t>(brickStoredLod) * sizeof(uint16_t);
        for (uint32_t lz = 0; lz < brickStoredLod; ++lz) {
            const size_t srcZIdx = static_cast<size_t>(srcZ0 + static_cast<int>(lz));
            for (uint32_t ly = 0; ly < brickStoredLod; ++ly) {
                const size_t srcYIdx = static_cast<size_t>(srcY0 + static_cast<int>(ly));
                const uint16_t* srcRow =
                    halfSrc + (srcZIdx * srcH + srcYIdx) * srcW + static_cast<size_t>(srcX0);
                uint8_t* dstRow =
                    mapped + (static_cast<uint64_t>(lz) * brickStoredLod + ly) * paddedBytesPerRow;
                std::memcpy(dstRow, srcRow, rowBytes);
            }
        }
        return;
    }

    // Per-voxel path: LOD > 0 (any format), boundary brick, or non-HalfFloat
    // source. read() returns the voxel in physical units (e.g. HU) regardless
    // of format; we sum + average + pack to half here.
    auto sample = [&](int x, int y, int z) -> float {
        x = std::clamp(x, 0, static_cast<int>(srcW) - 1);
        y = std::clamp(y, 0, static_cast<int>(srcH) - 1);
        z = std::clamp(z, 0, static_cast<int>(srcD) - 1);
        const size_t idx = (static_cast<size_t>(z) * srcH + y) * srcW + x;
        switch (src.format) {
        case Format::HalfFloat:
            return glm::unpackHalf1x16(static_cast<const uint16_t*>(src.data)[idx]);
        case Format::Int16:
            return src.slope * static_cast<float>(static_cast<const int16_t*>(src.data)[idx])
                 + src.intercept;
        case Format::Uint16:
            return src.slope * static_cast<float>(static_cast<const uint16_t*>(src.data)[idx])
                 + src.intercept;
        }
        return 0.0f;
    };
    const float scale = 1.0f / static_cast<float>(factor * factor * factor);
    for (uint32_t lz = 0; lz < brickStoredLod; ++lz) {
        const int srcZBase = srcZ0 + static_cast<int>(lz * factor);
        for (uint32_t ly = 0; ly < brickStoredLod; ++ly) {
            const int srcYBase = srcY0 + static_cast<int>(ly * factor);
            uint16_t* dstRow = reinterpret_cast<uint16_t*>(
                mapped + (static_cast<uint64_t>(lz) * brickStoredLod + ly) * paddedBytesPerRow);
            for (uint32_t lx = 0; lx < brickStoredLod; ++lx) {
                const int srcXBase = srcX0 + static_cast<int>(lx * factor);
                float sum = 0.0f;
                for (uint32_t dz = 0; dz < factor; ++dz) {
                    for (uint32_t dy = 0; dy < factor; ++dy) {
                        for (uint32_t dx = 0; dx < factor; ++dx) {
                            sum += sample(srcXBase + static_cast<int>(dx),
                                          srcYBase + static_cast<int>(dy),
                                          srcZBase + static_cast<int>(dz));
                        }
                    }
                }
                dstRow[lx] = glm::packHalf1x16(sum * scale);
            }
        }
    }
}

} // namespace

BrickedVolume::StreamPerFrameStats
BrickedVolume::updateStreaming(const std::vector<uint32_t>& visiblePageIndices,
                               uint64_t frameIdx) {
    StreamPerFrameStats stats{};
    if (m_mode != Mode::Streaming) return stats;
    if (!m_device || !m_queue) return stats;

    // ------------------------------------------------------------------
    // 1. Walk the visible set once. For each non-empty visible brick:
    //    - resident at ANY LOD -> bump lastFrameUsed (no migration)
    //    - not resident -> queue an upload at the desired LOD
    // No migration: once a brick is in the atlas it stays at its original LOD
    // until LRU evicts it (only when not visible). This keeps the visual
    // stable while the camera moves -- selection only steers NEW uploads.
    // Stale-LOD residents render slightly blurry rather than as holes.
    // ------------------------------------------------------------------
    struct NewlyNeeded { uint32_t pageIdx; uint8_t lod; };
    std::vector<NewlyNeeded> newlyNeeded;
    newlyNeeded.reserve(std::min<size_t>(visiblePageIndices.size(),
                                          kStreamUploadsPerFrame * 4u));
    for (uint32_t pageIdx : visiblePageIndices) {
        if (!pageHasData(pageIdx)) continue;
        auto it = m_pageToSlot.find(pageIdx);
        if (it != m_pageToSlot.end()) {
            const uint32_t residentSlot = it->second.first;
            const uint8_t  residentLod  = it->second.second;
            m_slotStates[residentLod][residentSlot].lastFrameUsed = frameIdx;
            continue;
        }
        newlyNeeded.push_back({pageIdx, m_lodTableHost[pageIdx]});
    }
    if (newlyNeeded.empty()) return stats;

    // v1-beta beta-4: prioritise lower LOD (closer / higher detail) so the
    // K-per-frame budget spends on bricks the viewer most needs to see first.
    // Without this the iteration order in the cull (bz->by->bx) puts the
    // far-side L3 bricks before the central L0 bricks, and central detail
    // (e.g. the bone core of a CT phantom) takes many frames to migrate in.
    std::sort(newlyNeeded.begin(), newlyNeeded.end(),
              [](const NewlyNeeded& a, const NewlyNeeded& b) {
                  return a.lod < b.lod;
              });

    const uint32_t totalSlots = m_atlasGrid.x * m_atlasGrid.y * m_atlasGrid.z;
    const uint32_t budget = std::min<uint32_t>(static_cast<uint32_t>(newlyNeeded.size()),
                                                kStreamUploadsPerFrame);

    // ------------------------------------------------------------------
    // 2. For each newly-needed brick, pick a target slot AT THE CHOSEN LOD,
    //    falling back to coarser LODs (lod+1, lod+2, ...) if that atlas has
    //    no free or LRU-evictable slot. Together with "no migration" this
    //    means every visible brick gets *some* LOD as long as the union of
    //    L0..L3 atlases has space, so the shader never returns the sentinel
    //    for a brick that should be visible.
    //    Each LOD has its own slot space (m_slotStates[lod] has totalSlots
    //    entries). Preference: empty slot; else LRU-evict from non-visible
    //    residents (lastFrameUsed != frameIdx). Visible-this-frame slots are
    //    never evicted.
    // ------------------------------------------------------------------
    struct UploadTarget { uint32_t pageIdx; uint32_t slot; uint8_t lod; };
    std::vector<UploadTarget> targets;
    targets.reserve(budget);

    auto pickSlotForLod = [&](uint8_t lod) -> uint32_t {
        auto& states = m_slotStates[lod];
        for (uint32_t s = 0; s < totalSlots; ++s) {
            if (states[s].residentPageIdx == kEmptySlot) return s;
        }
        uint64_t bestFrame = UINT64_MAX;
        uint32_t bestSlot  = kEmptySlot;
        for (uint32_t s = 0; s < totalSlots; ++s) {
            if (states[s].residentPageIdx != kEmptySlot &&
                states[s].lastFrameUsed != frameIdx &&
                states[s].lastFrameUsed < bestFrame) {
                bestFrame = states[s].lastFrameUsed;
                bestSlot  = s;
            }
        }
        return bestSlot;
    };

    for (uint32_t i = 0; i < budget; ++i) {
        const uint32_t pageIdx = newlyNeeded[i].pageIdx;
        uint32_t pickedSlot = kEmptySlot;
        uint8_t  pickedLod  = newlyNeeded[i].lod;
        for (uint8_t lod = newlyNeeded[i].lod; lod < kLodLevels; ++lod) {
            pickedSlot = pickSlotForLod(lod);
            if (pickedSlot != kEmptySlot) { pickedLod = lod; break; }
        }
        if (pickedSlot == kEmptySlot) continue;  // every LOD atlas full of visible bricks
        targets.push_back({pageIdx, pickedSlot, pickedLod});
    }

    const uint32_t uploads = static_cast<uint32_t>(targets.size());
    if (uploads == 0) return stats;

    // ------------------------------------------------------------------
    // 3. Record the upload pass. With multi-LOD destinations we transition
    //    each LOD atlas once (only LODs that actually receive an upload).
    // ------------------------------------------------------------------
    const uint32_t frameSlot = static_cast<uint32_t>(frameIdx % kStreamingFramesInFlight);

    std::array<bool, kLodLevels> lodTouched = {false, false, false, false};
    for (const auto& t : targets) lodTouched[t.lod] = true;

    auto enc = m_device->createCommandEncoder();
    for (uint32_t lv = 0; lv < kLodLevels; ++lv) {
        if (lodTouched[lv]) {
            enc->transitionTextureLayout(m_atlasTexes[lv].get(),
                                         rhi::TextureLayout::ShaderReadOnly,
                                         rhi::TextureLayout::TransferDst);
        }
    }

    for (uint32_t i = 0; i < uploads; ++i) {
        const uint32_t pageIdx = targets[i].pageIdx;
        const uint32_t slot    = targets[i].slot;
        const uint8_t  lod     = targets[i].lod;
        auto& states = m_slotStates[lod];
        const BrickStagingLayout L = computeBrickLayout(lod);

        // Evict previous resident of this slot at this LOD, if any.
        const uint32_t prevPage = states[slot].residentPageIdx;
        if (prevPage != kEmptySlot) {
            m_pageToSlot.erase(prevPage);
            m_pageTableHost[prevPage] = kEmptySlot;
            ++stats.bricksEvicted;
        } else {
            ++m_usedSlotsPerLod[lod];
        }

        // Lazy-alloc the staging slot at L0 size (largest LOD); smaller LODs
        // just write a prefix. Slots are indexed by frame + upload index.
        auto& stagingPtr = m_brickStaging[frameSlot][i];
        if (!stagingPtr) {
            const BrickStagingLayout L0 = computeBrickLayout(0);
            rhi::BufferDesc bd{};
            bd.size  = L0.totalBytes;  // sized for the largest LOD's brick
            bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
            bd.label = "BrickStreamStaging";
            stagingPtr = m_device->createBuffer(bd);
            if (!stagingPtr) {
                LOG_ERROR("BrickedVolume") << "brick staging alloc failed";
                return stats;
            }
        }

        // Decompose pageIdx into virtual-brick coords.
        const uint32_t bx = pageIdx % m_pageGrid.x;
        const uint32_t by = (pageIdx / m_pageGrid.x) % m_pageGrid.y;
        const uint32_t bz =  pageIdx / (m_pageGrid.x * m_pageGrid.y);

        // CPU-pack from the L0 voxel source. The VoxelSource carries the
        // format (HalfFloat now; Int16 / Uint16 in Step 5.2) and the rescale
        // slope/intercept; packBrickToStaging dispatches at the per-voxel read.
        const VoxelSource srcData = voxelSourceL0();
        uint8_t* mapped = static_cast<uint8_t*>(stagingPtr->map());
        packBrickToStaging(srcData, m_volSize.x, m_volSize.y, m_volSize.z,
                           bx, by, bz, lod,
                           mapped, L.paddedBytesPerRow);
        stagingPtr->unmap();

        // Atlas slot origin in voxels (LOD-specific stride).
        const uint32_t sx = slot % m_atlasGrid.x;
        const uint32_t sy = (slot / m_atlasGrid.x) % m_atlasGrid.y;
        const uint32_t sz =  slot / (m_atlasGrid.x * m_atlasGrid.y);

        rhi::BufferTextureCopyInfo bc{};
        bc.buffer = stagingPtr.get();
        bc.offset = 0;
#ifdef __EMSCRIPTEN__
        bc.bytesPerRow  = L.paddedBytesPerRow;
        bc.rowsPerImage = L.brickStored;
#else
        bc.bytesPerRow  = 0;
        bc.rowsPerImage = 0;
#endif
        rhi::TextureCopyInfo tc{};
        tc.texture  = m_atlasTexes[lod].get();
        tc.mipLevel = 0;
        tc.origin   = {static_cast<int32_t>(sx * L.brickStored),
                       static_cast<int32_t>(sy * L.brickStored),
                       static_cast<int32_t>(sz * L.brickStored)};
        tc.aspect   = 0;
        enc->copyBufferToTexture(bc, tc,
            rhi::Extent3D{L.brickStored, L.brickStored, L.brickStored});

        // Update CPU mirrors. m_pageTableHost only reflects L0 residents; the
        // current shader only knows about L0, so non-L0 bricks read as
        // sentinel and appear as holes until beta-5 makes the shader sample
        // the chosen LOD's atlas.
        states[slot] = AtlasSlotState{pageIdx, lod, frameIdx};
        m_pageToSlot[pageIdx] = {slot, lod};
        m_pageTableHost[pageIdx] = encodePage(slot, lod);
        ++stats.bricksUploaded;
    }

    for (uint32_t lv = 0; lv < kLodLevels; ++lv) {
        if (lodTouched[lv]) {
            enc->transitionTextureLayout(m_atlasTexes[lv].get(),
                                         rhi::TextureLayout::TransferDst,
                                         rhi::TextureLayout::ShaderReadOnly);
        }
    }

    // ------------------------------------------------------------------
    // 4. Push the page-table mirror to the GPU. Full re-upload: at typical
    //    page-grid sizes (<= 16x16x8 = 8 KB) the cost is negligible
    //    compared to per-frame brick copies.
    // ------------------------------------------------------------------
    {
        auto& ptStaging = m_pageStaging[frameSlot];
        const uint64_t pageBytes = pageTableSize();
        if (!ptStaging) {
            rhi::BufferDesc bd{};
            bd.size  = pageBytes;
            bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
            bd.label = "PageTableStreamStaging";
            ptStaging = m_device->createBuffer(bd);
            if (!ptStaging) {
                LOG_ERROR("BrickedVolume") << "page table staging alloc failed";
                return stats;
            }
        }
        std::memcpy(ptStaging->map(), m_pageTableHost.data(), pageBytes);
        ptStaging->unmap();
        enc->copyBufferToBuffer(ptStaging.get(), 0, m_pageTable.get(), 0, pageBytes);
    }

    auto cmd = enc->finish();
    m_queue->submit(cmd.get());
    // Do NOT waitIdle here -- the frame-in-flight model handles synchronisation
    // and the next frame's reuse of m_brickStaging[frameSlot] is safe because
    // kStreamingFramesInFlight queue submits precede that reuse.

    return stats;
}

} // namespace rendering
