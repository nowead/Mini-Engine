#version 450

// Post-process fragment shader (Vulkan)
// Combined ACES Filmic Tonemapping + Bloom composite + SSAO + FXAA 3.11
//
// Pipeline:
//   1. Read HDR + Bloom + SSAO → composite
//   2. Apply AO darkening (ambient occlusion)
//   3. Apply ACES tonemap → LDR
//   4. Apply FXAA on tonemapped result

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D hdrTexture;
layout(set = 0, binding = 1) uniform texture2D bloomTexture;
layout(set = 0, binding = 2) uniform sampler   hdrSampler;
layout(set = 0, binding = 3) uniform texture2D ssaoTexture;

layout(push_constant) uniform PostProcessParams {
    vec2  texelSize;      // 1.0 / resolution
    float bloomStrength;  // [0..1] how much bloom to add
    float exposure;       // EV adjustment applied before ACES
    float aoStrength;     // [0..1] SSAO darkening strength
    float tonemapEnabled; // 1.0 = apply ACES, 0.0 = passthrough (raw HDR clamped)
    float debugView;      // 0=normal, 1-6=GBuffer passthrough, 7=SSAO mask, 8=bloom mask
    float fxaaEnabled;    // 1.0 = FXAA on, 0.0 = no anti-aliasing
} params;

// ---- ACES Filmic Tone Mapping -----------------------------------------------
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ---- Helper: tonemap a single HDR+bloom+SSAO sample at arbitrary UV ---------
vec3 tonemappedSample(vec2 uv) {
    vec3 hdr   = texture(sampler2D(hdrTexture,   hdrSampler), uv).rgb;
    vec3 bloom = texture(sampler2D(bloomTexture, hdrSampler), uv).rgb;
    float ao   = texture(sampler2D(ssaoTexture,  hdrSampler), uv).r;

    // Apply bloom
    vec3 composite = hdr + bloom * params.bloomStrength;

    // Apply ambient occlusion: darken proportional to aoStrength
    // ao=1.0 → fully lit, ao=0.0 → fully occluded
    composite *= mix(1.0, ao, params.aoStrength);

    vec3 exposed = composite * max(params.exposure, 0.001);
    return (params.tonemapEnabled > 0.5) ? ACESFilm(exposed) : clamp(exposed, 0.0, 1.0);
}

// ---- Perceptual Luminance (Rec. 601) ----------------------------------------
float luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

// ---- FXAA 3.11 (simplified) -------------------------------------------------
const float EDGE_THRESHOLD     = 0.125;
const float EDGE_THRESHOLD_MIN = 0.0625;
const float SUBPIX_QUALITY     = 0.75;

