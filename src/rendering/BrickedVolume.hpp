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

    BrickedVolume() = default;
    ~BrickedVolume() = default;
    BrickedVolume(const BrickedVolume&) = delete;
    BrickedVolume& operator=(const BrickedVolume&) = delete;

    // Build the atlas + page table from a dense R16Float intensity volume.
    // halfData is row-major (x-fastest) of size w*h*d uint16 (R16Float bits).
    // emptyValueHalf is the half-float bit pattern that counts as "air" --
    // bricks whose 64^3 interior is uniformly this value are skipped.
    // atlasGrid is the atlas capacity in bricks. Returns false on RHI failure
    // or atlas-full (logged).
    bool build(rhi::RHIDevice* device, rhi::RHIQueue* queue,
               const std::vector<uint16_t>& halfData,
               uint32_t w, uint32_t h, uint32_t d,
               uint16_t emptyValueHalf,
               glm::uvec3 atlasGrid = glm::uvec3(4, 4, 4));

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

private:
    std::unique_ptr<rhi::RHITexture>     m_atlasTex;
    std::unique_ptr<rhi::RHITextureView> m_atlasView;
    std::unique_ptr<rhi::RHIBuffer>      m_pageTable;
    glm::uvec3 m_volSize{0};
    glm::uvec3 m_pageGrid{0};
    glm::uvec3 m_atlasGrid{0};
    uint32_t   m_usedSlots = 0;
};

} // namespace rendering
