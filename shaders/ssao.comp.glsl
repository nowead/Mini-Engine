/*
 * SSAO Compute Shader — Screen-Space Ambient Occlusion
 *
 * Algorithm: hemisphere sampling in view space (16 samples)
 *   1. Reconstruct view-space position from depth buffer
 *   2. Construct TBN frame using interleaved gradient noise rotation
 *   3. Sample 16-point hemisphere; for each, project to screen and compare depth
 *   4. Output AO factor [0, 1] where 0 = fully occluded, 1 = fully visible
 *
 * Inputs:
 *   binding 0 — depth texture (D32Sfloat, separate texture)
 *   binding 1 — AO output (R8Unorm, storage image)
 *   binding 2 — sampler
 * Push constants: projection matrix, radius, bias, near, far, invW, invH, pad×2
 */
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D depthTex;
layout(set = 0, binding = 1, r8) uniform writeonly image2D aoOut;
layout(set = 0, binding = 2) uniform sampler samp;

layout(push_constant) uniform PC {
    mat4  projection;   // view→clip (64 bytes)
    float radius;       // world-space kernel radius
    float bias;         // depth comparison bias
    float near;
    float far;
    float invW;
    float invH;
    float pad0;
    float pad1;
} pc;  // total: 64 + 32 = 96 bytes

// 8 hemisphere sample directions (cosine-weighted, low-discrepancy)
const vec3 SAMPLES[8] = vec3[](
    vec3( 0.5381,  0.1856,  0.4319),
    vec3(-0.1379,  0.2486,  0.4430),
    vec3( 0.3371,  0.5679,  0.0057),
    vec3(-0.6999,  0.0451,  0.0019),
    vec3( 0.0689,  0.1598,  0.8547),
    vec3(-0.3577,  0.5301,  0.4358),
    vec3( 0.7119,  0.0154,  0.0918),
    vec3(-0.4776,  0.2847,  0.0271)
);

// Linearize Vulkan depth [0,1] → positive view-space depth
float linearizeDepth(float d) {
    return pc.near * pc.far / (pc.far - d * (pc.far - pc.near));
}

// Reconstruct view-space position from UV + linear depth
vec3 viewPosFromDepth(vec2 uv, float linearDepth) {
    vec2 ndc = uv * 2.0 - 1.0;
    float tanHalfFovX = 1.0 / pc.projection[0][0];
    float tanHalfFovY = 1.0 / pc.projection[1][1];
    return vec3(ndc.x * tanHalfFovX * linearDepth,
                ndc.y * tanHalfFovY * linearDepth,
               -linearDepth);  // Vulkan view space: -Z forward
}

// Interleaved gradient noise — no texture needed
float noise(ivec2 coord) {
    return fract(52.9829189 * fract(dot(vec2(coord), vec2(0.06711056, 0.00583715))));
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imgSize = imageSize(aoOut);
    if (pixel.x >= imgSize.x || pixel.y >= imgSize.y) return;

    vec2 uv = (vec2(pixel) + 0.5) * vec2(pc.invW, pc.invH);

    float depth = texture(sampler2D(depthTex, samp), uv).r;

    // Sky / clear pixel — AO = 1.0 (fully lit)
    if (depth >= 1.0) {
        imageStore(aoOut, pixel, vec4(1.0));
        return;
    }

    float linearDepth = linearizeDepth(depth);
    vec3  origin      = viewPosFromDepth(uv, linearDepth);

    // Random rotation via interleaved gradient noise
    float angle   = noise(pixel) * 6.28318530;
    vec3  randDir = vec3(cos(angle), sin(angle), 0.0);

    // Normal estimation from depth neighbours
    vec2 du = vec2(pc.invW, 0.0);
    vec2 dv = vec2(0.0, pc.invH);
    float dR = linearizeDepth(texture(sampler2D(depthTex, samp), uv + du).r);
    float dU = linearizeDepth(texture(sampler2D(depthTex, samp), uv + dv).r);
    vec3 tangent   = normalize(viewPosFromDepth(uv + du, dR) - origin);
    vec3 bitangent = normalize(viewPosFromDepth(uv + dv, dU) - origin);
    vec3 normal    = normalize(cross(tangent, bitangent));
    if (normal.z < 0.0) normal = -normal;  // ensure pointing towards camera (+z in view space)

    // Gram-Schmidt TBN
    tangent   = normalize(randDir - normal * dot(randDir, normal));
    bitangent = cross(normal, tangent);
    mat3 TBN  = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < 8; ++i) {
        float scale = float(i + 1) / 8.0;
        scale = mix(0.1, 1.0, scale * scale);  // closer samples weighted more

        vec3 sampleDir = TBN * SAMPLES[i];
        vec3 samplePos = origin + sampleDir * pc.radius * scale;

        // Project sample to screen
        vec4 projected = pc.projection * vec4(samplePos, 1.0);
        projected.xyz /= projected.w;
        vec2 sampleUV = projected.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        float sampleDepth = linearizeDepth(texture(sampler2D(depthTex, samp), sampleUV).r);

        // Range check + comparison
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(origin.z - sampleDepth));
        occlusion += (sampleDepth >= -samplePos.z + pc.bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / 8.0);
    imageStore(aoOut, pixel, vec4(ao));
}
