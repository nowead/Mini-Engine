@group(0) @binding(0) var envMap: texture_cube<f32>;
@group(0) @binding(1) var envSampler: sampler;
@group(0) @binding(2) var outputPrefiltered: texture_storage_2d_array<rgba16float, write>;

struct RoughnessUBO {
    roughness: f32,
};
@group(0) @binding(3) var<uniform> params: RoughnessUBO;

const PI: f32 = 3.14159265359;

fn getCubeDir(face: u32, uv: vec2<f32>) -> vec3<f32> {
    let st = uv * 2.0 - 1.0;
    switch (face) {
        case 0u: { return normalize(vec3<f32>( 1.0, -st.y, -st.x)); }
        case 1u: { return normalize(vec3<f32>(-1.0, -st.y,  st.x)); }
        case 2u: { return normalize(vec3<f32>( st.x,  1.0,  st.y)); }
        case 3u: { return normalize(vec3<f32>( st.x, -1.0, -st.y)); }
        case 4u: { return normalize(vec3<f32>( st.x, -st.y,  1.0)); }
        case 5u: { return normalize(vec3<f32>(-st.x, -st.y, -1.0)); }
        default: { return vec3<f32>(0.0); }
    }
}

fn radicalInverse_VdC(bits_in: u32) -> f32 {
    var bits = bits_in;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, N: u32) -> vec2<f32> {
    return vec2<f32>(f32(i) / f32(N), radicalInverse_VdC(i));
}

fn importanceSampleGGX(Xi: vec2<f32>, N: vec3<f32>, roughness: f32) -> vec3<f32> {
    let a = roughness * roughness;

    let phi = 2.0 * PI * Xi.x;
    let cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    let sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    let H = vec3<f32>(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    let up = select(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 0.0, 1.0), abs(N.z) < 0.999);
    let tangent = normalize(cross(up, N));
    let bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>) {
    let size = textureDimensions(outputPrefiltered);

    if (globalID.x >= size.x || globalID.y >= size.y || globalID.z >= 6u) {
        return;
    }

    let uv = (vec2<f32>(f32(globalID.x), f32(globalID.y)) + 0.5) / vec2<f32>(f32(size.x), f32(size.y));
    let N = getCubeDir(globalID.z, uv);
    let R = N;
    let V = R;

    let SAMPLE_COUNT: u32 = 1024u;
    var prefilteredColor = vec3<f32>(0.0);
    var totalWeight: f32 = 0.0;

    for (var i: u32 = 0u; i < SAMPLE_COUNT; i = i + 1u) {
        let Xi = hammersley(i, SAMPLE_COUNT);
        let H = importanceSampleGGX(Xi, N, params.roughness);
        let L = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += textureSampleLevel(envMap, envSampler, L, 0.0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / totalWeight;

    textureStore(outputPrefiltered, vec2<u32>(globalID.xy), globalID.z, vec4<f32>(prefilteredColor, 1.0));
}
