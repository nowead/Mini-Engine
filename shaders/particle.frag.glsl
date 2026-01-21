#version 450

// Inputs from vertex shader
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Simple circular particle shape
    vec2 center = fragTexCoord - vec2(0.5);
    float dist = length(center) * 2.0;

    // Soft circular falloff
    float alpha = 1.0 - smoothstep(0.0, 1.0, dist);

    // Apply particle color with alpha
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);

    // Discard fully transparent pixels
    if (outColor.a < 0.01) {
        discard;
    }
}
