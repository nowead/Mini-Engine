// Unified PostProcess pass (WebGPU/WASM)
// Reads: HDR buffer + Bloom + SSAO
// Applies: SSAO composite → Bloom composite → ACES tonemap → FXAA → swapchain
//
// Replaces the two-pass tonemap.wgsl + fxaa.wgsl pipeline.
// FXAA samples neighboring pixels via hdrTexture and applies a quick
// tonemap+gamma to approximate the LDR values that FXAA expects.

@group(0) @binding(0) var hdrTexture:   texture_2d<f32>;
@group(0) @binding(1) var bloomTexture: texture_2d<f32>;
@group(0) @binding(2) var ssaoTexture:  texture_2d<f32>;
@group(0) @binding(3) var hdrSampler:   sampler;

struct PostProcessParams {
    bloomStrength : f32,
    exposure      : f32,
    aoStrength    : f32,
    debugView     : i32,
    fxaaOn        : u32,
    tonemapOn     : u32,
    texelW        : f32,
    texelH        : f32,
}
@group(0) @binding(4) var<uniform> params: PostProcessParams;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    let x = f32(i & 1u) * 4.0 - 1.0;
    let y = 1.0 - f32((i >> 1u) & 1u) * 4.0;
    var out: VertexOutput;
    out.pos = vec4<f32>(x, y, 0.0, 1.0);
    out.uv = vec2<f32>((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

fn ACESFilm(x: vec3<f32>) -> vec3<f32> {
    let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn luma(rgb: vec3<f32>) -> f32 {
    return dot(rgb, vec3<f32>(0.299, 0.587, 0.114));
}

// Tonemap + gamma a raw HDR neighbor sample (used by FXAA edge detection)
fn smpLDR(uv: vec2<f32>) -> vec3<f32> {
    let hdr = textureSampleLevel(hdrTexture, hdrSampler, uv, 0.0).rgb;
    let ldr = ACESFilm(hdr * params.exposure);
    return pow(ldr, vec3<f32>(1.0 / 2.2));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let uv = input.uv;

    let hdr   = textureSample(hdrTexture,   hdrSampler, uv).rgb;
    let bloom = textureSample(bloomTexture, hdrSampler, uv).rgb;
    let ao    = textureSample(ssaoTexture,  hdrSampler, uv).r;

    // Debug view: bypass composite and show individual G-Buffer channels
    // 7=ssao, 8=bloom (other channels handled in deferred lighting pass)
    if params.debugView == 7 {
        return vec4<f32>(ao, ao, ao, 1.0);
    }
    if params.debugView == 8 {
        return vec4<f32>(bloom, 1.0);
    }

    // SSAO + Bloom composite
    let aoFactor  = mix(1.0, ao, params.aoStrength);
    let exposed   = hdr * params.exposure;
    let composite = exposed * aoFactor + bloom * params.bloomStrength;

    // Tonemap (conditional)
    var ldrColor: vec3<f32>;
    if params.tonemapOn != 0u {
        ldrColor = ACESFilm(composite);
    } else {
        ldrColor = clamp(composite, vec3<f32>(0.0), vec3<f32>(1.0));
    }

    // Gamma correction (WebGPU swapchain has no automatic sRGB conversion)
    let gamma = pow(ldrColor, vec3<f32>(1.0 / 2.2));

    // Early exit if FXAA disabled
    if params.fxaaOn == 0u {
        return vec4<f32>(gamma, 1.0);
    }

    // ---- FXAA (simplified 3.11) ----
    // Neighbors are approximated by sampling hdrTexture and applying tonemap+gamma,
    // since we have no separate LDR intermediate texture in this unified pass.
    let ts = vec2<f32>(params.texelW, params.texelH);

    let lumaM = luma(gamma);
    let lumaN = luma(smpLDR(uv + vec2<f32>( 0.0, -ts.y)));
    let lumaS = luma(smpLDR(uv + vec2<f32>( 0.0,  ts.y)));
    let lumaW = luma(smpLDR(uv + vec2<f32>(-ts.x,  0.0)));
    let lumaE = luma(smpLDR(uv + vec2<f32>( ts.x,  0.0)));

    let lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    let lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    let lumaRange = lumaMax - lumaMin;

    let EDGE_THRESHOLD     = 0.125;
    let EDGE_THRESHOLD_MIN = 0.0625;
    let SUBPIX_QUALITY     = 0.75;

    // Early exit — no significant edge
    if lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD) {
        return vec4<f32>(gamma, 1.0);
    }

    // Corner samples (non-uniform flow after conditional return)
    let lumaNW = luma(smpLDR(uv + vec2<f32>(-ts.x, -ts.y)));
    let lumaNE = luma(smpLDR(uv + vec2<f32>( ts.x, -ts.y)));
    let lumaSW = luma(smpLDR(uv + vec2<f32>(-ts.x,  ts.y)));
    let lumaSE = luma(smpLDR(uv + vec2<f32>( ts.x,  ts.y)));

    let subpixSum = 2.0 * (lumaN + lumaS + lumaW + lumaE) + (lumaNW + lumaNE + lumaSW + lumaSE);
    let subpixAvg = subpixSum / 12.0;
    let subpixBlend = smoothstep(0.0, 1.0, clamp(abs(subpixAvg - lumaM) / lumaRange, 0.0, 1.0));
    let subpixFactor = subpixBlend * subpixBlend * SUBPIX_QUALITY;

    let edgeH = abs(lumaNW - lumaN + lumaNE) + abs(lumaW - lumaM + lumaE) * 2.0 + abs(lumaSW - lumaS + lumaSE);
    let edgeV = abs(lumaNW - lumaW + lumaSW) + abs(lumaN - lumaM + lumaS) * 2.0 + abs(lumaNE - lumaE + lumaSE);
    let isHorizontal = edgeH >= edgeV;

    let perpStep = select(ts.x, ts.y, isHorizontal);
    let edgeStep = select(vec2<f32>(0.0, ts.y), vec2<f32>(ts.x, 0.0), isHorizontal);

    let lumaPx1 = select(lumaW, lumaN, isHorizontal);
    let lumaPx2 = select(lumaE, lumaS, isHorizontal);
    let grad1 = abs(lumaPx1 - lumaM);
    let grad2 = abs(lumaPx2 - lumaM);
    let is1Steeper = grad1 >= grad2;
    let gradThreshold = 0.25 * max(grad1, grad2);
    let perpSign = select(1.0, -1.0, is1Steeper);

    var uvEdge = uv;
    if isHorizontal { uvEdge.y += perpSign * 0.5 * perpStep; }
    else            { uvEdge.x += perpSign * 0.5 * perpStep; }
    let lumaEdgeMid = 0.5 * (lumaM + select(lumaPx2, lumaPx1, is1Steeper));

    var uv1 = uvEdge - edgeStep;
    var uv2 = uvEdge + edgeStep;
    var end1 = luma(smpLDR(uv1)) - lumaEdgeMid;
    var end2 = luma(smpLDR(uv2)) - lumaEdgeMid;
    var done1 = abs(end1) >= gradThreshold;
    var done2 = abs(end2) >= gradThreshold;

    for (var i = 0; i < 12; i++) {
        if !done1 { uv1 -= edgeStep; end1 = luma(smpLDR(uv1)) - lumaEdgeMid; done1 = abs(end1) >= gradThreshold; }
        if !done2 { uv2 += edgeStep; end2 = luma(smpLDR(uv2)) - lumaEdgeMid; done2 = abs(end2) >= gradThreshold; }
        if done1 && done2 { break; }
    }

    let dist1 = select(uv.y - uv1.y, uv.x - uv1.x, isHorizontal);
    let dist2 = select(uv2.y - uv.y, uv2.x - uv.x, isHorizontal);
    let distMin   = min(dist1, dist2);
    let distTotal = dist1 + dist2;
    let lumaM_rel   = lumaM - lumaEdgeMid;
    let endNearer   = select(end2, end1, dist1 < dist2);
    let correctSide = (endNearer < 0.0) != (lumaM_rel < 0.0);
    let edgeBlend   = select(0.0, 0.5 - distMin / max(distTotal, 0.0001), correctSide);
    let blendFactor = max(edgeBlend, subpixFactor);

    var uvFinal = uv;
    if isHorizontal { uvFinal.y += blendFactor * perpSign * perpStep; }
    else            { uvFinal.x += blendFactor * perpSign * perpStep; }

    return vec4<f32>(smpLDR(uvFinal), 1.0);
}
