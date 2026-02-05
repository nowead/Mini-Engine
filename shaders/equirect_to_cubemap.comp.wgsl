@group(0) @binding(0) var equirectMap: texture_2d<f32>;
@group(0) @binding(1) var mapSampler: sampler;
@group(0) @binding(2) var outputCubemap: texture_storage_2d_array<rgba16float, write>;

const PI: f32 = 3.14159265359;

fn getCubeDir(face: u32, uv: vec2<f32>) -> vec3<f32> {
    let st = uv * 2.0 - 1.0;
    switch (face) {
        case 0u: { return normalize(vec3<f32>( 1.0, -st.y, -st.x)); }  // +X
        case 1u: { return normalize(vec3<f32>(-1.0, -st.y,  st.x)); }  // -X
        case 2u: { return normalize(vec3<f32>( st.x,  1.0,  st.y)); }  // +Y
        case 3u: { return normalize(vec3<f32>( st.x, -1.0, -st.y)); }  // -Y
        case 4u: { return normalize(vec3<f32>( st.x, -st.y,  1.0)); }  // +Z
        case 5u: { return normalize(vec3<f32>(-st.x, -st.y, -1.0)); }  // -Z
        default: { return vec3<f32>(0.0); }
    }
}

fn dirToEquirect(dir: vec3<f32>) -> vec2<f32> {
    let phi = atan2(dir.z, dir.x);
    let theta = asin(clamp(dir.y, -1.0, 1.0));
    return vec2<f32>(phi / (2.0 * PI) + 0.5, theta / PI + 0.5);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>) {
    let size = textureDimensions(outputCubemap);

    if (globalID.x >= size.x || globalID.y >= size.y || globalID.z >= 6u) {
        return;
    }

    let uv = (vec2<f32>(f32(globalID.x), f32(globalID.y)) + 0.5) / vec2<f32>(f32(size.x), f32(size.y));
    let dir = getCubeDir(globalID.z, uv);
    let equirectUV = dirToEquirect(dir);

    let color = textureSampleLevel(equirectMap, mapSampler, equirectUV, 0.0);

    textureStore(outputCubemap, vec2<u32>(globalID.xy), globalID.z, color);
}
