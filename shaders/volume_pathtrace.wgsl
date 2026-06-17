// M4 v1 -- volumetric path tracing with progressive temporal accumulation (WebGPU).
// Mirrors volume_pathtrace.frag.glsl: integrator is the v0 Woodcock + Henyey-
// Greenstein + single-light NEE + inline-SPP loop, but the output writes linear
// HDR into a ping-pong history texture instead of tonemapped sRGB; a separate
// display shader handles the tonemap once the average is final. Tonemap must
// run AFTER averaging because Reinhard is non-linear.

struct VolumeUBO {
    invView:   mat4x4<f32>,
    invProj:   mat4x4<f32>,
    cameraPos: vec4<f32>,
    aabbMin:   vec4<f32>,
    aabbMax:   vec4<f32>,
    params:    vec4<f32>,
    tf:        vec4<f32>,
    lowColor:  vec4<f32>,
    highColor: vec4<f32>,
    window:    vec4<f32>,
    light:     vec4<f32>,
    shade:     vec4<f32>,
    shadow:    vec4<f32>,
    occ:       vec4<f32>,
    pathtrace: vec4<f32>,
    accum:     vec4<f32>,   // x = previous accumulated sample count N
    volSize:   vec4<f32>,   // M3-3 xyz = source volume voxel dims (brick indexing)
    atlasGrid: vec4<f32>,   // M3-3 xyz = atlas capacity in bricks (slot unpack)
    envTop:    vec4<f32>,   // M4 v2 P1 rgb = sky top color, w = intensity multiplier
    envBot:    vec4<f32>,   // M4 v2 P1 rgb = sky bottom color, w = enable flag (0/1)
};

@group(0) @binding(0) var<uniform> ubo: VolumeUBO;
@group(0) @binding(1) var volumeTex0:    texture_3d<f32>;     // brick atlas L0 (R16Float)
@group(0) @binding(2) var volumeSampler: sampler;
@group(0) @binding(3) var tfLUT:         texture_2d<f32>;
@group(0) @binding(4) var historyTex:    texture_2d<f32>;
@group(0) @binding(5) var<storage, read> pageSlots: array<u32>;  // page table: (lod<<30)|slot or 0xFFFFFFFF
@group(0) @binding(6) var volumeTex1:    texture_3d<f32>;     // brick atlas L1 (beta-5)
@group(0) @binding(7) var volumeTex2:    texture_3d<f32>;     // brick atlas L2 (beta-5)
@group(0) @binding(8) var volumeTex3:    texture_3d<f32>;     // brick atlas L3 (beta-5)

// beta-5: page entry packs (lod<<30)|slot. Decode + sample the chosen LOD's
// atlas. brickStored(lod) = (64>>lod)+2.
fn sampleVolume(uvw: vec3<f32>) -> f32 {
    let vp = clamp(uvw, vec3<f32>(0.0), vec3<f32>(0.999999)) * ubo.volSize.xyz;
    let brickIdx = vec3<i32>(vp) / 64;
    let localF   = vp - vec3<f32>(brickIdx * 64);   // [0, 64) in L0 source voxels
    let pageGrid = vec3<i32>((ubo.volSize.xyz + 63.0) / 64.0);
    let pageIdx  = (brickIdx.z * pageGrid.y + brickIdx.y) * pageGrid.x + brickIdx.x;
    let page     = pageSlots[pageIdx];
    if (page == 0xFFFFFFFFu) { return 0.0; }
    let slot = page & 0x3FFFFFFFu;
    let lod  = page >> 30u;
    let atlasG      = vec3<i32>(ubo.atlasGrid.xyz);
    let brickStored = i32(64u >> lod) + 2;
    let sx = i32(slot) % atlasG.x;
    let sy = (i32(slot) / atlasG.x) % atlasG.y;
    let sz = i32(slot) / (atlasG.x * atlasG.y);
    let slotOrigin = vec3<f32>(f32(sx * brickStored + 1),
                               f32(sy * brickStored + 1),
                               f32(sz * brickStored + 1));
    let localFlod = localF / f32(1u << lod);
    let atlasVox  = slotOrigin + localFlod;
    let atlasUvw  = (atlasVox + vec3<f32>(0.5)) / vec3<f32>(atlasG * brickStored);
    if (lod == 0u) { return textureSampleLevel(volumeTex0, volumeSampler, atlasUvw, 0.0).r; }
    if (lod == 1u) { return textureSampleLevel(volumeTex1, volumeSampler, atlasUvw, 0.0).r; }
    if (lod == 2u) { return textureSampleLevel(volumeTex2, volumeSampler, atlasUvw, 0.0).r; }
    return                  textureSampleLevel(volumeTex3, volumeSampler, atlasUvw, 0.0).r;
}

