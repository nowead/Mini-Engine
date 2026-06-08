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

    // v1-beta beta-5 page-table encoding: high 2 bits = LOD (0..3), low 30 bits
    // = atlas slot index. Sentinel 0xFFFFFFFF keeps its meaning (shaders MUST
    // check page == kEmptySlot before decoding -- decoded sentinel would look
    // like a valid L3 slot otherwise).
    static constexpr uint32_t kPageLodShift = 30;
    static constexpr uint32_t kPageSlotMask = 0x3FFFFFFFu;
    static constexpr uint32_t encodePage(uint32_t slot, uint8_t lod) {
        return (uint32_t(lod) << kPageLodShift) | (slot & kPageSlotMask);
    }

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
        // v1-beta beta-3: LOD distribution across visible non-empty bricks.
        // lodCounts[lv] = how many of the visible-non-empty bricks would be
        // sampled at LOD lv if the streaming respected the selection. Recorded
        // for the stats panel only in beta-3; beta-4 wires it into uploads.
        uint32_t lodCounts[4] = {0, 0, 0, 0};
    };

    // v1-2 mode: Static = atlas fully populated at load time (v0 behavior);
    // Streaming = atlas starts empty + source data kept in CPU RAM, bricks
    // page in on demand. Mode is decided at build() time by comparing the
    // non-empty brick count to atlas capacity. Runtime mode change unsupported.
    enum class Mode { StaticFullyLoaded, Streaming };

    // v1-3 streaming: how many brick uploads we'll execute in one frame.
    // Per-frame staging pool is sized at this. Best-effort -- if more bricks
    // need to come in than this budget, the remainder waits a frame.
    static constexpr uint32_t kStreamUploadsPerFrame = 64;
    static constexpr uint32_t kStreamingFramesInFlight = 2;

    // v1-3 alpha auto-sizing budget. The build picks the smallest balanced
    // atlas that holds all non-empty bricks (so Static mode wins whenever
    // possible) and caps the total VRAM allocation to this budget; volumes
    // whose data exceeds the budget fall to Streaming with a recommendation
    // logged. 512 MB is a generous-but-safe default for a single volume on
    // a typical desktop or browser WebGPU device.
    static constexpr uint64_t kAutoAtlasBudgetBytes = 512ULL * 1024 * 1024;
    static constexpr uint32_t kAutoAtlasMinAxis     = 2;

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

    // v1-beta beta-3: write the selected LOD for a virtual brick. beta-4
    // streaming will read these values when picking which atlas (and which
    // mipData level) to upload from. No-op in Static mode.
    void setBrickLod(uint32_t pageIdx, uint8_t lod) {
        if (pageIdx < m_lodTableHost.size()) m_lodTableHost[pageIdx] = lod;
    }
    uint8_t brickLod(uint32_t pageIdx) const {
        return (pageIdx < m_lodTableHost.size()) ? m_lodTableHost[pageIdx] : 0u;
    }

    // v1-beta LOD: bricks at LOD > 0 are box-filter-downsampled from the L0
    // source on the fly during the streaming pack (disk paging Step 3). The
    // previous m_mipChain[L1..L3] member is gone -- the chain typically took
    // ~14% of L0's RAM on top of the source (~280 MB on 1024^3), which
    // dominated the disk-paging story for big volumes.
    static constexpr uint32_t kLodLevels = 4;        // L0 + 3 box-filter levels

    // A non-owning view over the L0 half-float source data. Decouples callers
    // from the underlying storage so the disk-paging track can swap
    // m_originalHalfData out for an mmap-backed region without touching the
    // pack / streaming code.
    struct HalfDataView {
        const uint16_t* data = nullptr;
        size_t          size = 0;
        bool empty() const noexcept { return size == 0; }
    };
    HalfDataView halfDataL0() const {
        return {m_originalHalfData.data(), m_originalHalfData.size()};
    }

    // v1-3 streaming update. Given the list of virtual brick page indices
    // the camera frustum sees this frame, this method:
    //   1. Bumps lastFrameUsed on resident slots that are in the visible set.
    //   2. Picks up to kStreamUploadsPerFrame missing visible bricks.
    //   3. Evicts the LRU non-visible slots to make room (if needed).
    //   4. Copies the new bricks from CPU source into the atlas via staging.
    //   5. Pushes the updated page table to the GPU.
    // No-op in Static mode. Returns counts for the stats panel.
    struct StreamPerFrameStats {
        uint32_t bricksUploaded = 0;
        uint32_t bricksEvicted  = 0;
    };
    StreamPerFrameStats updateStreaming(const std::vector<uint32_t>& visiblePageIndices,
                                        uint64_t frameIdx);

    // v1-2: per-virtual-brick "is this brick non-empty in the source data?"
    // Computed once during build() so updateBrickStreaming can distinguish
    // visible-empty (skip, never needs upload) from visible-non-empty-but-
    // not-resident (the "missing" count -- v1-3 will turn that into upload
    // requests). Bit-packed: 1 bit per virtual brick.
    bool pageHasData(uint32_t pageIdx) const {
        if (pageIdx >= m_pageGrid.x * m_pageGrid.y * m_pageGrid.z) return false;
        return (m_pageOccupancy[pageIdx >> 3] >> (pageIdx & 7)) & 1u;
    }

    // L0 (full-resolution) atlas view used by the existing shader bindings.
    // beta-5 adds per-LOD bindings via atlasViewLod(level). In Static mode only
    // L0 is allocated (the other slots are unused by the encoded page table),
    // so fall back to L0 when callers ask for a higher level -- WebGPU bind
    // groups reject nullptr entries, but the shader never reads these atlases
    // for a Static-mode volume so the duplicate binding is harmless.
    rhi::RHITextureView* atlasView()         const { return m_atlasViews[0].get(); }
    rhi::RHITextureView* atlasViewLod(uint32_t level) const {
        if (level >= kLodLevels) return nullptr;
        return m_atlasViews[level] ? m_atlasViews[level].get() : m_atlasViews[0].get();
    }
    // Brick voxel extent per LOD: L0=66, L1=34, L2=18, L3=10 (kBrickSize >> L + 2).
    static constexpr uint32_t kBrickStoredAtLod(uint32_t level) {
        return ((kBrickSize >> level) | 0u) + 2u;
    }
    rhi::RHIBuffer*      pageTable()    const { return m_pageTable.get(); }
    uint64_t             pageTableSize() const {
        return static_cast<uint64_t>(m_pageGrid.x) * m_pageGrid.y * m_pageGrid.z * sizeof(uint32_t);
    }

    glm::uvec3 volSize()    const { return m_volSize; }
    glm::uvec3 pageGrid()   const { return m_pageGrid; }
    glm::uvec3 atlasGrid()  const { return m_atlasGrid; }
    glm::uvec3 atlasVoxels() const { return m_atlasGrid * kBrickStored; }
    uint32_t   usedSlots()  const {
        return m_usedSlotsPerLod[0] + m_usedSlotsPerLod[1]
             + m_usedSlotsPerLod[2] + m_usedSlotsPerLod[3];
    }
    uint32_t   usedSlotsAtLod(uint32_t lod) const {
        return (lod < kLodLevels) ? m_usedSlotsPerLod[lod] : 0u;
    }
    uint32_t   totalSlots() const { return m_atlasGrid.x * m_atlasGrid.y * m_atlasGrid.z; }

    // Memory used by the atlas texture in bytes (R16Float = 2 B per voxel).
    // Used slots only — empty slots cost the same VRAM since the texture is
    // allocated dense, but this is the "live data" footprint that matters for
    // M3-3 v1 streaming budgets. v1-beta beta-4: sums over all LODs, weighting
    // each LOD by its (smaller) brick storage size.
    uint64_t atlasBytesAllocated() const {
        const glm::uvec3 v = atlasVoxels();
        return static_cast<uint64_t>(v.x) * v.y * v.z * 2;
    }
    uint64_t atlasBytesUsed() const {
        uint64_t total = 0;
        for (uint32_t lv = 0; lv < kLodLevels; ++lv) {
            const uint64_t stored = kBrickStoredAtLod(lv);
            total += static_cast<uint64_t>(m_usedSlotsPerLod[lv]) * stored * stored * stored * 2;
        }
        return total;
    }
    // Equivalent dense-volume bytes (the storage we'd need without bricking).
    uint64_t denseBytes() const {
        return static_cast<uint64_t>(m_volSize.x) * m_volSize.y * m_volSize.z * 2;
    }

