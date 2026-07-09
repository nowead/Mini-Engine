#version 450

// M3-1: build a min/max occupancy grid for empty-space skipping. One invocation
// per macrocell: scan the cell's voxels in the brick atlas (M3-3) and write the
// cell's [min,max] intensity. The ray-march pass later skips cells whose windowed
// density is zero (air), which dominates medical volumes.
//
// M3-3 note: the volume is now stored as a brick atlas + page table indirection,
// not a single dense 3D texture. Empty bricks return 0 here (matching the
// fragment-side sampleVolume helper); cells fully inside an empty brick will
// therefore correctly report cmax=0 and be skipped at march time.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0) uniform OccUBO {
    uvec4 volDim;     // xyz = volume dims (voxels), w = cellSize
    uvec4 gridDim;    // xyz = grid dims (cells), w unused
    uvec4 pageGrid;   // M3-3 xyz = brick grid dims, w unused
    uvec4 atlasGrid;  // M3-3 xyz = atlas capacity in bricks, w unused
    uvec4 brickInfo0; // Option C xyz = L0 interior per axis, w = halo bitmask
    // Last-brick shrink follow-up: physical L0 atlas dims. Occupancy uses
    // integer texel coords (texelFetch), so this is unused here but the
    // struct must stay in sync with the CPU-side OccUBO buffer size.
    uvec4 atlasPhys0;
} ubo;
layout(set = 0, binding = 1) uniform texture3D volumeTex;     // M3-3 brick atlas (R16Float)
layout(set = 0, binding = 2) uniform sampler   volSampler;    // texelFetch needs a combined sampler
layout(std430, set = 0, binding = 3) buffer OccBuf {
    vec2 cells[];     // per cell: (min, max)
};
layout(std430, set = 0, binding = 4) readonly buffer PageTable {
    uint pageSlots[]; // M3-3 page table: per virtual brick, atlas slot or 0xFFFFFFFF
};

// Integer-voxel sample through the brick atlas. Mirrors the fragment-side
// sampleVolume helper but skips linear filtering / halo offsets -- we want the
// canonical voxel value, not a filtered one.
float fetchVoxel(uvec3 v) {
    uvec3 brickIdx = v / 64u;
    uvec3 local    = v - brickIdx * 64u;   // [0, 63]
    uint pageIdx = (brickIdx.z * ubo.pageGrid.y + brickIdx.y) * ubo.pageGrid.x + brickIdx.x;
    uint page    = pageSlots[pageIdx];
    if (page == 0xFFFFFFFFu) return 0.0;
    // beta-5: page packs (lod<<30)|slot. Occupancy runs at load time when only
    // L0 is populated, so we only handle the L0 atlas here.
    uint slot = page & 0x3FFFFFFFu;
    uint sx = slot % ubo.atlasGrid.x;
    uint sy = (slot / ubo.atlasGrid.x) % ubo.atlasGrid.y;
    uint sz =  slot / (ubo.atlasGrid.x * ubo.atlasGrid.y);
    // Option C L0 per-axis stored size + halo offset.
    uvec3 halo0   = uvec3(ubo.brickInfo0.w & 1u,
                          (ubo.brickInfo0.w >> 1u) & 1u,
                          (ubo.brickInfo0.w >> 2u) & 1u);
    uvec3 stored0 = ubo.brickInfo0.xyz + halo0 * 2u;
    ivec3 atlasCoord = ivec3(sx * stored0.x + halo0.x + local.x,
                             sy * stored0.y + halo0.y + local.y,
                             sz * stored0.z + halo0.z + local.z);
    return texelFetch(sampler3D(volumeTex, volSampler), atlasCoord, 0).r;
}

void main() {
    uvec3 cell = gl_GlobalInvocationID.xyz;
    if (cell.x >= ubo.gridDim.x || cell.y >= ubo.gridDim.y || cell.z >= ubo.gridDim.z) return;

    uint  cs   = ubo.volDim.w;
    uvec3 base = cell * cs;
    float mn = 1e30;
    float mx = -1e30;
    for (uint z = 0u; z < cs; ++z)
    for (uint y = 0u; y < cs; ++y)
    for (uint x = 0u; x < cs; ++x) {
        uvec3 v = base + uvec3(x, y, z);
        if (v.x >= ubo.volDim.x || v.y >= ubo.volDim.y || v.z >= ubo.volDim.z) continue;
        float val = fetchVoxel(v);
        mn = min(mn, val);
        mx = max(mx, val);
    }
    uint idx = (cell.z * ubo.gridDim.y + cell.y) * ubo.gridDim.x + cell.x;
    cells[idx] = vec2(mn, mx);
}