void main() {
    vec2 uv = fragUV;
    vec2 ts = params.texelSize;

    // ---- Debug views: bypass full pipeline ----
    // Views 1-6: deferred_lighting already wrote raw G-Buffer data to HDR;
    //            sample it directly (no tonemap, no FXAA).
    if (params.debugView >= 1.0 && params.debugView < 7.0) {
        vec3 raw = texture(sampler2D(hdrTexture, hdrSampler), uv).rgb;
        outColor = vec4(raw, 1.0);
        return;
    }
    // View 7: SSAO mask — show screen-space ambient occlusion buffer as grayscale
    if (params.debugView >= 7.0 && params.debugView < 8.0) {
        float ao = texture(sampler2D(ssaoTexture, hdrSampler), uv).r;
        outColor = vec4(vec3(ao), 1.0);
        return;
    }
    // View 8: Bloom mask — amplify bloom texture to make it visible
    if (params.debugView >= 8.0) {
        vec3 bloom = texture(sampler2D(bloomTexture, hdrSampler), uv).rgb;
        outColor = vec4(min(bloom * 8.0, vec3(1.0)), 1.0);
        return;
    }

    // ---- FXAA disabled: output tonemapped center sample directly ----
    if (params.fxaaEnabled < 0.5) {
        outColor = vec4(tonemappedSample(uv), 1.0);
        return;
    }

    // ---- Cardinal tonemapped samples ----
    vec3 rgbM = tonemappedSample(uv);
    float lumaM = luma(rgbM);
    float lumaN = luma(tonemappedSample(uv + vec2( 0.0, -ts.y)));
    float lumaS = luma(tonemappedSample(uv + vec2( 0.0,  ts.y)));
    float lumaW = luma(tonemappedSample(uv + vec2(-ts.x,  0.0)));
    float lumaE = luma(tonemappedSample(uv + vec2( ts.x,  0.0)));

    float lumaMin   = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax   = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit — no significant edge
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD)) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // ---- Corner samples ----
    float lumaNW = luma(tonemappedSample(uv + vec2(-ts.x, -ts.y)));
    float lumaNE = luma(tonemappedSample(uv + vec2( ts.x, -ts.y)));
    float lumaSW = luma(tonemappedSample(uv + vec2(-ts.x,  ts.y)));
    float lumaSE = luma(tonemappedSample(uv + vec2( ts.x,  ts.y)));

    // Sub-pixel blend factor
    float subpixSum   = 2.0 * (lumaN + lumaS + lumaW + lumaE) + (lumaNW + lumaNE + lumaSW + lumaSE);
    float subpixAvg   = subpixSum / 12.0;
    float subpixBlend = smoothstep(0.0, 1.0, clamp(abs(subpixAvg - lumaM) / lumaRange, 0.0, 1.0));
    float subpixFactor = subpixBlend * subpixBlend * SUBPIX_QUALITY;

    // ---- Edge direction ----
    float edgeH = abs(lumaNW - lumaN  + lumaNE)
                + abs(lumaW  - lumaM  + lumaE)  * 2.0
                + abs(lumaSW - lumaS  + lumaSE);
    float edgeV = abs(lumaNW - lumaW  + lumaSW)
                + abs(lumaN  - lumaM  + lumaS)  * 2.0
                + abs(lumaNE - lumaE  + lumaSE);
    bool isHorizontal = (edgeH >= edgeV);

    float perpStep = isHorizontal ? ts.y : ts.x;
    vec2  edgeStep = isHorizontal ? vec2(ts.x, 0.0) : vec2(0.0, ts.y);

    float lumaPx1 = isHorizontal ? lumaN : lumaW;
    float lumaPx2 = isHorizontal ? lumaS : lumaE;
    float grad1   = abs(lumaPx1 - lumaM);
    float grad2   = abs(lumaPx2 - lumaM);
    bool  is1Steeper    = (grad1 >= grad2);
    float gradThreshold = 0.25 * max(grad1, grad2);
    float perpSign      = is1Steeper ? -1.0 : 1.0;

    vec2 uvEdge = uv;
    if (isHorizontal) uvEdge.y += perpSign * 0.5 * perpStep;
    else              uvEdge.x += perpSign * 0.5 * perpStep;

    float lumaEdgeMid = 0.5 * (lumaM + (is1Steeper ? lumaPx1 : lumaPx2));

    // ---- Walk along edge to find endpoints (12 steps) ----
    vec2  uv1 = uvEdge - edgeStep;
    vec2  uv2 = uvEdge + edgeStep;
    float end1 = luma(tonemappedSample(uv1)) - lumaEdgeMid;
    float end2 = luma(tonemappedSample(uv2)) - lumaEdgeMid;
    bool  done1 = (abs(end1) >= gradThreshold);
    bool  done2 = (abs(end2) >= gradThreshold);

    for (int i = 0; i < 12; i++) {
        if (!done1) { uv1 -= edgeStep; end1 = luma(tonemappedSample(uv1)) - lumaEdgeMid; done1 = abs(end1) >= gradThreshold; }
        if (!done2) { uv2 += edgeStep; end2 = luma(tonemappedSample(uv2)) - lumaEdgeMid; done2 = abs(end2) >= gradThreshold; }
        if (done1 && done2) break;
    }

    float dist1     = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2     = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    float distMin   = min(dist1, dist2);
    float distTotal = dist1 + dist2;

    float lumaM_rel   = lumaM - lumaEdgeMid;
    float endNearer   = (dist1 < dist2) ? end1 : end2;
    bool  correctSide = (endNearer < 0.0) != (lumaM_rel < 0.0);
    float edgeBlend   = correctSide ? (0.5 - distMin / max(distTotal, 0.0001)) : 0.0;

    float blendFactor = max(edgeBlend, subpixFactor);

    vec2 uvFinal = uv;
    if (isHorizontal) uvFinal.y += blendFactor * perpSign * perpStep;
    else              uvFinal.x += blendFactor * perpSign * perpStep;

    outColor = vec4(tonemappedSample(uvFinal), 1.0);
}
