#version 450

// M4 v1 -- volumetric path tracing with progressive temporal accumulation.
//
// The integrator is unchanged from v0 (Woodcock free-flight + Henyey-Greenstein
// phase + single-light NEE + inline-SPP averaging). What's new is the output:
// instead of writing a tone-mapped sRGB sample to the swapchain, we read the
// previous frame's running average from a sampled HDR history texture, blend
// the new sample in linearly (running mean), and write linear HDR back to the
// accumulation target. A separate display shader tonemaps the result to the
// swapchain. Tonemap must run AFTER averaging, not per-frame, because Reinhard
// is non-linear.
//
// Bind-group layout differs from volume_march -- path-trace gets its own
// pipeline layout (no depth, no occupancy; adds the history sampler).

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform VolumeUBO {
    mat4 invView;
    mat4 invProj;
    vec4 cameraPos;
    vec4 aabbMin;
    vec4 aabbMax;
    vec4 params;     // y = extinction, z = densityScale, x/w unused here
    vec4 tf;         // x = densityThreshold, y = colorMix, z = useLUT (0/1), w unused here
    vec4 lowColor;
    vec4 highColor;
    vec4 window;     // x = windowCenter, y = windowWidth
    vec4 light;      // xyz = light dir (world), w unused here
    vec4 shade;      // x = ambient, y = lightIntensity (reuses diffuse slider)
    vec4 shadow;     // unused here
    vec4 occ;        // unused here
    vec4 pathtrace;  // x = spp, y = HG anisotropy g, z = frame seed, w = max bounces
    vec4 accum;      // x = previous accumulated sample count N (host-incremented)
    vec4 volSize;    // M3-3 xyz = source volume voxel dims (for brick indexing)
    vec4 atlasGrid;  // M3-3 xyz = atlas capacity in bricks (slot unpack)
    vec4 envTop;     // M4 v2 P1 rgb = sky top color, w = intensity multiplier
    vec4 envBot;     // M4 v2 P1 rgb = sky bottom color, w = enable flag (0/1)
} ubo;

layout(set = 0, binding = 1) uniform texture3D volumeTex0;    // brick atlas L0 (R16Float)
layout(set = 0, binding = 2) uniform sampler   volumeSampler;
layout(set = 0, binding = 3) uniform texture2D tfLUT;
layout(set = 0, binding = 4) uniform texture2D historyTex;
layout(std430, set = 0, binding = 5) readonly buffer PageTable {
    uint pageSlots[];  // page table: per virtual brick, (lod<<30)|slot or 0xFFFFFFFF
};
layout(set = 0, binding = 6) uniform texture3D volumeTex1;    // brick atlas L1 (beta-5)
layout(set = 0, binding = 7) uniform texture3D volumeTex2;    // brick atlas L2 (beta-5)
layout(set = 0, binding = 8) uniform texture3D volumeTex3;    // brick atlas L3 (beta-5)

// beta-5: page entry packs (lod<<30)|slot. Decode + sample the chosen LOD's
// atlas. brickStored(lod) = (64>>lod)+2 = 66/34/18/10.
float sampleVolume(vec3 uvw) {
    vec3 vp = clamp(uvw, vec3(0.0), vec3(0.999999)) * ubo.volSize.xyz;
    ivec3 brickIdx = ivec3(vp) / 64;
    vec3  localF   = vp - vec3(brickIdx * 64);   // [0, 64) in L0 source voxels
    ivec3 pageGrid = ivec3((ubo.volSize.xyz + 63.0) / 64.0);
    int   pageIdx  = (brickIdx.z * pageGrid.y + brickIdx.y) * pageGrid.x + brickIdx.x;
    uint  page     = pageSlots[pageIdx];
    if (page == 0xFFFFFFFFu) return 0.0;

    uint slot = page & 0x3FFFFFFFu;
    uint lod  = page >> 30;

    ivec3 atlasG      = ivec3(ubo.atlasGrid.xyz);
    int   brickStored = int(64u >> lod) + 2;
    int sx = int(slot) % atlasG.x;
    int sy = (int(slot) / atlasG.x) % atlasG.y;
    int sz = int(slot) / (atlasG.x * atlasG.y);
    vec3 localFlod = localF / float(1u << lod);
    vec3 atlasVox  = vec3(float(sx * brickStored + 1),
                          float(sy * brickStored + 1),
                          float(sz * brickStored + 1)) + localFlod;
    vec3 atlasUvw  = (atlasVox + 0.5) / vec3(atlasG * brickStored);

    if (lod == 0u) return texture(sampler3D(volumeTex0, volumeSampler), atlasUvw).r;
    if (lod == 1u) return texture(sampler3D(volumeTex1, volumeSampler), atlasUvw).r;
    if (lod == 2u) return texture(sampler3D(volumeTex2, volumeSampler), atlasUvw).r;
    return                texture(sampler3D(volumeTex3, volumeSampler), atlasUvw).r;
}

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// PRNG (PCG)
// ---------------------------------------------------------------------------
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float rnd(inout uint seed) {
    seed = pcg(seed);
    return float(seed) / 4294967296.0;
}

