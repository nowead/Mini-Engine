// TAA Resolve — WebGPU WGSL version (sub-task C2 port)
// Fullscreen pass: reproject previous resolved frame (history) by the G-Buffer
// velocity, clip to the current 3x3 neighborhood (variance clipping), blend.
// Mirrors taa_resolve.comp.glsl. WebGPU uses a fullscreen render pass (not a
// compute storage write) to match the rest of the WGSL post chain.
//
//   binding 0 — current HDR color (this frame's lighting result)
//   binding 1 — history (previous resolved frame)
//   binding 2 — velocity (G-Buffer target 3, RG = curr-prev UV)
//   binding 3 — linear sampler
//   binding 4 — params UBO { invW, invH, blend, historyValid }

struct Params {
    invW:         f32,
    invH:         f32,
    blend:        f32,
    historyValid: f32,   // 0 on first frame / after resize → output current only
}

@group(0) @binding(0) var currColor:    texture_2d<f32>;
@group(0) @binding(1) var historyColor: texture_2d<f32>;
@group(0) @binding(2) var velocityTex:  texture_2d<f32>;
@group(0) @binding(3) var samp:         sampler;
@group(0) @binding(4) var<uniform> params: Params;

struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0)       uv:  vec2<f32>,
}

// Fullscreen triangle.
@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> VsOut {
    var p = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0),
    );
    var out: VsOut;
    out.pos = vec4<f32>(p[vi], 0.0, 1.0);
    // UV in [0,1] with top-left origin (WebGPU framebuffer convention).
    out.uv  = vec2<f32>((p[vi].x + 1.0) * 0.5, (1.0 - p[vi].y) * 0.5);
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let uv   = in.uv;
    let curr = textureSampleLevel(currColor, samp, uv, 0.0).rgb;

    if (params.historyValid < 0.5) {
        return vec4<f32>(curr, 1.0);   // seed history
    }

    // 3x3 neighborhood min/max of the current frame (variance clipping).
    var nmin = curr;
    var nmax = curr;
    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            if (x == 0 && y == 0) { continue; }
            let o = vec2<f32>(f32(x) * params.invW, f32(y) * params.invH);
            let c = textureSampleLevel(currColor, samp, uv + o, 0.0).rgb;
            nmin = min(nmin, c);
            nmax = max(nmax, c);
        }
    }

    // Reproject history: velocity = currUV - prevUV, so prev sample is uv - vel.
    let vel    = textureSampleLevel(velocityTex, samp, uv, 0.0).rg;
    let histUV = uv - vel;

    var result: vec3<f32>;
    if (histUV.x < 0.0 || histUV.x > 1.0 || histUV.y < 0.0 || histUV.y > 1.0) {
        result = curr;   // disocclusion / off-screen → no history
    } else {
        var hist = textureSampleLevel(historyColor, samp, histUV, 0.0).rgb;
        hist     = clamp(hist, nmin, nmax);   // variance clipping
        result   = mix(curr, hist, params.blend);
    }
    return vec4<f32>(result, 1.0);
}
