#version 450

// M4 v0 -- volumetric path tracing with Henyey-Greenstein phase function and
// Woodcock (delta) free-flight distance sampling. Trades the absorption-only
// Beer-Lambert march of volume_march for a Monte Carlo integrator: at each
// scattering event we (a) accumulate direct light via next-event estimation
// (single shadow ray, Woodcock-tracked) and (b) optionally continue the path
// for indirect contributions, terminated by Russian roulette.
//
// No history buffer in v0; SPP samples are averaged inline within the shader.
// Inputs/outputs share the volume_march bind-group layout so VolumeRenderer can
// flip pipelines at runtime without rebuilding bind groups.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform VolumeUBO {
    mat4 invView;
    mat4 invProj;
    vec4 cameraPos;
    vec4 aabbMin;
    vec4 aabbMax;
    vec4 params;     // x = stepSize (unused PT), y = extinction, z = densityScale, w = maxSteps (unused PT)
    vec4 tf;         // x = densityThreshold, y = colorMix, z = useLUT (0/1), w = useDepth (unused PT)
    vec4 lowColor;
    vec4 highColor;
    vec4 window;     // x = windowCenter, y = windowWidth
    vec4 light;      // xyz = light dir (world), w = shadingEnable (unused PT)
    vec4 shade;      // x = ambient, y = lightIntensity (reuses diffuse slider), zw spare
    vec4 shadow;     // unused PT
    vec4 occ;        // unused PT (the integrator picks events from density itself)
    vec4 pathtrace;  // x = spp (1..32), y = HG anisotropy g, z = frame seed, w = max bounces
} ubo;

layout(set = 0, binding = 1) uniform texture2D depthTex;     // unused but layout needs it
layout(set = 0, binding = 2) uniform sampler   depthSampler; // unused
layout(set = 0, binding = 3) uniform texture3D volumeTex;
layout(set = 0, binding = 4) uniform sampler   volumeSampler;
layout(set = 0, binding = 5) uniform texture2D tfLUT;
layout(std430, set = 0, binding = 6) readonly buffer OccGrid { vec2 occCells[]; };  // unused PT

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// PRNG (PCG-derived). One uint of state mutated per call.
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
// Henyey-Greenstein phase function (eval + importance sampling).
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
    // Ortho frame around wi.
    vec3 w  = wi;
    vec3 ax = abs(w.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 u  = normalize(cross(ax, w));
    vec3 v  = cross(w, u);
    return normalize(u * sinTheta * cos(phi) + v * sinTheta * sin(phi) + w * cosTheta);
}

