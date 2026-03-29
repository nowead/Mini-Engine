// FXAA pass (WebGPU/WASM)
// Input : LDR RGBA8 intermediate texture (written by tonemap pass)
// Output: Anti-aliased image → swapchain
//
// Algorithm: simplified FXAA 3.11 — sub-pixel blend + 12-step edge walk

@group(0) @binding(0) var ldrTexture: texture_2d<f32>;
@group(0) @binding(1) var ldrSampler: sampler;

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

// Perceptual luminance (Rec. 601)
fn luma(rgb: vec3<f32>) -> f32 {
    return dot(rgb, vec3<f32>(0.299, 0.587, 0.114));
}

// Explicit LOD-0 sample for use in non-uniform flow (loops / conditionals)
fn smp(uv: vec2<f32>) -> vec3<f32> {
    return textureSampleLevel(ldrTexture, ldrSampler, uv, 0.0).rgb;
}

// FXAA quality settings
const EDGE_THRESHOLD     : f32 = 0.125;  // minimum local contrast ratio to trigger AA
const EDGE_THRESHOLD_MIN : f32 = 0.0625; // absolute minimum (suppresses noise in dark areas)
const SUBPIX_QUALITY     : f32 = 0.75;   // sub-pixel aliasing blend strength [0..1]

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let uv = input.uv;
    let ts = 1.0 / vec2<f32>(textureDimensions(ldrTexture));

    // ---- Cardinal samples (uniform flow — textureSample with implicit LOD is fine) ----
    let rgbM = textureSample(ldrTexture, ldrSampler, uv).rgb;
    let lumaM = luma(rgbM);
    let lumaN = luma(textureSample(ldrTexture, ldrSampler, uv + vec2<f32>( 0.0, -ts.y)).rgb);
    let lumaS = luma(textureSample(ldrTexture, ldrSampler, uv + vec2<f32>( 0.0,  ts.y)).rgb);
    let lumaW = luma(textureSample(ldrTexture, ldrSampler, uv + vec2<f32>(-ts.x,  0.0)).rgb);
    let lumaE = luma(textureSample(ldrTexture, ldrSampler, uv + vec2<f32>( ts.x,  0.0)).rgb);

    let lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    let lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    let lumaRange = lumaMax - lumaMin;

    // Early exit — no significant edge
    if lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD) {
        return vec4<f32>(rgbM, 1.0);
    }

    // ---- Corner samples (non-uniform flow after conditional return) ----
    let lumaNW = luma(smp(uv + vec2<f32>(-ts.x, -ts.y)));
    let lumaNE = luma(smp(uv + vec2<f32>( ts.x, -ts.y)));
    let lumaSW = luma(smp(uv + vec2<f32>(-ts.x,  ts.y)));
    let lumaSE = luma(smp(uv + vec2<f32>( ts.x,  ts.y)));

    // Sub-pixel blend factor: detects aliasing finer than one pixel
    let subpixSum = 2.0 * (lumaN + lumaS + lumaW + lumaE) + (lumaNW + lumaNE + lumaSW + lumaSE);
    let subpixAvg = subpixSum / 12.0;
    let subpixBlend = smoothstep(0.0, 1.0, clamp(abs(subpixAvg - lumaM) / lumaRange, 0.0, 1.0));
    let subpixFactor = subpixBlend * subpixBlend * SUBPIX_QUALITY;

    // ---- Edge direction (Sobel-like operator) ----
    let edgeH = abs(lumaNW - lumaN + lumaNE)
              + abs(lumaW  - lumaM + lumaE) * 2.0
              + abs(lumaSW - lumaS + lumaSE);
    let edgeV = abs(lumaNW - lumaW  + lumaSW)
              + abs(lumaN  - lumaM  + lumaS) * 2.0
              + abs(lumaNE - lumaE  + lumaSE);
    let isHorizontal = edgeH >= edgeV;

    let perpStep = select(ts.x, ts.y, isHorizontal);
    let edgeStep = select(vec2<f32>(0.0, ts.y), vec2<f32>(ts.x, 0.0), isHorizontal);

    let lumaPx1 = select(lumaW, lumaN, isHorizontal);
    let lumaPx2 = select(lumaE, lumaS, isHorizontal);
    let grad1    = abs(lumaPx1 - lumaM);
    let grad2    = abs(lumaPx2 - lumaM);
    let is1Steeper    = grad1 >= grad2;
    let gradThreshold = 0.25 * max(grad1, grad2);

    let perpSign = select(1.0, -1.0, is1Steeper);

    var uvEdge = uv;
    if isHorizontal {
        uvEdge.y += perpSign * 0.5 * perpStep;
    } else {
        uvEdge.x += perpSign * 0.5 * perpStep;
    }
    let lumaEdgeMid = 0.5 * (lumaM + select(lumaPx2, lumaPx1, is1Steeper));

    // ---- Walk along edge to find its endpoints ----
    var uv1 = uvEdge - edgeStep;
    var uv2 = uvEdge + edgeStep;
    var end1 = luma(smp(uv1)) - lumaEdgeMid;
    var end2 = luma(smp(uv2)) - lumaEdgeMid;
    var done1 = abs(end1) >= gradThreshold;
    var done2 = abs(end2) >= gradThreshold;

    for (var i = 0; i < 12; i++) {
        if !done1 {
            uv1 -= edgeStep;
            end1 = luma(smp(uv1)) - lumaEdgeMid;
            done1 = abs(end1) >= gradThreshold;
        }
        if !done2 {
            uv2 += edgeStep;
            end2 = luma(smp(uv2)) - lumaEdgeMid;
            done2 = abs(end2) >= gradThreshold;
        }
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
    if isHorizontal {
        uvFinal.y += blendFactor * perpSign * perpStep;
    } else {
        uvFinal.x += blendFactor * perpSign * perpStep;
    }

    return vec4<f32>(smp(uvFinal), 1.0);
}