// ---------------------------------------------------------------------------
// Henyey-Greenstein
// ---------------------------------------------------------------------------
float hgPhase(float cosTheta, float g) {
    float denom = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * PI * pow(max(denom, 1e-6), 1.5));
}
vec3 sampleHG(vec3 wi, float g, inout uint seed) {
    float u1 = rnd(seed), u2 = rnd(seed);
    float cosTheta;
    if (abs(g) < 1e-3) {
        cosTheta = 1.0 - 2.0 * u1;
    } else {
        float sqr = (1.0 - g * g) / (1.0 - g + 2.0 * g * u1);
        cosTheta = (1.0 + g * g - sqr * sqr) / (2.0 * g);
    }
    cosTheta = clamp(cosTheta, -1.0, 1.0);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 2.0 * PI * u2;
    vec3 w  = wi;
    vec3 ax = abs(w.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 u  = normalize(cross(ax, w));
    vec3 v  = cross(w, u);
    return normalize(u * sinTheta * cos(phi) + v * sinTheta * sin(phi) + w * cosTheta);
}

// ---------------------------------------------------------------------------
// Volume / TF helpers
// ---------------------------------------------------------------------------
float sampleDensity(vec3 uvw) {
    float raw = sampleVolume(uvw);
    float n   = clamp((raw - (ubo.window.x - ubo.window.y * 0.5)) /
                      max(ubo.window.y, 1e-6), 0.0, 1.0);
    return max(n * ubo.params.z - ubo.tf.x, 0.0);
}
vec4 sampleTF(float density) {
    if (ubo.tf.z > 0.5) {
        return texture(sampler2D(tfLUT, volumeSampler),
                       vec2(clamp(density, 0.0, 1.0), 0.5));
    }
    vec3 c = mix(ubo.lowColor.rgb, ubo.highColor.rgb,
                 clamp(density * ubo.tf.y, 0.0, 1.0));
    return vec4(c, clamp(density, 0.0, 1.0));
}
// M4 v2 P1: top-bottom gradient environment lighting. Disabled when envBot.w
// is zero so existing demos render identically (sampled value is vec3(0)).
vec3 sampleEnvironment(vec3 dir) {
    if (ubo.envBot.w < 0.5) return vec3(0.0);
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(ubo.envBot.rgb, ubo.envTop.rgb, t) * ubo.envTop.w;
}

vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {
    vec3 invD = 1.0 / rd;
    vec3 t0 = (bmin - ro) * invD;
    vec3 t1 = (bmax - ro) * invD;
    vec3 ts = min(t0, t1);
    vec3 tb = max(t0, t1);
    return vec2(max(max(ts.x, ts.y), ts.z), min(min(tb.x, tb.y), tb.z));
}

float transmittance(vec3 p, vec3 L, vec3 bmin, vec3 bmax, vec3 boxSize,
                    float sigmaMax, inout uint seed) {
    vec2 hit = intersectAABB(p, L, bmin, bmax);
    float tFar = hit.y;
    if (tFar <= 0.0) return 1.0;
    float t = 0.0;
    for (int i = 0; i < 64; ++i) {
        t += -log(max(1.0 - rnd(seed), 1e-6)) / sigmaMax;
        if (t >= tFar) return 1.0;
        vec3 q = p + L * t;
        vec3 uvw = (q - bmin) / boxSize;
        float sigma_t = sampleDensity(uvw) * ubo.params.y;
        if (rnd(seed) < sigma_t / sigmaMax) return 0.0;
    }
    return 1.0;
}

vec3 tracePath(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax,
               float sigmaMax, inout uint seed) {
    vec2 hit = intersectAABB(ro, rd, bmin, bmax);
    float tNear = max(hit.x, 0.0);
    float tFar  = hit.y;
    // Primary ray missed the volume -> background = environment sample (or
    // black when env is disabled).
    if (tNear >= tFar) return sampleEnvironment(rd);

    vec3 boxSize = bmax - bmin;
    ro = ro + rd * tNear;
    float remaining = tFar - tNear;

    vec3 result      = vec3(0.0);
    vec3 throughput  = vec3(1.0);
    vec3 L           = normalize(ubo.light.xyz);
    float lightI     = ubo.shade.y;
    float ambient    = ubo.shade.x;
    float g          = ubo.pathtrace.y;
    int   maxBounce  = max(0, int(ubo.pathtrace.w));

    for (int b = 0; b <= maxBounce; ++b) {
        float t = 0.0;
        bool exited = false;
        for (int it = 0; it < 128; ++it) {
            t += -log(max(1.0 - rnd(seed), 1e-6)) / sigmaMax;
            if (t >= remaining) { exited = true; break; }
            vec3 q = ro + rd * t;
            vec3 uvw = (q - bmin) / boxSize;
            float sigma_t = sampleDensity(uvw) * ubo.params.y;
            if (rnd(seed) < sigma_t / sigmaMax) break;
        }
        if (exited) {
            // Path escaped without another scatter -- remaining throughput
            // lights the camera via the environment in the current direction.
            result += throughput * sampleEnvironment(rd);
            break;
        }

        vec3 p = ro + rd * t;
        vec3 uvw = (p - bmin) / boxSize;
        float density = sampleDensity(uvw);
        vec3 albedo = sampleTF(density).rgb;

        float Tl    = transmittance(p, L, bmin, bmax, boxSize, sigmaMax, seed);
        float phase = hgPhase(dot(-rd, L), g);
        result += throughput * albedo * lightI * phase * Tl;
        result += throughput * albedo * ambient;

        if (b == maxBounce) break;
        rd = sampleHG(rd, g, seed);
        ro = p;
        vec2 hit2 = intersectAABB(ro, rd, bmin, bmax);
        if (hit2.y <= 0.0) break;
        remaining = hit2.y;
        throughput *= albedo;

        float rrProb = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.1, 0.95);
        if (rnd(seed) > rrProb) break;
        throughput /= rrProb;
    }
    return result;
}

