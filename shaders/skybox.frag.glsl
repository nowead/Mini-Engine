#version 450

layout(location = 0) in vec3 fragRayDir;

layout(binding = 0) uniform UniformBufferObject {
    mat4 invViewProj;
    vec3 sunDirection;
    float time;
    int useEnvironmentMap;
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

// Procedural sky: gradient + sun disk, used when no HDR env map is loaded
vec3 proceduralSky(vec3 rayDir, vec3 sunDir) {
    float elev = rayDir.y;

    // Sky gradient: deep blue at zenith → warm orange at horizon → dark ground
    vec3 zenith  = vec3(0.05, 0.15, 0.40);
    vec3 horizon = vec3(0.60, 0.40, 0.25);
    vec3 ground  = vec3(0.10, 0.08, 0.05);

    vec3 sky;
    if (elev > 0.0)
        sky = mix(horizon, zenith, sqrt(elev));
    else
        sky = mix(horizon, ground, clamp(-elev * 4.0, 0.0, 1.0));

    // Sun disk + glow
    float s = max(0.0, dot(rayDir, sunDir));
    sky += vec3(1.0, 0.95, 0.8) * (pow(s, 512.0) + pow(s, 8.0) * 0.5);

    return sky;
}

void main() {
    vec3 color;

    if (ubo.useEnvironmentMap != 0) {
        // HDR environment cubemap path
        color = texture(samplerCube(environmentMap, envSampler), normalize(fragRayDir)).rgb;
        color *= ubo.exposure;
        color  = ACESFilm(color);
        color  = pow(color, vec3(1.0 / 2.2));
    } else {
        // Procedural sky fallback (no HDR loaded)
        color = proceduralSky(normalize(fragRayDir), normalize(ubo.sunDirection));
        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    }

    outColor = vec4(color, 1.0);
}
