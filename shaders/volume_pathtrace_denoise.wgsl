// M4 v2 P2.1 -- denoise pass for path-traced volumetric rendering (WebGPU).
// Sits between the path-trace and display passes: reads the linear HDR running
// average from the accumulation texture and writes a (denoised) RGBA16Float
// intermediate. P2.1 is plumbing only -- this shader is intentionally a pass-
// through identity copy so we can verify the new pipeline routes correctly
// before adding the real A-trous wavelet filter in P2.2.

@group(0) @binding(0) var inputTex: texture_2d<f32>;

struct VSOut { @builtin(position) position: vec4<f32> };

@vertex
fn vs_main(@builtin(vertex_index) vertIdx: u32) -> VSOut {
    let uv = vec2<f32>(f32((vertIdx << 1u) & 2u), f32(vertIdx & 2u));
    var out: VSOut;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

@fragment
fn fs_main(@builtin(position) fragPos: vec4<f32>) -> @location(0) vec4<f32> {
    let fragCoord = vec2<i32>(fragPos.xy);
    let hdr = textureLoad(inputTex, fragCoord, 0).rgb;
    return vec4<f32>(hdr, 1.0);
}
