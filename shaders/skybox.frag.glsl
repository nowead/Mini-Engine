#version 450

layout(location = 0) in vec3 fragRayDir;

layout(binding = 0) uniform UniformBufferObject {
    mat4 invViewProj;
    vec3 sunDirection;
    float time;
    int useEnvironmentMap;  // Not used anymore - always use HDR
    float exposure;
} ubo;

// HDR environment cubemap
layout(binding = 1) uniform textureCube environmentMap;
layout(binding = 2) uniform sampler envSampler;

layout(location = 0) out vec4 outColor;

// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // Sample HDR environment cubemap
    vec3 envColor = texture(samplerCube(environmentMap, envSampler), normalize(fragRayDir)).rgb;

    // Apply exposure
    envColor *= ubo.exposure;

    // ACES tone mapping (matches PBR shader)
    envColor = ACESFilm(envColor);

    // Gamma correction
    envColor = pow(envColor, vec3(1.0 / 2.2));

    outColor = vec4(envColor, 1.0);
}
