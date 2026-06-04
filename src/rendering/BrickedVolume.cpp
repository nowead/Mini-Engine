#include "BrickedVolume.hpp"

#include "src/utils/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace rendering {

namespace {

// Source voxel fetch with edge clamping (so brick halos at the volume boundary
// replicate the edge voxel instead of reading out-of-bounds).
inline uint16_t srcVoxel(const std::vector<uint16_t>& src,
                         int x, int y, int z,
                         uint32_t W, uint32_t H, uint32_t D) {
    x = std::clamp(x, 0, static_cast<int>(W) - 1);
    y = std::clamp(y, 0, static_cast<int>(H) - 1);
    z = std::clamp(z, 0, static_cast<int>(D) - 1);
    return src[(static_cast<size_t>(z) * H + y) * W + x];
}

// True iff every interior voxel of the brick at (bx, by, bz) equals emptyValueHalf.
// "Interior" = the 64^3 voxels actually owned by the brick; the halo is ignored
// for the empty test (a brick whose interior is air but whose halo would have
// data is still empty: the *neighbour* brick is the one that needs the gradient,
// and ITS halo brings in our zeros).
bool isInteriorEmpty(const std::vector<uint16_t>& src,
                     uint32_t bx, uint32_t by, uint32_t bz,
                     uint32_t W, uint32_t H, uint32_t D,
                     uint16_t emptyValueHalf) {
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
                if (src[row + x] != emptyValueHalf) return false;
            }
        }
    }
    return true;
}

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

    m_volSize   = glm::uvec3(w, h, d);
    m_pageGrid  = glm::uvec3((w + kBrickSize - 1) / kBrickSize,
                             (h + kBrickSize - 1) / kBrickSize,
                             (d + kBrickSize - 1) / kBrickSize);
    // Auto-size: cover the whole pageGrid up to the per-axis cap. Caller can
    // override by passing a non-zero atlasGrid for known-large or known-dense
    // workloads. Auto-size never exceeds (8,8,8) = 512 slots ~= 292 MB.
    if (atlasGrid.x == 0 || atlasGrid.y == 0 || atlasGrid.z == 0) {
        atlasGrid = glm::uvec3(std::min(m_pageGrid.x, kAutoAtlasAxisCap),
                               std::min(m_pageGrid.y, kAutoAtlasAxisCap),
                               std::min(m_pageGrid.z, kAutoAtlasAxisCap));
    }
    m_atlasGrid = atlasGrid;

    const uint32_t atlasVoxelsX = atlasGrid.x * kBrickStored;
    const uint32_t atlasVoxelsY = atlasGrid.y * kBrickStored;
    const uint32_t atlasVoxelsZ = atlasGrid.z * kBrickStored;
    const uint32_t totalSlots   = atlasGrid.x * atlasGrid.y * atlasGrid.z;
    const uint32_t totalPageEntries = m_pageGrid.x * m_pageGrid.y * m_pageGrid.z;

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

    m_usedSlots = 0;
    for (uint32_t bz = 0; bz < m_pageGrid.z; ++bz) {
        for (uint32_t by = 0; by < m_pageGrid.y; ++by) {
            for (uint32_t bx = 0; bx < m_pageGrid.x; ++bx) {
                if (isInteriorEmpty(halfData, bx, by, bz, w, h, d, emptyValueHalf)) continue;
                if (m_usedSlots >= totalSlots) {
                    // Atlas exhausted. Finish the scan to count *all* non-empty
                    // bricks so the user gets a concrete "you need >= N slots"
                    // recommendation instead of just "more". Cube-root then ceil
                    // gives a balanced atlasGrid suggestion (clamped by pageGrid
                    // per axis -- never recommend more slots per axis than
                    // virtual bricks).
                    uint32_t totalNonEmpty = m_usedSlots + 1;  // include current
                    for (uint32_t cz = bz; cz < m_pageGrid.z; ++cz) {
                        const uint32_t cyStart = (cz == bz) ? by : 0u;
                        for (uint32_t cy = cyStart; cy < m_pageGrid.y; ++cy) {
                            const uint32_t cxStart = (cz == bz && cy == by) ? (bx + 1u) : 0u;
                            for (uint32_t cx = cxStart; cx < m_pageGrid.x; ++cx) {
                                if (!isInteriorEmpty(halfData, cx, cy, cz, w, h, d, emptyValueHalf))
                                    ++totalNonEmpty;
                            }
                        }
                    }
                    const double cbrt = std::cbrt(static_cast<double>(totalNonEmpty));
                    const uint32_t axisGuess = static_cast<uint32_t>(std::ceil(cbrt));
                    const glm::uvec3 recommend(
                        std::min(std::max(axisGuess, atlasGrid.x), m_pageGrid.x),
                        std::min(std::max(axisGuess, atlasGrid.y), m_pageGrid.y),
                        std::min(std::max(axisGuess, atlasGrid.z), m_pageGrid.z));
                    LOG_ERROR("BrickedVolume") << "atlas full: volume "
                        << w << "x" << h << "x" << d << " has " << totalNonEmpty
                        << " non-empty bricks but atlas " << atlasGrid.x << "x"
                        << atlasGrid.y << "x" << atlasGrid.z << " holds only "
                        << totalSlots << " slots. Recommend atlasGrid ("
                        << recommend.x << "," << recommend.y << "," << recommend.z
                        << ") = " << (recommend.x * recommend.y * recommend.z)
                        << " slots (~" << ((static_cast<uint64_t>(recommend.x) * recommend.y
                                            * recommend.z * kBrickStored * kBrickStored
                                            * kBrickStored * 2) >> 20) << " MB)";
                    return false;
                }

                const uint32_t slot = m_usedSlots++;
                const uint32_t pageIdx = (bz * m_pageGrid.y + by) * m_pageGrid.x + bx;
                pageTable[pageIdx] = slot;

                // Atlas slot origin (in voxels) for linear unpacking:
                // slot = sx + sy*Ax + sz*Ax*Ay.
                const uint32_t sx = slot % atlasGrid.x;
                const uint32_t sy = (slot / atlasGrid.x) % atlasGrid.y;
                const uint32_t sz =  slot / (atlasGrid.x * atlasGrid.y);
                const uint32_t aOriginX = sx * kBrickStored;
                const uint32_t aOriginY = sy * kBrickStored;
                const uint32_t aOriginZ = sz * kBrickStored;

                // Copy the 66^3 brick (interior + 1-voxel halo) from source.
                // Source coords range [bx*64 - 1, bx*64 + 64] inclusive; clamped
                // by srcVoxel when reading.
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
                            dstRow[lx] = srcVoxel(halfData, srcXcoord, srcY, srcZ, w, h, d);
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // 2. Allocate atlas texture + upload.
    // ------------------------------------------------------------------
    {
        rhi::TextureDesc td{};
        td.size          = rhi::Extent3D{atlasVoxelsX, atlasVoxelsY, atlasVoxelsZ};
        td.dimension     = rhi::TextureDimension::Texture3D;
        td.format        = rhi::TextureFormat::R16Float;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
        td.label         = "VolumeBrickAtlas";
        m_atlasTex = device->createTexture(td);
        if (!m_atlasTex) { LOG_ERROR("BrickedVolume") << "atlas texture create failed"; return false; }

        rhi::BufferDesc bd{};
        bd.size  = atlasBytes;
        bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        bd.label = "BrickAtlasStaging";
        auto staging = device->createBuffer(bd);
        if (!staging) { LOG_ERROR("BrickedVolume") << "atlas staging create failed"; return false; }
        std::memcpy(staging->map(), atlasBuf.data(), atlasBytes);
        staging->unmap();

        auto enc = device->createCommandEncoder();
        enc->transitionTextureLayout(m_atlasTex.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::TransferDst);

        rhi::BufferTextureCopyInfo bufferCopy{};
        bufferCopy.buffer       = staging.get();
        bufferCopy.offset       = 0;
#ifdef __EMSCRIPTEN__
        bufferCopy.bytesPerRow  = paddedBytesPerRow;
        bufferCopy.rowsPerImage = atlasVoxelsY;
#else
        // Vulkan path: bytesPerRow is interpreted as TEXELS (see CLAUDE.md).
        // 0 = tightly packed.
        bufferCopy.bytesPerRow  = 0;
        bufferCopy.rowsPerImage = 0;
#endif
        rhi::TextureCopyInfo texCopy{};
        texCopy.texture  = m_atlasTex.get();
        texCopy.mipLevel = 0;
        texCopy.origin   = {0, 0, 0};
        texCopy.aspect   = 0;
        enc->copyBufferToTexture(bufferCopy, texCopy,
                                 rhi::Extent3D{atlasVoxelsX, atlasVoxelsY, atlasVoxelsZ});
        enc->transitionTextureLayout(m_atlasTex.get(),
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
        m_atlasView = m_atlasTex->createView(vd);
        if (!m_atlasView) { LOG_ERROR("BrickedVolume") << "atlas view create failed"; return false; }
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
        << ", " << m_usedSlots << "/" << totalSlots << " atlas slots"
        << " (atlas " << atlasVoxelsX << "x" << atlasVoxelsY << "x" << atlasVoxelsZ << ")";
    return true;
}

} // namespace rendering
