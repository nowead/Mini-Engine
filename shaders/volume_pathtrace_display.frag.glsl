#version 450

// M4 v1 -- display pass for path-traced volumetric rendering. Reads the linear
// HDR running average from the accumulation texture and tonemaps to swapchain
// sRGB. Kept deliberately tiny so it doesn't compete with the integrator for
// shader time -- the heavy work is upstream in volume_pathtrace.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D accumTex;
layout(set = 0, binding = 1) uniform sampler   accumSampler;

void main() {
    ivec2 fragCoord = ivec2(gl_FragCoord.xy);
    vec3 hdr = texelFetch(sampler2D(accumTex, accumSampler), fragCoord, 0).rgb;
    // Reinhard, matches what the v0 path-trace shader did inline.
    vec3 ldr = hdr / (1.0 + hdr);
    outColor = vec4(ldr, 1.0);
}
