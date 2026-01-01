#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Simple lighting calculation
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diffuse = max(dot(normalize(fragNormal), lightDir), 0.3);

    // Apply lighting to color
    vec3 finalColor = fragColor * diffuse;

    outColor = vec4(finalColor, 1.0);
}