void main() {
    ivec2 fragCoord  = ivec2(gl_FragCoord.xy);
    vec2  screenSize = vec2(textureSize(sampler2D(historyTex, volumeSampler), 0));
    vec2  uv         = vec2(fragCoord) / screenSize;
    vec2  ndcXY      = uv * 2.0 - 1.0;

    vec4 vFar = ubo.invProj * vec4(ndcXY, 1.0, 1.0);
    vFar /= vFar.w;
    vec3 worldFar = vec3(ubo.invView * vFar);
    vec3 ro = ubo.cameraPos.xyz;
    vec3 rd = normalize(worldFar - ro);

    vec3 bmin = ubo.aabbMin.xyz;
    vec3 bmax = ubo.aabbMax.xyz;

    uint seed = uint(fragCoord.x) * 1973u
              + uint(fragCoord.y) * 9277u
              + floatBitsToUint(ubo.pathtrace.z) * 26699u
              + 1u;

    float sigmaMax = ubo.params.y * max(ubo.params.z, 1.0) + 1e-3;

    int   spp = clamp(int(ubo.pathtrace.x), 1, 32);
    vec3  current = vec3(0.0);
    for (int s = 0; s < spp; ++s) {
        current += tracePath(ro, rd, bmin, bmax, sigmaMax, seed);
    }
    current /= float(spp);

    // Progressive temporal accumulation. At sample count N=0 the prev*N term
    // collapses to zero so any stale history content is ignored -- no need to
    // clear the texture on reset. Output is LINEAR HDR; the display pass
    // tonemaps after the average is final.
    vec3  prev      = texelFetch(sampler2D(historyTex, volumeSampler), fragCoord, 0).rgb;
    float N         = max(ubo.accum.x, 0.0);
    vec3  blended   = (prev * N + current) / (N + 1.0);

    outColor = vec4(blended, 1.0);
}