const PI: f32 = 3.14159265359;

struct VSOut { @builtin(position) position: vec4<f32> };

@vertex
fn vs_main(@builtin(vertex_index) vertIdx: u32) -> VSOut {
    let uv = vec2<f32>(f32((vertIdx << 1u) & 2u), f32(vertIdx & 2u));
    var out: VSOut;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

fn pcg(v: u32) -> u32 {
    let state = v * 747796405u + 2891336453u;
    let word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
fn rnd(seed: ptr<function, u32>) -> f32 {
    *seed = pcg(*seed);
    return f32(*seed) / 4294967296.0;
}

fn hgPhase(cosTheta: f32, g: f32) -> f32 {
    let denom = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * PI * pow(max(denom, 1e-6), 1.5));
}
fn sampleHG(wi: vec3<f32>, g: f32, seed: ptr<function, u32>) -> vec3<f32> {
    let u1 = rnd(seed);
    let u2 = rnd(seed);
    var cosTheta: f32;
    if (abs(g) < 1e-3) {
        cosTheta = 1.0 - 2.0 * u1;
    } else {
        let sqr = (1.0 - g * g) / (1.0 - g + 2.0 * g * u1);
        cosTheta = (1.0 + g * g - sqr * sqr) / (2.0 * g);
    }
    cosTheta = clamp(cosTheta, -1.0, 1.0);
    let sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    let phi = 2.0 * PI * u2;
    let w  = wi;
    let ax = select(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), abs(w.x) > 0.1);
    let u  = normalize(cross(ax, w));
    let v  = cross(w, u);
    return normalize(u * sinTheta * cos(phi) + v * sinTheta * sin(phi) + w * cosTheta);
}

fn sampleDensity(uvw: vec3<f32>) -> f32 {
    let raw = sampleVolume(uvw);
    let n   = clamp((raw - (ubo.window.x - ubo.window.y * 0.5)) /
                    max(ubo.window.y, 1e-6), 0.0, 1.0);
    return max(n * ubo.params.z - ubo.tf.x, 0.0);
}
fn sampleTF(density: f32) -> vec4<f32> {
    if (ubo.tf.z > 0.5) {
        return textureSampleLevel(tfLUT, volumeSampler,
                                  vec2<f32>(clamp(density, 0.0, 1.0), 0.5), 0.0);
    }
    let c = mix(ubo.lowColor.rgb, ubo.highColor.rgb,
                clamp(density * ubo.tf.y, 0.0, 1.0));
    return vec4<f32>(c, clamp(density, 0.0, 1.0));
}
// M4 v2 P1: simple top-bottom gradient sky. Direction dir.y > 0 leans towards
// envTop, < 0 towards envBot. Disabled when envBot.w < 0.5 -- caller multiplies
// by zero so the v1 black-background behaviour is preserved.
fn sampleEnvironment(dir: vec3<f32>) -> vec3<f32> {
    if (ubo.envBot.w < 0.5) { return vec3<f32>(0.0); }
    let t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    let col = mix(ubo.envBot.rgb, ubo.envTop.rgb, t);
    return col * ubo.envTop.w;
}

fn intersectAABB(ro: vec3<f32>, rd: vec3<f32>,
                 bmin: vec3<f32>, bmax: vec3<f32>) -> vec2<f32> {
    let invD = 1.0 / rd;
    let t0 = (bmin - ro) * invD;
    let t1 = (bmax - ro) * invD;
    let ts = min(t0, t1);
    let tb = max(t0, t1);
    return vec2<f32>(max(max(ts.x, ts.y), ts.z), min(min(tb.x, tb.y), tb.z));
}

fn transmittance(p: vec3<f32>, L: vec3<f32>,
                 bmin: vec3<f32>, bmax: vec3<f32>, boxSize: vec3<f32>,
                 sigmaMax: f32, seed: ptr<function, u32>) -> f32 {
    let hit = intersectAABB(p, L, bmin, bmax);
    let tFar = hit.y;
    if (tFar <= 0.0) { return 1.0; }
    var t: f32 = 0.0;
    for (var i = 0; i < 64; i = i + 1) {
        t = t + (-log(max(1.0 - rnd(seed), 1e-6)) / sigmaMax);
        if (t >= tFar) { return 1.0; }
        let q = p + L * t;
        let uvw = (q - bmin) / boxSize;
        let sigma_t = sampleDensity(uvw) * ubo.params.y;
        if (rnd(seed) < sigma_t / sigmaMax) { return 0.0; }
    }
    return 1.0;
}

