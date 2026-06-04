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
#include <unordered_map>
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

    // v1-alpha streaming per-frame counters. v1-1 populates visibleBricks +
    // visibleNonEmpty; v1-2 adds visibleMissing for streaming-mode volumes;
    // bricksUploaded / bricksEvicted / visibleResident light up in v1-3.
    struct StreamUpdateStats {
        uint32_t visibleBricks    = 0;   // total page-grid bricks intersecting frustum (incl. empty)
        uint32_t visibleNonEmpty  = 0;   // subset that has data in the source volume
        uint32_t bricksUploaded   = 0;
        uint32_t bricksEvicted    = 0;
        uint32_t visibleResident  = 0;
        uint32_t visibleMissing   = 0;
    };

    // v1-2 mode: Static = atlas fully populated at load time (v0 behavior);
    // Streaming = atlas starts empty + source data kept in CPU RAM, bricks
    // page in on demand. Mode is decided at build() time by comparing the
    // non-empty brick count to atlas capacity. Runtime mode change unsupported.
    enum class Mode { StaticFullyLoaded, Streaming };

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

    Mode mode()         const { return m_mode; }
    bool isStreaming()  const { return m_mode == Mode::Streaming; }

    // v1-2: per-virtual-brick "is this brick non-empty in the source data?"
    // Computed once during build() so updateBrickStreaming can distinguish
    // visible-empty (skip, never needs upload) from visible-non-empty-but-
    // not-resident (the "missing" count -- v1-3 will turn that into upload
    // requests). Bit-packed: 1 bit per virtual brick.
    bool pageHasData(uint32_t pageIdx) const {
        if (pageIdx >= m_pageGrid.x * m_pageGrid.y * m_pageGrid.z) return false;
        return (m_pageOccupancy[pageIdx >> 3] >> (pageIdx & 7)) & 1u;
    }

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
    std::vector<uint8_t>                 m_pageOccupancy;  // v1-2 bitmap: 1 = source brick has data
    glm::uvec3 m_volSize{0};
    glm::uvec3 m_pageGrid{0};
    glm::uvec3 m_atlasGrid{0};
    uint32_t   m_usedSlots = 0;

    // v1-2 streaming-mode state. Empty / default-constructed in Static mode.
    Mode m_mode = Mode::StaticFullyLoaded;
    std::vector<uint16_t> m_originalHalfData;  // CPU mirror, indexed (z*H + y)*W + x
    uint16_t m_emptyValueHalf = 0;             // for empty-slot init + halo padding
    struct AtlasSlotState {
        uint32_t residentPageIdx = kEmptySlot; // page index living in this slot, or kEmptySlot
        uint64_t lastFrameUsed   = 0;          // LRU key (monotonically increasing)
    };
    std::vector<AtlasSlotState> m_slotStates;  // sized to totalSlots() in streaming mode
    std::unordered_map<uint32_t, uint32_t> m_pageToSlot;  // page index -> slot index
};

} // namespace rendering
