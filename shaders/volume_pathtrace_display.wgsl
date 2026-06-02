// M4 v1 -- display pass for path-traced volumetric rendering (WebGPU). Reads the
// linear HDR running average from the accumulation texture and tonemaps to the
// swapchain. Mirrors volume_pathtrace_display.frag.glsl.

// textureLoad doesn't need a sampler in WGSL, so the bind layout drops the
// sampler slot on this backend. (The Vulkan/GLSL side keeps it for texelFetch.)
@group(0) @binding(0) var accumTex: texture_2d<f32>;

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
    let hdr = textureLoad(accumTex, fragCoord, 0).rgb;
    let ldr = hdr / (1.0 + hdr);
    return vec4<f32>(ldr, 1.0);
}
