#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0) uniform texture2D equirectMap;
layout(binding = 1) uniform sampler mapSampler;
layout(binding = 2, rgba16f) uniform writeonly image2DArray outputCubemap;

const float PI = 3.14159265359;

// Convert cubemap face + UV to 3D direction
vec3 getCubeDir(uint face, vec2 uv) {
    // Map UV from [0,1] to [-1,1]
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

// Convert 3D direction to equirectangular UV
vec2 dirToEquirect(vec3 dir) {
    float phi = atan(dir.z, dir.x);     // [-PI, PI]
    float theta = asin(clamp(dir.y, -1.0, 1.0));  // [-PI/2, PI/2]

    vec2 uv;
    uv.x = phi / (2.0 * PI) + 0.5;     // [0, 1]
    uv.y = theta / PI + 0.5;            // [0, 1]
    return uv;
}

void main() {
    uvec3 id = gl_GlobalInvocationID;
    ivec3 size = imageSize(outputCubemap);

    if (id.x >= uint(size.x) || id.y >= uint(size.y) || id.z >= 6u) return;

    vec2 uv = (vec2(id.xy) + 0.5) / vec2(size.xy);
    vec3 dir = getCubeDir(id.z, uv);
    vec2 equirectUV = dirToEquirect(dir);

    vec4 color = texture(sampler2D(equirectMap, mapSampler), equirectUV);

    imageStore(outputCubemap, ivec3(id), color);
}
