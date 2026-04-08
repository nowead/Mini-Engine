/*
 * SSAO Bilateral Blur Compute Shader
 *
 * 4×4 Gaussian kernel weighted by depth similarity (edge-preserving).
 * Runs at full resolution: same size as the SSAO texture.
 *
 * Inputs:
 *   binding 0 — raw SSAO texture (R8Unorm, sampled, separate)
 *   binding 1 — depth texture (D32Sfloat, sampled, separate)
 *   binding 2 — blurred AO output (R8Unorm, storage)
 *   binding 3 — sampler
 * Push constants: invW, invH, near, far, depthThreshold, pad
 */
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D aoTex;
layout(set = 0, binding = 1) uniform texture2D depthTex;
layout(set = 0, binding = 2, r8) uniform writeonly image2D aoBlurOut;
layout(set = 0, binding = 3) uniform sampler samp;

layout(push_constant) uniform PC {
    float invW;
    float invH;
    float near;
    float far;
    float depthThreshold;
    float pad;
} pc;

float linearizeDepth(float d) {
    return pc.near * pc.far / (pc.far - d * (pc.far - pc.near));
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imgSize = imageSize(aoBlurOut);
    if (pixel.x >= imgSize.x || pixel.y >= imgSize.y) return;

    vec2 uv = (vec2(pixel) + 0.5) * vec2(pc.invW, pc.invH);

    float centerDepth = linearizeDepth(texture(sampler2D(depthTex, samp), uv).r);

    // 4×4 bilateral filter (radius 2, 5×5 kernel)
    float totalAO = 0.0;
    float totalWeight = 0.0;

    const int RADIUS = 2;
    for (int y = -RADIUS; y <= RADIUS; ++y) {
        for (int x = -RADIUS; x <= RADIUS; ++x) {
            vec2 offset = vec2(x, y) * vec2(pc.invW, pc.invH);
            vec2 sUV = uv + offset;

            float sDepth = linearizeDepth(texture(sampler2D(depthTex, samp), sUV).r);
            float depthDiff = abs(centerDepth - sDepth);

            // Bilateral weight: Gaussian spatial × depth similarity
            float spatialW = exp(-float(x*x + y*y) / (2.0 * float(RADIUS * RADIUS)));
            float depthW   = exp(-depthDiff / pc.depthThreshold);
            float w = spatialW * depthW;

            totalAO     += texture(sampler2D(aoTex, samp), sUV).r * w;
            totalWeight += w;
        }
    }

    float result = (totalWeight > 0.0) ? (totalAO / totalWeight) : 1.0;
    imageStore(aoBlurOut, pixel, vec4(result));
}
