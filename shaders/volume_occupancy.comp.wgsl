// M3-1: build a min/max occupancy grid for empty-space skipping (WebGPU). One
// invocation per macrocell: scan the cell's voxels in the brick atlas (M3-3) and
// write the cell's [min,max] intensity into a storage buffer. textureLoad needs
// no sampler (so the bind layout omits it on this backend; the buffer is at
// binding 2, the page table at binding 3).
//
// M3-3 note: the volume is now stored as a brick atlas + page table indirection,
// not a single dense 3D texture. Empty bricks return 0 here, matching the
// fragment-side sampleVolume helper.

struct OccUBO {
    volDim:    vec4<u32>,   // xyz = volume dims (voxels), w = cellSize
    gridDim:   vec4<u32>,   // xyz = grid dims (cells), w unused
    pageGrid:  vec4<u32>,   // M3-3 xyz = brick grid dims, w unused
    atlasGrid: vec4<u32>,   // M3-3 xyz = atlas capacity in bricks, w unused
    brickInfo0: vec4<u32>,  // Option C xyz = L0 interior per axis, w = halo bitmask
    // Last-brick shrink follow-up: physical L0 atlas dims. Occupancy uses
    // integer texel coords (textureLoad), so this is unused here but the
    // struct must stay in sync with the CPU-side OccUBO buffer size.
    atlasPhys0: vec4<u32>,
};

@group(0) @binding(0) var<uniform> ubo: OccUBO;
@group(0) @binding(1) var volumeTex: texture_3d<f32>;
@group(0) @binding(2) var<storage, read_write> cells: array<vec2<f32>>;
@group(0) @binding(3) var<storage, read>       pageSlots: array<u32>;

fn fetchVoxel(v: vec3<u32>) -> f32 {
    let brickIdx = v / 64u;
    let local    = v - brickIdx * 64u;   // [0, 63]
    let pageIdx = (brickIdx.z * ubo.pageGrid.y + brickIdx.y) * ubo.pageGrid.x + brickIdx.x;
    let page    = pageSlots[pageIdx];
    if (page == 0xFFFFFFFFu) { return 0.0; }
    // beta-5: page packs (lod<<30)|slot. Occupancy runs at load time when only
    // L0 is populated, so we only handle the L0 atlas here.
    let slot = page & 0x3FFFFFFFu;
    let sx = slot % ubo.atlasGrid.x;
    let sy = (slot / ubo.atlasGrid.x) % ubo.atlasGrid.y;
    let sz =  slot / (ubo.atlasGrid.x * ubo.atlasGrid.y);
    // Option C L0 per-axis stored size + halo offset (see brickInfo0).
    let halo0     = vec3<u32>(ubo.brickInfo0.w & 1u,
                              (ubo.brickInfo0.w >> 1u) & 1u,
                              (ubo.brickInfo0.w >> 2u) & 1u);
    let stored0   = ubo.brickInfo0.xyz + halo0 * 2u;
    let atlasCoord = vec3<i32>(i32(sx * stored0.x + halo0.x + local.x),
                               i32(sy * stored0.y + halo0.y + local.y),
                               i32(sz * stored0.z + halo0.z + local.z));
    return textureLoad(volumeTex, atlasCoord, 0).r;
}

@compute @workgroup_size(4, 4, 4)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    if (gid.x >= ubo.gridDim.x || gid.y >= ubo.gridDim.y || gid.z >= ubo.gridDim.z) {
        return;
    }
    let cs = ubo.volDim.w;
    let base = gid * cs;
    var mn = 1e30;
    var mx = -1e30;
    for (var z = 0u; z < cs; z = z + 1u) {
        for (var y = 0u; y < cs; y = y + 1u) {
            for (var x = 0u; x < cs; x = x + 1u) {
                let v = base + vec3<u32>(x, y, z);
                if (v.x >= ubo.volDim.x || v.y >= ubo.volDim.y || v.z >= ubo.volDim.z) {
                    continue;
                }
                let val = fetchVoxel(v);
                mn = min(mn, val);
                mx = max(mx, val);
            }
        }
    }
    let idx = (gid.z * ubo.gridDim.y + gid.y) * ubo.gridDim.x + gid.x;
    cells[idx] = vec2<f32>(mn, mx);
}
