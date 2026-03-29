// Tonemap pass: HDR render target → swapchain (ACES filmic + gamma correction)
// WebGPU/WASM path — fullscreen triangle, no vertex buffer

@group(0) @binding(0) var hdrTexture: texture_2d<f32>;
@group(0) @binding(1) var hdrSampler: sampler;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    // Fullscreen triangle — 3 vertices cover the entire screen
    // Vertex 0: NDC (-1, 1) = top-left
    // Vertex 1: NDC ( 3, 1) = top-right (extends past edge)
    // Vertex 2: NDC (-1,-3) = bottom-left (extends past edge)
    let x = f32(i & 1u) * 4.0 - 1.0;
    let y = 1.0 - f32((i >> 1u) & 1u) * 4.0;
    var out: VertexOutput;
    out.pos = vec4<f32>(x, y, 0.0, 1.0);
    // WebGPU UV: (0,0) = top-left; NDC y=1 = top → uv.y = (1 - y) / 2
    out.uv = vec2<f32>((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

fn ACESFilm(x: vec3<f32>) -> vec3<f32> {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let hdr = textureSample(hdrTexture, hdrSampler, input.uv).rgb;

    // ACES filmic tone mapping
    let ldr = ACESFilm(hdr);

    // Gamma correction (WebGPU swapchain is BGRA8Unorm — no automatic sRGB conversion)
    let gamma = pow(ldr, vec3<f32>(1.0 / 2.2));

    return vec4<f32>(gamma, 1.0);
}
