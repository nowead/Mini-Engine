// Bloom separable Gaussian blur (two entry points: horizontal and vertical)
// Input/output: half-resolution RGBA16Float bloom texture

@group(0) @binding(0) var bloomTexture: texture_2d<f32>;
@group(0) @binding(1) var bloomSampler: sampler;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv:       vec2<f32>,
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

// 7-tap Gaussian kernel (σ ≈ 1.5, sum = 1.0)
const W0: f32 = 0.0625;   // offset ±3
const W1: f32 = 0.125;    // offset ±2
const W2: f32 = 0.1875;   // offset ±1
const W3: f32 = 0.25;     // offset  0

fn gaussian_blur(uv: vec2<f32>, step: vec2<f32>) -> vec4<f32> {
    // textureSampleLevel: safe in non-uniform flow (after conditional returns in FXAA)
    var acc  = textureSampleLevel(bloomTexture, bloomSampler, uv,              0.0).rgb * W3;
    acc     += textureSampleLevel(bloomTexture, bloomSampler, uv - step,       0.0).rgb * W2;
    acc     += textureSampleLevel(bloomTexture, bloomSampler, uv + step,       0.0).rgb * W2;
    acc     += textureSampleLevel(bloomTexture, bloomSampler, uv - step * 2.0, 0.0).rgb * W1;
    acc     += textureSampleLevel(bloomTexture, bloomSampler, uv + step * 2.0, 0.0).rgb * W1;
    acc     += textureSampleLevel(bloomTexture, bloomSampler, uv - step * 3.0, 0.0).rgb * W0;
    acc     += textureSampleLevel(bloomTexture, bloomSampler, uv + step * 3.0, 0.0).rgb * W0;
    return vec4<f32>(acc, 1.0);
}

@fragment
fn fs_horizontal(input: VertexOutput) -> @location(0) vec4<f32> {
    let step = vec2<f32>(1.0 / f32(textureDimensions(bloomTexture).x), 0.0);
    return gaussian_blur(input.uv, step);
}

@fragment
fn fs_vertical(input: VertexOutput) -> @location(0) vec4<f32> {
    let step = vec2<f32>(0.0, 1.0 / f32(textureDimensions(bloomTexture).y));
    return gaussian_blur(input.uv, step);
}
