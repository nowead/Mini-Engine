@group(0) @binding(0) var outputLUT: texture_storage_2d<rg16float, write>;

const PI: f32 = 3.14159265359;

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

fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let a = roughness;
    let k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    let ggx2 = geometrySchlickGGX(NdotV, roughness);
    let ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

fn integrateBRDF(NdotV: f32, roughness: f32) -> vec2<f32> {
    let V = vec3<f32>(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    var A: f32 = 0.0;
    var B: f32 = 0.0;

    let N = vec3<f32>(0.0, 0.0, 1.0);

    let SAMPLE_COUNT: u32 = 1024u;
    for (var i: u32 = 0u; i < SAMPLE_COUNT; i = i + 1u) {
        let Xi = hammersley(i, SAMPLE_COUNT);
        let H = importanceSampleGGX(Xi, N, roughness);
        let L = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(L.z, 0.0);
        let NdotH = max(H.z, 0.0);
        let VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            let G = geometrySmith(N, V, L, roughness);
            let G_Vis = (G * VdotH) / (NdotH * NdotV);
            let Fc = pow(1.0 - VdotH, 5.0);

            A = A + (1.0 - Fc) * G_Vis;
            B = B + Fc * G_Vis;
        }
    }

    A = A / f32(SAMPLE_COUNT);
    B = B / f32(SAMPLE_COUNT);
    return vec2<f32>(A, B);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>) {
    let pos = vec2<i32>(i32(globalID.x), i32(globalID.y));
    let size = textureDimensions(outputLUT);

    if (pos.x >= i32(size.x) || pos.y >= i32(size.y)) {
        return;
    }

    var NdotV = (f32(pos.x) + 0.5) / f32(size.x);
    var roughness = (f32(pos.y) + 0.5) / f32(size.y);

    NdotV = max(NdotV, 0.001);
    roughness = max(roughness, 0.001);

    let result = integrateBRDF(NdotV, roughness);

    textureStore(outputLUT, vec2<u32>(globalID.xy), vec4<f32>(result, 0.0, 1.0));
}