fn tracePath(ro_in: vec3<f32>, rd_in: vec3<f32>,
             bmin: vec3<f32>, bmax: vec3<f32>,
             sigmaMax: f32, seed: ptr<function, u32>) -> vec3<f32> {
    var hit = intersectAABB(ro_in, rd_in, bmin, bmax);
    let tNear = max(hit.x, 0.0);
    let tFar  = hit.y;
    // Primary ray missed the volume entirely -> the background colour the
    // path-trace contributes is the environment sample (or black when env is
    // disabled). Without this the background stayed clamped at 0 even with
    // env on.
    if (tNear >= tFar) { return sampleEnvironment(rd_in); }

    let boxSize = bmax - bmin;
    var ro = ro_in + rd_in * tNear;
    var rd = rd_in;
    var remaining = tFar - tNear;

    var result     = vec3<f32>(0.0);
    var throughput = vec3<f32>(1.0);
    let L          = normalize(ubo.light.xyz);
    let lightI     = ubo.shade.y;
    let ambient    = ubo.shade.x;
    let g          = ubo.pathtrace.y;
    let maxBounce  = max(0, i32(ubo.pathtrace.w));

    for (var b = 0; b <= maxBounce; b = b + 1) {
        var t: f32 = 0.0;
        var exited = false;
        for (var it = 0; it < 128; it = it + 1) {
            t = t + (-log(max(1.0 - rnd(seed), 1e-6)) / sigmaMax);
            if (t >= remaining) { exited = true; break; }
            let q   = ro + rd * t;
            let uvw = (q - bmin) / boxSize;
            let sigma_t = sampleDensity(uvw) * ubo.params.y;
            if (rnd(seed) < sigma_t / sigmaMax) { break; }
        }
        if (exited) {
            // The path escaped the volume without another scatter event. The
            // remaining throughput now lights the camera via whatever it sees
            // in that direction -- the environment for b > 0, or background
            // when this was the primary ray (handled by the tNear>=tFar guard
            // above too).
            result = result + throughput * sampleEnvironment(rd);
            break;
        }

        let p = ro + rd * t;
        let uvw = (p - bmin) / boxSize;
        let density = sampleDensity(uvw);
        let albedo  = sampleTF(density).rgb;

        let Tl    = transmittance(p, L, bmin, bmax, boxSize, sigmaMax, seed);
        let phase = hgPhase(dot(-rd, L), g);
        result = result + throughput * albedo * lightI * phase * Tl;
        result = result + throughput * albedo * ambient;

        if (b == maxBounce) { break; }
        rd = sampleHG(rd, g, seed);
        ro = p;
        let hit2 = intersectAABB(ro, rd, bmin, bmax);
        if (hit2.y <= 0.0) { break; }
        remaining = hit2.y;
        throughput = throughput * albedo;

        let rrProb = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.1, 0.95);
        if (rnd(seed) > rrProb) { break; }
        throughput = throughput / rrProb;
    }
    return result;
}

@fragment
fn fs_main(@builtin(position) fragPos: vec4<f32>) -> @location(0) vec4<f32> {
    let screenSize = vec2<f32>(textureDimensions(historyTex));
    let uv         = fragPos.xy / screenSize;
    let ndcXY      = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    var vFar = ubo.invProj * vec4<f32>(ndcXY, 1.0, 1.0);
    vFar = vFar / vFar.w;
    let worldFar = (ubo.invView * vFar).xyz;
    let ro = ubo.cameraPos.xyz;
    let rd = normalize(worldFar - ro);

    let bmin = ubo.aabbMin.xyz;
    let bmax = ubo.aabbMax.xyz;

    let fragCoord = vec2<i32>(fragPos.xy);
    var seed: u32 = u32(fragCoord.x) * 1973u
                  + u32(fragCoord.y) * 9277u
                  + bitcast<u32>(ubo.pathtrace.z) * 26699u
                  + 1u;

    let sigmaMax = ubo.params.y * max(ubo.params.z, 1.0) + 1e-3;

    let spp = clamp(i32(ubo.pathtrace.x), 1, 32);
    var current = vec3<f32>(0.0);
    for (var s = 0; s < spp; s = s + 1) {
        current = current + tracePath(ro, rd, bmin, bmax, sigmaMax, &seed);
    }
    current = current / f32(spp);

    // Progressive temporal accumulation. N=0 collapses prev*N to zero so stale
    // history content is ignored without needing to clear the texture on reset.
    let prev    = textureLoad(historyTex, fragCoord, 0).rgb;
    let N       = max(ubo.accum.x, 0.0);
    let blended = (prev * N + current) / (N + 1.0);

    return vec4<f32>(blended, 1.0);
}
