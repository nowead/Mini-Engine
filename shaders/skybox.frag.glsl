#version 450

layout(location = 0) in vec3 fragRayDir;

layout(binding = 0) uniform UniformBufferObject {
    mat4 invViewProj;
    vec3 sunDirection;
    float time;
} ubo;

layout(location = 0) out vec4 outColor;

// Procedural sky gradient
vec3 getSkyColor(vec3 rayDir, vec3 sunDir) {
    vec3 dir = normalize(rayDir);

    // Sky colors
    vec3 skyColorZenith = vec3(0.15, 0.35, 0.65);   // Deep blue at top
    vec3 skyColorHorizon = vec3(0.6, 0.75, 0.9);    // Light blue at horizon
    vec3 groundColor = vec3(0.2, 0.2, 0.22);        // Dark gray below horizon

    // Gradient based on Y component
    float t = dir.y * 0.5 + 0.5;  // Map [-1,1] to [0,1]

    // Below horizon
    if (dir.y < 0.0) {
        float groundBlend = smoothstep(-0.1, 0.0, dir.y);
        return mix(groundColor, skyColorHorizon, groundBlend);
    }

    // Above horizon - gradient from horizon to zenith
    vec3 skyColor = mix(skyColorHorizon, skyColorZenith, pow(t, 0.5));

    // Sun
    float sunDot = dot(dir, normalize(sunDir));

    // Sun disk
    float sunIntensity = pow(max(sunDot, 0.0), 256.0) * 2.0;  // Sharp sun disk
    vec3 sunColor = vec3(1.0, 0.95, 0.8);

    // Sun glow (halo)
    float glowIntensity = pow(max(sunDot, 0.0), 8.0) * 0.3;
    vec3 glowColor = vec3(1.0, 0.8, 0.5);

    // Combine
    vec3 finalColor = skyColor;
    finalColor += sunColor * sunIntensity;
    finalColor += glowColor * glowIntensity;

    // Horizon haze
    float hazeAmount = 1.0 - pow(abs(dir.y), 0.3);
    vec3 hazeColor = vec3(0.7, 0.75, 0.85);
    finalColor = mix(finalColor, hazeColor, hazeAmount * 0.3);

    return finalColor;
}

void main() {
    vec3 skyColor = getSkyColor(fragRayDir, ubo.sunDirection);
    outColor = vec4(skyColor, 1.0);
}
