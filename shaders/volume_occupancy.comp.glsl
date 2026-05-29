#version 450

// M3-1: build a min/max occupancy grid for empty-space skipping. One invocation
// per macrocell: scan the cell's voxels in the R16Float volume and write the cell's
// [min,max] intensity. The ray-march pass later skips cells whose windowed density
// is zero (air), which dominates medical volumes.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0) uniform OccUBO {
    uvec4 volDim;    // xyz = volume dims (voxels), w = cellSize
    uvec4 gridDim;   // xyz = grid dims (cells), w unused
} ubo;
layout(set = 0, binding = 1) uniform texture3D volumeTex;
layout(set = 0, binding = 2) uniform sampler   volSampler;   // texelFetch needs a combined sampler
layout(std430, set = 0, binding = 3) buffer OccBuf {
    vec2 cells[];    // per cell: (min, max)
};

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
        float val = texelFetch(sampler3D(volumeTex, volSampler), ivec3(v), 0).r;
        mn = min(mn, val);
        mx = max(mx, val);
    }
    uint idx = (cell.z * ubo.gridDim.y + cell.y) * ubo.gridDim.x + cell.x;
    cells[idx] = vec2(mn, mx);
}
