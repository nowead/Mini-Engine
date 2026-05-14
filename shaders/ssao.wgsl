// SSAO — WebGPU render-pipeline version of ssao.comp.glsl
// Reads depth, reconstructs view-space position, 8-sample hemisphere AO.
// Output: R8Unorm color attachment (r = AO factor, 0=occluded, 1=lit)

struct SSAOParams {
    projection: mat4x4<f32>,  // offset  0 — view→clip
    radius:     f32,           // offset 64
    bias:       f32,
    near:       f32,
    far:        f32,
    invW:       f32,           // 1 / ssaoTextureWidth  (= 2/screenW at half-res)
    invH:       f32,           // 1 / ssaoTextureHeight
    _pad0:      f32,
    _pad1:      f32,
}  // 96 bytes, aligned to 16

@group(0) @binding(0) var depthTex: texture_depth_2d;
@group(0) @binding(1) var samp:     sampler;
@group(0) @binding(2) var<uniform> params: SSAOParams;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv:        vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    let x = f32(i & 1u) * 4.0 - 1.0;
    let y = 1.0 - f32((i >> 1u) & 1u) * 4.0;
    var out: VertexOutput;
    out.pos = vec4<f32>(x, y, 0.0, 1.0);
    out.uv  = vec2<f32>((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

// 8 cosine-weighted hemisphere sample directions (low-discrepancy)
const SAMPLES = array<vec3<f32>, 8>(
    vec3<f32>( 0.5381,  0.1856,  0.4319),
    vec3<f32>(-0.1379,  0.2486,  0.4430),
    vec3<f32>( 0.3371,  0.5679,  0.0057),
    vec3<f32>(-0.6999,  0.0451,  0.0019),
    vec3<f32>( 0.0689,  0.1598,  0.8547),
    vec3<f32>(-0.3577,  0.5301,  0.4358),
    vec3<f32>( 0.7119,  0.0154,  0.0918),
    vec3<f32>(-0.4776,  0.2847,  0.0271),
);

fn linearizeDepth(d: f32) -> f32 {
    return params.near * params.far / (params.far - d * (params.far - params.near));
}

fn viewPosFromDepth(uv: vec2<f32>, linearDepth: f32) -> vec3<f32> {
    let ndc = uv * 2.0 - 1.0;
    // projection[0][0] = 1/tan(fovX/2), projection[1][1] = 1/tan(fovY/2)
    let tanHalfFovX = 1.0 / params.projection[0][0];
    let tanHalfFovY = 1.0 / params.projection[1][1];
    return vec3<f32>(
         ndc.x * tanHalfFovX * linearDepth,
         ndc.y * tanHalfFovY * linearDepth,
        -linearDepth,   // Vulkan/WebGPU view space: -Z forward
    );
}

fn gradientNoise(pixel: vec2<f32>) -> f32 {
    return fract(52.9829189 * fract(dot(pixel, vec2<f32>(0.06711056, 0.00583715))));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let uv    = input.uv;
    let depth = textureSample(depthTex, samp, uv);

    // Sky pixel — no occlusion
    if (depth >= 1.0) { return vec4<f32>(1.0); }

    let linearDepth = linearizeDepth(depth);
    let origin      = viewPosFromDepth(uv, linearDepth);

    // Noise rotation: use pixel coordinate for interleaved gradient noise
    let pixel  = floor(input.pos.xy);
    let angle  = gradientNoise(pixel) * 6.28318530;
    var randDir = vec3<f32>(cos(angle), sin(angle), 0.0);

    // Estimate surface normal from depth neighbours
    let du = vec2<f32>(params.invW, 0.0);
    let dv = vec2<f32>(0.0, params.invH);
    let dR = linearizeDepth(textureSampleLevel(depthTex, samp, uv + du, 0));
    let dU = linearizeDepth(textureSampleLevel(depthTex, samp, uv + dv, 0));
    var tangent   = normalize(viewPosFromDepth(uv + du, dR) - origin);
    var bitangent = normalize(viewPosFromDepth(uv + dv, dU) - origin);
    var normal    = normalize(cross(tangent, bitangent));
    if (normal.z < 0.0) { normal = -normal; }  // ensure faces camera (+Z in view space)

    // Gram-Schmidt TBN
    tangent   = normalize(randDir - normal * dot(randDir, normal));
    bitangent = cross(normal, tangent);
    let TBN = mat3x3<f32>(tangent, bitangent, normal);  // columns = basis vectors

    var occlusion: f32 = 0.0;
    for (var i: i32 = 0; i < 8; i++) {
        let t     = f32(i + 1) / 8.0;
        let scale = mix(0.1, 1.0, t * t);  // accumulate closer samples more
        let sampleDir = TBN * SAMPLES[i];
        let samplePos = origin + sampleDir * params.radius * scale;

        // Project sample to screen UV
        var proj = params.projection * vec4<f32>(samplePos, 1.0);
        proj /= proj.w;
        let sampleUV = proj.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) { continue; }

        let sampleDepth = linearizeDepth(textureSampleLevel(depthTex, samp, sampleUV, 0));
        let rangeCheck  = smoothstep(0.0, 1.0, params.radius / abs(origin.z - sampleDepth));
        occlusion      += select(0.0, 1.0, sampleDepth >= -samplePos.z + params.bias) * rangeCheck;
    }

    let ao = 1.0 - (occlusion / 8.0);
    return vec4<f32>(ao, 0.0, 0.0, 1.0);
}
