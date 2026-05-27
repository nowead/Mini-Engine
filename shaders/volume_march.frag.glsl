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
    vec4 tf;          // transfer function: x = densityThreshold, y = colorMix, z/w spare (7-4)
    vec4 lowColor;    // rgb = color at low density (7-4, user-picked)
    vec4 highColor;   // rgb = color at high density
} ubo;

layout(set = 0, binding = 1) uniform texture2D depthTex;
layout(set = 0, binding = 2) uniform sampler   depthSampler;
layout(set = 0, binding = 3) uniform texture3D volumeTex;
layout(set = 0, binding = 4) uniform sampler   volumeSampler;

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

    // Scene occlusion distance from the depth buffer (point sampled, matches deferred).
    float depth = texelFetch(sampler2D(depthTex, depthSampler), fragCoord, 0).r;
    float sceneDist = 1e30;
    if (depth < 1.0) {
        vec4 vS = ubo.invProj * vec4(ndcXY, depth, 1.0);
        vS /= vS.w;
        vec3 worldS = vec3(ubo.invView * vS);
        sceneDist = length(worldS - ro);
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
        float density = texture(sampler3D(volumeTex, volumeSampler), uvw).r * ubo.params.z;
        density = max(density - ubo.tf.x, 0.0);

        if (density > 0.0) {
            // Beer-Lambert opacity for this step.
            float alpha = 1.0 - exp(-density * extinction * stepSize);
            // Transfer function: user-picked low/high density colors, blended by
            // density (colorMix controls how fast it reaches highColor).
            vec3 color = mix(ubo.lowColor.rgb, ubo.highColor.rgb,
                             clamp(density * ubo.tf.y, 0.0, 1.0));
            accum.rgb += (1.0 - accum.a) * color * alpha;   // premultiplied
            accum.a   += (1.0 - accum.a) * alpha;
        }
        t += stepSize;
    }

    outColor = accum;  // premultiplied; blended One / OneMinusSrcAlpha over HDR
}
