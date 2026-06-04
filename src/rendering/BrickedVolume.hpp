#pragma once

// ============================================================================
// BrickedVolume -- sparse volume storage via brick atlas + page table.
//
// The source volume is partitioned into fixed-size bricks (kBrickSize voxels
// per side). At build time the CPU walks every virtual brick, drops the ones
// whose interior is all "air" (matches the supplied empty value), and packs
// the rest into a 3D atlas texture. A page table (storage buffer of uint32s)
// maps each virtual brick to its atlas slot, or kEmptySlot for "no data".
//
// Bricks are stored with a 1-voxel halo on every side (so kBrickStored^3 per
// slot) so the shader can linearly interpolate up to the brick boundary
// without artefacts -- the halo carries the neighbouring brick's edge voxels
// (clamped to source extents for boundary bricks).
//
// Dual-backend: same buffer/texture upload pattern as VolumeRenderer's
// uploadHalf -- one staging buffer for the atlas (WebGPU row stride padded
// to 256B), one tiny staging for the page table. No streaming yet; this
// build assumes every non-empty brick fits in the configured atlas.
// ============================================================================

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include <rhi/RHIBuffer.hpp>
#include <rhi/RHIDevice.hpp>
#include <rhi/RHIQueue.hpp>
#include <rhi/RHITexture.hpp>

namespace rendering {

class BrickedVolume {
public:
    static constexpr uint32_t kBrickSize   = 64;            // interior voxels per side
    static constexpr uint32_t kBrickStored = kBrickSize + 2; // 66, with 1-voxel halo
    static constexpr uint32_t kEmptySlot   = 0xFFFFFFFFu;   // page table sentinel

    // v1-alpha streaming per-frame counters. v1-1 only populates visibleBricks;
    // bricksUploaded / bricksEvicted / visibleResident / visibleMissing fill
    // in v1-3 once the LRU + incremental upload path lands.
    struct StreamUpdateStats {
        uint32_t visibleBricks    = 0;   // total page-grid bricks intersecting frustum (incl. empty)
        uint32_t visibleNonEmpty  = 0;   // subset that has data in the source volume
        uint32_t bricksUploaded   = 0;
        uint32_t bricksEvicted    = 0;
        uint32_t visibleResident  = 0;
        uint32_t visibleMissing   = 0;
    };

    // Auto-size cap per axis. (8,8,8) = 512 slots * 66^3 * 2B ~= 292 MB, the
    // upper bound we're willing to allocate from a load-time decision. Larger
    // volumes either get truncated (caller passes a bigger override) or fail
    // build() with a recommendation (see kAtlasFullRecommendBias below).
    static constexpr uint32_t kAutoAtlasAxisCap = 8;

    BrickedVolume() = default;
    ~BrickedVolume() = default;
    BrickedVolume(const BrickedVolume&) = delete;
    BrickedVolume& operator=(const BrickedVolume&) = delete;

    // Build the atlas + page table from a dense R16Float intensity volume.
    // halfData is row-major (x-fastest) of size w*h*d uint16 (R16Float bits).
    // emptyValueHalf is the half-float bit pattern that counts as "air" --
    // bricks whose 64^3 interior is uniformly this value are skipped.
    // atlasGrid = (0,0,0) means "auto" -- pick min(pageGrid, kAutoAtlasAxisCap)
    // per axis. Pass an explicit value to override (e.g. for a known-dense
    // benchmark where the auto-cap is too small). Returns false on RHI
    // failure or atlas-full (logged with a recommended atlasGrid).
    bool build(rhi::RHIDevice* device, rhi::RHIQueue* queue,
               const std::vector<uint16_t>& halfData,
               uint32_t w, uint32_t h, uint32_t d,
               uint16_t emptyValueHalf,
               glm::uvec3 atlasGrid = glm::uvec3(0));

    // v1-1 CPU-side page table mirror (per virtual brick, atlas slot or
    // kEmptySlot). v0 build copies its local pageTable here at end of build
    // so frustum-cull code can ask "is this page non-empty?" without a GPU
    // readback. v1-3 streaming will rewrite entries as bricks page in/out.
    const std::vector<uint32_t>& pageTableHost() const { return m_pageTableHost; }

    rhi::RHITextureView* atlasView()    const { return m_atlasView.get(); }
    rhi::RHIBuffer*      pageTable()    const { return m_pageTable.get(); }
    uint64_t             pageTableSize() const {
        return static_cast<uint64_t>(m_pageGrid.x) * m_pageGrid.y * m_pageGrid.z * sizeof(uint32_t);
    }

    glm::uvec3 volSize()    const { return m_volSize; }
    glm::uvec3 pageGrid()   const { return m_pageGrid; }
    glm::uvec3 atlasGrid()  const { return m_atlasGrid; }
    glm::uvec3 atlasVoxels() const { return m_atlasGrid * kBrickStored; }
    uint32_t   usedSlots()  const { return m_usedSlots; }
    uint32_t   totalSlots() const { return m_atlasGrid.x * m_atlasGrid.y * m_atlasGrid.z; }

    // Memory used by the atlas texture in bytes (R16Float = 2 B per voxel).
    // Used slots only — empty slots cost the same VRAM since the texture is
    // allocated dense, but this is the "live data" footprint that matters for
    // M3-3 v1 streaming budgets.
    uint64_t atlasBytesAllocated() const {
        const glm::uvec3 v = atlasVoxels();
        return static_cast<uint64_t>(v.x) * v.y * v.z * 2;
    }
    uint64_t atlasBytesUsed() const {
        return static_cast<uint64_t>(m_usedSlots) * kBrickStored * kBrickStored * kBrickStored * 2;
    }
    // Equivalent dense-volume bytes (the storage we'd need without bricking).
    uint64_t denseBytes() const {
        return static_cast<uint64_t>(m_volSize.x) * m_volSize.y * m_volSize.z * 2;
    }

private:
    std::unique_ptr<rhi::RHITexture>     m_atlasTex;
    std::unique_ptr<rhi::RHITextureView> m_atlasView;
    std::unique_ptr<rhi::RHIBuffer>      m_pageTable;
    std::vector<uint32_t>                m_pageTableHost;  // v1-1 CPU mirror
    glm::uvec3 m_volSize{0};
    glm::uvec3 m_pageGrid{0};
    glm::uvec3 m_atlasGrid{0};
    uint32_t   m_usedSlots = 0;
};

} // namespace rendering
