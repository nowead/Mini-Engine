// M3-1: build a min/max occupancy grid for empty-space skipping (WebGPU). One
// invocation per macrocell: scan the cell's voxels in the R16Float volume and write
// the cell's [min,max] intensity into a storage buffer. textureLoad needs no sampler
// (so the bind layout omits it on this backend; the buffer is at binding 2).

struct OccUBO {
    volDim:  vec4<u32>,   // xyz = volume dims (voxels), w = cellSize
    gridDim: vec4<u32>,   // xyz = grid dims (cells), w unused
};

@group(0) @binding(0) var<uniform> ubo: OccUBO;
@group(0) @binding(1) var volumeTex: texture_3d<f32>;
@group(0) @binding(2) var<storage, read_write> cells: array<vec2<f32>>;

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
                let val = textureLoad(volumeTex, vec3<i32>(v), 0).r;
                mn = min(mn, val);
                mx = max(mx, val);
            }
        }
    }
    let idx = (gid.z * ubo.gridDim.y + gid.y) * ubo.gridDim.x + gid.x;
    cells[idx] = vec2<f32>(mn, mx);
}
