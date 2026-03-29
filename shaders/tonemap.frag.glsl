#version 450

// Fullscreen tonemap fragment shader (Vulkan)
// Reads RGBA16Float HDR texture, applies ACES filmic tonemapping, outputs to swapchain.
// No gamma correction: Vulkan swapchain uses an sRGB format (e.g. BGRA8UnormSrgb)
// which automatically converts linear → sRGB on write.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D hdrTexture;
layout(set = 0, binding = 1) uniform sampler hdrSampler;

vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(sampler2D(hdrTexture, hdrSampler), fragUV).rgb;
    vec3 ldr = ACESFilm(hdr);
    outColor = vec4(ldr, 1.0);
}
