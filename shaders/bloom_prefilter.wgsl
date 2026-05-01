// Bloom prefilter: extracts bright pixels from HDR buffer
// Output: half-resolution RGBA16Float bloom seed texture

@group(0) @binding(0) var hdrTexture: texture_2d<f32>;
@group(0) @binding(1) var hdrSampler: sampler;

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

// Soft-knee threshold parameters
const BLOOM_THRESHOLD : f32 = 1.0;   // HDR luminance above which bloom activates
const BLOOM_KNEE      : f32 = 0.5;   // Soft knee half-width for smooth falloff

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let color      = textureSample(hdrTexture, hdrSampler, input.uv).rgb;
    let brightness = max(color.r, max(color.g, color.b));

    // Quadratic soft-knee: smooth transition around threshold
    let lo  = BLOOM_THRESHOLD - BLOOM_KNEE;
    let rq  = clamp(brightness - lo, 0.0, 2.0 * BLOOM_KNEE);
    let weight       = (rq * rq) / (4.0 * BLOOM_KNEE * BLOOM_KNEE + 0.00001);
    let contribution = color * max(weight, brightness - BLOOM_THRESHOLD)
                             / max(brightness, 0.00001);

    return vec4<f32>(max(contribution, vec3<f32>(0.0)), 1.0);
}
