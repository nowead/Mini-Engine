// SSAO Bilateral Blur — WebGPU render-pipeline version of ssao_blur.comp.glsl
// 5×5 Gaussian kernel weighted by depth similarity (edge-preserving).
// Output: R8Unorm color attachment (r = blurred AO)

struct SSAOBlurParams {
    invW:           f32,
    invH:           f32,
    near:           f32,
    far:            f32,
    depthThreshold: f32,
    _pad0:          f32,
    _pad1:          f32,
    _pad2:          f32,
}  // 32 bytes (2 × 16-byte alignment blocks)

@group(0) @binding(0) var aoTex:    texture_2d<f32>;
@group(0) @binding(1) var depthTex: texture_depth_2d;
@group(0) @binding(2) var samp:     sampler;
@group(0) @binding(3) var<uniform> params: SSAOBlurParams;

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

fn linearizeDepth(d: f32) -> f32 {
    return params.near * params.far / (params.far - d * (params.far - params.near));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let uv          = input.uv;
    let centerDepth = linearizeDepth(textureSample(depthTex, samp, uv));

    var totalAO:     f32 = 0.0;
    var totalWeight: f32 = 0.0;

    let RADIUS = 2;
    let r      = f32(RADIUS);

    for (var y: i32 = -RADIUS; y <= RADIUS; y++) {
        for (var x: i32 = -RADIUS; x <= RADIUS; x++) {
            let offset = vec2<f32>(f32(x), f32(y)) * vec2<f32>(params.invW, params.invH);
            let sUV    = uv + offset;

            let sDepth    = linearizeDepth(textureSample(depthTex, samp, sUV));
            let depthDiff = abs(centerDepth - sDepth);

            let spatialW = exp(-f32(x * x + y * y) / (2.0 * r * r));
            let depthW   = exp(-depthDiff / params.depthThreshold);
            let w        = spatialW * depthW;

            totalAO     += textureSample(aoTex, samp, sUV).r * w;
            totalWeight += w;
        }
    }

    let result = select(totalAO / totalWeight, 1.0, totalWeight <= 0.0);
    return vec4<f32>(result, 0.0, 0.0, 1.0);
}
