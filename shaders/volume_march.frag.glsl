#version 450

// Roadmap Phase 7-3 -- volume ray marching with depth-aware compositing.
//
// Fullscreen pass run after deferred lighting. For each pixel it reconstructs a
// world-space camera ray (matching deferred_lighting.frag's depth reconstruction:
// invProj then invView), intersects the volume's world AABB, clamps the far end
// to the scene depth so opaque geometry occludes the volume, marches front-to-back
// accumulating Beer-Lambert opacity, and outputs PREMULTIPLIED color. The pipeline
// blends it over the HDR target with One / OneMinusSrcAlpha (premultiplied over).

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform VolumeUBO {
    mat4 invView;
    mat4 invProj;
    vec4 cameraPos;   // xyz
    vec4 aabbMin;     // xyz world-space volume bounds
    vec4 aabbMax;     // xyz
    vec4 params;      // x = stepSize (world units), y = extinction, z = densityScale, w = maxSteps
    vec4 tf;          // x = densityThreshold, y = colorMix, z = useLUT (0/1), w = useDepth
    vec4 lowColor;    // rgb = color at low density (Custom preset)
    vec4 highColor;   // rgb = color at high density (Custom preset)
    vec4 window;      // x = windowCenter, y = windowWidth (intensity -> [0,1]); zw spare
    vec4 light;       // xyz = light dir (world), w = shadingEnable (0/1)
    vec4 shade;       // x = ambient, y = diffuse, z = gradEps (texture-space step), w spare
    vec4 shadow;      // x = enable (0/1), y = stepSize (world), z = maxSteps, w = strength
    vec4 occ;         // xyz = occupancy grid dims (cells), w = skipEnable (0/1)
    vec4 pathtrace;   // unused by march (path-trace pipeline)
    vec4 accum;       // unused by march (used by path-trace accumulation)
    vec4 volSize;     // M3-3 xyz = source volume voxel dims (for brick indexing)
    vec4 atlasGrid;   // M3-3 xyz = atlas capacity in bricks (slot unpack)
} ubo;

layout(set = 0, binding = 1) uniform texture2D depthTex;
layout(set = 0, binding = 2) uniform sampler   depthSampler;
layout(set = 0, binding = 3) uniform texture3D volumeTex;     // M3-3 brick atlas (R16Float)
layout(set = 0, binding = 4) uniform sampler   volumeSampler;
layout(set = 0, binding = 5) uniform texture2D tfLUT;         // 256x1 density -> (rgb, opacity)
layout(std430, set = 0, binding = 6) readonly buffer OccGrid {
    vec2 occCells[];   // per macrocell: (min, max) intensity, for empty-space skipping
};
layout(std430, set = 0, binding = 7) readonly buffer PageTable {
    uint pageSlots[];  // M3-3 page table: per virtual brick, atlas slot index or 0xFFFFFFFF
};

// M3-3 brick sampling: page-table indirection from [0,1]^3 volume UV to a
// linearly-filtered density sample in the brick atlas. Empty bricks return 0.
// kBrickSize = 64 (interior), kBrickStored = 66 (1-voxel halo each side).
float sampleVolume(vec3 uvw) {
    vec3 vp = clamp(uvw, vec3(0.0), vec3(0.999999)) * ubo.volSize.xyz;
    ivec3 brickIdx = ivec3(vp) / 64;
    vec3  localF   = vp - vec3(brickIdx * 64);   // [0, 64) inside the brick
    ivec3 pageGrid = ivec3((ubo.volSize.xyz + 63.0) / 64.0);
    int pageIdx = (brickIdx.z * pageGrid.y + brickIdx.y) * pageGrid.x + brickIdx.x;
    uint slot = pageSlots[pageIdx];
    if (slot == 0xFFFFFFFFu) return 0.0;

    ivec3 atlasG = ivec3(ubo.atlasGrid.xyz);
    int sx = int(slot) % atlasG.x;
    int sy = (int(slot) / atlasG.x) % atlasG.y;
    int sz = int(slot) / (atlasG.x * atlasG.y);
    // Interior of the brick starts at slot*66 + 1 (offset past the -1 halo).
    vec3 atlasVox = vec3(float(sx * 66 + 1), float(sy * 66 + 1), float(sz * 66 + 1)) + localF;
    vec3 atlasUvw = (atlasVox + 0.5) / vec3(atlasG * 66);
    return texture(sampler3D(volumeTex, volumeSampler), atlasUvw).r;
}

