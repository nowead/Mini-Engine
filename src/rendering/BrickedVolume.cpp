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
                          const std::vector<uint16_t>& halfData,
                          uint32_t w, uint32_t h, uint32_t d,
                          uint16_t emptyValueHalf,
                          glm::uvec3 atlasGrid) {
    if (!device || !queue) return false;
    if (w == 0 || h == 0 || d == 0) return false;
    if (halfData.size() < static_cast<size_t>(w) * h * d) {
        LOG_ERROR("BrickedVolume") << "halfData too small for " << w << "x" << h << "x" << d;
        return false;
    }
    // Cache for v1-3 updateStreaming.
    m_device = device;
    m_queue  = queue;

    // Build using a temporary VoxelSource view over the input parameter; only
    // the Streaming branch below copies it into m_originalHalfData + sets
    // m_voxelSource (the Static path never reads the source again after
    // atlas pack, so paying the copy there would just waste RAM).
    const VoxelSource buildView{halfData.data(), halfData.size(),
                                VoxelSource::Format::HalfFloat,
                                1.0f, 0.0f};
    const uint16_t emptyValueRaw = emptyValueHalf;  // HalfFloat path: raw == half bits

    m_volSize   = glm::uvec3(w, h, d);
    m_pageGrid  = glm::uvec3((w + kBrickSize - 1) / kBrickSize,
                             (h + kBrickSize - 1) / kBrickSize,
                             (d + kBrickSize - 1) / kBrickSize);
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

    // Auto-size: pick the smallest balanced atlas that holds every non-empty
    // brick (so Static wins whenever feasible), bounded by kAutoAtlasBudgetBytes
    // to keep VRAM allocation reasonable. The cube-root start point keeps the
    // atlas shape close to cubic, and we shrink the longest axis until we fit
    // the budget if we overshoot. Caller can pass an explicit atlasGrid to
    // override the policy entirely.
    if (atlasGrid.x == 0 || atlasGrid.y == 0 || atlasGrid.z == 0) {
        const uint32_t axisGuess = std::max<uint32_t>(
            kAutoAtlasMinAxis,
            static_cast<uint32_t>(std::ceil(std::cbrt(static_cast<double>(std::max(nonEmptyCount, 1u))))));
        atlasGrid = glm::uvec3(std::min(axisGuess, m_pageGrid.x),
                               std::min(axisGuess, m_pageGrid.y),
                               std::min(axisGuess, m_pageGrid.z));
        auto atlasBytesOf = [](glm::uvec3 a) {
            const uint64_t vx = static_cast<uint64_t>(a.x) * kBrickStored;
            const uint64_t vy = static_cast<uint64_t>(a.y) * kBrickStored;
            const uint64_t vz = static_cast<uint64_t>(a.z) * kBrickStored;
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

    const uint32_t atlasVoxelsX = atlasGrid.x * kBrickStored;
    const uint32_t atlasVoxelsY = atlasGrid.y * kBrickStored;
    const uint32_t atlasVoxelsZ = atlasGrid.z * kBrickStored;
    const uint32_t totalSlots   = atlasGrid.x * atlasGrid.y * atlasGrid.z;

    // ------------------------------------------------------------------
    // 1. Build the atlas image + page table in CPU memory.
    // ------------------------------------------------------------------
    // Atlas storage: row-major (x-fastest) with WebGPU's 256-byte row alignment.
    const uint32_t tightBytesPerRow = atlasVoxelsX * 2;
#ifdef __EMSCRIPTEN__
    const uint32_t paddedBytesPerRow = (tightBytesPerRow + 255u) & ~255u;
#else
    const uint32_t paddedBytesPerRow = tightBytesPerRow;
#endif
    const uint64_t atlasBytes =
        static_cast<uint64_t>(paddedBytesPerRow) * atlasVoxelsY * atlasVoxelsZ;

    // Initialise atlas with the empty value -- bricks we never write stay "air".
    // (uint16_t per voxel; memset works for byte fills, so use std::fill on a
    // uint16-typed view of the buffer for correctness.)
    std::vector<uint8_t> atlasBuf;
    atlasBuf.resize(atlasBytes);
    {
        // Fill each row's "live" prefix with emptyValueHalf; padding stays 0
        // (never sampled because atlas dims are exactly atlasVoxelsX-wide).
        for (uint32_t z = 0; z < atlasVoxelsZ; ++z) {
            for (uint32_t y = 0; y < atlasVoxelsY; ++y) {
                uint16_t* row = reinterpret_cast<uint16_t*>(
                    atlasBuf.data() + (static_cast<uint64_t>(z) * atlasVoxelsY + y) * paddedBytesPerRow);
                std::fill(row, row + atlasVoxelsX, emptyValueHalf);
            }
        }
    }

    std::vector<uint32_t> pageTable(totalPageEntries, kEmptySlot);

    // ------------------------------------------------------------------
    // Mode decision. Static when every non-empty brick fits the atlas
    // (v0 behaviour); Streaming when not. v1-3 alpha: the pre-scan already
    // ran (before atlas auto-sizing) so we just compare counts here.
    // ------------------------------------------------------------------
    if (nonEmptyCount <= totalSlots) {
        m_mode = Mode::StaticFullyLoaded;
    } else {
        m_mode = Mode::Streaming;
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

    // ------------------------------------------------------------------
    // 1c. Static-only packing pass. Streaming mode skips this entirely;
    // atlasBuf stays filled with emptyValueHalf and pageTable stays
    // all-sentinel (the empty atlas the shader will sample until v1-3
    // pages bricks in).
    // ------------------------------------------------------------------
    m_usedSlotsPerLod = {0, 0, 0, 0};
    if (m_mode == Mode::StaticFullyLoaded) {
        for (uint32_t bz = 0; bz < m_pageGrid.z; ++bz) {
            for (uint32_t by = 0; by < m_pageGrid.y; ++by) {
                for (uint32_t bx = 0; bx < m_pageGrid.x; ++bx) {
                    const uint32_t pageIdx = (bz * m_pageGrid.y + by) * m_pageGrid.x + bx;
                    const bool hasData = (m_pageOccupancy[pageIdx >> 3] >> (pageIdx & 7)) & 1u;
                    if (!hasData) continue;

                    const uint32_t slot = m_usedSlotsPerLod[0]++;
                    pageTable[pageIdx] = encodePage(slot, 0);  // Static always L0

                    // Atlas slot origin (in voxels) for linear unpacking:
                    // slot = sx + sy*Ax + sz*Ax*Ay.
                    const uint32_t sx = slot % atlasGrid.x;
                    const uint32_t sy = (slot / atlasGrid.x) % atlasGrid.y;
                    const uint32_t sz =  slot / (atlasGrid.x * atlasGrid.y);
                    const uint32_t aOriginX = sx * kBrickStored;
                    const uint32_t aOriginY = sy * kBrickStored;
                    const uint32_t aOriginZ = sz * kBrickStored;

                    // Copy the 66^3 brick (interior + 1-voxel halo) from source.
                    const int srcX0 = static_cast<int>(bx * kBrickSize) - 1;
                    const int srcY0 = static_cast<int>(by * kBrickSize) - 1;
                    const int srcZ0 = static_cast<int>(bz * kBrickSize) - 1;
                    for (uint32_t lz = 0; lz < kBrickStored; ++lz) {
                        for (uint32_t ly = 0; ly < kBrickStored; ++ly) {
                            uint16_t* dstRow = reinterpret_cast<uint16_t*>(
                                atlasBuf.data()
                                + (static_cast<uint64_t>(aOriginZ + lz) * atlasVoxelsY
                                   + (aOriginY + ly)) * paddedBytesPerRow
                                + aOriginX * 2);
                            const int srcZ = srcZ0 + static_cast<int>(lz);
                            const int srcY = srcY0 + static_cast<int>(ly);
                            for (uint32_t lx = 0; lx < kBrickStored; ++lx) {
                                const int srcXcoord = srcX0 + static_cast<int>(lx);
                                dstRow[lx] = srcVoxelHalfBits(buildView, srcXcoord, srcY, srcZ, w, h, d);
                            }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // 1d. Streaming-mode state setup. m_usedSlots stays 0 (no resident
    // bricks yet); v1-3 will increment it as pages stream in.
    // ------------------------------------------------------------------
    if (m_mode == Mode::Streaming) {
        m_originalHalfData = halfData;            // copy for v1-3 brick extraction
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
    // Static mode allocates only L0 and packs the brick data into it.
    // Streaming mode allocates all four LODs; L0 starts empty (filled by
    // streaming page-ins), L1..L3 are empty-initialised so the shader can
    // sample them safely once beta-5 binds them (until then they go unused).
    // ------------------------------------------------------------------
    const uint32_t lodLevelsToAlloc = (m_mode == Mode::Streaming) ? kLodLevels : 1u;
    uint64_t totalAtlasBytes = 0;
    for (uint32_t lv = 0; lv < lodLevelsToAlloc; ++lv) {
        const uint32_t brickStoredLod = kBrickStoredAtLod(lv);
        const uint32_t voxelsX_lv = atlasGrid.x * brickStoredLod;
        const uint32_t voxelsY_lv = atlasGrid.y * brickStoredLod;
        const uint32_t voxelsZ_lv = atlasGrid.z * brickStoredLod;
        const uint32_t tightRowLv = voxelsX_lv * 2;
#ifdef __EMSCRIPTEN__
        const uint32_t paddedRowLv = (tightRowLv + 255u) & ~255u;
#else
        const uint32_t paddedRowLv = tightRowLv;
#endif
        const uint64_t bytesLv =
            static_cast<uint64_t>(paddedRowLv) * voxelsY_lv * voxelsZ_lv;
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
        if (!m_atlasTexes[lv]) {
            LOG_ERROR("BrickedVolume") << "atlas texture L" << lv << " create failed"; return false;
        }

        // Build the CPU-side init buffer:
        //   L0: use the prebuilt atlasBuf (already empty-filled, packed if Static)
        //   L1..L3: allocate fresh, fill with emptyValueHalf row-by-row
        std::vector<uint8_t> initBufLocal;
        const uint8_t* initBytes = nullptr;
        if (lv == 0) {
            initBytes = atlasBuf.data();
        } else {
            initBufLocal.resize(bytesLv);
            for (uint32_t z = 0; z < voxelsZ_lv; ++z) {
                for (uint32_t y = 0; y < voxelsY_lv; ++y) {
                    uint16_t* row = reinterpret_cast<uint16_t*>(
                        initBufLocal.data()
                        + (static_cast<uint64_t>(z) * voxelsY_lv + y) * paddedRowLv);
                    std::fill(row, row + voxelsX_lv, emptyValueHalf);
                }
            }
            initBytes = initBufLocal.data();
        }

        rhi::BufferDesc bd{};
        bd.size  = bytesLv;
        bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        bd.label = "BrickAtlasStaging";
        auto staging = device->createBuffer(bd);
        if (!staging) {
            LOG_ERROR("BrickedVolume") << "atlas staging L" << lv << " create failed"; return false;
        }
        std::memcpy(staging->map(), initBytes, bytesLv);
        staging->unmap();

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
    if (m_mode == Mode::Streaming) {
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

    // Atlas auto-size (same policy as build()).
    if (atlasGrid.x == 0 || atlasGrid.y == 0 || atlasGrid.z == 0) {
        const uint32_t axisGuess = std::max<uint32_t>(
            kAutoAtlasMinAxis,
            static_cast<uint32_t>(std::ceil(std::cbrt(static_cast<double>(std::max(nonEmptyCount, 1u))))));
        atlasGrid = glm::uvec3(std::min(axisGuess, m_pageGrid.x),
                               std::min(axisGuess, m_pageGrid.y),
                               std::min(axisGuess, m_pageGrid.z));
        auto atlasBytesOf = [](glm::uvec3 a) {
            const uint64_t vx = static_cast<uint64_t>(a.x) * kBrickStored;
            const uint64_t vy = static_cast<uint64_t>(a.y) * kBrickStored;
            const uint64_t vz = static_cast<uint64_t>(a.z) * kBrickStored;
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

    // Streaming mode setup. Mirror the build() Streaming branch.
    m_mode = Mode::Streaming;
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
    const uint32_t lodLevelsToAlloc = kLodLevels;
    uint64_t totalAtlasBytes = 0;
    for (uint32_t lv = 0; lv < lodLevelsToAlloc; ++lv) {
        const uint32_t brickStoredLod = kBrickStoredAtLod(lv);
        const uint32_t voxelsX_lv = atlasGrid.x * brickStoredLod;
        const uint32_t voxelsY_lv = atlasGrid.y * brickStoredLod;
        const uint32_t voxelsZ_lv = atlasGrid.z * brickStoredLod;
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

        std::vector<uint8_t> initBufLocal;
        initBufLocal.resize(bytesLv);
        for (uint32_t z = 0; z < voxelsZ_lv; ++z) {
            for (uint32_t y = 0; y < voxelsY_lv; ++y) {
                uint16_t* row = reinterpret_cast<uint16_t*>(
                    initBufLocal.data() + (static_cast<uint64_t>(z) * voxelsY_lv + y) * paddedRowLv);
                std::fill(row, row + voxelsX_lv, emptyValueHalf);
            }
        }

        rhi::BufferDesc bd{};
        bd.size = bytesLv;
        bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        bd.label = "BrickAtlasStaging";
        auto staging = device->createBuffer(bd);
        if (!staging) { LOG_ERROR("BrickedVolume") << "atlas staging L" << lv << " create failed"; return false; }
        std::memcpy(staging->map(), initBufLocal.data(), bytesLv);
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