// ---------------------------------------------------------------------------
// Volume / TF helpers (mirror volume_march so visuals are coherent across modes).
// ---------------------------------------------------------------------------
float sampleDensity(vec3 uvw) {
    float raw = texture(sampler3D(volumeTex, volumeSampler), uvw).r;
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
vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {
    vec3 invD = 1.0 / rd;
    vec3 t0 = (bmin - ro) * invD;
    vec3 t1 = (bmax - ro) * invD;
    vec3 ts = min(t0, t1);
    vec3 tb = max(t0, t1);
    return vec2(max(max(ts.x, ts.y), ts.z), min(min(tb.x, tb.y), tb.z));
}

// Visibility along (p, L) via Woodcock tracking: 1 if reaches the AABB exit
// without a "real" collision, 0 otherwise.
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

// One path-traced sample (Monte Carlo radiance estimate along the camera ray).
vec3 tracePath(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax,
               float sigmaMax, inout uint seed) {
    vec2 hit = intersectAABB(ro, rd, bmin, bmax);
    float tNear = max(hit.x, 0.0);
    float tFar  = hit.y;
    if (tNear >= tFar) return vec3(0.0);

    vec3 boxSize = bmax - bmin;
    ro = ro + rd * tNear;
    float remaining = tFar - tNear;

    vec3 result      = vec3(0.0);
    vec3 throughput  = vec3(1.0);
    vec3 L           = normalize(ubo.light.xyz);
    float lightI     = ubo.shade.y;          // reuse the "diffuse" slider as light intensity
    float ambient    = ubo.shade.x;
    float g          = ubo.pathtrace.y;
    int   maxBounce  = max(0, int(ubo.pathtrace.w));

    for (int b = 0; b <= maxBounce; ++b) {
        // Sample next collision via Woodcock tracking inside the AABB.
        float t = 0.0;
        bool exited = false;
        for (int it = 0; it < 128; ++it) {
            t += -log(max(1.0 - rnd(seed), 1e-6)) / sigmaMax;
            if (t >= remaining) { exited = true; break; }
            vec3 q = ro + rd * t;
            vec3 uvw = (q - bmin) / boxSize;
            float sigma_t = sampleDensity(uvw) * ubo.params.y;
            if (rnd(seed) < sigma_t / sigmaMax) break;   // real scattering event
        }
        if (exited) break;

        vec3 p = ro + rd * t;
        vec3 uvw = (p - bmin) / boxSize;
        float density = sampleDensity(uvw);
        vec3 albedo = sampleTF(density).rgb;

        // Direct light contribution (next-event estimation, single directional light).
        float Tl    = transmittance(p, L, bmin, bmax, boxSize, sigmaMax, seed);
        float phase = hgPhase(dot(-rd, L), g);
        result += throughput * albedo * lightI * phase * Tl;

        // Ambient term (constant environmental contribution at this event).
        result += throughput * albedo * ambient;

        // Continue path (indirect) up to the bounce budget.
        if (b == maxBounce) break;
        rd = sampleHG(rd, g, seed);
        ro = p;
        vec2 hit2 = intersectAABB(ro, rd, bmin, bmax);
        if (hit2.y <= 0.0) break;
        remaining = hit2.y;
        throughput *= albedo;

        // Russian roulette to keep variance bounded.
        float rrProb = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.1, 0.95);
        if (rnd(seed) > rrProb) break;
        throughput /= rrProb;
    }
    return result;
}

void main() {
    ivec2 fragCoord  = ivec2(gl_FragCoord.xy);
    vec2  screenSize = vec2(textureSize(sampler2D(depthTex, depthSampler), 0));
    vec2  uv         = vec2(fragCoord) / screenSize;
    vec2  ndcXY      = uv * 2.0 - 1.0;

    vec4 vFar = ubo.invProj * vec4(ndcXY, 1.0, 1.0);
    vFar /= vFar.w;
    vec3 worldFar = vec3(ubo.invView * vFar);
    vec3 ro = ubo.cameraPos.xyz;
    vec3 rd = normalize(worldFar - ro);

    vec3 bmin = ubo.aabbMin.xyz;
    vec3 bmax = ubo.aabbMax.xyz;

    // Per-pixel, per-frame seed for the PRNG. The frame seed (ubo.pathtrace.z)
    // is bumped each frame on the host so the noise pattern decorrelates.
    uint seed = uint(fragCoord.x) * 1973u
              + uint(fragCoord.y) * 9277u
              + floatBitsToUint(ubo.pathtrace.z) * 26699u
              + 1u;

    // Upper bound for delta tracking. extinction * max(densityScale, 1) is safe
    // when the windowed density is in [0,1] and the threshold subtracts.
    float sigmaMax = ubo.params.y * max(ubo.params.z, 1.0) + 1e-3;

    int   spp   = clamp(int(ubo.pathtrace.x), 1, 32);
    vec3  accum = vec3(0.0);
    for (int s = 0; s < spp; ++s) {
        accum += tracePath(ro, rd, bmin, bmax, sigmaMax, seed);
    }
    accum /= float(spp);

    // Simple Reinhard tonemap so highlights don't blow out; the swapchain is sRGB
    // and the linear write is encoded automatically on store.
    accum = accum / (1.0 + accum);

    outColor = vec4(accum, 1.0);
}
