// M4 v2 P2.3 -- multi-iteration A-trous wavelet denoiser (WebGPU). 5x5 cross-
// bilateral kernel with a color guide, run three times at exponentially growing
// tap spacings (1, 2, 4) using ping-pong denoise textures. Each iteration binds
// its own tiny stride UBO so a single pipeline drives all three passes.

struct StrideUBO {
    // x = tap spacing in pixels for this iteration. yzw reserved for future
    // per-iteration parameters (e.g. sigma multipliers, level index).
    stride: vec4<u32>,
};

@group(0) @binding(0) var inputTex: texture_2d<f32>;
@group(0) @binding(1) var<uniform> params: StrideUBO;

struct VSOut { @builtin(position) position: vec4<f32> };

@vertex
fn vs_main(@builtin(vertex_index) vertIdx: u32) -> VSOut {
    let uv = vec2<f32>(f32((vertIdx << 1u) & 2u), f32(vertIdx & 2u));
    var out: VSOut;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

fn atrousWeight(dx: i32, dy: i32) -> f32 {
    let w = array<f32, 5>(1.0/16.0, 4.0/16.0, 6.0/16.0, 4.0/16.0, 1.0/16.0);
    return w[dx + 2] * w[dy + 2];
}

@fragment
fn fs_main(@builtin(position) fragPos: vec4<f32>) -> @location(0) vec4<f32> {
    let dims      = vec2<i32>(textureDimensions(inputTex));
    let center    = vec2<i32>(fragPos.xy);
    let centerCol = textureLoad(inputTex, center, 0).rgb;

    // Color-similarity weight: tighter sigma -> more edges preserved. 0.35 in
    // HDR-linear works across the noise magnitudes we see at SPP=1..8. Same
    // constant across iterations -- the stride change alone widens the reach.
    let sigmaC2 = 0.35 * 0.35;
    let stride  = i32(params.stride.x);

    var accum: vec3<f32> = vec3<f32>(0.0);
    var wSum:  f32       = 0.0;
    for (var dy = -2; dy <= 2; dy = dy + 1) {
        for (var dx = -2; dx <= 2; dx = dx + 1) {
            let p   = clamp(center + vec2<i32>(dx, dy) * stride,
                            vec2<i32>(0, 0), dims - vec2<i32>(1, 1));
            let col = textureLoad(inputTex, p, 0).rgb;
            let d   = col - centerCol;
            let wC  = exp(-dot(d, d) / sigmaC2);
            let wK  = atrousWeight(dx, dy);
            let w   = wK * wC;
            accum = accum + col * w;
            wSum  = wSum + w;
        }
    }
    let denoised = accum / max(wSum, 1e-6);
    return vec4<f32>(denoised, 1.0);
}