private:
    // v1-beta beta-2: one atlas texture per LOD (L0=full, L1=half, L2=quarter,
    // L3=eighth in each axis). Static mode populates only m_atlasTexes[0];
    // Streaming mode allocates all four but currently only L0 receives data
    // (beta-3 selects which LOD goes into which slot, beta-4 actually uploads
    // to non-L0 atlases, beta-5 makes the shader sample the right LOD).
    std::array<std::unique_ptr<rhi::RHITexture>,     kLodLevels> m_atlasTexes;
    std::array<std::unique_ptr<rhi::RHITextureView>, kLodLevels> m_atlasViews;
    std::unique_ptr<rhi::RHIBuffer>      m_pageTable;
    std::vector<uint32_t>                m_pageTableHost;  // v1-1 CPU mirror
    std::vector<uint8_t>                 m_pageOccupancy;  // v1-2 bitmap: 1 = source brick has data
    glm::uvec3 m_volSize{0};
    glm::uvec3 m_pageGrid{0};
    glm::uvec3 m_atlasGrid{0};

    // v1-2 streaming-mode state. Empty / default-constructed in Static mode.
    Mode m_mode = Mode::StaticFullyLoaded;
    std::vector<uint16_t> m_originalHalfData;  // CPU mirror, indexed (z*H + y)*W + x
    uint16_t m_emptyValueHalf = 0;             // for empty-slot init + halo padding
    struct AtlasSlotState {
        uint32_t residentPageIdx = kEmptySlot; // page index living in this slot, or kEmptySlot
        uint32_t residentLod     = 0;          // v1-beta: which LOD this slot holds
        uint64_t lastFrameUsed   = 0;          // LRU key (monotonically increasing)
    };
    // v1-beta beta-4: one slot-state vector per LOD. Each LOD atlas has its
    // own (independent) slot space sized to atlasGrid total. Slot at LOD lv
    // is stored in m_atlasTexes[lv][m_slotStates[lv][slot].residentPageIdx].
    std::array<std::vector<AtlasSlotState>, kLodLevels> m_slotStates;
    // page index -> (slot, lod). A brick is at exactly one slot at one LOD
    // at any given time; the streaming update may migrate it as the camera
    // moves (LRU drops the old LOD slot, a new upload lands at the new LOD).
    std::unordered_map<uint32_t, std::pair<uint32_t, uint8_t>> m_pageToSlot;
    // Per-LOD used-slot counter -- usedSlots() returns the sum.
    std::array<uint32_t, kLodLevels> m_usedSlotsPerLod = {0, 0, 0, 0};
    // v1-beta beta-2: CPU mirror of "which LOD this virtual brick is currently
    // resident at". One byte per virtual brick, defaults to 0. beta-3 writes
    // selection, beta-4 streaming respects it, beta-5 shader reads via a GPU
    // buffer mirror (not allocated yet -- introduced when first used).
    std::vector<uint8_t> m_lodTableHost;

    // v1-3 cached RHI handles (set in build() so updateStreaming can do its
    // own copy + submit without the caller threading them through every frame).
    rhi::RHIDevice* m_device = nullptr;
    rhi::RHIQueue*  m_queue  = nullptr;

    // v1-3 streaming staging pools. Per frame-in-flight, K slots; each slot
    // holds one brick (kBrickStored^3 voxels, WebGPU-aligned row stride).
    // Allocated lazily on first upload to the slot. Re-used round-robin by
    // frame index; the GPU is guaranteed to have finished frame N's copy
    // before frame N+kStreamingFramesInFlight reuses the same slot.
    std::array<std::array<std::unique_ptr<rhi::RHIBuffer>, kStreamUploadsPerFrame>,
               kStreamingFramesInFlight> m_brickStaging;
    // Per-frame staging for the page table buffer (small: 4 B per virtual
    // brick). Cheaper to push the whole table than tracking dirty ranges
    // for the first cut; v1-beta can switch to incremental updates if
    // page tables ever get large enough to matter.
    std::array<std::unique_ptr<rhi::RHIBuffer>, kStreamingFramesInFlight> m_pageStaging;
};

} // namespace rendering
