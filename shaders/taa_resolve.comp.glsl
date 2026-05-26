/*
 * TAA Resolve — Temporal Anti-Aliasing (sub-task C2)
 *
 * Reprojects the previous resolved frame (history) into the current frame using
 * the G-Buffer screen-space velocity, clips it to the 3x3 neighborhood of the
 * current color (variance clipping — suppresses ghosting on disocclusion and
 * fast motion), and blends current + clamped-history. The jittered projection
 * (Renderer) makes each frame super-sample a different sub-pixel position, so
 * the accumulated result is anti-aliased.
 *
 *   binding 0 — current HDR color (this frame's lighting result)
 *   binding 1 — history (previous resolved frame)
 *   binding 2 — velocity (G-Buffer target 3, RG = curr-prev UV)
 *   binding 3 — resolve output (RGBA16Float storage image = history[write])
 *   binding 4 — linear sampler
 */
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D currColor;
layout(set = 0, binding = 1) uniform texture2D historyColor;
layout(set = 0, binding = 2) uniform texture2D velocityTex;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D resolveOut;
layout(set = 0, binding = 4) uniform sampler samp;

layout(push_constant) uniform PC {
    float invW;
    float invH;
    float blend;          // history weight (e.g. 0.9)
    uint  historyValid;   // 0 on the first frame / after a resize → output current only
} pc;

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz = imageSize(resolveOut);
    if (px.x >= sz.x || px.y >= sz.y) return;

    vec2 uv   = (vec2(px) + 0.5) * vec2(pc.invW, pc.invH);
    vec3 curr = texture(sampler2D(currColor, samp), uv).rgb;

    // First frame / no valid history yet: pass current through (seeds history).
    if (pc.historyValid == 0u) {
        imageStore(resolveOut, px, vec4(curr, 1.0));
        return;
    }

    // 3x3 neighborhood min/max of the current frame for variance clipping.
    vec3 nmin = curr;
    vec3 nmax = curr;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            vec3 c = texture(sampler2D(currColor, samp),
                             uv + vec2(float(x) * pc.invW, float(y) * pc.invH)).rgb;
            nmin = min(nmin, c);
            nmax = max(nmax, c);
        }
    }

    // Reproject history. velocity = currUV - prevUV, so prev sample is uv - vel.
    vec2 vel    = texture(sampler2D(velocityTex, samp), uv).rg;
    vec2 histUV = uv - vel;

    vec3 result;
    if (histUV.x < 0.0 || histUV.x > 1.0 || histUV.y < 0.0 || histUV.y > 1.0) {
        // Reprojected off-screen (disocclusion) — no usable history.
        result = curr;
    } else {
        vec3 hist = texture(sampler2D(historyColor, samp), histUV).rgb;
        hist      = clamp(hist, nmin, nmax);   // variance clipping vs current neighborhood
        result    = mix(curr, hist, pc.blend);
    }

    imageStore(resolveOut, px, vec4(result, 1.0));
}
