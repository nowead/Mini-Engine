#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0) uniform textureCube envMap;
layout(binding = 1) uniform sampler envSampler;
layout(binding = 2, rgba16f) uniform writeonly image2DArray outputPrefiltered;

layout(binding = 3) uniform RoughnessUBO {
    float roughness;
};

const float PI = 3.14159265359;

// Convert cubemap face + UV to 3D direction (Vulkan convention)
vec3 getCubeDir(uint face, vec2 uv) {
    vec2 st = uv * 2.0 - 1.0;
    switch (face) {
        case 0u: return normalize(vec3( 1.0, -st.y, -st.x));  // +X
        case 1u: return normalize(vec3(-1.0, -st.y,  st.x));  // -X
        case 2u: return normalize(vec3( st.x,  1.0,  st.y));  // +Y
        case 3u: return normalize(vec3( st.x, -1.0, -st.y));  // -Y
        case 4u: return normalize(vec3( st.x, -st.y,  1.0));  // +Z
        case 5u: return normalize(vec3(-st.x, -st.y, -1.0));  // -Z
    }
    return vec3(0.0);
}

// Hammersley low-discrepancy sequence
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverse_VdC(i));
}

// GGX importance sampling
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main() {
    uvec3 id = gl_GlobalInvocationID;
    ivec3 size = imageSize(outputPrefiltered);

    if (id.x >= uint(size.x) || id.y >= uint(size.y) || id.z >= 6u) return;

    vec2 uv = (vec2(id.xy) + 0.5) / vec2(size.xy);
    vec3 N = getCubeDir(id.z, uv);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;
    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; i++) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += texture(samplerCube(envMap, envSampler), L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / totalWeight;

    imageStore(outputPrefiltered, ivec3(id), vec4(prefilteredColor, 1.0));
}
