#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0) uniform textureCube envMap;
layout(binding = 1) uniform sampler envSampler;
layout(binding = 2, rgba16f) uniform writeonly image2DArray outputIrradiance;

const float PI = 3.14159265359;

// Convert cubemap face + UV to 3D direction
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

void main() {
    uvec3 id = gl_GlobalInvocationID;
    ivec3 size = imageSize(outputIrradiance);

    if (id.x >= uint(size.x) || id.y >= uint(size.y) || id.z >= 6u) return;

    vec2 uv = (vec2(id.xy) + 0.5) / vec2(size.xy);
    vec3 normal = getCubeDir(id.z, uv);

    // Convolution: hemisphere integral over the normal direction
    vec3 irradiance = vec3(0.0);

    // Build tangent frame
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, normal));
    up = cross(normal, right);

    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // Spherical to Cartesian (tangent space)
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // Tangent space to world space
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;

            irradiance += texture(samplerCube(envMap, envSampler), sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }

    irradiance = PI * irradiance / nrSamples;

    imageStore(outputIrradiance, ivec3(id), vec4(irradiance, 1.0));
}