// Ray-AABB slab test. Returns (tNear, tFar); tNear > tFar means miss.
vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {
    vec3 invD = 1.0 / rd;
    vec3 t0 = (bmin - ro) * invD;
    vec3 t1 = (bmax - ro) * invD;
    vec3 tsmall = min(t0, t1);
    vec3 tbig   = max(t0, t1);
    float tn = max(max(tsmall.x, tsmall.y), tsmall.z);
    float tf = min(min(tbig.x, tbig.y), tbig.z);
    return vec2(tn, tf);
}

void main() {
    ivec2 fragCoord  = ivec2(gl_FragCoord.xy);
    vec2  screenSize = vec2(textureSize(sampler2D(depthTex, depthSampler), 0));
    vec2  uv         = vec2(fragCoord) / screenSize;
    vec2  ndcXY      = uv * 2.0 - 1.0;

    // Camera ray: reconstruct the far-plane world point and aim from the camera.
    vec4 vFar = ubo.invProj * vec4(ndcXY, 1.0, 1.0);
    vFar /= vFar.w;
    vec3 worldFar = vec3(ubo.invView * vFar);
    vec3 ro = ubo.cameraPos.xyz;
    vec3 rd = normalize(worldFar - ro);

    // Scene occlusion distance from the depth buffer (point sampled, matches
    // deferred). tf.w = useDepth: 0 disables occlusion (standalone viewer with no
    // geometry / dummy depth) -- march the full box.
    float sceneDist = 1e30;
    if (ubo.tf.w > 0.5) {
        float depth = texelFetch(sampler2D(depthTex, depthSampler), fragCoord, 0).r;
        if (depth < 1.0) {
            vec4 vS = ubo.invProj * vec4(ndcXY, depth, 1.0);
            vS /= vS.w;
            vec3 worldS = vec3(ubo.invView * vS);
            sceneDist = length(worldS - ro);
        }
    }

    vec2  hit   = intersectAABB(ro, rd, ubo.aabbMin.xyz, ubo.aabbMax.xyz);
    float tNear = max(hit.x, 0.0);
    float tFar  = min(hit.y, sceneDist);
    if (tNear >= tFar) {            // missed the box, or fully behind geometry
        outColor = vec4(0.0);
        return;
    }

    const float stepSize   = ubo.params.x;
    const float extinction = ubo.params.y;
    const int   maxSteps   = int(ubo.params.w);
    const vec3  boxSize     = ubo.aabbMax.xyz - ubo.aabbMin.xyz;

    vec4  accum = vec4(0.0);   // premultiplied rgb + alpha
    float t     = tNear;
    for (int i = 0; i < maxSteps; ++i) {
        if (t >= tFar || accum.a > 0.99) break;

        vec3 wp  = ro + rd * t;
        vec3 uvw = (wp - ubo.aabbMin.xyz) / boxSize;   // [0,1]^3

        // Empty-space skipping: if this macrocell's max windowed density is zero
        // (air), jump straight to the cell exit instead of stepping through it.
        // Note: this checks per sample (not per cell-entry). The per-cell-entry
        // variant added warp divergence that outweighed the saved storage reads
        // (which were cache-coherent anyway).
        if (ubo.occ.w > 0.5) {
            vec3  gd    = ubo.occ.xyz;
            vec3  cellf = floor(clamp(uvw, vec3(0.0), vec3(0.999999)) * gd);
            int   ci    = (int(cellf.z) * int(gd.y) + int(cellf.y)) * int(gd.x) + int(cellf.x);
            float cmax  = occCells[ci].y;
            float wn    = clamp((cmax - (ubo.window.x - ubo.window.y * 0.5)) / max(ubo.window.y, 1e-6), 0.0, 1.0);
            if (max(wn * ubo.params.z - ubo.tf.x, 0.0) <= 0.0) {
                vec3 cellMin = ubo.aabbMin.xyz + (cellf / gd) * boxSize;
                vec3 cellMax = ubo.aabbMin.xyz + ((cellf + 1.0) / gd) * boxSize;
                vec3 te0 = (cellMin - ro) / rd;
                vec3 te1 = (cellMax - ro) / rd;
                vec3 tem = max(te0, te1);
                float tExit = min(min(tem.x, tem.y), tem.z);
                t = tExit + stepSize * 0.5;   // step just past the empty cell
                continue;
            }
        }

        float raw = sampleVolume(uvw);
        // Window/level: map [center - width/2, center + width/2] -> [0,1] (contrast).
        float n = clamp((raw - (ubo.window.x - ubo.window.y * 0.5)) / max(ubo.window.y, 1e-6), 0.0, 1.0);
        float density = n * ubo.params.z;
        density = max(density - ubo.tf.x, 0.0);

        if (density > 0.0) {
            // Transfer function: LUT preset (useLUT) or the Custom 2-color gradient.
            vec3  color;
            float opacityWeight;
            if (ubo.tf.z > 0.5) {
                vec4 lut = texture(sampler2D(tfLUT, volumeSampler),
                                   vec2(clamp(density, 0.0, 1.0), 0.5));
                color         = lut.rgb;
                opacityWeight = lut.a;
            } else {
                color         = mix(ubo.lowColor.rgb, ubo.highColor.rgb,
                                    clamp(density * ubo.tf.y, 0.0, 1.0));
                opacityWeight = density;
            }
            // Gradient shading: the density gradient is the surface normal; light it
            // with Lambert. Gives the volume 3D form instead of flat absorption.
            if (ubo.light.w > 0.5) {
                float e = ubo.shade.z;
                vec3 g;
                g.x = sampleVolume(uvw + vec3(e, 0, 0)) - sampleVolume(uvw - vec3(e, 0, 0));
                g.y = sampleVolume(uvw + vec3(0, e, 0)) - sampleVolume(uvw - vec3(0, e, 0));
                g.z = sampleVolume(uvw + vec3(0, 0, e)) - sampleVolume(uvw - vec3(0, 0, e));
                float glen = length(g);
                // Normal points toward DECREASING density (outward from a dense body).
                vec3  L   = normalize(ubo.light.xyz);
                float ndl = (glen > 1e-6) ? max(dot(-g / glen, L), 0.0) : 0.0;

                // Volumetric soft shadow: march a secondary ray toward the light and
                // accumulate optical depth, so dense regions self-shadow.
                float shadowF = 1.0;
                if (ubo.shadow.x > 0.5 && ndl > 0.0) {
                    float sStep = ubo.shadow.y;
                    int   sMax  = int(ubo.shadow.z);
                    float tau   = 0.0;
                    float st    = sStep;   // start one step out to avoid self-occlusion
                    for (int s = 0; s < sMax; ++s) {
                        vec3 suvw = (wp + L * st - ubo.aabbMin.xyz) / boxSize;
                        if (any(lessThan(suvw, vec3(0.0))) || any(greaterThan(suvw, vec3(1.0)))) break;
                        float sr = sampleVolume(suvw);
                        float sn = clamp((sr - (ubo.window.x - ubo.window.y * 0.5)) / max(ubo.window.y, 1e-6), 0.0, 1.0);
                        float sd = max(sn * ubo.params.z - ubo.tf.x, 0.0);
                        tau += sd * extinction * ubo.shadow.w * sStep;
                        if (tau > 8.0) break;   // effectively fully shadowed
                        st += sStep;
                    }
                    shadowF = exp(-tau);
                }
                color *= ubo.shade.x + ubo.shade.y * ndl * shadowF;   // ambient + shadowed diffuse
            }

            // Beer-Lambert opacity for this step.
            float alpha = 1.0 - exp(-opacityWeight * extinction * stepSize);
            accum.rgb += (1.0 - accum.a) * color * alpha;   // premultiplied
            accum.a   += (1.0 - accum.a) * alpha;
        }
        t += stepSize;
    }

    outColor = accum;  // premultiplied; blended One / OneMinusSrcAlpha over HDR
}
