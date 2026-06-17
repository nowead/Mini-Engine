// M4 v2 P2.2 -- single-iteration A-trous wavelet denoiser (WebGPU). Reads the
// linear-HDR running average from the path-trace accumulation and writes a
// color-guided cross-bilateral 5x5 blur to the denoise intermediate. P2.3 will
// add multi-iteration ping-pong with exponentially growing tap spacing; here we
// stay at spacing=1 (the standard A-trous level-0 kernel) so the cost is bounded
// and the visual change is easy to read.

@group(0) @binding(0) var inputTex: texture_2d<f32>;

struct VSOut { @builtin(position) position: vec4<f32> };

@vertex
fn vs_main(@builtin(vertex_index) vertIdx: u32) -> VSOut {
    let uv = vec2<f32>(f32((vertIdx << 1u) & 2u), f32(vertIdx & 2u));
    var out: VSOut;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

// 5x5 separable kernel weights (1, 4, 6, 4, 1) / 16 -> 2D outer product.
// Embedded as a 25-entry lookup for the simple non-separable inner loop.
fn atrousWeight(dx: i32, dy: i32) -> f32 {
    let w = array<f32, 5>(1.0/16.0, 4.0/16.0, 6.0/16.0, 4.0/16.0, 1.0/16.0);
    return w[dx + 2] * w[dy + 2];
}

@fragment
fn fs_main(@builtin(position) fragPos: vec4<f32>) -> @location(0) vec4<f32> {
    let dims      = vec2<i32>(textureDimensions(inputTex));
    let center    = vec2<i32>(fragPos.xy);
    let centerCol = textureLoad(inputTex, center, 0).rgb;

    // Color-similarity weight: tighter sigma -> more edges preserved.
    // 0.35 reads HDR-linear noise as "the same surface" without blurring across
    // sharp transfer-function ridges.
    let sigmaC2 = 0.35 * 0.35;

    // A-trous level-2 tap spacing: 25 taps cover a ~32x32 px window. Single-
    // iteration spatial blur is intentionally gentle -- progressive temporal
    // accumulation handles most of the convergence; this pass cleans grain in
    // the first few frames after camera motion. P2.3 will cascade three
    // ping-pong iterations at strides 1/2/4 for stronger visible smoothing.
    let stride = 4;

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
